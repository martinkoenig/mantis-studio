#include "projected_sessions.hpp"
#include "projected_calibration.hpp"
#include <limits>
#include <mantis/artifact_store.hpp>
#include <mantis/replay.hpp>
#include <sstream>
#include <thread>
namespace mantis::services {
namespace {
void bounded_id(std::string_view value) {
    if (value.empty() || value.size() > data::max_semantic_id || value.find('\0') != value.npos)
        fail(Status::invalid_argument, "Projected identity must be nonempty, at most 256 bytes, without NUL");
}
class Digest : public std::streambuf {
    uint64_t position{};

  public:
    uint64_t value{14695981039346656037ull};

  protected:
    std::streamsize xsputn(const char *p, std::streamsize n) override {
        for (std::streamsize i = 0; i < n; ++i)
            value = (value ^ static_cast<unsigned char>(p[i])) * 1099511628211ull;
        position += n;
        return n;
    }
    int_type overflow(int_type c) override {
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            char b = traits_type::to_char_type(c);
            xsputn(&b, 1);
        }
        return traits_type::not_eof(c);
    }
    pos_type seekoff(off_type n, std::ios_base::seekdir d, std::ios_base::openmode) override {
        return n == 0 && d == std::ios_base::cur ? pos_type(position) : pos_type(off_type(-1));
    }
};
bool overlaps(const device::Descriptor &d, std::string_view plugin, const device::ProjectedGraph &g) {
    if (d.plugin_id != plugin)
        return false;
    if (d.id == g.parent || d.parent == g.parent)
        return true;
    return std::any_of(g.components.begin(), g.components.end(), [&](const auto &c) {
        return c.descriptor.id == d.id || c.descriptor.parent == d.id ||
               std::find(d.children.begin(), d.children.end(), c.descriptor.id) != d.children.end();
    });
}
void count(ProjectedEvidenceSummary &s, const data::AcquisitionBundle &b) {
    ++s.bundles;
    s.trigger_events += b.triggers.size();
    b.frameset ? ++s.captured : ++s.evidence_only;
    s.unresolved_request_records += b.evidence.unresolved_requests.size();
    s.loss_records += b.evidence.losses.size();
    for (const auto &e : b.evidence.emitters) {
        switch (e.commanded.presence()) {
        case data::Presence::established:
            ++s.commanded_established;
            break;
        case data::Presence::unknown:
            ++s.commanded_unknown;
            break;
        case data::Presence::unavailable:
            ++s.commanded_unavailable;
            break;
        }
        if (const auto *a = e.acknowledged.get()) {
            switch (*a->result) {
            case data::AcknowledgementResult::success:
                ++s.acknowledgement_success;
                break;
            case data::AcknowledgementResult::rejected:
                ++s.acknowledgement_rejected;
                break;
            case data::AcknowledgementResult::failed:
                ++s.acknowledgement_failed;
                break;
            }
        } else if (e.acknowledged.presence() == data::Presence::unknown)
            ++s.acknowledgement_unknown;
        else
            ++s.acknowledgement_unavailable;
        for (const auto &v : e.exposure_effective) {
            switch (v.state.presence()) {
            case data::Presence::established:
                ++s.effective_established;
                break;
            case data::Presence::unknown:
                ++s.effective_unknown;
                break;
            case data::Presence::unavailable:
                ++s.effective_unavailable;
                break;
            }
        }
    }
}
} // namespace
struct ProjectedSessions::Impl {
    plugins::Registry &registry;
    jobs::Manager &jobs;
    std::shared_ptr<artifact::Store> store;
    EventSink event;
    struct Session {
        mutable std::mutex mutex;
        ProjectedCaptureInfo info;
        device::ProjectedGraph graph;
        std::string request, signature;
        std::optional<Id> program_source;
        std::unique_ptr<device::ProjectedRun> run;
        std::shared_ptr<ProjectedCalibrationBinding> calibration;
        std::shared_ptr<const data::AcquisitionBundle> latest;
        std::jthread recorder;
        bool mutation_requested{}, start_resolved{};
        std::atomic_bool recorder_done{};
        std::condition_variable initialized;
        ~Session() {
            if (recorder.joinable())
                recorder.join();
        }
    };
    struct Replay {
        mutable std::mutex mutex;
        std::shared_ptr<const data::AcquisitionBundle> latest;
    };
    struct FinalizationLease {
        std::shared_ptr<artifact::Store> store;
        std::shared_ptr<Session> session;
        EventSink event;
        FinalizationLease(std::shared_ptr<artifact::Store> storage, std::shared_ptr<Session> s, EventSink e)
            : store(std::move(storage)), session(std::move(s)), event(std::move(e)) {}
        ~FinalizationLease() {
            // A queued job can be cancelled without ever invoking its task. Its
            // sealed artifact must still leave FINALIZING as a recoverable prefix.
            try {
                const auto raw = session->info.raw_artifact;
                if (store->get(raw).state == artifact::ArtifactState::finalizing) {
                    store->abandon(raw);
                    {
                        std::lock_guard lock(session->mutex);
                        session->info.storage_state = artifact::ArtifactState::recoverable;
                        session->info.recording_error =
                            Error{Status::cancelled, "Finalization did not complete", "projected-storage"};
                    }
                    event("projected.recording_failed", "projected", session->info.id.value);
                }
            } catch (const std::exception &e) {
                try {
                    event("error", "projected-storage", e.what());
                } catch (...) {
                }
            }
        }
    };
    std::map<Id, std::shared_ptr<Session>> sessions;
    std::map<Id, std::shared_ptr<Replay>> replays;
    std::vector<device::Descriptor> cameras;
    Impl(plugins::Registry &r, jobs::Manager &j, std::shared_ptr<artifact::Store> s, EventSink e)
        : registry(r), jobs(j), store(std::move(s)), event(std::move(e)) {}
    ~Impl() {
        // Stop bypasses recorder I/O; join all public calls/recorders before releasing executors.
        for (auto &[id, s] : sessions)
            s->run->stop();
        for (auto &[id, s] : sessions)
            if (s->recorder.joinable())
                s->recorder.join();
    }
    std::shared_ptr<Session> find(const Id &id) const {
        auto it = sessions.find(id);
        if (it == sessions.end())
            fail(Status::not_found, "Projected capture not found");
        return it->second;
    }
    bool active(const Session &s) const {
        const auto snapshot = s.run->snapshot();
        return !snapshot.cleanup_resolved || snapshot.terminal.close_error.has_value();
    }
    std::vector<ProjectedDeviceInfo> devices() const {
        std::vector<ProjectedDeviceInfo> result;
        for (const auto &p : registry.projected_light_parents(100))
            result.push_back({p.plugin_id, p.graph});
        if (result.size() > 64)
            fail(Status::busy, "Projected discovery exceeds 64 parents");
        return result;
    }
    device::ProjectedGraph graph(const ProjectedCaptureRequest &q) const {
        bounded_id(q.plugin_id);
        bounded_id(q.parent.value);
        auto parents = devices();
        auto found = std::find_if(parents.begin(), parents.end(), [&](const auto &p) {
            return p.plugin_id == q.plugin_id && p.graph.parent == q.parent;
        });
        if (found == parents.end())
            fail(Status::not_found, "Unknown plugin/parent projected graph");
        return found->graph;
    }
    data::AcquisitionProgram program(const ProjectedCaptureRequest &q) const {
        if (auto p = std::get_if<data::AcquisitionProgram>(&q.program))
            return *p;
        const auto &id = std::get<Id>(q.program);
        bounded_id(id.value);
        auto a = store->get(id);
        if (a.state != artifact::ArtifactState::finalized || a.type.name != "org.mantis.RawCapture" ||
            a.type.schema_version != 3)
            fail(Status::incompatible, "Program source requires finalized RawCapture schema 3");
        return store->capture_header(id).program;
    }
    void ownership(const std::string &plugin, const device::ProjectedGraph &g) const {
        for (const auto &d : cameras)
            if (overlaps(d, plugin, g))
                fail(Status::busy, "Projected resource belongs to camera capture");
        for (const auto &[id, s] : sessions)
            if (active(*s) && s->info.plugin_id == plugin)
                for (const auto &c : g.components)
                    if (overlaps(c.descriptor, plugin, s->graph))
                        fail(Status::busy, "Projected resource belongs to another run");
    }
    ProjectedCaptureInfo info(const std::shared_ptr<Session> &s) const {
        std::lock_guard lock(s->mutex);
        auto snapshot = s->run->snapshot();
        auto result = s->info;
        result.run = std::move(snapshot);
        result.active = !result.run.cleanup_resolved;
        return result;
    }
    void record(const std::shared_ptr<Session> &s) {
        struct Done {
            Session &s;
            ~Done() { s.recorder_done = true; }
        } done{*s};
        bool healthy = true, terminal_reported = false;
        auto report_terminal = [&] {
            const auto final = s->run->snapshot();
            const auto kind = final.state == device::ProjectedState::completed   ? "projected.completed"
                              : final.state == device::ProjectedState::cancelled ? "projected.cancelled"
                                                                                 : "projected.failed";
            event(kind, "projected", s->info.id.value);
            terminal_reported = true;
        };
        try {
            for (;;) {
                auto next = s->run->next(50);
                if (!next)
                    throw Failure(next.error());
                if (*next) {
                    auto bundle = **next;
                    // Frozen MRAWREC3 has a 32-byte header and 8-byte footer.
                    // Structural sizing never traverses pixels or queries SQLite.
                    const auto payload = data::bundle_encoded_size(*bundle);
                    uint64_t bytes{};
                    {
                        std::lock_guard lock(s->mutex);
                        const auto remaining = std::numeric_limits<uint64_t>::max() - s->info.bytes;
                        if (remaining < 40 || payload > remaining - 40)
                            fail(Status::io, "Projected capture byte accounting overflow",
                                 "projected-storage");
                        bytes = payload + 40;
                    }
                    auto source = s->calibration->source_provenance(bundle->key.sequence);
                    if (!source.empty())
                        store->initialize_projected_source_provenance(s->info.raw_artifact,
                                                                      std::move(source));
                    store->append_bundle(s->info.raw_artifact, *bundle);
                    std::lock_guard lock(s->mutex);
                    ++s->info.committed;
                    s->info.bytes += bytes;
                    count(s->info.evidence, *bundle);
                    s->latest = bundle;
                    s->info.latest_bundle_sequence = bundle->key.sequence;
                    if (const auto *step = bundle->evidence.step.get())
                        s->info.last_evidence_step = *step;
                } else if (s->run->snapshot().cleanup_resolved)
                    break;
            }
            {
                std::unique_lock lock(s->mutex);
                s->initialized.wait(lock, [&] { return s->start_resolved; });
            }
            auto snapshot = s->run->snapshot();
            report_terminal();
            store->record_run_outcome(s->info.raw_artifact, device::recorded_run_outcome(snapshot));
            const auto raw = s->info.raw_artifact;
            store->prepare_finalize(raw);
            {
                const auto a = store->get(raw);
                std::lock_guard lock(s->mutex);
                s->info.bytes = a.bytes;
                s->info.storage_state = a.state;
            }
            event("projected.finalizing", "projected", s->info.id.value);
            auto lease = std::make_shared<FinalizationLease>(store, s, event);
            auto id = jobs.submit("Finalize projected RawCapture",
                                  [storage = store, raw, notify = event, s,
                                   lease](jobs::Context &c) -> std::optional<artifact::ArtifactReference> {
                                      try {
                                          auto a = storage->finalize(raw, c.cancellation);
                                          {
                                              std::lock_guard lock(s->mutex);
                                              s->info.storage_state = a.state;
                                              s->info.bytes = a.bytes;
                                          }
                                          notify("projected.finalized", "projected", s->info.id.value);
                                          return artifact::ArtifactReference{raw, a.hash};
                                      } catch (const Failure &e) {
                                          {
                                              std::lock_guard lock(s->mutex);
                                              s->info.recording_error = e.error;
                                              s->info.storage_state = artifact::ArtifactState::recoverable;
                                          }
                                          throw;
                                      }
                                  });
            {
                std::lock_guard lock(s->mutex);
                s->info.finalization_job = id;
            }
        } catch (const Failure &e) {
            healthy = false;
            {
                std::lock_guard lock(s->mutex);
                s->info.recording_error = e.error;
            }
            s->run->recording_fault(e.error);
        } catch (const std::exception &e) {
            healthy = false;
            Error error{Status::io, e.what(), "projected-recording"};
            {
                std::lock_guard lock(s->mutex);
                s->info.recording_error = error;
            }
            s->run->recording_fault(error);
        }
        if (!healthy) {
            s->run->wait_terminal(60000);
            try {
                store->abandon(s->info.raw_artifact);
                std::lock_guard lock(s->mutex);
                s->info.storage_state = artifact::ArtifactState::recoverable;
            } catch (const std::exception &e) {
                event("error", "projected", e.what());
            }
            event("projected.recording_failed", "projected", s->info.id.value);
        }
        if (!terminal_reported)
            report_terminal();
    }
};
ProjectedSessions::ProjectedSessions(plugins::Registry &r, jobs::Manager &j,
                                     std::shared_ptr<artifact::Store> s, EventSink e)
    : impl_(std::make_unique<Impl>(r, j, std::move(s), std::move(e))) {}
