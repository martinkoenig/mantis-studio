#pragma once
#include <mantis/projected_light_io.hpp>
#include <sstream>
namespace storage_fixture {
using namespace mantis;
using namespace mantis::data;
using namespace std::chrono_literals;
inline const RunId run{{"run"}};
inline const GenerationId gen{{"gen"}};
inline const ComponentId camera{{"cam"}}, emitter{{"emit"}}, controller{{"ctl"}};
inline RuntimeTimestamp host(int64_t n = 0) { return {{n}, {{{"host"}, "runtime"}, gen}}; }
inline SemanticTimestamp sensor(int64_t n = 0) { return {n, {{{"sensor"}, "device"}, gen}}; }
inline ContentReference ref(std::string id = "cfg") {
    return {{std::move(id)}, {"org.example.Config", 1}, Hash{"fnv1a64", "abcd"}, 5};
}
inline ProgramReference program_ref() {
    return {{{"prog"}},
            Hash{"fnv1a64", "abcd"},
            ContentReference{{"prog"}, schema::acquisition_program, Hash{"fnv1a64", "abcd"}, 2}};
}
inline Participants participants() { return {{{camera, {{"stream"}}, "left"}}, {emitter}, {controller}}; }
inline SourceFrameKey frame() { return {camera, {{{"stream"}}, gen}, 7}; }
inline FrameSetKey frameset() { return {run, {{{"set"}}, gen}, 7}; }
inline TriggerKey trigger(uint64_t n) { return {run, controller, gen, {n}}; }
inline EvidenceSource proof(EvidenceMethod m = EvidenceMethod::controller_report,
                            ComponentId source = controller) {
    return {source, m, ref()};
}
inline ExactCalibrationReference calibration(std::string kind) {
    return {{{"cal"}, 1, 3},
            ContentReference{{kind == "CameraCalibration" ? "camera-art" : "rig-art"},
                             {"org.mantis." + kind, 1},
                             Hash{"fnv1a64", "0123"},
                             3}};
}
inline ClockMappingEvidence mapping() {
    return {{{{"sensor"}, "device"}, {{"host"}, "runtime"}, 1.25, -0.0, 0.0}, gen, gen, ref("mapping")};
}
inline ExposureAssociation association(uint64_t n) {
    return {frame(), trigger(n), AssociationMethod::native_trigger,
            proof(EvidenceMethod::camera_metadata, camera), NativeTriggerIdentity{controller, gen, 0}};
}
inline CameraFrameEvidence camera_frame() {
    CameraFrameEvidence f;
    f.frame = frame();
    f.camera_role = "left";
    f.width = 4;
    f.height = 1;
    f.source_timestamp = sensor(0);
    f.host_received = host(9);
    f.timestamp_meaning = TimestampMeaning::exposure_start;
    f.exposure = ExposureEvidence{Duration{10},
                                  Unavailable{},
                                  Duration{10},
                                  TimeInterval{sensor(0), sensor(10)},
                                  proof(EvidenceMethod::camera_metadata, camera),
                                  0.0};
    f.sync = SyncEvidence{{{"sync"}, 2}, time::SyncQuality::hardware, association(2)};
    f.camera_calibration = calibration("CameraCalibration");
    f.rig_calibration = calibration("RigCalibration");
    f.original_calibration = Unavailable{};
    return f;
}
inline Published images() {
    Packet image;
    image.type = schema::image;
    image.header.sequence.value = 7;
    image.header.timestamp = {0, {{"c"}, "clock"}};
    image.header.received = {9};
    image.header.sync = {{"sync"}, 2};
    image.header.sync_quality = time::SyncQuality::software;
    image.header.calibration = {{"cal"}, 1, 3};
    image.header.frame = {{"world"}, "World"};
    image.header.metadata = {{"role", "left"}};
    memory::BufferBuilder pixels(4);
    auto b = pixels.writable();
    b[0] = std::byte{0};
    b[1] = std::byte{1};
    b[2] = std::byte{127};
    b[3] = std::byte{255};
    image.attributes = {
        {{"org.mantis.pixels", schema::ScalarType::u8, {4}, {1}, "intensity"}, std::move(pixels).publish()}};
    Packet parent;
    parent.type = schema::frameset;
    parent.header = image.header;
    parent.frames = {publish(std::move(image))};
    return publish(std::move(parent));
}
inline ProjectedCaptureHeader header() {
    AcquisitionProgram p;
    p.identity = program_ref();
    p.participants = participants();
    p.repetitions = 3;
    AcquisitionStep a;
    a.index = 9;
    a.label = "capture";
    a.emitters = {{emitter, EmitterState::on}};
    a.capture = {CaptureMode::hardware_trigger, {camera}, TriggerIntent{controller, {{"req"}}, {camera}}};
    a.evidence_requirement = EvidenceRequirement::exposure_effective;
    a.required_scope = EvidenceScope::optical_emission;
    a.settle = 2ns;
    a.max_duration = 1s;
    AcquisitionStep b;
    b.index = 42;
    b.label = "dark";
    b.emitters = {{emitter, EmitterState::off}};
    b.capture.mode = CaptureMode::none;
    b.evidence_requirement = EvidenceRequirement::controller_acknowledged;
    b.max_duration = 2s;
    p.steps = {a, b};
    p.bounds = {10s, 3s, 6, 100, 100, 16 * 1024 * 1024, 2};
    return {p, run, {{"execution"}}, {16, 101, 102, 103, 104, 4096}};
}
inline AcquisitionBundle bundle(unsigned n) {
    AcquisitionBundle b;
    b.key = {run, {n}};
    b.published = host(static_cast<int64_t>(n) * 1000000);
    auto &e = b.evidence;
    e.key = {run, {n * 4}};
    e.program = program_ref();
    e.participants = participants();
    e.implementations = {{{"org.example.Executor"}, {1, 2, 3}, "build", ref()}};
    e.emitters = {{emitter, Unknown{}, Unavailable{}, Unknown{}, {}}};
    e.frameset = Unavailable{};
    e.rig_calibration = Unknown{};
    e.disposition = AcquisitionDisposition::startup;
    e.diagnostic = "fixture";
    if (n)
        e.causal_predecessors = {{run, {(n - 1) * 4}}};
    if (n == 1) {
        e.step = StepInstance{run, 0, 9};
        e.frameset = frameset();
        e.frames = {camera_frame()};
        e.triggers = {trigger(2)};
        e.clock_mappings = {mapping()};
        e.rig_calibration = calibration("RigCalibration");
        e.disposition = AcquisitionDisposition::captured;
        EmitterEvidence em;
        em.emitter = emitter;
        em.commanded = EmitterCommand{{{"on-request"}}, emitter, EmitterState::on, host(0)};
        em.acknowledged = Acknowledgement{
            {{"on-request"}}, AcknowledgementStage::completion,  AcknowledgementResult::success, proof(),
            sensor(0),        EvidenceScope::controller_register};
        em.observed = StateObservation{EmitterState::on, EvidenceScope::electrical_enable,
                                       proof(EvidenceMethod::electrical_readback), sensor(0),
                                       TimeInterval{sensor(0), sensor(10)}};
        em.exposure_effective = {
            {frame(), ExposureEffectiveState{frame(),
                                             EmitterState::on,
                                             EvidenceScope::optical_emission,
                                             proof(EvidenceMethod::optical_sensor, emitter),
                                             {sensor(0), sensor(10)}}}};
        e.emitters = {em};
        b.frameset = images();
    } else if (n == 2) {
        e.step = StepInstance{run, 0, 42};
        e.disposition = AcquisitionDisposition::control_only;
        e.causal_predecessors.push_back({run, {0}});
        for (unsigned i = 0; i < 3; ++i) {
            TriggerEvent t;
            t.key = trigger(i);
            t.step = {run, 0, 9};
            t.request = {{"req"}};
            t.kind = i == 0   ? TriggerEvent::Kind::requested
                     : i == 1 ? TriggerEvent::Kind::acknowledged_accepted
                              : TriggerEvent::Kind::observed;
            t.evidence =
                proof(i == 0 ? EvidenceMethod::software_dispatch : EvidenceMethod::controller_report);
            t.host_dispatched = host(0);
            t.host_received = host(9);
            t.device_time = sensor(0);
            t.uncertainty_ns = 0.0;
            t.clock_mapping = mapping();
            t.intended_endpoints = {camera};
            t.requested_exposure = frame();
            t.native_trigger = Unavailable{};
            t.actual_endpoints = Unknown{};
            if (i == 1)
                t.acknowledgement = Acknowledgement{
                    {{"req"}}, AcknowledgementStage::acceptance,  AcknowledgementResult::success, proof(),
                    sensor(0), EvidenceScope::controller_register};
            if (i == 2) {
                t.native_trigger = NativeTriggerIdentity{controller, gen, 0};
                t.actual_endpoints = std::vector<ComponentId>{camera};
                t.exposure_association = association(2);
            }
            e.triggers.push_back(t.key);
            b.triggers.push_back(t);
        }
    } else if (n == 3) {
        e.disposition = AcquisitionDisposition::failed;
        e.reason = AcquisitionReason::device_failure;
        e.unresolved_requests = {{{{"lost"}}, emitter, AcquisitionReason::evidence_missing}};
        e.losses = {{LossKind::excluded_frame, camera, uint64_t{0}, AcquisitionReason::timeout, frame(),
                     RequestId{{"req"}}}};
        e.diagnostic = "explicit failure";
    }
    return b;
}
inline std::string encode(const AcquisitionBundle &b) {
    std::ostringstream out;
    write_bundle(out, b);
    return out.str();
}
inline std::string encode(const ProjectedCaptureHeader &h) {
    std::ostringstream out;
    write_capture_header(out, h);
    return out.str();
}
inline ProjectedRunOutcome outcome(RecordedRunDisposition disposition = RecordedRunDisposition::failed,
                                   AcquisitionReason reason = AcquisitionReason::device_failure) {
    ProjectedRunOutcome o;
    o.run = run;
    o.generation = header().generation;
    o.disposition = disposition;
    o.reason = reason;
    if (disposition == RecordedRunDisposition::failed)
        o.initiating_error = Error{Status::plugin_failed, "device failed", "executor"};
    return o;
}
inline std::string encode(const ProjectedCaptureOutcome &o) {
    std::ostringstream out;
    write_run_outcome(out, o);
    return out.str();
}
} // namespace storage_fixture

namespace storage_fixture {
inline ProjectedRunOutcome detailed_outcome() {
    auto o = outcome();
    o.initiating_error.reset();
    o.abort_error = Error{Status::plugin_failed, "abort failed", "executor"};
    o.stop_error.reset();
    o.close_error.reset();
    RecordedAbortOutcome a;
    a.run = run;
    a.fenced_generation = Unavailable{};
    a.inhibited = true;
    a.stale_work_fenced = Unknown{};
    a.off_requested = true;
    auto e = bundle(0).evidence.emitters[0];
    e.commanded = EmitterCommand{{{"off-request"}}, emitter, EmitterState::off, host()};
    e.acknowledged = Unavailable{};
    e.observed = Unknown{};
    e.exposure_effective = {{frame(), Unknown{}},
                            {SourceFrameKey{camera, {{{"stream"}}, gen}, 8}, Unavailable{}}};
    a.emitters = {e};
    a.error = {7, 23};
    o.abort_outcome = a;
    o.diagnostic = "cleanup fault";
    return o;
}
} // namespace storage_fixture
