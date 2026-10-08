#include "projected_facts.hpp"
#include <algorithm>
#include <condition_variable>
#include <mantis/projected_run.hpp>
#include <mutex>
#include <thread>

namespace mantis::device {
namespace {
using namespace data;
using Clock = std::chrono::steady_clock;
bool terminal(ProjectedState s) {
    return s == ProjectedState::completed || s == ProjectedState::cancelled || s == ProjectedState::failed;
}
Error error(Status s, std::string text) {
    return {s, std::move(text), "projected-run"};
}
void require(bool b, AcquisitionReason reason, std::string text) {
    if (!b)
        throw std::pair{reason, error(Status::incompatible, std::move(text))};
}
template <class T> bool contains(const std::vector<T> &v, const T &x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}
template <class F> auto guarded(F f) -> decltype(f()) {
    try {
        return f();
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    } catch (const std::exception &e) {
        return std::unexpected(error(Status::plugin_failed, e.what()));
    } catch (...) {
        return std::unexpected(error(Status::plugin_failed, "Executor exception"));
    }
}
uint64_t product(uint64_t a, uint64_t b) {
    require(b == 0 || a <= UINT64_MAX / b, AcquisitionReason::resource_limit, "Resource count overflow");
    return a * b;
}
Clock::time_point deadline(Clock::time_point now, Duration duration) {
    auto ticks = std::chrono::duration_cast<Clock::duration>(duration);
    require(duration.count() > 0 && ticks > Clock::duration::zero() &&
                now.time_since_epoch() <= Clock::duration::max() - ticks,
            AcquisitionReason::resource_limit, "Deadline arithmetic overflow");
    return now + ticks;
}
} // namespace

struct ProjectedRun::Impl {
    std::unique_ptr<ProjectedExecutor> executor;
    const AcquisitionProgram program;
    const ProjectedGraph graph;
    const ProjectedRunConfig config;
    const ProjectedIdentity identity;
    const ProjectedClock now;
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    ProjectedRunSnapshot view;
    enum class Job { idle, prepare, start } job{Job::idle};
    bool abort_started{}, abort_done{}, finished{}, fault{}, cancellation{};
    bool prepare_done{}, start_done{}, started{}, prepare_submitted{}, start_submitted{};
    uint64_t wake_version{};
    Clock::time_point run_deadline{}, evidence_deadline{Clock::time_point::max()};
    std::vector<std::shared_ptr<const AcquisitionBundle>> queue;
    size_t queue_read{}, queue_write{}, queue_size{};
    std::jthread worker, watchdog;

    // Worker-only correlation state. Two bits per instance (covered, trigger-requested),
    // at most 250,000 bytes for the L1 million-instance ceiling.
    std::vector<uint8_t> coverage;
    std::vector<std::pair<uint32_t, size_t>> indices;
    std::vector<CausalOrdinal> evidence_keys;
    std::vector<TriggerKey> trigger_keys, pending_triggers;
    std::vector<EmitterCommand> commands;
    struct StepCommand {
        size_t slot;
        uint32_t emitter, command;
    };
    std::vector<StepCommand> step_commands;
    struct RequestRecord {
        RequestId request;
        ComponentId target;
        std::optional<size_t> step;
        bool operator==(const RequestRecord &) const = default;
    };
    std::vector<RequestRecord> unresolved_requests, resolved_requests;
    std::vector<SourceFrameKey> pending_frames;
    struct TriggerRecord {
        TriggerKey key;
        size_t slot;
    };
    std::vector<TriggerRecord> trigger_steps;
    struct AssociationRecord {
        SourceFrameKey frame;
        TriggerKey trigger;
        std::optional<size_t> slot;
    };
    std::vector<AssociationRecord> associations;
    std::vector<std::pair<ComponentId, GenerationId>> native_generations;
    struct FrameRecord {
        CameraFrameEvidence value;
        std::optional<size_t> slot;
    };
    std::vector<FrameRecord> frames;
    std::vector<std::pair<StreamId, GenerationId>> stream_generations;
    struct ControllerRecord {
        ComponentId source;
        GenerationId generation;
        EventSequence last;
        bool event{};
    };
    std::vector<ControllerRecord> controllers;
    struct PendingStep {
        size_t slot;
        AcquisitionEvidence evidence;
        Clock::time_point expires;
        bool represented{};
    };
    std::vector<PendingStep> pending;
    std::optional<AcquisitionBundle> previous;
    uint64_t payload_bytes{}, metadata_bytes{}, publication_metadata_bytes{}, event_count{};
    size_t entries{};

