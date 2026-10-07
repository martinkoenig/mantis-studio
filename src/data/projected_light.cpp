#include <cmath>
#include <cstring>
#include <mantis/projected_light.hpp>
#include <set>

namespace mantis::data {
namespace {
void require(bool ok, std::string message) {
    if (!ok)
        fail(Status::invalid_argument, std::move(message), "projected-light");
}
template <class F> Result<void> checked(F f) {
    try {
        f();
        return {};
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    }
}
void text(const std::string &s, bool nonempty = true, size_t bound = max_semantic_string) {
    require((!nonempty || !s.empty()) && s.size() <= bound, "String is empty or exceeds semantic bound");
}
void id(const Id &v) {
    text(v.value, true, max_semantic_id);
}
template <class T> void id(const SemanticId<T> &v) {
    id(v.id);
}
template <class T, class F> void present(const Evidence<T> &v, F f) {
    if (auto p = v.get())
        f(*p);
}
template <class T> void bound(const std::vector<T> &v, size_t n = max_semantic_entries) {
    require(v.size() <= n, "Table exceeds semantic entry bound");
}
template <class T> void unique(const std::vector<T> &v, size_t n = max_semantic_entries) {
    bound(v, n);
    std::set<T> seen;
    for (const auto &x : v)
        require(seen.insert(x).second, "Duplicate semantic identity");
}
void components(const std::vector<ComponentId> &v, size_t n = max_participants) {
    unique(v, n);
    for (const auto &x : v)
        id(x);
}
bool contains(const std::vector<ComponentId> &v, const ComponentId &x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}
void type(const schema::DataTypeId &v) {
    text(v.name);
    require(v.name.find('.') != std::string::npos && v.version > 0, "Invalid schema identity");
}
void hash(const Hash &v) {
    text(v.algorithm);
    text(v.hex);
    require(v.hex.size() % 2 == 0 && std::all_of(v.hex.begin(), v.hex.end(),
                                                 [](char c) {
                                                     return (c >= '0' && c <= '9') ||
                                                            (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                                                 }),
            "Hash requires hexadecimal bytes");
}
void reference(const ContentReference &v) {
    id(v.id);
    type(v.type);
    present(v.hash, hash);
}
void program(const ProgramReference &v) {
    id(v.id);
    present(v.hash, hash);
    present(v.content, [&](const auto &r) {
        reference(r);
        require(r.type == schema::acquisition_program, "Program reference has wrong schema");
        require(r.id == v.id.id, "Program reference identity mismatch");
        if (r.hash.get() && v.hash.get())
            require(*r.hash.get() == *v.hash.get(), "Program hash mismatch");
    });
}
void calibration_ref(const ExactCalibrationReference &v) {
    id(v.calibration.id);
    require(v.calibration.schema_version > 0, "Calibration schema must be positive");
    present(v.content, [&](const auto &r) {
        reference(r);
        require(r.type.version == v.calibration.schema_version,
                "Calibration content reference schema mismatch");
        require(r.revision == v.calibration.revision, "Calibration content reference revision mismatch");
        require(r.hash.get(), "Resolved calibration content reference requires an established hash");
    });
}
void calibration_kind(const ExactCalibrationReference &v, std::string_view name) {
    calibration_ref(v);
    present(v.content, [&](const auto &r) {
        require(r.type.name == name && r.type.version == v.calibration.schema_version,
                "Calibration content reference kind/schema mismatch");
    });
}
void clock(const ClockIdentity &v) {
    id(v.domain.id);
    text(v.domain.name, false);
    id(v.generation);
}
bool same_clock(const ClockIdentity &a, const ClockIdentity &b) {
    return a.domain.id == b.domain.id && a.generation == b.generation;
}
void timestamp(const SemanticTimestamp &v) {
    clock(v.clock);
}
void runtime_timestamp(const RuntimeTimestamp &v) {
    clock(v.clock);
}
void interval(const TimeInterval &v) {
    timestamp(v.start);
    timestamp(v.end);
    require(same_clock(v.start.clock, v.end.clock), "Interval mixes clock domains/generations");
    require(v.start.nanoseconds <= v.end.nanoseconds, "Interval end precedes start");
}
void mapping(const ClockMappingEvidence &v) {
    id(v.mapping.source.id);
    id(v.mapping.target.id);
    text(v.mapping.source.name, false);
    text(v.mapping.target.name, false);
    id(v.source_generation);
    id(v.target_generation);
    reference(v.reference);
    require(std::isfinite(v.mapping.scale) && v.mapping.scale > 0 && std::isfinite(v.mapping.offset_ns) &&
                std::isfinite(v.mapping.uncertainty_ns) && v.mapping.uncertainty_ns >= 0,
            "Invalid clock mapping");
}
void step(const StepInstance &v) {
    id(v.run_id);
}
void frame(const SourceFrameKey &v) {
    id(v.camera);
    id(v.stream.id);
    id(v.stream.generation);
}
void frameset(const FrameSetKey &v) {
    id(v.run_id);
    id(v.stream.id);
    id(v.stream.generation);
}
void trigger(const TriggerKey &v) {
    id(v.run_id);
    id(v.source);
    id(v.controller_generation);
}
void evidence_key(const EvidenceKey &v) {
    id(v.run_id);
}
void bundle_key(const BundleKey &v) {
    id(v.run_id);
}
void source(const EvidenceSource &v) {
    id(v.source);
    present(v.reference, reference);
    if (v.method == EvidenceMethod::validated_executor)
        require(v.reference.get(), "Executor evidence requires immutable contract reference");
    require(v.method >= EvidenceMethod::software_dispatch && v.method <= EvidenceMethod::imported,
            "Invalid evidence method");
}
void scope(EvidenceScope v, const EvidenceSource &s) {
    require(v >= EvidenceScope::controller_register && v <= EvidenceScope::optical_emission,
            "Invalid evidence scope");
    require(s.method != EvidenceMethod::software_dispatch && s.method != EvidenceMethod::software_association,
            "Software request/correspondence cannot establish observed/effective state");
    require(s.method == EvidenceMethod::controller_report || s.method == EvidenceMethod::register_readback ||
                s.method == EvidenceMethod::electrical_readback ||
                s.method == EvidenceMethod::optical_sensor ||
                s.method == EvidenceMethod::validated_executor || s.method == EvidenceMethod::imported,
            "Evidence method cannot establish emitter state");
    if (s.method == EvidenceMethod::register_readback || s.method == EvidenceMethod::controller_report)
        require(v == EvidenceScope::controller_register,
                "Register readback cannot establish electrical/optical state");
    if (s.method == EvidenceMethod::electrical_readback)
        require(v == EvidenceScope::electrical_enable,
                "Electrical readback cannot establish optical emission");
    if (s.method == EvidenceMethod::optical_sensor)
        require(v == EvidenceScope::optical_emission, "Optical sensor scope mismatch");
    if (s.method == EvidenceMethod::validated_executor)
        require(s.reference.get(), "Executor guarantee requires an immutable evidence contract reference");
}
void state(EmitterState v) {
    require(v == EmitterState::off || v == EmitterState::on, "Invalid OFF/ON state");
}
void acknowledgement(const Acknowledgement &v) {
    id(v.request);
    source(v.evidence);
    present(v.time, timestamp);
    scope(v.scope, v.evidence);
    require(v.stage == AcknowledgementStage::acceptance || v.stage == AcknowledgementStage::completion,
            "Invalid acknowledgement stage");
    require(v.result >= AcknowledgementResult::success && v.result <= AcknowledgementResult::failed,
            "Invalid acknowledgement result");
    require(v.evidence.method == EvidenceMethod::controller_report ||
                v.evidence.method == EvidenceMethod::validated_executor ||
                v.evidence.method == EvidenceMethod::imported,
            "Acknowledgement requires controller/executor evidence");
}
void association(const ExposureAssociation &v) {
    frame(v.frame);
    trigger(v.trigger);
    source(v.evidence);
    require(v.method >= AssociationMethod::native_trigger && v.method <= AssociationMethod::imported,
            "Invalid exposure association method");
    present(v.native_trigger, [&](const auto &n) {
        id(n.controller);
        id(n.generation);
        require(n.controller == v.trigger.source && n.generation == v.trigger.controller_generation,
                "Exposure native trigger identity has mismatched generation scope");
    });
    if (v.method == AssociationMethod::native_trigger) {
        require(v.native_trigger.get(),
                "Native exposure association requires actual scoped native trigger identity");
        require(v.evidence.method == EvidenceMethod::camera_metadata ||
                    v.evidence.method == EvidenceMethod::controller_report,
                "Native trigger association requires source evidence");
    }
    if (v.method == AssociationMethod::validated_executor)
        require(v.evidence.method == EvidenceMethod::validated_executor && v.evidence.reference.get(),
                "Executor association requires its immutable evidence contract");
}
void exposure(const ExposureEvidence &v) {
    auto duration = [](Duration d) {
        require(d.count() > 0, "Established exposure duration must be positive");
    };
    present(v.requested_duration, duration);
    present(v.startup_readback_duration, duration);
    present(v.integration_duration, duration);
    present(v.interval, [](const auto &i) {
        interval(i);
        require(i.end.nanoseconds > i.start.nanoseconds, "Exposure interval must be positive");
    });
    source(v.evidence);
    present(v.uncertainty_ns,
            [](double x) { require(std::isfinite(x) && x >= 0, "Invalid exposure uncertainty"); });
    if (v.interval.get() || v.integration_duration.get())
        require(v.evidence.method != EvidenceMethod::software_dispatch &&
                    v.evidence.method != EvidenceMethod::software_association,
                "Request cannot establish exposure timing");
}
void camera_frame(const CameraFrameEvidence &v) {
    frame(v.frame);
    text(v.camera_role);
    require(v.width > 0 && v.height > 0, "Source image dimensions must be positive");
    present(v.source_timestamp, timestamp);
    present(v.host_received, runtime_timestamp);
    present(v.timestamp_meaning, [](auto m) {
        require(m >= TimestampMeaning::exposure_start && m <= TimestampMeaning::device_event,
                "Invalid timestamp meaning");
    });
    present(v.exposure, exposure);
    present(v.sync, [&](const auto &s) {
        id(s.group.id);
        require(s.quality >= time::SyncQuality::unknown && s.quality <= time::SyncQuality::hardware,
                "Invalid SyncQuality");
        present(s.hardware_association, [&](const auto &a) {
            association(a);
            require(a.frame == v.frame, "Sync association references another frame");
        });
        if (s.quality == time::SyncQuality::hardware) {
            auto a = s.hardware_association.get();
            require(a && (a->method == AssociationMethod::native_trigger ||
                          a->method == AssociationMethod::validated_executor),
                    "Hardware SyncQuality requires actual trigger-to-camera evidence");
        }
    });
    present(v.camera_calibration, [](const auto &r) { calibration_kind(r, "org.mantis.CameraCalibration"); });
    present(v.rig_calibration, [](const auto &r) { calibration_kind(r, "org.mantis.RigCalibration"); });
    present(v.original_calibration, calibration_ref);
}
void participants(const Participants &v) {
    bound(v.cameras, 16);
    components(v.emitters);
    components(v.controllers);
    require(v.cameras.size() + v.emitters.size() + v.controllers.size() > 0 &&
                v.cameras.size() + v.emitters.size() + v.controllers.size() <= max_participants,
            "Participants require 1..64 components");
    std::set<ComponentId> seen;
    std::set<StreamId> streams;
    for (const auto &c : v.cameras) {
        id(c.component);
        id(c.stream);
        text(c.role);
        require(seen.insert(c.component).second && streams.insert(c.stream).second,
                "Duplicate camera/stream participant");
    }
    for (const auto &e : v.emitters)
        require(seen.insert(e).second, "Duplicate participant");
    for (const auto &c : v.controllers)
        require(seen.insert(c).second, "Duplicate participant");
}
bool camera_participant(const Participants &p, const ComponentId &id_) {
    return std::any_of(p.cameras.begin(), p.cameras.end(), [&](const auto &c) { return c.component == id_; });
}
bool participant(const Participants &p, const ComponentId &v) {
    return camera_participant(p, v) || contains(p.emitters, v) || contains(p.controllers, v);
}
void implementation(const ImplementationIdentity &v) {
    id(v.implementation);
    text(v.build);
    present(v.configuration, reference);
}
void emitter(const EmitterEvidence &v, const std::vector<CameraFrameEvidence> &frames) {
    id(v.emitter);
    bound(v.exposure_effective, 16);
    present(v.commanded, [&](const auto &c) {
        id(c.request);
        id(c.target);
        state(c.state);
        runtime_timestamp(c.dispatched);
        require(c.target == v.emitter, "Emitter command target mismatch");
    });
    present(v.acknowledged, [&](const auto &a) {
        acknowledgement(a);
        if (v.commanded.get())
            require(a.request == v.commanded.get()->request, "Emitter acknowledgement request mismatch");
    });
    present(v.observed, [](const auto &o) {
        require(o.state.has_value(), "Observed state value is required; absence is not OFF");
        state(*o.state);
        source(o.evidence);
        scope(o.scope, o.evidence);
        present(o.time, timestamp);
        present(o.coverage, interval);
    });
    std::set<SourceFrameKey> seen;
    for (const auto &x : v.exposure_effective) {
        frame(x.frame);
        require(seen.insert(x.frame).second, "Duplicate camera effective-state association");
        auto found =
            std::find_if(frames.begin(), frames.end(), [&](const auto &f) { return f.frame == x.frame; });
        require(found != frames.end(), "Effective-state camera has no source frame evidence");
        present(x.state, [&](const auto &s) {
            require(s.frame == x.frame, "Effective-state frame mismatch");
            require(s.state.has_value(), "Effective state value is required; absence is not OFF");
            state(*s.state);
            source(s.evidence);
            scope(s.scope, s.evidence);
            interval(s.coverage);
            auto e = found->exposure.get();
            auto i = e ? e->interval.get() : nullptr;
            require(i && same_clock(i->start.clock, s.coverage.start.clock) &&
                        s.coverage.start.nanoseconds <= i->start.nanoseconds &&
                        s.coverage.end.nanoseconds >= i->end.nanoseconds,
                    "Effective state must cover the entire established exposure in its clock generation");
        });
    }
}
void reason(AcquisitionReason v) {
    require(v >= AcquisitionReason::none && v <= AcquisitionReason::cleanup_failure,
            "Invalid acquisition reason");
}
uint64_t multiply(uint64_t a, uint64_t b, std::string message) {
    require(!b || a <= UINT64_MAX / b, std::move(message));
    return a * b;
}
uint64_t add(uint64_t a, uint64_t b, std::string message) {
    require(a <= UINT64_MAX - b, std::move(message));
    return a + b;
}
void validate_program(const AcquisitionProgram &v) {
    require(v.type == schema::acquisition_program, "Expected AcquisitionProgram schema 1");
    program(v.identity);
    participants(v.participants);
    bound(v.steps, max_program_steps);
    require(!v.steps.empty(), "Program requires ordered nonempty steps");
    require(v.repetitions > 0, "Repetition count must be positive; zero is not unlimited");
    auto expanded = multiply(v.steps.size(), v.repetitions, "Step repetition multiplication overflow");
    require(expanded <= max_executed_steps, "Program exceeds 1,000,000 executed step instances");
    const auto &b = v.bounds;
    require(b.max_duration.count() > 0 && b.max_on_duration.count() > 0,
            "Run and ON duration bounds must be positive finite");
    require(b.max_step_instances >= expanded && b.max_step_instances <= max_executed_steps &&
                b.max_commands > 0 && b.max_events >= expanded && b.max_bytes > 0 &&
                b.max_in_flight_captures > 0,
            "Run resource bounds must be positive and admit declared execution");
    std::set<uint32_t> indices;
    std::set<std::string> labels;
    for (const auto &s : v.steps) {
        text(s.label);
        require(indices.insert(s.index).second && labels.insert(s.label).second,
                "Duplicate step index/label");
        bound(s.emitters, max_participants);
        require(s.emitters.size() == v.participants.emitters.size(),
                "Step requires complete emitter OFF/ON vector");
        std::set<ComponentId> states;
        for (const auto &e : s.emitters) {
            id(e.emitter);
            state(e.state);
            require(contains(v.participants.emitters, e.emitter), "Step references unknown emitter");
            require(states.insert(e.emitter).second, "Duplicate emitter state in step");
        }
        require(s.settle.count() >= 0 && s.max_duration.count() > 0 && s.settle < s.max_duration,
                "Step requires nonnegative settle shorter than positive finite duration");
        require(s.evidence_requirement >= EvidenceRequirement::commanded_only &&
                    s.evidence_requirement <= EvidenceRequirement::exposure_effective,
                "Invalid evidence requirement");
        require(s.required_scope >= EvidenceScope::controller_register &&
                    s.required_scope <= EvidenceScope::optical_emission,
                "Invalid required evidence scope");
        require(s.settle < b.max_duration, "Settle interval cannot fit within run deadline");
        if (s.capture.mode != CaptureMode::none &&
            std::any_of(s.emitters.begin(), s.emitters.end(),
                        [](const auto &e) { return e.state == EmitterState::on; }))
            require(s.settle < b.max_on_duration, "ON settle interval cannot fit within continuous ON bound");
        const auto &c = s.capture;
        components(c.cameras, 16);
        require(c.mode >= CaptureMode::none && c.mode <= CaptureMode::hardware_trigger,
                "Invalid capture mode");
        for (const auto &camera : c.cameras)
            require(camera_participant(v.participants, camera), "Capture references unknown camera");
        if (c.mode == CaptureMode::none) {
            require(c.cameras.empty() && !c.trigger,
                    "Control-only step cannot contain capture/trigger targets");
            require(s.evidence_requirement != EvidenceRequirement::exposure_effective,
                    "Control-only step cannot require exposure-effective evidence");
        } else {
            require(!c.cameras.empty(), "Capture requires explicit cameras");
            require((c.mode == CaptureMode::hardware_trigger) == c.trigger.has_value(),
                    "Hardware capture requires trigger intent; free-running forbids it");
        }
        if (c.trigger) {
            const auto &t = *c.trigger;
            id(t.controller);
            id(t.request);
            components(t.endpoints);
            require(contains(v.participants.controllers, t.controller),
                    "Trigger references unknown controller");
            require(!t.endpoints.empty(), "Trigger requires intended endpoints");
            for (const auto &e : t.endpoints)
                require(participant(v.participants, e), "Trigger references unknown endpoint");
            for (const auto &camera : c.cameras)
                require(contains(t.endpoints, camera), "Trigger omits capture camera endpoint");
        }
    }
}
void validate_trigger(const TriggerEvent &v) {
    require(v.type == schema::trigger_event, "Expected TriggerEvent schema 1");
    trigger(v.key);
    step(v.step);
    id(v.request);
    source(v.evidence);
    require(v.step.run_id == v.key.run_id, "Trigger step belongs to another run");
    require(v.kind >= TriggerEvent::Kind::requested && v.kind <= TriggerEvent::Kind::cancelled,
            "Invalid trigger event kind");
    components(v.intended_endpoints);
    require(!v.intended_endpoints.empty(), "Trigger requires intended endpoints");
    present(v.native_trigger, [&](const auto &n) {
        id(n.controller);
        id(n.generation);
        require(n.controller == v.key.source && n.generation == v.key.controller_generation,
                "Native trigger identity requires matching controller generation scope");
        require(v.kind != TriggerEvent::Kind::requested,
                "Software request cannot establish native trigger identity");
    });
    present(v.device_time, timestamp);
    present(v.host_received, runtime_timestamp);
    present(v.host_dispatched, runtime_timestamp);
    present(v.clock_mapping, mapping);
    present(v.uncertainty_ns,
            [](double n) { require(std::isfinite(n) && n >= 0, "Invalid trigger uncertainty"); });
    present(v.actual_endpoints, [&](const auto &a) {
        components(a);
        require(v.kind == TriggerEvent::Kind::observed ||
                    v.kind == TriggerEvent::Kind::acknowledged_completed,
                "Event stage cannot establish actual delivery");
        for (const auto &e : a)
            require(contains(v.intended_endpoints, e), "Delivered endpoint not intended");
    });
    present(v.requested_exposure, frame);
    present(v.exposure_association, [&](const auto &a) {
        association(a);
        require(a.trigger == v.key, "Exposure association references another trigger event");
        if (a.native_trigger.get() && v.native_trigger.get())
            require(*a.native_trigger.get() == *v.native_trigger.get(),
                    "Exposure association native trigger mismatch");
        require(v.kind == TriggerEvent::Kind::observed ||
                    v.kind == TriggerEvent::Kind::acknowledged_completed,
                "Request/acceptance cannot establish exposure association");
        require(contains(v.intended_endpoints, a.frame.camera), "Exposure camera not an intended endpoint");
    });
    bool ack = v.kind == TriggerEvent::Kind::acknowledged_accepted ||
               v.kind == TriggerEvent::Kind::acknowledged_completed || v.kind == TriggerEvent::Kind::rejected;
    require(ack == (v.acknowledgement.get() != nullptr),
            "Acknowledgement event requires exactly its acknowledgement evidence");
    present(v.acknowledgement, [&](const auto &a) {
        acknowledgement(a);
        require(a.request == v.request, "Trigger acknowledgement request mismatch");
        require(a.evidence.source == v.key.source && v.evidence.source == v.key.source &&
                    (v.evidence.method == EvidenceMethod::controller_report ||
                     v.evidence.method == EvidenceMethod::validated_executor ||
                     v.evidence.method == EvidenceMethod::imported),
                "Trigger acknowledgement must come from its named controller");
        if (v.kind == TriggerEvent::Kind::acknowledged_accepted)
            require(a.stage == AcknowledgementStage::acceptance && a.result == AcknowledgementResult::success,
                    "Acceptance stage/result mismatch");
        if (v.kind == TriggerEvent::Kind::acknowledged_completed)
            require(a.stage == AcknowledgementStage::completion && a.result == AcknowledgementResult::success,
                    "Completion stage/result mismatch");
        if (v.kind == TriggerEvent::Kind::rejected)
            require(a.result == AcknowledgementResult::rejected || a.result == AcknowledgementResult::failed,
                    "Rejection result mismatch");
    });
    if (v.kind == TriggerEvent::Kind::requested)
        require(v.evidence.method == EvidenceMethod::software_dispatch && v.host_dispatched.get(),
                "Request requires dispatch evidence/time");
    if (v.kind == TriggerEvent::Kind::observed)
        require(v.evidence.method != EvidenceMethod::software_dispatch &&
                    v.evidence.method != EvidenceMethod::software_association,
                "Observed trigger requires event-source evidence");
}
void validate_evidence(const AcquisitionEvidence &v) {
    require(v.type == schema::acquisition_evidence, "Expected AcquisitionEvidence schema 1");
    evidence_key(v.key);
    program(v.program);
    participants(v.participants);
    text(v.diagnostic, false);
    present(v.step, [&](const auto &s) {
        step(s);
        require(s.run_id == v.key.run_id, "Evidence step belongs to another run");
    });
    unique(v.causal_predecessors);
    for (const auto &k : v.causal_predecessors) {
        evidence_key(k);
        require(k.run_id == v.key.run_id && k.ordinal < v.key.ordinal,
                "Causal predecessor must precede evidence in same run");
    }
    bound(v.implementations, max_participants);
    require(!v.implementations.empty(), "Evidence requires implementation/config version identity");
    std::set<Id> implementations;
    for (const auto &i : v.implementations) {
        implementation(i);
        require(implementations.insert(i.implementation).second, "Duplicate implementation identity");
    }
    present(v.frameset, [&](const auto &f) {
        frameset(f);
        require(f.run_id == v.key.run_id, "FrameSet belongs to another run");
    });
    bound(v.frames, 16);
    std::set<SourceFrameKey> seen_frames;
    std::map<StreamId, GenerationId> frame_generations;
    for (const auto &f : v.frames) {
        camera_frame(f);
        require(seen_frames.insert(f.frame).second, "Duplicate source frame");
        auto [it, inserted] = frame_generations.emplace(f.frame.stream.id, f.frame.stream.generation);
        require(inserted || it->second == f.frame.stream.generation,
                "Evidence mixes source stream generations");
        present(f.sync, [&](const auto &s) {
            present(s.hardware_association, [&](const auto &a) {
                require(a.trigger.run_id == v.key.run_id, "Hardware sync trigger belongs to another run");
            });
        });
        auto c = std::find_if(v.participants.cameras.begin(), v.participants.cameras.end(),
                              [&](const auto &p) { return p.component == f.frame.camera; });
        require(c != v.participants.cameras.end() && c->stream == f.frame.stream.id &&
                    c->role == f.camera_role,
                "Source frame camera/stream/role disagrees with participants");
    }
    bound(v.emitters, max_participants);
    require(v.emitters.size() == v.participants.emitters.size(),
            "Evidence requires every emitter state group");
    std::set<ComponentId> emitters;
    for (const auto &e : v.emitters) {
        require(contains(v.participants.emitters, e.emitter) && emitters.insert(e.emitter).second,
                "Unknown/duplicate evidence emitter");
        emitter(e, v.frames);
        require(e.exposure_effective.size() == v.frames.size(),
                "Every emitter requires exactly one effective-state entry per source frame");
    }
    unique(v.triggers);
    std::map<ComponentId, GenerationId> controller_generations;
    for (const auto &t : v.triggers) {
        trigger(t);
        require(t.run_id == v.key.run_id, "Evidence trigger belongs to another run");
        auto [it, inserted] = controller_generations.emplace(t.source, t.controller_generation);
        require(inserted || it->second == t.controller_generation,
                "Evidence mixes controller generations within a run");
    }
    bound(v.clock_mappings, max_participants);
    for (const auto &m : v.clock_mappings)
        mapping(m);
    present(v.rig_calibration, [](const auto &r) { calibration_kind(r, "org.mantis.RigCalibration"); });
    require(v.disposition >= AcquisitionDisposition::startup &&
                v.disposition <= AcquisitionDisposition::cancelled,
            "Invalid acquisition disposition");
    reason(v.reason);
    if (v.disposition == AcquisitionDisposition::captured)
        require(v.frameset.get() && !v.frames.empty() && v.step.get(),
                "Captured evidence requires FrameSet/frame/step association");
    if (v.disposition == AcquisitionDisposition::control_only)
        require(!v.frameset.get() && v.frames.empty() && v.step.get(),
                "Control-only evidence cannot claim captured frames");
    if (v.disposition == AcquisitionDisposition::failed)
        require(v.reason != AcquisitionReason::none, "Failed evidence requires typed reason");
    bound(v.unresolved_requests);
    std::set<RequestId> requests;
    for (const auto &r : v.unresolved_requests) {
        id(r.request);
        id(r.target);
        reason(r.reason);
        require(participant(v.participants, r.target) && requests.insert(r.request).second &&
                    r.reason != AcquisitionReason::none,
                "Invalid/duplicate unresolved request");
    }
    bound(v.losses);
    for (const auto &l : v.losses) {
        id(l.source);
        reason(l.reason);
        require(l.kind >= LossKind::command && l.kind <= LossKind::excluded_frame, "Invalid loss kind");
        present(l.frame, frame);
        present(l.request, [](const auto &r) { id(r); });
    }
}
void validate_bundle(const AcquisitionBundle &v) {
    require(v.type == schema::acquisition_bundle, "Expected AcquisitionBundle schema 1");
    bundle_key(v.key);
    runtime_timestamp(v.published);
    validate_evidence(v.evidence);
    require(v.evidence.key.run_id == v.key.run_id, "Bundle evidence belongs to another run");
    require(v.triggers.size() <= max_bundle_members - 1 - (v.frameset ? 1 : 0),
            "Bundle exceeds 64 total members");
    if (v.frameset) {
        require(v.frameset->type == schema::frameset, "Bundle attachment must be FrameSet schema 1");
        require(!v.frameset->frames.empty() && v.frameset->frames.size() <= 16 &&
                    v.frameset->attributes.empty(),
                "FrameSet requires 1..16 image children and no flat attributes");
        auto key = v.evidence.frameset.get();
        require(key && v.frameset->header.sequence.value == key->sequence,
                "Bundle FrameSet/evidence sequence mismatch");
        require(v.frameset->frames.size() == v.evidence.frames.size(), "Bundle source frame count mismatch");
        for (size_t i = 0; i < v.frameset->frames.size(); ++i) {
            const auto &image = v.frameset->frames[i];
            require(image && image->type == schema::image && image->frames.empty(),
                    "FrameSet child must be an ImageFrame schema 1 without children");
            for (const auto &a : image->attributes) {
                auto r = schema::validate(a.descriptor, a.buffer.size());
                if (!r)
                    throw Failure(r.error());
            }
            require(image->header.sequence.value == v.evidence.frames[i].frame.native_sequence,
                    "Bundle source frame sequence/order mismatch");
        }
    }
    std::set<TriggerKey> keys;
    for (size_t i = 0; i < v.triggers.size(); ++i) {
        const auto &t = v.triggers[i];
        validate_trigger(t);
        require(t.key.run_id == v.key.run_id && keys.insert(t.key).second,
                "Bundle trigger belongs to another run or duplicates identity");
        for (size_t j = 0; j < i; ++j) {
            const auto &p = v.triggers[j].key;
            if (p.source == t.key.source && p.controller_generation == t.key.controller_generation)
                require(p.sequence < t.key.sequence,
                        "Trigger source-local event sequence must strictly increase");
        }
        require(std::find(v.evidence.triggers.begin(), v.evidence.triggers.end(), t.key) !=
                    v.evidence.triggers.end(),
                "Bundle trigger missing from evidence references");
    }
}
void validate_observation(const LaserObservation &v) {
    require(v.type == schema::laser_observation, "Expected LaserObservation schema 1");
    present(v.key.run_id, [](const auto &r) { id(r); });
    id(v.key.producer_stream);
    id(v.key.producer_generation);
    const auto &c = v.context;
    camera_frame(c.source);
    present(c.frameset, frameset);
    present(c.bundle, bundle_key);
    present(c.raw_input, reference);
    id(c.optical_frame.id);
    text(c.optical_frame.name);
    present(c.preprocessing, [](const auto &p) {
        for (double n : p.original_from_processed)
            require(std::isfinite(n), "Preprocessing transform must be finite");
        const auto &m = p.original_from_processed;
        double determinant = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                             m[2] * (m[3] * m[7] - m[4] * m[6]);
        require(std::isfinite(determinant) && determinant != 0, "Preprocessing transform must be invertible");
        present(p.reference, reference);
    });
    components(c.requested_emitters);
    unique(c.emitter_patterns);
    for (const auto &p : c.emitter_patterns) {
        id(p.emitter);
        id(p.pattern);
    }
    bound(c.emitter_evidence, max_participants);
    std::set<ComponentId> emitters;
    for (const auto &e : c.emitter_evidence) {
        require(emitters.insert(e.emitter).second, "Duplicate observation emitter evidence");
        emitter(e, {c.source});
    }
    auto same_run = [&](const RunId &r) {
        require(v.key.run_id.get() && *v.key.run_id.get() == r,
                "Observation correlation requires matching established run");
    };
    present(c.frameset, [&](const auto &f) { same_run(f.run_id); });
    present(c.bundle, [&](const auto &b) { same_run(b.run_id); });
    present(c.correlation, [&](const auto &p) {
        program(p.program);
        step(p.step);
        same_run(p.step.run_id);
    });
    present(c.source.sync, [&](const auto &s) {
        present(s.hardware_association, [&](const auto &a) { same_run(a.trigger.run_id); });
    });
    present(c.acquisition_evidence, [&](const auto &e) {
        evidence_key(e);
        same_run(e.run_id);
    });
    unique(c.triggers);
    for (const auto &t : c.triggers) {
        trigger(t);
        same_run(t.run_id);
    }
    bound(c.clock_mappings, max_participants);
    for (const auto &m : c.clock_mappings)
        mapping(m);
    implementation(c.producer);
    present(c.parameters, reference);
    bound(c.exact_inputs);
    std::set<Id> inputs;
    for (const auto &r : c.exact_inputs) {
        reference(r);
        require(inputs.insert(r.id).second, "Duplicate exact input reference");
    }
    require(c.origin >= ObservationOrigin::real && c.origin <= ObservationOrigin::imported,
            "Invalid observation origin");
    if (c.origin != ObservationOrigin::imported)
        require(v.key.run_id.get(), "Real/synthetic observation requires an established run identity");
    present(c.producer_completed, runtime_timestamp);
    text(v.diagnostic, false);
    require(v.disposition >= ObservationDisposition::success &&
                v.disposition <= ObservationDisposition::extractor_unavailable,
            "Invalid extractor disposition");
    require(v.sample_count <= max_observation_samples, "Observation exceeds software sample bound");
    components(v.emitter_dictionary, max_semantic_entries);
    unique(v.line_dictionary);
    for (const auto &l : v.line_dictionary) {
        id(l.emitter);
        id(l.pattern);
        id(l.local_line);
        require(contains(v.emitter_dictionary, l.emitter),
                "Line dictionary emitter is absent from emitter dictionary");
    }
    present(v.confidence_interpretation, implementation);
    bound(v.attributes, max_semantic_attributes);
    if (v.disposition != ObservationDisposition::success)
        require(v.sample_count == 0, "Failed/unavailable extraction cannot claim successful samples");
    if (v.sample_count == 0) {
        require(v.attributes.empty(),
                "N=0 requires no sample attributes; do not fabricate zero-shaped/fake rows");
        return;
    }
    std::map<std::string, const Attribute *, std::less<>> columns;
    uint64_t payload = 0;
    for (const auto &a : v.attributes) {
        text(a.descriptor.name);
        text(a.descriptor.unit, false);
        require(a.descriptor.name != "org.mantis.position",
                "Canonical XYZ position is not a LaserObservation attribute");
        auto r = schema::validate(a.descriptor, a.buffer.size());
        if (!r)
            throw Failure(r.error());
        require(a.descriptor.shape.front() == v.sample_count, "All sample columns must share N");
        require(columns.emplace(a.descriptor.name, &a).second, "Duplicate observation attribute");
        payload = add(payload, a.buffer.size(), "Observation payload sum overflow");
        require(payload <= max_semantic_payload, "Observation exceeds semantic payload bound");
    }
    auto column = [&](std::string_view name, schema::ScalarType scalar,
                      bool pixel = false) -> const Attribute * {
        auto it = columns.find(name);
        if (it == columns.end())
            return nullptr;
        auto a = it->second;
        const auto &d = a->descriptor;
        require(d.scalar == scalar && d.shape == (pixel ? std::vector<uint64_t>{v.sample_count, 2}
                                                        : std::vector<uint64_t>{v.sample_count}),
                std::string(name) + " has wrong scalar/shape");
        require(d.unit == (pixel ? "pixel" : ""), std::string(name) + " has wrong unit");
        auto size = schema::scalar_size(scalar);
        require(d.stride.back() >= size, std::string(name) + " has overlapping scalar stride");
        if (pixel)
            require(d.stride[0] >= d.stride[1] + size, "source_pixel has overlapping row stride");
        return a;
    };
    auto pixels = column(laser::source_pixel, schema::ScalarType::f32, true);
    auto flags = column(laser::quality_flags, schema::ScalarType::u32);
    require(pixels && flags, "N>0 requires source_pixel and quality_flags");
    auto ei = column(laser::emitter_index, schema::ScalarType::u32);
    auto ev = column(laser::emitter_valid, schema::ScalarType::u8);
    auto li = column(laser::line_index, schema::ScalarType::u32);
    auto lv = column(laser::line_valid, schema::ScalarType::u8);
    auto cf = column(laser::confidence, schema::ScalarType::f32);
    auto cv = column(laser::confidence_valid, schema::ScalarType::u8);
    require(bool(ei) == bool(ev) && bool(li) == bool(lv) && bool(cf) == bool(cv),
            "Value columns require paired validity masks");
    if (cf)
        require(v.confidence_interpretation.get(), "Confidence requires producer interpretation/version");
    // Read only required columns; generic namespaced extension buffers retain their original semantics.
    auto map = [](const Attribute *a) -> std::span<const std::byte> {
        if (!a)
            return {};
        auto r = a->buffer.map_read();
        if (!r)
            throw Failure(r.error());
        return *r;
    };
    auto pbytes = map(pixels), fbytes = map(flags), ebytes = map(ei), evbytes = map(ev), lbytes = map(li),
         lvbytes = map(lv), cbytes = map(cf), cvbytes = map(cv);
    auto read = []<class T>(const Attribute *a, std::span<const std::byte> b, uint64_t row,
                            uint64_t col = 0) {
        T x;
        auto offset = row * a->descriptor.stride[0];
        if (col)
            offset += col * a->descriptor.stride[1];
        std::memcpy(&x, b.data() + offset, sizeof(T));
        return x;
    };
    for (uint64_t row = 0; row < v.sample_count; ++row) {
        require(std::isfinite(read.operator()<float>(pixels, pbytes, row)) &&
                    std::isfinite(read.operator()<float>(pixels, pbytes, row, 1)),
                "Source pixel coordinates must be finite");
        uint32_t emitter_index_ = 0, line_index_ = 0;
        auto quality = read.operator()<uint32_t>(flags, fbytes, row);
        uint8_t emitter_valid_ = 0, line_valid_ = 0;
        if (ei) {
            emitter_valid_ = read.operator()<uint8_t>(ev, evbytes, row);
            emitter_index_ = read.operator()<uint32_t>(ei, ebytes, row);
            require(emitter_valid_ <= 1, "Emitter validity mask must be 0 or 1");
            require(bool(quality & laser::emitter_unknown) == (emitter_valid_ == 0),
                    "Emitter unknown quality bit contradicts validity mask");
            require(emitter_valid_ ? emitter_index_ < v.emitter_dictionary.size() : emitter_index_ == 0,
                    "Emitter index out of range or unknown placeholder is not zero");
        }
        if (li) {
            line_valid_ = read.operator()<uint8_t>(lv, lvbytes, row);
            line_index_ = read.operator()<uint32_t>(li, lbytes, row);
            require(line_valid_ <= 1, "Line validity mask must be 0 or 1");
            require(bool(quality & laser::line_unknown) == (line_valid_ == 0),
                    "Line unknown quality bit contradicts validity mask");
            require(line_valid_ ? line_index_ < v.line_dictionary.size() : line_index_ == 0,
                    "Line index out of range or unknown placeholder is not zero");
            if (line_valid_)
                require(emitter_valid_ &&
                            v.line_dictionary[line_index_].emitter == v.emitter_dictionary[emitter_index_],
                        "Valid line requires compatible valid emitter attribution");
        }
        if (cf) {
            auto mask = read.operator()<uint8_t>(cv, cvbytes, row);
            float x = read.operator()<float>(cf, cbytes, row);
            require(mask <= 1, "Confidence validity mask must be 0 or 1");
            require(std::isfinite(x) && (mask ? (x >= 0 && x <= 1) : x == 0),
                    "Valid confidence must be finite in [0,1]; unknown placeholder must be finite zero");
        }
        // Ambiguous/rejected and all future bits retain their L0/producer semantics unchanged.
    }
}
} // namespace
namespace laser {
schema::AttributeDescriptor source_pixel_descriptor(uint64_t n) {
    return {std::string(source_pixel), schema::ScalarType::f32, {n, 2}, {8, 4}, "pixel"};
}
schema::AttributeDescriptor quality_flags_descriptor(uint64_t n) {
    return {std::string(quality_flags), schema::ScalarType::u32, {n}, {4}, ""};
}
} // namespace laser
Result<void> validate(const AcquisitionProgram &v) {
    return checked([&] { validate_program(v); });
}
Result<void> validate(const TriggerEvent &v) {
    return checked([&] { validate_trigger(v); });
}
Result<void> validate(const AcquisitionEvidence &v) {
    return checked([&] { validate_evidence(v); });
}
Result<void> validate(const AcquisitionBundle &v) {
    return checked([&] { validate_bundle(v); });
}
Result<void> validate(const LaserObservation &v) {
    return checked([&] { validate_observation(v); });
}
Result<void> validate_successor(const AcquisitionBundle &previous, const AcquisitionBundle &next) {
    return checked([&] {
        validate_bundle(previous);
        validate_bundle(next);
        require(previous.key.run_id == next.key.run_id && previous.key.sequence < next.key.sequence,
                "Bundle sequence must strictly increase within the same run");
        require(previous.evidence.key.ordinal < next.evidence.key.ordinal,
                "Evidence causal ordinal must strictly increase");
        require(same_clock(previous.published.clock, next.published.clock) &&
                    previous.published.time.nanoseconds <= next.published.time.nanoseconds,
                "Publication time must remain in a monotonic clock generation");
        for (const auto &p : previous.evidence.triggers)
            for (const auto &n : next.evidence.triggers)
                if (p.source == n.source)
                    require(p.controller_generation == n.controller_generation,
                            "Controller generation change requires a new run");
        for (const auto &p : previous.triggers)
            for (const auto &n : next.triggers)
                if (p.key.source == n.key.source)
                    require(p.key.controller_generation == n.key.controller_generation &&
                                p.key.sequence < n.key.sequence,
                            "Published trigger source sequence must strictly increase within its generation");
        for (const auto &p : previous.evidence.frames)
            for (const auto &n : next.evidence.frames)
                if (p.frame.stream.id == n.frame.stream.id)
                    require(p.frame.stream.generation == n.frame.stream.generation,
                            "Source generation change requires a new run");
    });
}
Result<void> validate_successor(const LaserObservation &previous, const LaserObservation &next) {
    return checked([&] {
        validate_observation(previous);
        validate_observation(next);
        require(previous.key.run_id == next.key.run_id &&
                    previous.key.producer_stream == next.key.producer_stream &&
                    previous.key.producer_generation == next.key.producer_generation &&
                    previous.key.sequence < next.key.sequence,
                "Observation sequence must strictly increase within its run and producer stream generation");
        if (previous.context.source.frame.stream.id == next.context.source.frame.stream.id)
            require(previous.context.source.frame.stream.generation ==
                        next.context.source.frame.stream.generation,
                    "Observation source stream generation changed within a run");
    });
}
} // namespace mantis::data
