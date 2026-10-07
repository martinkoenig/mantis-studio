#include <fstream>
#include <mantis/artifact_store.hpp>
#include <mantis/calibration_artifacts.hpp>
#include <mantis/calibration_dataset_builder.hpp>
#include <mantis/calibration_solver_opencv.hpp>
#include <mantis/capture_calibration.hpp>
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
        std::shared_ptr<CaptureCalibrationBinding> binding;
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
std::shared_ptr<artifact::Store> Runtime::project_store() const { return impl_->store; }
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
    std::optional<artifact::ActiveCalibration> active;
    if (ids.size() == 1)
        active = impl_->store->active_calibration(ids.front());
    if (active) {
        if (!composite)
            fail(Status::incompatible,
                 "Project RigCalibration binding requires composite FrameSet acquisition", "capture");
        auto rig = calibration::artifacts::load_rig_calibration(*impl_->store, active->artifact.id);
        if (!rig)
            throw Failure(rig.error());
        if (impl_->store->get(active->artifact.id).hash != active->artifact.hash)
            fail(Status::corrupt, "Active RigCalibration hash mismatch", "capture");
        std::vector<calibration::artifacts::CameraComponent> components;
        for (const auto &child : streams.front()->components()) {
            if (!device::image_participant(child)) continue;
            auto component = discovered_calibration_component(child);
            if (!component)
                throw Failure(component.error());
            components.push_back(std::move(*component));
        }
        if (components.empty())
            fail(Status::incompatible, "Active RigCalibration requires discovered measurement components",
                 "capture");
        auto compatible = calibration::artifacts::validate_rig_device(*rig, ids.front(), components);
        if (!compatible)
            throw Failure(compatible.error());
        provenance.calibration = active->reference;
        provenance.inputs.push_back(active->artifact.id);
        provenance.parameters["active_calibration_id"] = active->reference.id.value;
        provenance.parameters["active_calibration_schema_version"] =
            std::to_string(active->reference.schema_version);
        provenance.parameters["active_calibration_revision"] = std::to_string(active->reference.revision);
        provenance.parameters["active_rig_artifact_id"] = active->artifact.id.value;
        provenance.parameters["active_rig_artifact_hash_algorithm"] = active->artifact.hash.algorithm;
        provenance.parameters["active_rig_artifact_hash"] = active->artifact.hash.hex;
    }
    auto raw = impl_->store->begin({"org.mantis.RawCapture", composite ? 2u : 1u}, provenance);
    auto store = impl_->store;
    auto binding = std::make_shared<CaptureCalibrationBinding>(store, raw, active);
    std::unique_ptr<device::Session> session;
    try {
        session = std::make_unique<device::Session>(
            device::CaptureDescriptor{id, ids, active ? active->reference : calibration::Reference{}},
            std::move(streams),
            [store, raw, binding](data::Published p) {
                auto bound = binding->stamp(std::move(p));
                store->append(raw, *bound);
            },
            impl_->logger());
    } catch (...) {
        store->abandon(raw);
        throw;
    }
    auto [it, inserted] = impl_->captures.emplace(id, Impl::Capture{raw, std::move(session), binding, {}});
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
    if (auto live = impl_->captures.find(id); live != impl_->captures.end()) {
        packet = live->second.session->preview();
        if(packet) packet=live->second.binding->stamp(std::move(packet));
    }
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
        input = it->second.binding->stamp(it->second.session->first());
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
namespace {
namespace ca = calibration::artifacts;
template <class T> T calibration_checked(Result<T> value) {
    if (!value) throw Failure(value.error());
    return std::move(*value);
}
void calibration_checked(Result<void> value) {
    if (!value) throw Failure(value.error());
}
void calibration_text(std::string_view value, std::string_view field, bool required = true) {
    if ((required && value.empty()) || value.size() > calibration_string_limit ||
        value.find('\0') != std::string_view::npos)
        fail(Status::invalid_argument, std::string(field) + " must be nonempty when required, at most 4096 bytes and contain no NUL", "calibration");
}
std::optional<Id> calibration_series(const artifact::Store &store, const std::string &id,
                                      const artifact::ArtifactType &kind) {
    calibration_text(id, "series_id", false);
    if (id.empty()) return {};
    // Registry resolution, never document/provenance/filename inference. Reservation stays in M5.
    auto revision = store.calibration_revision({id}, 1);
    if (revision.kind != kind.name || revision.reference.schema_version != kind.schema_version)
        fail(Status::incompatible, "Calibration series has another artifact kind/schema", "calibration");
    return Id{id};
}
artifact::ArtifactReference calibration_input(const artifact::Store &store, const Id &id,
                                               const artifact::ArtifactType &kind) {
    calibration_text(id.value, "artifact_id");
    const auto a = store.get(id);
    if (a.type.name != kind.name || a.type.schema_version != kind.schema_version ||
        a.state != artifact::ArtifactState::finalized)
        fail(Status::incompatible, "Calibration input must be finalized " + kind.name + " schema " +
                                      std::to_string(kind.schema_version), "calibration");
    if (a.hash.algorithm.empty() || a.hash.hex.empty())
        fail(Status::corrupt, "Finalized calibration input lacks hash", "calibration");
    return {id, a.hash};
}
bool calibration_kind(std::string_view name) {
    return name == ca::target_type.name || name == ca::dataset_type.name ||
           name == ca::camera_type.name || name == ca::rig_type.name;
}
CalibrationEntry calibration_entry(const artifact::Store &store, const Id &id) {
    auto a = store.get(id);
    if (!calibration_kind(a.type.name)) fail(Status::incompatible, "Artifact is not a calibration", "calibration");
    return {std::move(a), store.artifact_revision(id).reference};
}
TargetSpecification target_specification(const calibration::CalibrationTarget &t) {
    return {t.grid, t.pattern, t.measurement};
}
MonoStageInfo stage_info(const calibration::MonoStageEvidence &s) {
    return {s.views.size(), s.residuals, s.coverage, s.opencv_solver_rms_px};
}
StereoStageInfo stage_info(const calibration::StereoStageEvidence &s) {
    return {s.pairs.size(), s.residuals, s.opencv_solver_rms_px};
}
SolverInfo solver_info(const ca::SolverImplementation &i) {
    return {i.opencv_version, i.mantis_version, i.mantis_build};
}
ca::SolverImplementation solver_implementation() {
    return {std::string(calibration::solver_opencv_version()), application_version, std::string(build_version)};
}
CalibrationInfo inspect_calibration(const artifact::Store &store, const Id &id) {
    calibration_text(id.value, "artifact_id");
    auto entry = calibration_entry(store, id);
    calibration_input(store, id, {entry.artifact.type.name, 1});
    if (entry.artifact.type.name == ca::target_type.name) {
        auto t = calibration_checked(ca::load_calibration_target(store, id));
        return {std::move(entry), TargetInfo{target_specification(t.target)}};
    }
    if (entry.artifact.type.name == ca::dataset_type.name) {
        auto d = calibration_checked(ca::load_calibration_dataset(store, id));
        auto summary = calibration_checked(calibration::summarize_dataset(d.dataset));
        DatasetInfo out{d.target_reference, d.dataset.raw_capture_ids, d.dataset.config, {}, d.dataset.records.size()};
        for (const auto &camera : d.dataset.cameras) {
            const auto &counts = summary.cameras.at(camera.role);
            out.cameras.push_back({camera, counts.analyzed, counts.detected, counts.no_target, counts.selected});
        }
        return {std::move(entry), std::move(out)};
    }
    if (entry.artifact.type.name == ca::camera_type.name) {
        auto c = calibration_checked(ca::load_camera_calibration(store, id));
        const auto &s = c.solution;
        return {std::move(entry), CameraInfo{c.dataset_reference, c.target_reference, s.camera, s.config,
                    s.training_model, s.final_model, stage_info(s.training_fit), stage_info(s.heldout_validation),
                    stage_info(s.final_fit), solver_info(c.implementation)}};
    }
    auto r = calibration_checked(ca::load_rig_calibration(store, id));
    const auto &s = r.solution;
    return {std::move(entry), RigInfo{r.dataset_reference, r.target_reference, r.left_camera_reference,
                r.right_camera_reference, s.left_camera, s.right_camera, s.config, s.left_final_intrinsics,
                s.right_final_intrinsics, s.final_model, s.rig, stage_info(s.training_fit),
                stage_info(s.heldout_validation), stage_info(s.final_fit), solver_info(r.implementation)}};
}
} // namespace
CalibrationInfo Runtime::create_calibration_target(const TargetCreate &request) {
    auto store = impl_->store;
    const auto &spec = request.target;
    if (const auto *c = std::get_if<calibration::CharucoDefinition>(&spec.pattern))
        calibration_text(c->dictionary, "dictionary");
    if (const auto &p = spec.measurement.provenance) {
        if (p->instrument) calibration_text(*p->instrument, "instrument", false);
        if (p->note) calibration_text(*p->note, "note", false);
    }
    auto series = calibration_series(*store, request.series_id, ca::target_type);
    // Unassigned identity by construction; M1 validation and M5 allocation are authoritative.
    auto created = calibration_checked(ca::create_calibration_target(*store,
        {{}, spec.grid, spec.pattern, spec.measurement}, series));
    impl_->event("calibration.target.created", "calibration", created.descriptor.id.value);
    return {{created.descriptor, created.value.revision}, TargetInfo{target_specification(created.value.target)}};
}
Id Runtime::build_calibration_dataset(const DatasetBuild &request) {
    auto store = impl_->store;
    if (request.raw_capture_artifact_ids.size() > calibration_source_limit ||
        request.camera_roles.size() > calibration_role_limit)
        fail(Status::invalid_argument, "Calibration request exceeds 1024 sources or 64 roles", "calibration");
    for (const auto &role : request.camera_roles) calibration_text(role, "camera_role");
    for (const auto &id : request.raw_capture_artifact_ids) calibration_text(id.value, "raw_capture_artifact_id");
    auto series = calibration_series(*store, request.series_id, ca::dataset_type);
    auto target_ref = calibration_input(*store, request.target_artifact_id, ca::target_type);
    auto target = calibration_checked(ca::load_calibration_target(*store, target_ref.id));
    auto ids = calibration_checked(calibration::canonical_raw_capture_ids(request.raw_capture_artifact_ids));
    auto config = calibration_checked(calibration::canonical_analysis_config(
        {1, request.camera_roles, 1, request.max_selected_per_camera}));
    for (const auto &id : ids) calibration_input(*store, id, {"org.mantis.RawCapture", 2});
    return impl_->jobs->submit("Build CalibrationDataset",
        [this, store, target = std::move(target.target), target_ref, ids = std::move(ids),
         config = std::move(config), series](jobs::Context &context) -> std::optional<artifact::ArtifactReference> {
            context.update(0.1, "Analyzing finalized RawCaptures");
            auto dataset = calibration_checked(calibration::build_calibration_dataset(
                store, ids, target, config, context.cancellation));
            context.cancellation.check();
            context.update(0.9, "Persisting CalibrationDataset");
            context.cancellation.check();
            auto created = calibration_checked(ca::create_calibration_dataset(*store, dataset, target_ref, series));
            impl_->event("calibration.dataset.created", "calibration", created.descriptor.id.value);
            return created.reference();
        });
}
Id Runtime::solve_camera_calibration(const CameraSolve &request) {
    auto store = impl_->store;
    calibration_text(request.camera_role, "camera_role");
    auto series = calibration_series(*store, request.series_id, ca::camera_type);
    calibration::MonoSolveConfig config{1, 1, 1, request.heldout_per_camera};
    calibration_checked(calibration::validate_mono_solve_config(config));
    auto dataset_ref = calibration_input(*store, request.dataset_artifact_id, ca::dataset_type);
    auto dataset = calibration_checked(ca::load_calibration_dataset(*store, dataset_ref.id));
    auto camera = std::find_if(dataset.dataset.cameras.begin(), dataset.dataset.cameras.end(),
                              [&](const auto &c) { return c.role == request.camera_role; });
    if (camera == dataset.dataset.cameras.end())
        fail(Status::invalid_argument, "Camera role does not occur in dataset", "calibration");
    auto target_ref = dataset.target_reference;
    return impl_->jobs->submit("Solve CameraCalibration: " + request.camera_role,
        [this, store, dataset = std::move(dataset.dataset), dataset_ref, target_ref,
         role = request.camera_role, config, series](jobs::Context &context) -> std::optional<artifact::ArtifactReference> {
            context.cancellation.check();
            context.update(0.1, "Solving one camera (OpenCV call is non-preemptive)");
            auto result = calibration::solve_camera_intrinsics(dataset, role, config);
            context.cancellation.check();
            auto solution = calibration_checked(std::move(result));
            context.update(0.9, "Persisting CameraCalibration");
            context.cancellation.check();
            auto created = calibration_checked(ca::create_camera_calibration(*store, solution,
                dataset_ref, target_ref, solver_implementation(), series));
            impl_->event("calibration.camera.created", "calibration", created.descriptor.id.value);
            return created.reference();
        });
}
Id Runtime::solve_rig_calibration(const RigSolve &request) {
    auto store = impl_->store;
    calibration_text(request.rig_frame.id.value, "rig_frame_id");
    calibration_text(request.rig_frame.name, "rig_frame_name");
    auto series = calibration_series(*store, request.series_id, ca::rig_type);
    auto dataset_ref = calibration_input(*store, request.dataset_artifact_id, ca::dataset_type);
    auto left_ref = calibration_input(*store, request.left_camera_artifact_id, ca::camera_type);
    auto right_ref = calibration_input(*store, request.right_camera_artifact_id, ca::camera_type);
    if (left_ref.id == right_ref.id)
        fail(Status::invalid_argument, "LEFT and RIGHT must be distinct CameraCalibration artifacts", "calibration");
    auto dataset = calibration_checked(ca::load_calibration_dataset(*store, dataset_ref.id));
    auto left = calibration_checked(ca::load_camera_calibration(*store, left_ref.id));
    auto right = calibration_checked(ca::load_camera_calibration(*store, right_ref.id));
    auto target_ref = dataset.target_reference;
    auto same = [](const auto &a, const auto &b) { return a.id == b.id && a.hash == b.hash; };
    if (!same(left.dataset_reference, dataset_ref) || !same(right.dataset_reference, dataset_ref) ||
        !same(left.target_reference, target_ref) || !same(right.target_reference, target_ref))
        fail(Status::incompatible, "Rig cameras must belong to the exact supplied dataset and target", "calibration");
    calibration::StereoSolveConfig config{1, 1, 1, left.solution.camera.role, right.solution.camera.role,
                                           request.heldout_pairs, request.rig_frame};
    calibration_checked(calibration::validate_stereo_solve_config(config));
    // Reuse M4's correspondence/config eligibility policy, including Checkerboard incompatibility.
    calibration_checked(calibration::partition_stereo_samples(dataset.dataset, config));
    return impl_->jobs->submit("Solve RigCalibration",
        [this, store, dataset = std::move(dataset.dataset), left = std::move(left.solution),
         right = std::move(right.solution), dataset_ref, target_ref, left_ref, right_ref,
         config, series](jobs::Context &context) -> std::optional<artifact::ArtifactReference> {
            context.cancellation.check();
            context.update(0.1, "Solving rig (OpenCV call is non-preemptive)");
            auto result = calibration::solve_stereo_rig(dataset, left, right, config);
            context.cancellation.check();
            auto solution = calibration_checked(std::move(result));
            context.update(0.9, "Persisting RigCalibration");
            context.cancellation.check();
            auto created = calibration_checked(ca::create_rig_calibration(*store, solution, dataset_ref,
                target_ref, left_ref, right_ref, solver_implementation(), series));
            impl_->event("calibration.rig.created", "calibration", created.descriptor.id.value);
            return created.reference();
        });
}
std::vector<CalibrationEntry> Runtime::calibrations() const {
    std::vector<CalibrationEntry> out;
    for (const auto &a : impl_->store->list())
        if (calibration_kind(a.type.name))
            out.push_back({a, impl_->store->artifact_revision(a.id).reference});
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        return std::tuple{a.artifact.type.name, a.reference.id, a.reference.revision, a.artifact.id} <
               std::tuple{b.artifact.type.name, b.reference.id, b.reference.revision, b.artifact.id};
    });
    return out;
}
CalibrationInfo Runtime::calibration_info(const Id &id) const { return inspect_calibration(*impl_->store, id); }
std::optional<ActiveCalibrationInfo> Runtime::active_calibration(const Id &id) const {
    calibration_text(id.value, "logical_device_id");
    if (auto active = impl_->store->active_calibration(id))
        return ActiveCalibrationInfo{id, active->reference, active->artifact};
    return {};
}
void Runtime::activate_calibration(const Id &device_id, const Id &rig_id) {
    calibration_text(device_id.value, "logical_device_id");
    calibration_input(*impl_->store, rig_id, ca::rig_type);
    std::vector<ca::CameraComponent> components;
    const auto current = devices();
    auto found = std::find_if(current.begin(), current.end(), [&](const auto &d) { return d.id == device_id; });
    if (found != current.end())
        components = calibration_checked(discovered_activation_components(*found, current));
    calibration_checked(ca::activate_rig_calibration(*impl_->store, device_id, rig_id, components));
    impl_->event("calibration.activated", "calibration", device_id.value + " " + rig_id.value);
}
void Runtime::clear_calibration(const Id &id) {
    calibration_text(id.value, "logical_device_id");
    impl_->store->clear_active_calibration(id);
    impl_->event("calibration.cleared", "calibration", id.value);
}
} // namespace mantis::services