    Impl(std::unique_ptr<ProjectedExecutor> e, AcquisitionProgram p, ProjectedRunConfig c,
         ProjectedIdentitySource ids, ProjectedClock clock)
        : executor(std::move(e)), program(std::move(p)), graph(executor->graph()), config(c),
          identity(ids ? ids() : ProjectedIdentity{RunId{Id::random()}, GenerationId{Id::random()}}),
          now(clock ? std::move(clock) : ProjectedClock{[] { return Clock::now(); }}) {
        view.identity = identity;
        view.transitions.reserve(7);
        view.queue.capacity = config.queue_capacity;
        worker = std::jthread([this] { work(); });
        try {
            watchdog = std::jthread([this] { watch(); });
        } catch (...) {
            request(AcquisitionReason::device_failure,
                    error(Status::plugin_failed, "Supervisor creation failed"));
            worker.join();
            throw;
        }
    }
    ~Impl() {
        request(AcquisitionReason::user_stop, {});
        worker.join();
        watchdog.join();
    }
    uint32_t bound(uint32_t t, bool running = false) const {
        auto value = std::min(t, graph.limits.max_call_timeout_ms);
        if (running) {
            auto current = now();
            if (current >= run_deadline)
                return 0;
            auto remaining = run_deadline - current;
            auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
            value = static_cast<uint32_t>(std::min<uint64_t>(value, static_cast<uint64_t>(millis)));
        }
        return value;
    }
    void signal() {
        ++wake_version;
        changed.notify_all();
    }
    void state(ProjectedState value) {
        if (view.state != value) {
            view.state = value;
            view.transitions.push_back(value);
        }
    }
    void latch(AcquisitionReason reason, std::optional<Error> failure) {
        if (terminal(view.state))
            return;
        if (failure) {
            fault = true;
            if (!view.terminal.initiating_error) {
                view.terminal.initiating_error = std::move(failure);
                view.terminal.reason = reason;
            }
        } else if (reason == AcquisitionReason::user_cancel) {
            cancellation = true;
            if (!fault)
                view.terminal.reason = reason;
        } else if (view.terminal.reason == AcquisitionReason::none && !fault) {
            view.terminal.reason = reason;
        }
        state(failure && view.state == ProjectedState::validating ? ProjectedState::failed
                                                                  : ProjectedState::stopping);
        signal();
    }
    void abort_once() {
        AcquisitionReason reason;
        {
            std::lock_guard lock(mutex);
            if (abort_started || terminal(view.state))
                return;
            abort_started = true;
            reason = view.terminal.reason;
        }
        // Never hold the state/queue mutex across an executor call.
        auto result = guarded([&] { return executor->abort(reason, bound(config.abort_timeout_ms)); });
        {
            std::lock_guard lock(mutex);
            if (result) {
                view.terminal.abort_outcome = std::move(*result);
                if (view.terminal.abort_outcome->error.category)
                    view.terminal.abort_error = error(Status::plugin_failed, "Structured abort failure");
                const auto &outcome = *view.terminal.abort_outcome;
                auto negative = [](const auto &value) { return value.get() && !*value.get(); };
                if (negative(outcome.inhibited) || negative(outcome.stale_work_fenced) ||
                    negative(outcome.off_requested))
                    view.terminal.abort_error = error(
                        Status::plugin_failed, "Executor reports an unsuccessful inhibit/fence/OFF request");
            } else
                view.terminal.abort_error = result.error();
            if (view.terminal.abort_error)
                fault = true;
            abort_done = true;
            signal();
        }
    }
    void request(AcquisitionReason reason, std::optional<Error> failure) {
        {
            std::lock_guard lock(mutex);
            if (terminal(view.state))
                return;
            latch(reason, std::move(failure));
        }
        abort_once();
    }
    void cleanup() {
        {
            std::lock_guard lock(mutex);
            if (view.state == ProjectedState::failed && !started)
                abort_done = true;
        }
        abort_once();
        {
            std::unique_lock lock(mutex);
            // Abort's finite deadline includes its result callback. Keep storage alive
            // until it returns; compliant executors finish within this finite bound.
            if (!changed.wait_for(lock, std::chrono::milliseconds(bound(config.abort_timeout_ms)),
                                  [&] { return abort_done; })) {
                fault = true;
                view.terminal.abort_error = error(Status::plugin_failed, "Abort deadline violated");
                view.terminal.close_error = error(Status::busy, "Cleanup refused while abort remains active");
                state(ProjectedState::failed);
                finished = true;
                signal();
                return;
            }
        }
        auto stopped = guarded([&] { return executor->stop(bound(config.cleanup_timeout_ms, started)); });
        auto closed = guarded([&] { return executor->close(bound(config.cleanup_timeout_ms, started)); });
        std::lock_guard lock(mutex);
        if (!stopped)
            view.terminal.stop_error = stopped.error();
        if (!closed)
            view.terminal.close_error = closed.error();
        fault |= !stopped || !closed;
        if (started && now() >= run_deadline && !view.terminal.initiating_error && !cancellation) {
            view.terminal.reason = AcquisitionReason::timeout;
            view.terminal.initiating_error =
                error(Status::plugin_failed, "Run deadline expired during cleanup");
            fault = true;
        }
        if (fault && !view.terminal.initiating_error && view.terminal.reason == AcquisitionReason::none)
            view.terminal.reason = AcquisitionReason::cleanup_failure;
        state(fault          ? ProjectedState::failed
              : cancellation ? ProjectedState::cancelled
                             : ProjectedState::completed);
        finished = true;
        signal();
    }
    void preflight() {
        auto valid = data::validate(program);
        if (!valid)
            throw std::pair{AcquisitionReason::rejected, valid.error()};
        require(!identity.run.id.value.empty() && !identity.generation.id.value.empty(),
                AcquisitionReason::rejected, "Empty run/generation identity");
        const auto &l = graph.limits;
        require(config.queue_capacity > 0 && config.queue_capacity <= l.max_pending_bundles &&
                    config.operation_timeout_ms > 0 && config.abort_timeout_ms > 0 &&
                    config.cleanup_timeout_ms > 0 && config.publication_timeout_ms > 0 &&
                    l.max_call_timeout_ms > 0 && l.max_call_timeout_ms <= 60000 &&
                    config.operation_timeout_ms <= 60000 && config.abort_timeout_ms <= 60000 &&
                    config.cleanup_timeout_ms <= 60000 && config.publication_timeout_ms <= 60000 &&
                    config.max_correlation_entries > 0,
                AcquisitionReason::resource_limit, "Invalid finite runtime configuration");
        const auto &b = program.bounds, &limit = l.bounds;
        require(program.steps.size() <= l.max_steps && program.participants.cameras.size() <= l.max_cameras,
                AcquisitionReason::resource_limit, "Step/camera limit exceeded");
        // Canonical validation has already checked multiplication against UINT64_MAX.
        auto count = program.repetitions * program.steps.size();
        require(count <= limit.max_step_instances && b.max_step_instances <= limit.max_step_instances &&
                    b.max_duration <= limit.max_duration && b.max_on_duration <= limit.max_on_duration &&
                    b.max_commands <= limit.max_commands && b.max_events <= limit.max_events &&
                    b.max_bytes <= limit.max_bytes &&
                    b.max_in_flight_captures <= limit.max_in_flight_captures,
                AcquisitionReason::resource_limit, "Program bounds exceed executor limits");
        auto component = [&](ComponentId id, ParticipantKind kind) -> const ProjectedComponent & {
            auto found = std::find_if(graph.components.begin(), graph.components.end(), [&](const auto &c) {
                return c.descriptor.id == id.id &&
                       (c.kind == kind ||
                        (kind == ParticipantKind::controller && c.kind == ParticipantKind::parent &&
                         (has_capability(c.descriptor, "org.mantis.trigger.hardware.v1") ||
                          has_capability(c.descriptor, "org.mantis.emitter.power-control.v1"))));
            });
            require(found != graph.components.end(), AcquisitionReason::rejected,
                    "Participant outside graph");
            return *found;
        };
        for (const auto &camera : program.participants.cameras) {
            const auto &c = component(camera.component, ParticipantKind::image);
            require(image_participant(c.descriptor) && c.image_source &&
                        c.image_source->stream == camera.stream && c.role == camera.role,
                    AcquisitionReason::rejected, "Camera binding differs from graph");
        }
        for (auto e : program.participants.emitters)
            (void)component(e, ParticipantKind::emitter);
        for (auto c : program.participants.controllers)
            (void)component(c, ParticipantKind::controller);
        for (const auto &step : program.steps) {
            require(step.max_duration <= l.max_step_duration, AcquisitionReason::resource_limit,
                    "Step duration exceeds executor limit");
            for (auto camera : step.capture.cameras)
                require(contains(component(camera, ParticipantKind::image).capture_modes, *step.capture.mode),
                        AcquisitionReason::rejected, "Inaccessible capture mode");
            if (step.capture.trigger) {
                const auto &controller =
                    component(step.capture.trigger->controller, ParticipantKind::controller);
                require(has_capability(controller.descriptor, "org.mantis.trigger.hardware.v1") &&
                            contains(controller.trigger_modes, CaptureMode::hardware_trigger),
                        AcquisitionReason::rejected, "Inaccessible trigger mode");
                for (auto endpoint : step.capture.trigger->endpoints)
                    require(contains(controller.trigger_endpoints, endpoint.id), AcquisitionReason::rejected,
                            "Inaccessible trigger endpoint");
            }
            for (const auto &intent : step.emitters) {
                const auto &emitter = component(intent.emitter, ParticipantKind::emitter);
                require(has_capability(emitter.descriptor, "org.mantis.emitter.power-control.v1") &&
                            contains(emitter.emitter_states, intent.state),
                        AcquisitionReason::rejected, "Inaccessible emitter state");
                auto available = [&](EvidenceMethod method) {
                    return contains(emitter.evidence_methods, method);
                };
                bool command = available(EvidenceMethod::software_dispatch) ||
                               available(EvidenceMethod::validated_executor);
                bool ack = available(EvidenceMethod::controller_report) ||
                           available(EvidenceMethod::validated_executor);
                bool effective = available(EvidenceMethod::validated_executor) ||
                                 available(EvidenceMethod::register_readback) ||
                                 available(EvidenceMethod::electrical_readback) ||
                                 available(EvidenceMethod::optical_sensor);
                require(
                    command &&
                        (*step.evidence_requirement != EvidenceRequirement::controller_acknowledged || ack) &&
                        (*step.evidence_requirement != EvidenceRequirement::exposure_effective ||
                         effective) &&
                        (*step.evidence_requirement == EvidenceRequirement::commanded_only ||
                         contains(emitter.evidence_scopes, step.required_scope)),
                    AcquisitionReason::rejected, "Inaccessible evidence requirement/scope");
            }
        }
        // Run-global identities must fit the *declared* command/event maxima, not
        // just the minimum facts needed to cover steps. References to unresolved
        // identities must also be resolvable within those same finite budgets.
        auto step_links = product(count, program.participants.emitters.size());
        auto source_keys = product(b.max_events, program.participants.cameras.size());
        auto associations_bound = detail::add_bytes(source_keys, product(b.max_events, 2));
        auto request_bound = detail::add_bytes(step_links, detail::add_bytes(b.max_commands,
            product(b.max_events, program.participants.cameras.size() +
                    program.participants.controllers.size() + 1)));
        auto required_entries = std::max({b.max_events, b.max_commands, count, step_links,
                                          source_keys, associations_bound, request_bound,
                                          uint64_t{data::max_participants + 1}});
        require(required_entries <= config.max_correlation_entries,
                AcquisitionReason::resource_limit, "Declared identity budgets exceed local correlation capacity");
        entries = static_cast<size_t>(required_entries);
        require(count <= entries, AcquisitionReason::resource_limit,
                "Step evidence cannot fit correlation reservation");
        uint64_t source_count = 0, command_count = 0, trigger_count = 0;
        for (const auto &step : program.steps) {
            source_count += step.capture.cameras.size(); // L1 bounds this sum to 256*16.
            if (*step.evidence_requirement != EvidenceRequirement::exposure_effective)
                command_count += step.emitters.size(); // At most 256*64.
            if (step.capture.trigger)
                ++trigger_count;
        }
        source_count = product(source_count, program.repetitions);
        command_count = product(command_count, program.repetitions);
        trigger_count = product(trigger_count, program.repetitions);
        require(source_count <= entries && command_count <= entries && trigger_count <= entries &&
                    count < b.max_events && trigger_count <= b.max_events - count - 1,
                AcquisitionReason::resource_limit,
                "Required frame/command/terminal event accounting cannot fit reservation");
        // Reserve metadata before arming. No large per-executed-step objects.
        uint64_t per_entry = sizeof(FrameRecord) + sizeof(TriggerKey) * 2 + sizeof(EmitterCommand) +
                             sizeof(CausalOrdinal) + 2 * sizeof(RequestRecord) + sizeof(SourceFrameKey) +
                             sizeof(TriggerRecord) + sizeof(AssociationRecord) + sizeof(StepCommand) + sizeof(PendingStep);
        uint64_t fixed = sizeof(Impl);
        auto reserve_bytes = [&](uint64_t n, uint64_t element) {
            if (n) fixed = detail::add_bytes(fixed, detail::allocation_bytes(product(n, element)));
        };
        reserve_bytes((count + 3) / 4, 1);
        reserve_bytes(config.queue_capacity, sizeof(std::shared_ptr<const AcquisitionBundle>));
        reserve_bytes(program.steps.size(), sizeof(std::pair<uint32_t, size_t>));
        // Twelve run-global tables, including the generic pending step table.
        fixed = detail::add_bytes(fixed, product(entries, per_entry));
        fixed = detail::add_bytes(fixed, product(12, detail::allocation_bytes(0)));
        reserve_bytes(data::max_participants, sizeof(decltype(native_generations)::value_type));
        reserve_bytes(data::max_participants, sizeof(ControllerRecord));
        reserve_bytes(data::max_participants + 1, sizeof(decltype(stream_generations)::value_type));
        fixed = detail::add_bytes(fixed, detail::heap_bytes(program));
        fixed = detail::add_bytes(fixed, detail::heap_bytes(graph));
        fixed = detail::add_bytes(fixed, detail::heap_bytes(identity.run));
        fixed = detail::add_bytes(fixed, detail::heap_bytes(identity.generation));
        require(fixed <= b.max_bytes, AcquisitionReason::resource_limit,
                "Correlation reservation exceeds byte budget");
        queue.resize(config.queue_capacity);
        coverage.assign((count + 3) / 4, 0);
        metadata_bytes = fixed;
        evidence_keys.reserve(entries);
        trigger_keys.reserve(entries);
        pending_triggers.reserve(entries);
        commands.reserve(entries);
        step_commands.reserve(entries);
        frames.reserve(entries);
        unresolved_requests.reserve(entries);
        resolved_requests.reserve(entries);
        pending_frames.reserve(entries);
        trigger_steps.reserve(entries);
        associations.reserve(entries);
        native_generations.reserve(data::max_participants);
        controllers.reserve(data::max_participants);
        stream_generations.reserve(data::max_participants + 1);
        pending.reserve(entries);
        for (size_t i = 0; i < program.steps.size(); ++i)
            indices.emplace_back(program.steps[i].index, i);
        std::sort(indices.begin(), indices.end());
        // Measure the actual reserved capacities (reserve may overallocate) rather
        // than assuming a particular standard-library growth policy.
        metadata_bytes = sizeof(Impl);
        auto charge_fixed = [&](uint64_t bytes) { metadata_bytes = detail::add_bytes(metadata_bytes, bytes); };
        charge_fixed(detail::heap_bytes(program));
        charge_fixed(detail::heap_bytes(graph));
        charge_fixed(detail::heap_bytes(identity.run));
        charge_fixed(detail::heap_bytes(identity.generation));
        std::lock_guard lock(mutex);
        charge_fixed(detail::heap_bytes(view.identity.run));
        charge_fixed(detail::heap_bytes(view.identity.generation));
        std::apply([&](const auto &...v) {
            auto charge_vector = [&](const auto &values) {
                using T = typename std::decay_t<decltype(values)>::value_type;
                if (values.capacity())
                    charge_fixed(detail::allocation_bytes(product(values.capacity(), sizeof(T))));
            };
            (charge_vector(v), ...);
        }, std::tie(queue, coverage, indices, evidence_keys, trigger_keys, pending_triggers, commands,
                    step_commands, frames, unresolved_requests, resolved_requests, pending_frames,
                    trigger_steps, associations, native_generations, controllers, stream_generations,
                    pending, view.transitions));
        require(metadata_bytes <= b.max_bytes, AcquisitionReason::resource_limit,
                "Actual metadata reservation exceeds byte budget");
        view.expected_steps = count;
    }
    uint8_t flags(size_t position) const {
        return (coverage[position / 4] >> (2 * (position % 4))) & 3;
    }
    void mark(size_t position, uint8_t bits) {
        coverage[position / 4] |= static_cast<uint8_t>(bits << (2 * (position % 4)));
    }
    bool covered() const {
        auto count = program.repetitions * program.steps.size();
        for (uint64_t position = 0; position < count; ++position)
            if (!(flags(position) & 1))
                return false;
        return true;
    }
    size_t slot(const StepInstance &s) const {
        auto it = std::lower_bound(indices.begin(), indices.end(), std::pair{s.step_index, size_t{0}});
        require(s.run_id == identity.run && s.repetition_index < program.repetitions && it != indices.end() &&
                    it->first == s.step_index,
                AcquisitionReason::contradictory_evidence, "Step outside active program");
        return s.repetition_index * program.steps.size() + it->second;
    }
    void room(size_t size) const {
        require(size < entries, AcquisitionReason::resource_limit, "Correlation reservation exhausted");
    }
    void count_event() {
        require(event_count < program.bounds.max_events, AcquisitionReason::resource_limit,
                "Event budget exceeded");
        ++event_count;
    }
    void controller(const TriggerKey &key, bool event) {
        require(key.run_id == identity.run && contains(program.participants.controllers, key.source),
                AcquisitionReason::contradictory_evidence, "Foreign trigger run/controller");
        auto it = std::find_if(controllers.begin(), controllers.end(),
                               [&](auto &c) { return c.source == key.source; });
        if (it == controllers.end()) {
            room(controllers.size());
            controllers.push_back({key.source, key.controller_generation, key.sequence, event});
        } else {
            require(it->generation == key.controller_generation &&
                        (!event || !it->event || it->last < key.sequence),
                    AcquisitionReason::contradictory_evidence, "Controller generation/sequence reuse");
            if (event) {
                it->last = key.sequence;
                it->event = true;
            }
        }
    }
    const ProjectedComponent &evidence_source(const EvidenceSource &source,
                                               std::optional<EvidenceScope> scope = {}) const {
        auto it = std::find_if(graph.components.begin(), graph.components.end(), [&](const auto &c) {
            return c.descriptor.id == source.source.id;
        });
        require(it != graph.components.end() && contains(it->evidence_methods, source.method) &&
                    (!scope || contains(it->evidence_scopes, *scope)),
                AcquisitionReason::contradictory_evidence, "Foreign or unadvertised evidence source/method/scope");
        return *it;
    }
    void emitter_source(const EvidenceSource &source, EvidenceScope scope, ComponentId target,
                        bool acknowledgement = false) const {
        const auto &c = evidence_source(source, scope);
        bool authority = (c.kind == ParticipantKind::controller || c.kind == ParticipantKind::parent) &&
                         has_capability(c.descriptor, "org.mantis.emitter.power-control.v1") &&
                         contains(c.controls, target.id);
        if (acknowledgement) {
            require(authority && (c.kind == ParticipantKind::parent ||
                                 contains(program.participants.controllers, source.source)),
                    AcquisitionReason::contradictory_evidence, "Acknowledgement lacks participating control authority");
        } else if (scope != EvidenceScope::optical_emission)
            require(c.descriptor.id == target.id || authority,
                    AcquisitionReason::contradictory_evidence, "Emitter readback from unrelated component");
    }
    void validate_sources(const AcquisitionBundle &bundle) const {
        for (const auto &emitter : bundle.evidence.emitters) {
            if (auto ack = emitter.acknowledged.get())
                emitter_source(ack->evidence, ack->scope, emitter.emitter, true);
            if (auto observed = emitter.observed.get())
                emitter_source(observed->evidence, observed->scope, emitter.emitter);
            for (const auto &effective : emitter.exposure_effective)
                if (auto value = effective.state.get())
                    emitter_source(value->evidence, value->scope, emitter.emitter);
        }
        for (const auto &frame : bundle.evidence.frames) {
            if (auto exposure = frame.exposure.get())
                (void)evidence_source(exposure->evidence);
            if (auto sync = frame.sync.get())
                if (auto association = sync->hardware_association.get())
                    (void)evidence_source(association->evidence);
        }
        for (const auto &trigger : bundle.triggers) {
            (void)evidence_source(trigger.evidence);
            if (auto ack = trigger.acknowledgement.get()) {
                const auto &source = evidence_source(ack->evidence, ack->scope);
                require(source.descriptor.id == trigger.key.source.id,
                        AcquisitionReason::contradictory_evidence, "Trigger acknowledgement from unrelated authority");
            }
            if (auto association = trigger.exposure_association.get())
                (void)evidence_source(association->evidence);
        }
    }
    static void acknowledgement_outcome(const Acknowledgement &ack) {
        if (ack.result == AcknowledgementResult::rejected)
            require(false, AcquisitionReason::rejected, "Emitter acknowledgement rejected");
        if (ack.result == AcknowledgementResult::failed)
            require(false, AcquisitionReason::device_failure, "Emitter acknowledgement failed");
    }
    void correlate(const AcquisitionBundle &bundle) {
        const auto &e = bundle.evidence;
        require(e.key.run_id == identity.run && bundle.key.run_id == identity.run,
                AcquisitionReason::contradictory_evidence, "Foreign run evidence");
        auto valid = data::validate(bundle);
        if (!valid)
            throw std::pair{AcquisitionReason::contradictory_evidence,
                            error(Status::incompatible, valid.error().message)};
        if (previous) {
            auto successor = data::validate_successor(*previous, bundle);
            if (!successor)
                throw std::pair{AcquisitionReason::contradictory_evidence,
                                error(Status::incompatible, successor.error().message)};
        }
        validate_sources(bundle);
        for (const auto &emitter : e.emitters)
            if (auto ack = emitter.acknowledged.get())
                acknowledgement_outcome(*ack);
        auto publication_charge = detail::allocation_bytes(sizeof(AcquisitionBundle) + 2 * sizeof(void *));
        publication_charge = detail::add_bytes(publication_charge, detail::heap_bytes(bundle));
        if (bundle.frameset)
            publication_charge = detail::add_bytes(publication_charge, detail::packet_metadata_bytes(*bundle.frameset));
        charge(publication_metadata_bytes, publication_charge);
        for (const auto &p : e.causal_predecessors)
            require(std::binary_search(evidence_keys.begin(), evidence_keys.end(), p.ordinal),
                    AcquisitionReason::contradictory_evidence, "Causal predecessor was never published");
        room(evidence_keys.size());
        count_event();
        evidence_keys.push_back(e.key.ordinal);
        for (const auto &f : e.frames) {
            auto generation = std::find_if(stream_generations.begin(), stream_generations.end(),
                                           [&](auto &g) { return g.first == f.frame.stream.id; });
            if (generation == stream_generations.end())
                stream_generations.emplace_back(f.frame.stream.id, f.frame.stream.generation);
            else
                require(generation->second == f.frame.stream.generation,
                        AcquisitionReason::contradictory_evidence, "Source stream generation reset");
            auto old =
                std::find_if(frames.begin(), frames.end(), [&](auto &r) { return r.value.frame == f.frame; });
            auto position = e.step.get() ? std::optional{slot(*e.step.get())} : std::nullopt;
            for (const auto &record : frames)
                if (position && record.slot == position && record.value.frame.camera == f.frame.camera)
                    require(record.value.frame == f.frame, AcquisitionReason::contradictory_evidence,
                            "Step has a second capture instead of late evidence");
            if (old == frames.end()) {
                room(frames.size());
                frames.push_back({f, position});
            } else {
                require((!old->slot || !position || old->slot == position) && detail::refine(old->value, f),
                        AcquisitionReason::contradictory_evidence,
                        "Contradictory duplicate source frame identity");
                if (position)
                    old->slot = position;
            }
            if (const auto *sync = f.sync.get())
                if (const auto *a = sync->hardware_association.get()) {
                    trigger_reference(a->trigger);
                    auto known = std::find_if(associations.begin(), associations.end(), [&](auto &v) {
                        return v.frame == a->frame && v.trigger == a->trigger;
                    });
                    if (known == associations.end()) {
                        room(associations.size());
                        associations.push_back({a->frame, a->trigger, position});
                    }
                }
            std::erase(pending_frames, f.frame);
        }
        for (const auto &t : bundle.triggers) {
            auto position = slot(t.step);
            const auto &step = program.steps[position % program.steps.size()];
            require(step.capture.trigger.has_value(), AcquisitionReason::contradictory_evidence,
                    "Free-running/control step reported hardware trigger event");
            const auto &intent = *step.capture.trigger;
            auto actual = t.intended_endpoints, expected = intent.endpoints;
            std::sort(actual.begin(), actual.end());
            std::sort(expected.begin(), expected.end());
            require(t.key.source == intent.controller && t.request == intent.request && actual == expected,
                    AcquisitionReason::contradictory_evidence, "Trigger intent mismatch");
            controller(t.key, true);
            require(!contains(trigger_keys, t.key), AcquisitionReason::contradictory_evidence,
                    "Duplicate trigger identity");
            room(trigger_keys.size());
            count_event();
            trigger_keys.push_back(t.key);
            trigger_steps.push_back({t.key, position});
            if (t.native_trigger.get())
                native(*t.native_trigger.get());
            auto associate = [&](const SourceFrameKey &frame) {
                require(contains(step.capture.cameras, frame.camera),
                        AcquisitionReason::contradictory_evidence,
                        "Trigger associates a camera outside capture intent");
                frame_reference(frame);
                auto known = std::find_if(associations.begin(), associations.end(),
                                          [&](auto &v) { return v.frame == frame && v.trigger == t.key; });
                if (known == associations.end()) {
                    room(associations.size());
                    associations.push_back({frame, t.key, position});
                }
            };
            if (t.requested_exposure.get())
                associate(*t.requested_exposure.get());
            if (t.exposure_association.get()) {
                associate(t.exposure_association.get()->frame);
                resolve_request({t.request, t.exposure_association.get()->frame.camera, position});
            }
            if (t.kind == TriggerEvent::Kind::observed ||
                t.kind == TriggerEvent::Kind::acknowledged_completed)
                resolve_request({t.request, intent.controller, position});
            std::erase(pending_triggers, t.key);
            if (t.kind == TriggerEvent::Kind::requested)
                mark(position, 2);
            if (t.kind == TriggerEvent::Kind::rejected)
                require(false, AcquisitionReason::rejected, "Requested trigger rejected");
            if (t.kind == TriggerEvent::Kind::timed_out)
                require(false, AcquisitionReason::timeout, "Requested trigger timed out");
            if (t.kind == TriggerEvent::Kind::cancelled) {
                std::lock_guard lock(mutex);
                require(view.state != ProjectedState::running, AcquisitionReason::device_failure,
                        "Executor cancelled trigger without daemon stop/cancel");
            }
        }
        for (const auto &t : e.triggers)
            trigger_reference(t);
        for (const auto &a : associations) {
            auto event = std::find_if(trigger_steps.begin(), trigger_steps.end(),
                                      [&](auto &t) { return t.key == a.trigger; });
            if (event != trigger_steps.end() && a.slot)
                require(event->slot == *a.slot, AcquisitionReason::contradictory_evidence,
                        "Exposure association crosses step instance");
            auto frame =
                std::find_if(frames.begin(), frames.end(), [&](auto &f) { return f.value.frame == a.frame; });
            if (frame != frames.end() && frame->slot) {
                require(!a.slot || frame->slot == a.slot, AcquisitionReason::contradictory_evidence,
                        "Trigger/frame step mismatch");
                if (event != trigger_steps.end())
                    require(event->slot == *frame->slot, AcquisitionReason::contradictory_evidence,
                            "Trigger/frame step mismatch");
            }
        }
        for (const auto &unresolved : e.unresolved_requests) {
            RequestRecord request{unresolved.request, unresolved.target,
                                  e.step.get() ? std::optional{slot(*e.step.get())} : std::nullopt};
            if (!contains(resolved_requests, request) && !contains(unresolved_requests, request)) {
                room(unresolved_requests.size());
                unresolved_requests.push_back(std::move(request));
            }
        }
        if (const auto *set = e.frameset.get()) {
            auto old = std::find_if(stream_generations.begin(), stream_generations.end(),
                                    [&](auto &g) { return g.first == set->stream.id; });
            if (old == stream_generations.end())
                stream_generations.emplace_back(set->stream.id, set->stream.generation);
            else
                require(old->second == set->stream.generation, AcquisitionReason::contradictory_evidence,
                        "FrameSet stream generation reset");
        }
        for (const auto &emitter : e.emitters)
            if (const auto *command = emitter.commanded.get()) {
                auto old = std::find_if(commands.begin(), commands.end(),
                                        [&](const auto &c) { return c.request == command->request; });
                if (old != commands.end())
                    require(detail::refine(*old, *command), AcquisitionReason::contradictory_evidence,
                            "Contradictory duplicate command request");
                else {
                    room(commands.size());
                    require(commands.size() < program.bounds.max_commands, AcquisitionReason::resource_limit,
                            "Command budget exceeded");
                    commands.push_back(*command);
                }
                if (e.step.get()) {
                    auto position = slot(*e.step.get());
                    auto emitter_index = static_cast<uint32_t>(
                        std::find(program.participants.emitters.begin(), program.participants.emitters.end(),
                                  emitter.emitter) -
                        program.participants.emitters.begin());
                    auto command_index = static_cast<uint32_t>(
                        std::find_if(commands.begin(), commands.end(),
                                     [&](auto &v) { return v.request == command->request; }) -
                        commands.begin());
                    auto known = std::find_if(step_commands.begin(), step_commands.end(), [&](auto &v) {
                        return v.slot == position && v.emitter == emitter_index;
                    });
                    if (known == step_commands.end()) {
                        room(step_commands.size());
                        step_commands.push_back({position, emitter_index, command_index});
                    } else
                        require(known->command == command_index, AcquisitionReason::contradictory_evidence,
                                "Step command identity changed in late evidence");
                }
                if (const auto *ack = emitter.acknowledged.get())
                    if (ack->request == command->request && ack->result == AcknowledgementResult::success &&
                        ack->stage == AcknowledgementStage::completion)
                        resolve_request({command->request, command->target,
                                         e.step.get() ? std::optional{slot(*e.step.get())} : std::nullopt});
            }
        if (e.step.get()) {
            auto position = slot(*e.step.get());
            for (const auto &emitter : e.emitters)
                if (const auto *ack = emitter.acknowledged.get()) {
                    auto emitter_index = static_cast<uint32_t>(
                        std::find(program.participants.emitters.begin(), program.participants.emitters.end(),
                                  emitter.emitter) -
                        program.participants.emitters.begin());
                    auto known = std::find_if(step_commands.begin(), step_commands.end(), [&](auto &v) {
                        return v.slot == position && v.emitter == emitter_index;
                    });
                    if (known != step_commands.end()) {
                        require(commands[known->command].request == ack->request,
                                AcquisitionReason::contradictory_evidence,
                                "Late acknowledgement does not match step command");
                        if (ack->stage == AcknowledgementStage::completion &&
                            ack->result == AcknowledgementResult::success)
                            resolve_request({ack->request, emitter.emitter, position});
                    }
                }
        }
        if (bundle.frameset)
            for (const auto &image : bundle.frameset->frames)
                for (const auto &attribute : image->attributes) {
                    require(attribute.buffer.size() <=
                                remaining_bytes(),
                            AcquisitionReason::resource_limit, "Published bulk payload budget exceeded");
                    payload_bytes += attribute.buffer.size();
                }
        if (const auto *instance = e.step.get())
            step_evidence(slot(*instance), e);
        previous = bundle; // Pixels remain shared and immutable.
        require(internal_metadata_bytes() <= remaining_bytes(), AcquisitionReason::resource_limit,
                "Retained correlation metadata exceeds byte budget");
    }
    uint64_t remaining_bytes() const {
        auto used = detail::add_bytes(metadata_bytes, detail::add_bytes(publication_metadata_bytes, payload_bytes));
        require(used <= program.bounds.max_bytes, AcquisitionReason::resource_limit, "In-memory resource budget exceeded");
        return program.bounds.max_bytes - used;
    }
    void charge(uint64_t &counter, uint64_t bytes) {
        require(bytes <= remaining_bytes(), AcquisitionReason::resource_limit, "In-memory metadata budget exceeded");
        counter = detail::add_bytes(counter, bytes);
    }
    uint64_t internal_metadata_bytes() const {
        // Vector object storage was reserved/charged before start. Charge dynamic
        // contents of each actual correlation copy; no whole-bundle multiplier.
        uint64_t bytes = previous ? detail::heap_bytes(*previous) : 0;
        auto add = [&](const auto &v) { bytes = detail::add_bytes(bytes, detail::heap_bytes(v)); };
        for (const auto &v : pending) add(v.evidence);
        for (const auto &v : frames) add(v.value);
        for (const auto &v : commands) add(v);
        for (const auto &v : trigger_keys) add(v);
        for (const auto &v : pending_triggers) add(v);
        for (const auto &v : pending_frames) add(v);
        for (const auto &v : unresolved_requests) { add(v.request); add(v.target); }
        for (const auto &v : resolved_requests) { add(v.request); add(v.target); }
        for (const auto &v : trigger_steps) add(v.key);
        for (const auto &v : associations) { add(v.frame); add(v.trigger); }
        for (const auto &v : native_generations) { add(v.first); add(v.second); }
        for (const auto &v : stream_generations) { add(v.first); add(v.second); }
        for (const auto &v : controllers) { add(v.source); add(v.generation); }
        return bytes;
    }
    void frame_reference(const SourceFrameKey &key) {
        require(std::any_of(program.participants.cameras.begin(), program.participants.cameras.end(),
                            [&](auto &c) { return c.component == key.camera && c.stream == key.stream.id; }),
                AcquisitionReason::contradictory_evidence, "Foreign source frame reference");
        auto known = std::find_if(stream_generations.begin(), stream_generations.end(),
                                  [&](auto &g) { return g.first == key.stream.id; });
        if (known == stream_generations.end())
            stream_generations.emplace_back(key.stream.id, key.stream.generation);
        else
            require(known->second == key.stream.generation, AcquisitionReason::contradictory_evidence,
                    "Referenced source generation reset");
        if (std::none_of(frames.begin(), frames.end(), [&](auto &r) { return r.value.frame == key; }) &&
            !contains(pending_frames, key)) {
            room(pending_frames.size());
            pending_frames.push_back(key);
        }
    }
    void native(const NativeTriggerIdentity &value) {
        auto old = std::find_if(native_generations.begin(), native_generations.end(),
                                [&](auto &v) { return v.first == value.controller; });
        if (old == native_generations.end()) {
            room(native_generations.size());
            native_generations.emplace_back(value.controller, value.generation);
        } else
            require(old->second == value.generation, AcquisitionReason::contradictory_evidence,
                    "Native trigger generation reset");
    }
    void resolve_request(const RequestRecord &request) {
        if (!contains(resolved_requests, request)) {
            room(resolved_requests.size());
            resolved_requests.push_back(request);
        }
        std::erase(unresolved_requests, request);
    }
    void trigger_reference(const TriggerKey &key) {
        controller(key, false);
        if (!contains(trigger_keys, key) && !contains(pending_triggers, key)) {
            room(pending_triggers.size());
            pending_triggers.push_back(key);
        }
    }
    bool requirements(const AcquisitionStep &step, const AcquisitionEvidence &e) {
        bool satisfied = true;
        for (const auto &intent : step.emitters) {
            auto it = std::find_if(e.emitters.begin(), e.emitters.end(),
                                   [&](auto &v) { return v.emitter == intent.emitter; });
            require(it != e.emitters.end(), AcquisitionReason::contradictory_evidence,
                    "Missing participating emitter");
            auto command = it->commanded.get();
            if (command)
                require(command->target == intent.emitter && command->state == intent.state,
                        AcquisitionReason::contradictory_evidence, "Emitter command contradicts intent");
            if (const auto *observed = it->observed.get())
                require(observed->state == intent.state, AcquisitionReason::contradictory_evidence,
                        "Observed state contradicts intent");
            if (*step.evidence_requirement != EvidenceRequirement::exposure_effective)
                satisfied &= command != nullptr;
            if (const auto *ack = it->acknowledged.get()) {
                if (command)
                    require(ack->request == command->request, AcquisitionReason::contradictory_evidence,
                            "Acknowledgement request mismatch");
                acknowledgement_outcome(*ack);
            }
            if (*step.evidence_requirement == EvidenceRequirement::controller_acknowledged) {
                auto ack = it->acknowledged.get();
                satisfied &=
                    ack && command && ack->request == command->request && ack->scope == step.required_scope;
            }
            for (const auto &effective : it->exposure_effective)
                if (const auto *value = effective.state.get())
                    require(value->state == intent.state, AcquisitionReason::contradictory_evidence,
                            "Effective emitter state contradicts intent");
            if (*step.evidence_requirement == EvidenceRequirement::exposure_effective) {
                satisfied &= !e.frames.empty();
                for (const auto &frame : e.frames) {
                    auto value = std::find_if(it->exposure_effective.begin(), it->exposure_effective.end(),
                                              [&](auto &v) { return v.frame == frame.frame; });
                    satisfied &= value != it->exposure_effective.end() && value->state.get() &&
                                 value->state.get()->scope == step.required_scope;
                }
            }
        }
        return satisfied;
    }
    void step_evidence(size_t position, const AcquisitionEvidence &e) {
        const auto &step = program.steps[position % program.steps.size()];
        bool represented = step.capture.mode == CaptureMode::none
                               ? e.disposition == AcquisitionDisposition::control_only
                               : e.disposition == AcquisitionDisposition::captured;
        (void)requirements(step, e); // Every established fact must agree with intent.
        for (const auto &f : e.frames) {
            require(contains(step.capture.cameras, f.frame.camera), AcquisitionReason::contradictory_evidence,
                    "Step source camera is outside capture intent");
            if (step.capture.mode == CaptureMode::free_running)
                if (const auto *sync = f.sync.get())
                    require(!sync->hardware_association.get(), AcquisitionReason::contradictory_evidence,
                            "Free-running frame claims a hardware trigger association");
        }
        require(step.capture.mode != CaptureMode::none || e.frames.empty(),
                AcquisitionReason::contradictory_evidence, "Control step reports captured sources");
        if (represented) {
            std::vector<ComponentId> cameras;
            for (const auto &f : e.frames)
                cameras.push_back(f.frame.camera);
            auto expected = step.capture.cameras;
            std::sort(cameras.begin(), cameras.end());
            std::sort(expected.begin(), expected.end());
            require(cameras == expected, AcquisitionReason::contradictory_evidence,
                    "Captured camera set differs from intent");
        }
        if (flags(position) & 1) {
            std::lock_guard lock(mutex);
            ++view.late_evidence;
            return;
        }
        auto found =
            std::find_if(pending.begin(), pending.end(), [&](auto &p) { return p.slot == position; });
        if (found == pending.end()) {
            room(pending.size());
            pending.push_back({position, e, deadline(now(), step.max_duration), represented});
            found = std::prev(pending.end());
        } else {
            {
                std::lock_guard lock(mutex);
                ++view.late_evidence;
            }
            if (represented) {
                if (found->represented && e.frameset.get())
                    require(found->evidence.frameset.get() &&
                                *found->evidence.frameset.get() == *e.frameset.get(),
                            AcquisitionReason::contradictory_evidence,
                            "Late evidence changes FrameSet identity");
                found->evidence.disposition = e.disposition;
                found->evidence.frameset = e.frameset;
                found->represented = true;
            }
            // Evidence-only command/acknowledgement publications need not repeat
            // captured frames. Partial later frame proof must reference actual keys.
            for (const auto &incoming : e.frames) {
                auto old = std::find_if(found->evidence.frames.begin(), found->evidence.frames.end(),
                                        [&](auto &f) { return f.frame == incoming.frame; });
                if (old == found->evidence.frames.end()) {
                    require(!found->represented || represented, AcquisitionReason::contradictory_evidence,
                            "Late proof introduces another captured source");
                    found->evidence.frames.push_back(incoming);
                } else
                    require(detail::refine(*old, incoming), AcquisitionReason::contradictory_evidence,
                            "Late source frame contradiction");
            }
            for (const auto &incoming : e.emitters) {
                auto &old = *std::find_if(found->evidence.emitters.begin(), found->evidence.emitters.end(),
                                          [&](auto &v) { return v.emitter == incoming.emitter; });
                require(detail::refine(old.commanded, incoming.commanded),
                        AcquisitionReason::contradictory_evidence,
                        "Late command contradicts established fact");
                if (const auto *ack = incoming.acknowledged.get()) {
                    if (const auto *prior = old.acknowledged.get())
                        require(prior->request == ack->request && prior->result == ack->result,
                                AcquisitionReason::contradictory_evidence,
                                "Late acknowledgement contradiction");
                    if (!old.acknowledged.get() || ack->scope == step.required_scope)
                        old.acknowledged = *ack;
                }
                for (const auto &effective : incoming.exposure_effective) {
                    auto prior = std::find_if(old.exposure_effective.begin(), old.exposure_effective.end(),
                                              [&](auto &v) { return v.frame == effective.frame; });
                    if (prior == old.exposure_effective.end()) {
                        old.exposure_effective.push_back(effective);
                        continue;
                    }
                    if (const auto *value = effective.state.get()) {
                        if (const auto *established = prior->state.get())
                            require(established->state == value->state,
                                    AcquisitionReason::contradictory_evidence,
                                    "Late effective state contradiction");
                        if (!prior->state.get() || value->scope == step.required_scope)
                            prior->state = *value;
                    }
                }
            }
        }
        for (const auto &emitter : found->evidence.emitters)
            if (emitter.commanded.get() && emitter.acknowledged.get() &&
                emitter.acknowledged.get()->request == emitter.commanded.get()->request &&
                emitter.acknowledged.get()->stage == AcknowledgementStage::completion &&
                emitter.acknowledged.get()->result == AcknowledgementResult::success)
                resolve_request({emitter.commanded.get()->request, emitter.emitter, position});
        if (found->represented && requirements(step, found->evidence) &&
            (step.capture.mode != CaptureMode::hardware_trigger || (flags(position) & 2))) {
            mark(position, 1);
            pending.erase(found);
            std::lock_guard lock(mutex);
            ++view.covered_steps;
        }
        auto captures = std::count_if(pending.begin(), pending.end(), [&](const auto &p) {
            return p.represented && program.steps[p.slot % program.steps.size()].capture.mode != CaptureMode::none;
        });
        require(static_cast<uint64_t>(captures) <= program.bounds.max_in_flight_captures,
                AcquisitionReason::resource_limit, "Unresolved capture instance limit exceeded");
        update_evidence_deadline();
    }
    void resolve_pending() {
        for (auto it = pending.begin(); it != pending.end();) {
            const auto &step = program.steps[it->slot % program.steps.size()];
            if (it->represented && requirements(step, it->evidence) &&
                (step.capture.mode != CaptureMode::hardware_trigger || (flags(it->slot) & 2))) {
                mark(it->slot, 1);
                it = pending.erase(it);
                std::lock_guard lock(mutex);
                ++view.covered_steps;
            } else
                ++it;
        }
        update_evidence_deadline();
    }
    void update_evidence_deadline() {
        auto earliest = Clock::time_point::max();
        for (const auto &p : pending)
            earliest = std::min(earliest, p.expires);
        std::lock_guard lock(mutex);
        evidence_deadline = earliest;
        signal();
    }
    bool publish(std::shared_ptr<const AcquisitionBundle> bundle) {
        auto expires =
            std::min(run_deadline, deadline(now(), std::chrono::milliseconds(config.publication_timeout_ms)));
        std::unique_lock lock(mutex);
        while (queue_size == config.queue_capacity && view.state == ProjectedState::running) {
            auto current = now();
            if (current >= expires)
                break;
            changed.wait_for(lock, expires - current);
        }
        if (queue_size == config.queue_capacity || view.state != ProjectedState::running ||
            now() >= run_deadline) {
            view.terminal.unqueued_bundle = std::move(bundle);
            if (now() >= run_deadline)
                latch(AcquisitionReason::timeout,
                      error(Status::plugin_failed, "Run deadline expired at publication"));
            else if (view.state == ProjectedState::running && queue_size == config.queue_capacity) {
                ++view.queue.saturation_failures;
                latch(AcquisitionReason::resource_limit,
                      error(Status::busy, "Authoritative publication queue saturated"));
            }
            lock.unlock();
            abort_once();
            return false;
        }
        queue[queue_write] = std::move(bundle);
        queue_write = (queue_write + 1) % config.queue_capacity;
        ++queue_size;
        ++view.queue.produced;
        view.queue.occupancy = queue_size;
        view.queue.high_water = std::max(view.queue.high_water, queue_size);
        signal();
        return true;
    }
    void execute() {
        for (;;) {
            {
                std::lock_guard lock(mutex);
                if (view.state != ProjectedState::running)
                    return;
            }
            if (std::any_of(pending.begin(), pending.end(), [&](auto &p) { return now() >= p.expires; })) {
                request(AcquisitionReason::evidence_missing,
                        error(Status::incompatible, "Step evidence window expired"));
                return;
            }
            if (now() >= run_deadline) {
                request(AcquisitionReason::timeout, error(Status::plugin_failed, "Run deadline expired"));
                return;
            }
            auto output = guarded([&] { return executor->next(bound(config.operation_timeout_ms, true)); });
            if (!output) {
                request(output.error().code == Status::incompatible
                            ? AcquisitionReason::contradictory_evidence
                            : AcquisitionReason::device_failure,
                        output.error());
                return;
            }
            if (!*output) {
                std::unique_lock lock(mutex);
                // Cooperative poll pacing only, never optical settle/exposure timing.
                auto version = wake_version;
                changed.wait_for(lock, std::chrono::milliseconds(1), [&] { return wake_version != version; });
                continue;
            }
            auto bundle = std::make_shared<const AcquisitionBundle>(std::move(**output));
            try {
                require(
                    std::none_of(pending.begin(), pending.end(), [&](auto &p) { return now() >= p.expires; }),
                    AcquisitionReason::evidence_missing, "Late evidence arrived after step window");
                correlate(*bundle);
                resolve_pending();
            } catch (...) {
                std::lock_guard lock(mutex);
                view.terminal.unqueued_bundle = bundle;
                throw;
            }
            if (now() >= run_deadline) {
                {
                    std::lock_guard lock(mutex);
                    view.terminal.unqueued_bundle = bundle;
                }
                request(AcquisitionReason::timeout,
                        error(Status::plugin_failed, "Publication arrived after run deadline"));
                return;
            }
            const auto disposition = *bundle->evidence.disposition;
            bool end = disposition >= AcquisitionDisposition::completed;
            if (end) {
                std::lock_guard lock(mutex);
                view.terminal.executor_terminal = bundle;
            }
            if (!publish(bundle))
                return;
            if (end) {
                if (disposition == AcquisitionDisposition::failed)
                    request(bundle->evidence.reason,
                            error(Status::plugin_failed, "Executor terminal failure"));
                else if (disposition == AcquisitionDisposition::completed) {
                    require(covered() && pending.empty() && pending_triggers.empty() &&
                                pending_frames.empty() && unresolved_requests.empty(),
                            AcquisitionReason::evidence_missing,
                            "Terminal completion has unresolved execution/evidence");
                    request(AcquisitionReason::none, {});
                } else {
                    bool requested;
                    {
                        std::lock_guard lock(mutex);
                        requested = view.state == ProjectedState::stopping;
                    }
                    require(requested, AcquisitionReason::device_failure,
                            "Unsolicited executor stop/cancellation");
                }
                return;
            }
        }
    }
    void work() noexcept {
        try {
            for (;;) {
                Job next;
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock, [&] {
                        return job != Job::idle || view.state == ProjectedState::stopping ||
                               terminal(view.state);
                    });
                    if (view.state == ProjectedState::stopping || terminal(view.state))
                        break;
                    next = job;
                    job = Job::idle;
                }
                if (next == Job::prepare) {
                    preflight();
                    auto valid = guarded(
                        [&] { return executor->validate(program, bound(config.operation_timeout_ms)); });
                    if (!valid)
                        throw std::pair{AcquisitionReason::rejected, valid.error()};
                    {
                        std::lock_guard lock(mutex);
                        view.validation = *valid;
                    }
                    require(valid->accepted, AcquisitionReason::rejected,
                            "Executor validation rejected program");
                    {
                        std::lock_guard lock(mutex);
                        if (view.state == ProjectedState::stopping)
                            break;
                    }
                    auto prepared = guarded(
                        [&] { return executor->prepare(program, bound(config.operation_timeout_ms)); });
                    if (!prepared)
                        throw std::pair{AcquisitionReason::rejected, prepared.error()};
                    {
                        std::lock_guard lock(mutex);
                        view.preparation = *prepared;
                    }
                    require(prepared->accepted, AcquisitionReason::rejected,
                            "Executor preparation rejected program");
                    std::lock_guard lock(mutex);
                    prepare_done = true;
                    if (view.state == ProjectedState::validating)
                        state(ProjectedState::ready);
                    signal();
                } else {
                    {
                        std::lock_guard lock(mutex);
                        if (view.state != ProjectedState::ready)
                            break;
                    }
                    auto result = guarded([&] {
                        return executor->start(identity.run, identity.generation,
                                               bound(config.operation_timeout_ms));
                    });
                    if (!result)
                        throw std::pair{AcquisitionReason::device_failure, result.error()};
                    auto expires = deadline(now(), program.bounds.max_duration);
                    {
                        std::lock_guard lock(mutex);
                        start_done = true;
                        started = true;
                        run_deadline = expires;
                        if (view.state == ProjectedState::ready)
                            state(ProjectedState::running);
                        signal();
                    }
                    execute();
                    break;
                }
            }
        } catch (const std::pair<AcquisitionReason, Error> &e) {
            request(e.first, e.second);
        } catch (const detail::AccountingOverflow &) {
            request(AcquisitionReason::resource_limit, error(Status::busy, "In-memory accounting overflow"));
        } catch (const std::bad_alloc &) {
            request(AcquisitionReason::resource_limit, error(Status::busy, "Runtime reservation exhausted"));
        } catch (const std::exception &e) {
            request(AcquisitionReason::device_failure, error(Status::plugin_failed, e.what()));
        } catch (...) {
            request(AcquisitionReason::device_failure,
                    error(Status::plugin_failed, "Sequencer worker exception"));
        }
        // Validation/preparation faults must go directly to FAILED: no start/ON work.
        // Cleanup still releases resources; STOPPING is not a visible preflight state.
        try {
            cleanup();
        } catch (...) {
            std::lock_guard lock(mutex);
            view.terminal.close_error = error(Status::plugin_failed, "Cleanup exception");
            fault = true;
            state(ProjectedState::failed);
            finished = true;
            signal();
        }
    }
    void watch() noexcept {
        try {
            std::unique_lock lock(mutex);
            while (!finished) {
                auto version = wake_version;
                if (view.state != ProjectedState::running) {
                    changed.wait(lock, [&] { return finished || wake_version != version; });
                    continue;
                }
                auto expires = std::min(run_deadline, evidence_deadline);
                auto current = now();
                if (current >= expires) {
                    auto reason = evidence_deadline <= run_deadline ? AcquisitionReason::evidence_missing
                                                                    : AcquisitionReason::timeout;
                    lock.unlock();
                    request(reason, error(Status::plugin_failed, "Finite evidence/run deadline expired"));
                    lock.lock();
                } else
                    changed.wait_for(lock, expires - current,
                                     [&] { return finished || wake_version != version; });
            }
        } catch (...) {
            request(AcquisitionReason::device_failure,
                    error(Status::plugin_failed, "Deadline supervisor exception"));
        }
    }
    Result<void> submit(Job phase) {
        std::unique_lock lock(mutex);
        auto expected = phase == Job::prepare ? ProjectedState::validating : ProjectedState::ready;
        if (view.state != expected || job != Job::idle ||
            (phase == Job::prepare ? prepare_submitted : start_submitted))
            return std::unexpected(error(Status::busy, "Run lifecycle does not admit this operation"));
        if (phase == Job::prepare)
            prepare_submitted = true;
        else
            start_submitted = true;
        job = phase;
        signal();
        uint64_t calls = phase == Job::prepare ? 2 : 1;
        auto timeout = std::chrono::milliseconds(calls * bound(config.operation_timeout_ms) +
                                                 bound(config.abort_timeout_ms) +
                                                 2ull * bound(config.cleanup_timeout_ms));
        bool done = changed.wait_for(lock, timeout, [&] {
            return terminal(view.state) || (phase == Job::prepare ? prepare_done : start_done);
        });
        if (!done) {
            lock.unlock();
            request(AcquisitionReason::timeout, error(Status::plugin_failed, "Lifecycle deadline expired"));
            return std::unexpected(error(Status::plugin_failed, "Lifecycle deadline expired"));
        }
        if (view.terminal.initiating_error)
            return std::unexpected(*view.terminal.initiating_error);
        if ((phase == Job::prepare && !prepare_done) || (phase == Job::start && !start_done))
            return std::unexpected(error(Status::cancelled, "Run stopped during lifecycle operation"));
        return {};
    }
};