ProjectedSessions::~ProjectedSessions() = default;
std::vector<ProjectedDeviceInfo> ProjectedSessions::devices() const { return impl_->devices(); }
bool ProjectedSessions::active(const std::string &plugin) const {
    for (const auto &[id, s] : impl_->sessions)
        if ((plugin.empty() || s->info.plugin_id == plugin) && (impl_->active(*s) || !s->recorder_done))
            return true;
    return false;
}
void ProjectedSessions::set_camera_resources(std::vector<device::Descriptor> resources) {
    impl_->cameras = std::move(resources);
}
void ProjectedSessions::check_camera(const std::vector<device::Descriptor> &resources) const {
    for (const auto &[id, s] : impl_->sessions)
        if (impl_->active(*s))
            for (const auto &d : resources)
                if (overlaps(d, s->info.plugin_id, s->graph))
                    fail(Status::busy, "Camera resource belongs to projected run");
}
device::ProjectedValidation ProjectedSessions::validate(const ProjectedCaptureRequest &q) {
    auto g = impl_->graph(q);
    auto p = impl_->program(q);
    impl_->ownership(q.plugin_id, g);
    try {
        // Reuse start's pure snapshot/compatibility check before executor opening.
        ProjectedCalibrationBinding calibration(*impl_->store, g, p);
    } catch (const Failure &e) {
        device::ProjectedValidation rejected;
        rejected.limits = g.limits;
        rejected.host_error = e.error;
        return rejected;
    }
    return device::ProjectedRun::validate_program(
        impl_->registry.open_projected_light(q.plugin_id, q.parent, 100), std::move(p), q.config);
}
ProjectedCaptureInfo ProjectedSessions::start(const ProjectedCaptureRequest &q, const std::string &request) {
    bounded_id(request);
    auto p = impl_->program(q);
    std::optional<Id> source;
    if (auto id = std::get_if<Id>(&q.program))
        source = *id;
    // Canonical bounded metadata, with fixed comparison identity, proves semantic retry identity.
    std::ostringstream signature(std::ios::binary);
    data::write_capture_header(
        signature, {p, {{"idempotency"}}, {{"idempotency"}}, device::recorded_run_config(q.config)});
    auto bytes = signature.str();
    if (std::holds_alternative<data::AcquisitionProgram>(q.program) && bytes.size() > projected_inline_bytes)
        fail(Status::invalid_argument, "Projected program exceeds 512 KiB");
    for (const auto &[id, s] : impl_->sessions)
        if (s->request == request) {
            if (s->signature != bytes || s->info.plugin_id != q.plugin_id || s->info.parent != q.parent ||
                s->program_source != source)
                fail(Status::invalid_argument, "Projected request ID reused with different payload");
            return impl_->info(s);
        }
    if (impl_->sessions.size() >= 64)
        fail(Status::busy, "Projected capture history capacity reached");
    auto g = impl_->graph(q);
    impl_->ownership(q.plugin_id, g);
    auto calibration = std::make_shared<ProjectedCalibrationBinding>(*impl_->store, g, p);
    auto s = std::make_shared<Impl::Session>();
    s->graph = g;
    s->request = request;
    s->signature = bytes;
    s->program_source = source;
    s->info.id = Id::random();
    s->info.plugin_id = q.plugin_id;
    s->info.parent = q.parent;
    s->info.program = p.identity;
    s->calibration = calibration;
    s->run = std::make_unique<device::ProjectedRun>(
        calibration_bound_executor(impl_->registry.open_projected_light(q.plugin_id, q.parent, 100),
                                   calibration),
        p, q.config);
    const auto identity = s->run->snapshot().identity;
    artifact::Provenance provenance;
    provenance.producer = q.plugin_id;
    provenance.parameters = {{"projected_capture_id", s->info.id.value}, {"parent_id", q.parent.value}};
    if (source)
        provenance.inputs.push_back(*source);
    calibration->provenance(provenance);
    s->info.raw_artifact = impl_->store->begin_projected_capture(
        {p, identity.run, identity.generation, device::recorded_run_config(q.config)}, provenance);
    s->info.bytes = impl_->store->get(s->info.raw_artifact).bytes;
    impl_->sessions.emplace(s->info.id, s);
    impl_->event("projected.created", "projected", s->info.id.value);
    // Header is synchronously durable before worker preparation can be requested.
    try {
        s->recorder = std::jthread([owner = impl_.get(), s] { owner->record(s); });
    } catch (const std::exception &e) {
        Error error{Status::io, e.what(), "projected-recorder"};
        {
            std::lock_guard lock(s->mutex);
            s->info.recording_error = error;
        }
        s->recorder_done = true;
        s->run->recording_fault(error);
        impl_->store->abandon(s->info.raw_artifact);
        {
            std::lock_guard lock(s->mutex);
            s->info.storage_state = artifact::ArtifactState::recoverable;
        }
        impl_->event("projected.recording_failed", "projected", s->info.id.value);
        return impl_->info(s);
    }
    struct Started {
        Impl::Session &s;
        ~Started() {
            std::lock_guard lock(s.mutex);
            s.start_resolved = true;
            s.initialized.notify_all();
        }
    } started_guard{*s};
    auto prepared = s->run->prepare();
    if (prepared) {
        impl_->event("projected.ready", "projected", s->info.id.value);
        auto started = s->run->start();
        if (started)
            impl_->event("projected.started", "projected", s->info.id.value);
    } else {
        const auto view = s->run->snapshot();
        if (!view.validation || !view.validation->accepted)
            impl_->event("projected.validation_failed", "projected", s->info.id.value);
    }
    return impl_->info(s);
}
ProjectedCaptureInfo ProjectedSessions::status(const Id &id) const { return impl_->info(impl_->find(id)); }
std::vector<ProjectedCaptureInfo> ProjectedSessions::list() const {
    std::vector<ProjectedCaptureInfo> result;
    for (const auto &[id, s] : impl_->sessions)
        result.push_back(impl_->info(s));
    return result;
}
ProjectedCaptureInfo ProjectedSessions::stop(const ProjectedStopRequest &q) {
    if (q.mode != ProjectedStopMode::normal_stop && q.mode != ProjectedStopMode::cancel)
        fail(Status::invalid_argument, "Invalid projected stop mode");
    auto s = impl_->find(q.capture);
    auto snapshot = s->run->snapshot();
    if (snapshot.identity.run != q.run || snapshot.identity.generation != q.generation)
        fail(Status::invalid_argument, "Projected run/generation mismatch");
    if (!s->mutation_requested && !snapshot.cleanup_resolved) {
        s->mutation_requested = true;
        impl_->event("projected.stopping", "projected", s->info.id.value);
        if (q.mode == ProjectedStopMode::normal_stop)
            s->run->stop();
        else
            s->run->cancel();
    }
    return impl_->info(s);
}
std::shared_ptr<const data::AcquisitionBundle> ProjectedSessions::latest(const Id &id) const {
    if (auto i = impl_->replays.find(id); i != impl_->replays.end()) {
        std::lock_guard lock(i->second->mutex);
        return i->second->latest;
    }
    auto s = impl_->find(id);
    std::lock_guard lock(s->mutex);
    return s->latest;
}
Id ProjectedSessions::replay(const Id &id, bool paced, bool verify) {
    if (impl_->replays.size() >= 64)
        fail(Status::busy, "Projected replay history capacity reached");
    auto preview = std::make_shared<Impl::Replay>();
    auto job = impl_->jobs.submit(
        verify ? "Verify projected replay" : "Replay projected capture",
        [store = impl_->store, id, paced, verify,
         preview](jobs::Context &c) -> std::optional<artifact::ArtifactReference> {
            uint64_t baseline{}, records{};
            for (unsigned pass = 0; pass < (verify ? 2u : 1u); ++pass) {
                device::BundleReplay source(store, id, paced);
                Digest digest;
                std::ostream canonical(&digest);
                if (verify)
                    data::write_capture_header(canonical, source.header());
                uint64_t count{};
                while (!source.finished()) {
                    c.cancellation.check();
                    auto next = source.next(50);
                    if (!next)
                        throw Failure(next.error());
                    if (!*next)
                        continue;
                    auto bundle = std::make_shared<const data::AcquisitionBundle>(std::move(**next));
                    if (verify)
                        data::write_bundle(canonical, *bundle);
                    ++count;
                    {
                        std::lock_guard lock(preview->mutex);
                        preview->latest = std::move(bundle);
                    }
                }
                if (verify) {
                    canonical.put(source.final_outcome() ? 1 : 0);
                    if (source.final_outcome())
                        data::write_run_outcome(canonical, {count, *source.final_outcome()});
                }
                if (pass && (baseline != digest.value || records != count))
                    fail(Status::corrupt, "Projected replay digest mismatch");
                baseline = digest.value;
                records = count;
            }
            c.update(1, "Projected replay PASS; passes=" + std::to_string(verify ? 2 : 1) + "; records=" +
                            std::to_string(records) + (verify ? "; digest=" + std::to_string(baseline) : ""));
            return artifact::ArtifactReference{id, store->get(id).hash};
        });
    impl_->replays.emplace(job, preview);
    return job;
}
std::function<void(const artifact::ArtifactDescriptor &)>
ProjectedSessions::artifact_update(const Id &raw) const {
    for (const auto &[id, s] : impl_->sessions)
        if (s->info.raw_artifact == raw)
            return [s](const artifact::ArtifactDescriptor &a) {
                std::lock_guard lock(s->mutex);
                s->info.storage_state = a.state;
                s->info.bytes = a.bytes;
            };
    return [](const artifact::ArtifactDescriptor &) {};
}
} // namespace mantis::services
