#include <fstream>
#include <mantis/artifact_store.hpp>
#include <mantis/device_runtime.hpp>
#include <mantis/pipeline_runtime.hpp>
#include <mantis/services.hpp>
#include <set>
namespace mantis::services {
struct Runtime::Impl {
    mutable std::mutex event_mutex;
    uint64_t event_sequence{};
    std::deque<Event> event_log;
    std::ofstream diagnostic_file;
    std::shared_ptr<artifact::Store> store;
    std::filesystem::path recipes;
    std::unique_ptr<plugins::Registry> registry;
    std::unique_ptr<device::Runtime> devices;
    std::unique_ptr<jobs::Manager> jobs;
    struct Capture {
        Id raw;
        std::unique_ptr<device::Session> session;
    };
    std::map<Id, Capture> captures;
    void event(std::string kind, std::string component, std::string message) {
        std::lock_guard lock(event_mutex);
        Event e{++event_sequence, std::move(kind), std::move(component), std::move(message)};
        if (event_log.size() == 512)
            event_log.pop_front();
        event_log.push_back(e);
        if (diagnostic_file) {
            diagnostic_file << e.sequence << '\t' << e.kind << '\t' << e.component << '\t' << e.message
                            << '\n';
            diagnostic_file.flush();
        }
    }
    LogSink logger() {
        return [this](const LogRecord &r) { event(r.level, r.component, r.message); };
    }
    explicit Impl(const Configuration &c) {
        recipes = c.recipes;
        store = std::make_shared<artifact::Store>(c.project);
        diagnostic_file.open(store->root() / "diagnostics.log", std::ios::app);
        registry =
            std::make_unique<plugins::Registry>(c.plugin_host, store->root() / "cache" / "hosts", logger());
        registry->discover(c.plugins, c.approved_in_process);
        devices = std::make_unique<device::Runtime>(*registry);
        jobs = std::make_unique<jobs::Manager>(logger());
        event("runtime.ready", "runtime", "Project opened: " + store->root().string());
    }
    ~Impl() {
        for (auto &[id, c] : captures) {
            c.session->stop();
            try {
                if (c.session->error().empty() && store->get(c.raw).state == artifact::ArtifactState::open)
                    store->finalize(c.raw);
            } catch (const std::exception &e) {
                event("error", "capture", e.what());
            }
        }
        jobs.reset();
    }
    CaptureInfo info(const Id &id, const Capture &c) const {
        auto m = c.session->metrics();
        return {id,           c.session->descriptor().devices,
                c.raw,        c.session->active(),
                m.popped,     m.dropped,
                m.high_water, c.session->error()};
    }
};
Runtime::Runtime(Configuration c) : impl_(std::make_unique<Impl>(c)) {}
Runtime::~Runtime() = default;
std::vector<device::Descriptor> Runtime::devices() const {
    return impl_->devices->list();
}
CaptureInfo Runtime::start_capture(const std::vector<Id> &ids) {
    if (ids.empty())
        fail(Status::invalid_argument, "Capture requires at least one device");
    if (impl_->captures.size() >= 64)
        fail(Status::busy, "Capture history capacity reached; open another project");
    std::set<Id> requested(ids.begin(), ids.end());
    if (requested.size() != ids.size())
        fail(Status::invalid_argument, "Duplicate capture device");
    for (auto &[id, c] : impl_->captures)
        if (c.session->active())
            for (auto &d : c.session->descriptor().devices)
                if (requested.contains(d))
                    fail(Status::busy, "Device already belongs to an active capture");
    std::vector<device::ImageStream *> streams;
    for (auto &id : ids) {
        auto &stream = impl_->devices->find(id);
        auto &caps = stream.descriptor().capabilities;
        if (std::find(caps.begin(), caps.end(), device::image_stream) == caps.end())
            fail(Status::incompatible, "Device lacks image stream capability");
        streams.push_back(&stream);
    }
    Id id = Id::random();
    artifact::Provenance provenance;
    provenance.producer = "org.mantis.capture";
    provenance.parameters["capture_id"] = id.value;
    auto raw = impl_->store->begin({"org.mantis.RawCapture", 1}, provenance);
    auto store = impl_->store;
    auto session = std::make_unique<device::Session>(
        device::CaptureDescriptor{id, ids, {}}, std::move(streams),
        [store, raw](data::Published p) { store->append(raw, *p); }, impl_->logger());
    auto [it, inserted] = impl_->captures.emplace(id, Impl::Capture{raw, std::move(session)});
    (void)inserted;
    impl_->event("capture.started", "capture", id.value);
    return impl_->info(id, it->second);
}
CaptureInfo Runtime::stop_capture(const Id &id) {
    auto it = impl_->captures.find(id);
    if (it == impl_->captures.end())
        fail(Status::not_found, "Capture not found");
    it->second.session->stop();
    if (impl_->store->get(it->second.raw).state == artifact::ArtifactState::open &&
        it->second.session->error().empty())
        impl_->store->finalize(it->second.raw);
    impl_->event("capture.stopped", "capture", id.value);
    return impl_->info(id, it->second);
}
std::vector<CaptureInfo> Runtime::captures() const {
    std::vector<CaptureInfo> out;
    for (auto &[id, c] : impl_->captures)
        out.push_back(impl_->info(id, c));
    return out;
}
Id Runtime::run_pipeline(const Id &capture, const std::string &recipe, const Id &artifact) {
    if (recipe.empty() || recipe.size() > 128 ||
        recipe.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
            std::string::npos)
        fail(Status::invalid_argument, "Invalid recipe name");
    data::Published input;
    Id raw;
    if (!artifact.value.empty()) {
        auto a = impl_->store->get(artifact);
        if (a.type.name != "org.mantis.RawCapture" || a.state != artifact::ArtifactState::finalized)
            fail(Status::incompatible, "Replay requires a finalized raw capture");
        input = impl_->store->packet(artifact);
        raw = artifact;
    } else {
        auto it = impl_->captures.find(capture);
        if (it == impl_->captures.end())
            fail(Status::not_found, "Capture not found");
        input = it->second.session->first();
        raw = it->second.raw;
    }
    auto config = pipeline::load_recipe(impl_->recipes / (recipe + ".json"),
                                        [this](const std::string &id) { return impl_->registry->node(id); });
    if (!config)
        throw Failure(config.error());
    auto plan = pipeline::compile(*config);
    if (!plan)
        throw Failure(plan.error());
    auto store = impl_->store;
    auto producer = plan->graph.nodes[plan->order.back()].descriptor.plugin_id;
    return impl_->jobs->submit(
        "Pipeline: " + recipe,
        [this, plan = std::move(*plan), store, input, raw, producer,
         recipe](jobs::Context &context) -> std::optional<artifact::ArtifactReference> {
            context.update(0.1, "Executing typed graph");
            auto output = pipeline::execute(plan, input, context.cancellation);
            if (!output)
                throw Failure(output.error());
            for (auto &timing : output->timings)
                impl_->event("pipeline.timing", "pipeline",
                             timing.node + " " + std::to_string(timing.nanoseconds) + " ns");
            context.cancellation.check();
            context.update(0.7, "Committing point cloud");
            artifact::Provenance provenance;
            provenance.producer = producer;
            provenance.inputs = {raw};
            provenance.calibration = input->header.calibration;
            provenance.parameters = {{"frame_sequence", std::to_string(input->header.sequence.value)},
                                     {"recipe", recipe + ".v1"}};
            auto id = store->begin({output->output->type.name, output->output->type.version},
                                   std::move(provenance));
            store->append(id, *output->output);
            auto a = store->finalize(id);
            impl_->event("artifact.created", "artifact", id.value);
            return artifact::ArtifactReference{id, a.hash};
        });
}
std::string Runtime::open_project(const std::filesystem::path &path, bool create) {
    if (impl_->jobs->busy())
        fail(Status::busy, "Jobs must finish before switching projects");
    for (auto &[id, c] : impl_->captures)
        if (c.session->active())
            fail(Status::busy, "Stop captures before switching projects");
    if (!create && !std::filesystem::exists(path / "manifest.json"))
        fail(Status::not_found, "Project does not exist");
    if (std::filesystem::absolute(path).lexically_normal() == impl_->store->root().lexically_normal())
        return project();
    auto next = std::make_shared<artifact::Store>(path);
    impl_->captures.clear();
    impl_->store = std::move(next);
    impl_->diagnostic_file.close();
    impl_->diagnostic_file.open(impl_->store->root() / "diagnostics.log", std::ios::app);
    impl_->event("project.opened", "project", project());
    return project();
}
std::string Runtime::project() const {
    return impl_->store->root().string();
}
std::vector<artifact::ArtifactDescriptor> Runtime::artifacts() const {
    return impl_->store->list();
}
std::filesystem::path Runtime::data_reference(const Id &id) const {
    if (impl_->store->get(id).state != artifact::ArtifactState::finalized)
        fail(Status::busy, "Only finalized artifacts can be visualized");
    return impl_->store->object_path(id);
}
Id Runtime::export_artifact(const Id &id, const std::filesystem::path &path) {
    auto store = impl_->store;
    auto a = store->get(id);
    if (a.state != artifact::ArtifactState::finalized)
        fail(Status::busy, "Export requires finalized artifact");
    auto output = std::filesystem::absolute(path);
    auto project = store->root();
    auto relative = output.lexically_normal().lexically_relative(project.lexically_normal());
    if (!relative.empty() && *relative.begin() != "..")
        fail(Status::invalid_argument, "Export outside the project store to preserve its immutable objects");
    if (std::filesystem::exists(output))
        fail(Status::invalid_argument, "Export destination already exists");
    return impl_->jobs->submit(
        "Export PLY",
        [this, store, id, a, output](jobs::Context &context) -> std::optional<artifact::ArtifactReference> {
            context.update(0.1, "Reading artifact");
            auto input = store->packet(id);
            auto part = output;
            part += "." + Id::random().value + ".part";
            try {
                impl_->registry->export_file("org.mantis.ply", *input, part, context.cancellation);
                context.cancellation.check();
                platform::durable_file(part);
                if (std::filesystem::exists(output))
                    fail(Status::invalid_argument, "Export destination appeared while writing");
                std::filesystem::rename(part, output);
                platform::durable_directory(output.parent_path());
            } catch (...) {
                std::error_code ec;
                std::filesystem::remove(part, ec);
                throw;
            }
            impl_->event("export.completed", "export", output.string());
            return artifact::ArtifactReference{id, a.hash};
        });
}
artifact::ArtifactDescriptor Runtime::recover_artifact(const Id &id) {
    auto result = impl_->store->recover(id);
    impl_->event("artifact.recovered", "artifact", id.value);
    return result;
}
std::vector<jobs::Snapshot> Runtime::jobs() const {
    return impl_->jobs->list();
}
void Runtime::cancel_job(const Id &id) {
    impl_->jobs->cancel(id);
}
std::vector<PluginInfo> Runtime::plugins() const {
    std::vector<PluginInfo> out;
    for (auto &p : impl_->registry->statuses())
        out.push_back({p.manifest.id, p.manifest.version, p.manifest.kind, p.manifest.execution, p.state,
                       p.diagnostic, p.manifest.permissions});
    return out;
}
void Runtime::enable_plugin(const std::string &id, bool enabled) {
    if (impl_->jobs->busy())
        fail(Status::busy, "Plugin lifecycle change requires idle job executor");
    impl_->registry->set_enabled(id, enabled);
    impl_->event("plugin.state", "plugin", id + (enabled ? " enabled" : " disabled"));
}
std::vector<Event> Runtime::events(uint64_t after) const {
    std::lock_guard lock(impl_->event_mutex);
    std::vector<Event> out;
    for (auto &e : impl_->event_log)
        if (e.sequence > after)
            out.push_back(e);
    return out;
}
} // namespace mantis::services