ProjectedRun::ProjectedRun(std::unique_ptr<ProjectedExecutor> executor, data::AcquisitionProgram program,
                           ProjectedRunConfig config, ProjectedIdentitySource ids, ProjectedClock clock) {
    if (!executor)
        fail(Status::invalid_argument, "Projected run requires an executor");
    impl_ = std::make_unique<Impl>(std::move(executor), std::move(program), config, std::move(ids),
                                   std::move(clock));
}
ProjectedRun::~ProjectedRun() = default;
Result<void> ProjectedRun::prepare() {
    return impl_->submit(Impl::Job::prepare);
}
Result<void> ProjectedRun::start() {
    return impl_->submit(Impl::Job::start);
}
void ProjectedRun::stop() {
    impl_->request(data::AcquisitionReason::user_stop, {});
}
void ProjectedRun::cancel() {
    impl_->request(data::AcquisitionReason::user_cancel, {});
}
Result<std::optional<std::shared_ptr<const data::AcquisitionBundle>>>
ProjectedRun::next(uint32_t timeout_ms) {
    if (timeout_ms > 60000)
        return std::unexpected(error(Status::invalid_argument, "Consumer timeout must be finite"));
    std::unique_lock lock(impl_->mutex);
    impl_->changed.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [&] { return impl_->queue_size != 0 || impl_->finished; });
    if (impl_->queue_size == 0)
        return std::optional<std::shared_ptr<const data::AcquisitionBundle>>{};
    auto value = std::move(impl_->queue[impl_->queue_read]);
    impl_->queue_read = (impl_->queue_read + 1) % impl_->config.queue_capacity;
    --impl_->queue_size;
    ++impl_->view.queue.consumed;
    impl_->view.queue.occupancy = impl_->queue_size;
    impl_->signal();
    return std::optional{std::move(value)};
}
bool ProjectedRun::wait_terminal(uint32_t timeout_ms) const {
    if (timeout_ms > 60000)
        return false;
    std::unique_lock lock(impl_->mutex);
    return impl_->changed.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                   [&] { return impl_->finished; });
}
ProjectedRunSnapshot ProjectedRun::snapshot() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->view;
}
void ProjectedRun::notify_clock_advanced() {
    std::lock_guard lock(impl_->mutex);
    impl_->signal();
}
} // namespace mantis::device
