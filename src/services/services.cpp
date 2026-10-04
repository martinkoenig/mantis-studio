#include <fstream>
#include <mantis/artifact_store.hpp>
#include <mantis/device_runtime.hpp>
#include <mantis/replay.hpp>
#include <mantis/data_io.hpp>
#include <nlohmann/json.hpp>
#include <mantis/pipeline_runtime.hpp>
#include <mantis/services.hpp>
#include <set>
#include <sstream>
namespace mantis::services {
namespace {
class ReplayDigest : public std::streambuf {
    uint64_t position_{};
  public:
    uint64_t value{14695981039346656037ull};
  protected:
    std::streamsize xsputn(const char *bytes, std::streamsize size) override {
        for (std::streamsize i = 0; i < size; ++i) value = (value ^ static_cast<unsigned char>(bytes[i])) * 1099511628211ull;
        position_ += static_cast<uint64_t>(size); return size;
    }
    int_type overflow(int_type byte) override {
        if (!traits_type::eq_int_type(byte, traits_type::eof())) { auto c = traits_type::to_char_type(byte); xsputn(&c, 1); }
        return traits_type::not_eof(byte);
    }
    pos_type seekoff(off_type offset, std::ios_base::seekdir direction, std::ios_base::openmode) override {
        return offset == 0 && direction == std::ios_base::cur ? pos_type(position_) : pos_type(off_type(-1));
    }
};
}
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
        Id finalization_job;
    };
    std::map<Id, Capture> captures;
    struct Lease { std::filesystem::path path; std::chrono::steady_clock::time_point expires; };
    std::map<Id, Lease> previews;
    struct Replay { pipeline::BoundedQueue<data::Published> preview{1, pipeline::QueuePolicy::latest_only}; };
    std::map<Id, std::shared_ptr<Replay>> replays;
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
        auto preview_directory = store->root() / "cache" / "previews";
        if (std::filesystem::exists(preview_directory))
            for (const auto &file : std::filesystem::directory_iterator(preview_directory)) {
                std::error_code ec; std::filesystem::remove(file.path(), ec);
            }
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
                else if (store->get(c.raw).state == artifact::ArtifactState::open) store->abandon(c.raw);
            } catch (const std::exception &e) {
                event("error", "capture", e.what());
            }
        }
        jobs.reset();
    }
    CaptureInfo info(const Id &id, const Capture &c) const {
        auto m = c.session->metrics();
        CaptureInfo out;
        out.id = id; out.devices = c.session->descriptor().devices; out.raw_artifact = c.raw;
        out.active = c.session->active(); out.frames = c.session->committed(); out.queue_high_water = m.high_water;
        out.error = c.session->error(); out.committed = c.session->committed(); out.produced = c.session->produced();
        out.queue_depth = m.occupancy; out.queue_capacity = c.session->queue_capacity(); out.queue_saturation = c.session->saturation();
        out.preview_drops = c.session->preview_metrics().dropped; out.duration = c.session->duration();
        out.diagnostics = c.session->diagnostics();
        // Loss count is only known for observed sequence gaps / produced uncommitted packets.
        // A disconnect cannot reveal how many physical exposures were never delivered.
        out.dropped = out.queue_saturation;
        for (const auto &[key, value] : out.diagnostics)
            if (key.ends_with(".sequence_gaps")) out.dropped += std::stoull(value);
        out.total_bytes = store->get(c.raw).bytes; out.finalization_job = c.finalization_job;
        if (out.duration > 0) {
            out.writer_mb_s = double(c.session->payload_bytes()) / out.duration / 1e6;
            out.writer_mib_s = double(c.session->payload_bytes()) / out.duration / 1048576;
        }
        out.diagnostics["preview_delivery_fps"] = out.duration > 0 ? std::to_string(double(c.session->preview_metrics().popped) / out.duration) : "unavailable";
        out.diagnostics["throughput_basis"] = "committed raw pixel payload / capture duration";
        out.diagnostics["raw_loss_observability"] = out.error.empty() ? "observed" : "failure; unobserved exposures unknown";
        return out;
    }
};
Runtime::Runtime(Configuration c) : impl_(std::make_unique<Impl>(c)) {}
Runtime::~Runtime() = default;
std::vector<device::Descriptor> Runtime::devices() const {
    bool active = false;
    for (auto &[id, capture] : impl_->captures) active |= capture.session->active();
    if (!active) {
        // Refresh only after all adapters have stopped; historical Sessions release
        // their stream pointers while retaining descriptors/metrics/first frames.
        for (auto &[id, capture] : impl_->captures) capture.session->stop();
        impl_->devices->refresh(*impl_->registry);
    }
    return impl_->devices->list();
}
CaptureInfo Runtime::start_capture(const std::vector<Id> &ids) {
    for (auto &[previous_id, capture] : impl_->captures) if (!capture.session->active()) capture.session->stop();
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
        if (std::find(caps.begin(), caps.end(), device::image_stream) == caps.end() &&
            std::find(caps.begin(), caps.end(), device::frameset_stream) == caps.end())
            fail(Status::incompatible, "Device lacks image stream capability");
        streams.push_back(&stream);
    }
    if (streams.size() > 1 && std::any_of(streams.begin(), streams.end(), [](auto *s) { return s->source_paced(); }))
        fail(Status::invalid_argument, "Capture a composite parent as one stream; independent cameras do not establish FrameSets");
    bool composite = std::find(streams.front()->descriptor().capabilities.begin(), streams.front()->descriptor().capabilities.end(), device::frameset_stream) != streams.front()->descriptor().capabilities.end();
    Id id = Id::random();
    artifact::Provenance provenance;
    provenance.producer = "org.mantis.capture";
    provenance.parameters["capture_id"] = id.value;
    if (composite) {
        const auto &descriptor = streams.front()->descriptor();
        provenance.producer = descriptor.plugin_id;
        std::string producer_version;
        for (const auto &plugin : impl_->registry->statuses())
            if (plugin.manifest.id == descriptor.plugin_id) producer_version = plugin.manifest.version;
        SemanticVersion parsed{}; char first{}, second{};
        std::istringstream version(producer_version);
        provenance.version = {};
        if (version >> parsed.major >> first >> parsed.minor >> second >> parsed.patch && first == '.' && second == '.')
            provenance.version = parsed;
        provenance.parameters = descriptor.metadata;
        provenance.parameters["producer_plugin_version"] = producer_version.empty() ? "unavailable" : producer_version;
        provenance.parameters["capture_id"] = id.value;
        provenance.parameters["logical_device_id"] = descriptor.id.value;
        provenance.parameters["format_version"] = "2";
        provenance.parameters["segment_max_bytes"] = "67108864";
        provenance.parameters["started_unix_ns"] = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        auto components = nlohmann::json::array();
        for (const auto &child : streams.front()->components())
            components.push_back({{"id", child.id.value}, {"name", child.name}, {"capabilities", child.capabilities}, {"metadata", child.metadata}});
        provenance.parameters["components"] = components.dump();
    }
    auto raw = impl_->store->begin({"org.mantis.RawCapture", composite ? 2u : 1u}, provenance);
    auto store = impl_->store;
    std::unique_ptr<device::Session> session;
    try { session = std::make_unique<device::Session>(
        device::CaptureDescriptor{id, ids, {}}, std::move(streams),
        [store, raw](data::Published p) { store->append(raw, *p); }, impl_->logger());
    } catch (...) { store->abandon(raw); throw; }
    auto [it, inserted] = impl_->captures.emplace(id, Impl::Capture{raw, std::move(session), {}});
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
        it->second.session->error().empty()) {
        if (impl_->store->get(it->second.raw).type.schema_version == 2) {
            auto store = impl_->store; auto raw = it->second.raw;
            store->prepare_finalize(raw);
            it->second.finalization_job = impl_->jobs->submit("Finalize RawCapture", [store, raw](jobs::Context &context) -> std::optional<artifact::ArtifactReference> {
                context.update(0.1, "Validating sealed segments");
                auto result = store->finalize(raw, context.cancellation);
                context.update(1, "Finalized RawCapture"); return artifact::ArtifactReference{raw, result.hash};
            });
        } else impl_->store->finalize(it->second.raw);
    }
    else if (impl_->store->get(it->second.raw).state == artifact::ArtifactState::open)
        impl_->store->abandon(it->second.raw);
    impl_->event("capture.stopped", "capture", id.value);
    return impl_->info(id, it->second);
}
std::vector<CaptureInfo> Runtime::captures() const {
    std::vector<CaptureInfo> out;
    for (auto &[id, c] : impl_->captures)
        out.push_back(impl_->info(id, c));
    return out;
}
PreviewReference Runtime::preview(const Id &id) {
    auto now = std::chrono::steady_clock::now();
    for (auto it = impl_->previews.begin(); it != impl_->previews.end();) {
        if (it->second.expires <= now) {
            std::error_code ec; std::filesystem::remove(it->second.path, ec);
            if (!ec) it = impl_->previews.erase(it); else ++it;
        } else ++it;
    }
    if (impl_->previews.size() >= 8) fail(Status::busy, "Preview lease capacity reached; release references or wait 60 seconds");
    data::Published packet;
    if (auto live = impl_->captures.find(id); live != impl_->captures.end()) packet = live->second.session->preview();
    else if (auto replay = impl_->replays.find(id); replay != impl_->replays.end()) {
        auto latest = replay->second->preview.try_pop(); if (latest) packet = *latest;
    } else fail(Status::not_found, "Capture/replay source not found");
    if (!packet) fail(Status::busy, "No new preview frame available");
    auto lease = Id::random();
    auto directory = impl_->store->root() / "cache" / "previews";
    std::filesystem::create_directories(directory);
    auto path = directory / (lease.value + ".packet");
    data::write_packet(path, *packet);
    impl_->previews.emplace(lease, Impl::Lease{path, now + std::chrono::seconds(60)});
    return {lease, path};
}
void Runtime::release_preview(const Id &id) {
    auto it = impl_->previews.find(id);
    if (it == impl_->previews.end()) return;
    std::error_code ec; std::filesystem::remove(it->second.path, ec);
    if (!ec) impl_->previews.erase(it); else it->second.expires = std::chrono::steady_clock::now();
}
Id Runtime::replay_capture(const Id &id, bool real_time, bool verify) {
    auto a = impl_->store->get(id);
    if (a.state != artifact::ArtifactState::finalized || a.type.name != "org.mantis.RawCapture")
        fail(Status::incompatible, "Replay requires a finalized RawCapture");
    if (impl_->replays.size() >= 64) fail(Status::busy, "Replay history capacity reached");
    auto preview = std::make_shared<Impl::Replay>();
    auto store = impl_->store;
    auto job = impl_->jobs->submit(verify ? "Verify deterministic replay" : "Replay RawCapture",
        [store, id, real_time, verify, preview](jobs::Context &context) -> std::optional<artifact::ArtifactReference> {
            std::string baseline;
            uint64_t frames{}, left{}, right{};
            for (unsigned pass = 0; pass < (verify ? 2u : 1u); ++pass) {
                auto source = device::recorded_source(store, id, real_time);
                auto start = source->start(); if (!start) throw Failure(start.error());
                uint64_t count{}; std::optional<uint64_t> sequence;
                // Streaming digest is bounded memory, includes canonical serialized metadata and pixels.
                ReplayDigest digest; std::ostream canonical(&digest);
                while (!source->finished()) {
                    context.cancellation.check(); auto next = source->next(); if (!next) throw Failure(next.error());
                    if (!*next) continue;
                    auto packet = *next;
                    if (sequence && packet->header.sequence.value != *sequence + 1) fail(Status::corrupt, "Replay sequence discontinuity");
                    sequence = packet->header.sequence.value;
                    if (verify) data::write_packet(canonical, *packet);
                    preview->preview.push(packet); ++count;
                    if (pass == 0) {
                        ++frames;
                        for (const auto &frame : packet->frames) {
                            auto role = frame->header.metadata.find("role");
                            if (role != frame->header.metadata.end() && role->second == "left") ++left;
                            if (role != frame->header.metadata.end() && role->second == "right") ++right;
                        }
                    }
                }
                source->stop();
                auto value = std::to_string(digest.value) + ":" + std::to_string(count);
                if (pass == 0) baseline = value;
                else if (value != baseline) fail(Status::corrupt, "Repeated replay digest mismatch");
            }
            auto report = nlohmann::json{{"framesets", frames}, {"left_frames", left}, {"right_frames", right},
                {"sequences", "continuous"}, {"raw_integrity", "PASS"}, {"replay", "PASS"},
                {"passes", verify ? 2 : 1}, {"digest", baseline}}.dump();
            context.update(1, report);
            return artifact::ArtifactReference{id, store->get(id).hash};
        });
    impl_->replays.emplace(job, std::move(preview)); return job;
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
    auto artifact = impl_->store->get(id);
    if (artifact.state != artifact::ArtifactState::finalized)
        fail(Status::busy, "Only finalized artifacts can be visualized");
    if (artifact.type.name == "org.mantis.RawCapture" && artifact.type.schema_version == 2)
        fail(Status::unsupported, "Segmented RawCapture uses the replay source API, not a standalone packet reference");
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
Id Runtime::recover_artifact_job(const Id &id) {
    auto store = impl_->store;
    if (store->get(id).state != artifact::ArtifactState::recoverable)
        fail(Status::invalid_argument, "Artifact is not recoverable");
    return impl_->jobs->submit("Recover RawCapture", [this, store, id](jobs::Context &context) -> std::optional<artifact::ArtifactReference> {
        context.update(0.1, "Validating capture records and recovering complete tail");
        auto result = store->recover(id, context.cancellation);
        impl_->event("artifact.recovered", "artifact", id.value);
        context.update(1, "Recovered RawCapture");
        return artifact::ArtifactReference{id, result.hash};
    });
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
