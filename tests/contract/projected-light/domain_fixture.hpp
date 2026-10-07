#pragma once
#include <mantis/projected_light.hpp>
namespace domain_fixture {
using namespace mantis;
using namespace mantis::data;
using namespace std::chrono_literals;
const RunId run{{"run-generation-A"}};
const ComponentId camera{{"camera-stable"}}, emitter_a{{"projector-copper"}}, emitter_b{{"projector-violet"}},
    controller{{"timing-component"}};
const GenerationId generation{{"generation-A"}};
SemanticTimestamp ts(int64_t n = 0) {
    return {n, {{{"sensor-clock"}, "sensor"}, generation}};
}
RuntimeTimestamp host_ts(int64_t n = 0) {
    return {{n}, {{{"host-monotonic"}, "runtime"}, generation}};
}
ProgramReference program_ref() {
    return {{{"program-A"}}, Unavailable{}, Unavailable{}};
}
ImplementationIdentity impl() {
    return {{"org.example.algorithm"}, {1, 2, 3}, "test-build", Unavailable{}};
}
EvidenceSource proof(EvidenceMethod method = EvidenceMethod::controller_report) {
    return {controller, method, Unavailable{}};
}
ContentReference content(std::string name = "input") {
    return {{std::move(name)}, {"org.example.Input", 1}, Hash{"sha256", "abcd"}, 1};
}
SourceFrameKey frame_key() {
    return {camera, {{{"image-stream"}}, generation}, 42};
}
CameraFrameEvidence source_frame() {
    CameraFrameEvidence f;
    f.frame = frame_key();
    f.camera_role = "left";
    f.width = 1280;
    f.height = 720;
    f.source_timestamp = ts();
    f.host_received = host_ts(100);
    f.timestamp_meaning = TimestampMeaning::exposure_start;
    f.camera_calibration = Unavailable{};
    f.rig_calibration = Unknown{};
    f.original_calibration = Unavailable{};
    ExposureEvidence e;
    e.interval = TimeInterval{ts(0), ts(10)};
    e.evidence = {camera, EvidenceMethod::camera_metadata, Unavailable{}};
    e.integration_duration = Duration{10};
    e.uncertainty_ns = 0.0;
    f.exposure = e;
    return f;
}
AcquisitionProgram off_program() {
    AcquisitionProgram p;
    p.identity = program_ref();
    p.participants.emitters = {emitter_a};
    p.repetitions = 1;
    AcquisitionStep s;
    s.label = "dark";
    s.capture.mode = CaptureMode::none;
    s.evidence_requirement = EvidenceRequirement::commanded_only;
    s.emitters = {{emitter_a, EmitterState::off}};
    s.max_duration = 10ms;
    p.steps = {s};
    p.bounds = {10s, 1s, max_executed_steps, 1'000'000, 1'000'000, 1024 * 1024, 1};
    return p;
}
TriggerEvent request(uint64_t sequence = 0) {
    TriggerEvent t;
    t.key = {run, controller, generation, {sequence}};
    t.step = {run, 0, 0};
    t.request = {{"request-A"}};
    t.kind = TriggerEvent::Kind::requested;
    t.evidence = proof(EvidenceMethod::software_dispatch);
    t.host_dispatched = host_ts();
    t.intended_endpoints = {camera};
    t.native_trigger = Unavailable{};
    t.actual_endpoints = Unavailable{};
    t.exposure_association = Unknown{};
    return t;
}
TriggerEvent observed(uint64_t sequence = 1) {
    auto t = request(sequence);
    t.kind = TriggerEvent::Kind::observed;
    t.evidence = proof(EvidenceMethod::electrical_readback);
    t.device_time = ts(20);
    t.actual_endpoints = std::vector<ComponentId>{camera};
    t.native_trigger = NativeTriggerIdentity{controller, generation, 0};
    t.exposure_association = ExposureAssociation{frame_key(),
                                                 t.key,
                                                 AssociationMethod::native_trigger,
                                                 {camera, EvidenceMethod::camera_metadata, Unavailable{}},
                                                 *t.native_trigger.get()};
    return t;
}
AcquisitionEvidence evidence() {
    AcquisitionEvidence e;
    e.disposition = AcquisitionDisposition::startup;
    e.key = {run, {0}};
    e.program = program_ref();
    e.participants = {{{camera, {{"image-stream"}}, "left"}}, {emitter_a}, {controller}};
    e.implementations = {impl()};
    EmitterEvidence state;
    state.emitter = emitter_a;
    state.commanded = Unknown{};
    state.acknowledged = Unavailable{};
    state.observed = Unavailable{};
    e.emitters = {state};
    e.frameset = Unavailable{};
    e.rig_calibration = Unavailable{};
    return e;
}
AcquisitionBundle bundle() {
    AcquisitionBundle b;
    b.key = {run, {0}};
    b.published = host_ts(100);
    b.evidence = evidence();
    return b;
}
template <class T> Attribute attr(schema::AttributeDescriptor d, const std::vector<T> &values) {
    auto bytes = std::as_bytes(std::span{values});
    memory::BufferBuilder storage(bytes.size());
    if (!bytes.empty())
        std::memcpy(storage.writable().data(), bytes.data(), bytes.size());
    return {std::move(d), std::move(storage).publish()};
}
template <class T> Attribute column(std::string_view name, schema::ScalarType scalar, std::vector<T> values) {
    auto n = values.size();
    auto bytes = sizeof(T);
    return attr({std::string(name), scalar, {n}, {bytes}, ""}, values);
}
Published frameset_packet() {
    Packet image;
    image.type = schema::image;
    image.header.sequence.value = frame_key().native_sequence;
    image.attributes = {
        attr({"org.mantis.pixels", schema::ScalarType::u8, {720, 1280}, {1280, 1}, "intensity"},
             std::vector<uint8_t>(720 * 1280, 7))};
    Packet fs;
    fs.type = schema::frameset;
    fs.header.sequence.value = 10;
    fs.frames = {publish(std::move(image))};
    return publish(std::move(fs));
}
LaserObservation observation(uint64_t n = 1) {
    LaserObservation o;
    o.key = {run, {{"producer-stream"}}, generation, {0}};
    o.context.source = source_frame();
    o.context.optical_frame = {{{"camera-optical"}}, "optical"};
    o.context.producer = impl();
    o.context.origin = ObservationOrigin::synthetic;
    o.context.raw_input = Unavailable{};
    o.context.parameters = content("parameters");
    o.context.exact_inputs = {content()};
    o.disposition = ObservationDisposition::success;
    o.sample_count = n;
    o.emitter_dictionary = {emitter_a};
    o.line_dictionary = {{emitter_a, {{"pattern"}}, 1, {{"line-A"}}}};
    if (n) {
        o.attributes = {
            attr(laser::source_pixel_descriptor(n), std::vector<float>(static_cast<size_t>(n) * 2, 0.5f)),
            attr(laser::quality_flags_descriptor(n), std::vector<uint32_t>(n, 0)),
            column(laser::emitter_index, schema::ScalarType::u32, std::vector<uint32_t>(n, 0)),
            column(laser::emitter_valid, schema::ScalarType::u8, std::vector<uint8_t>(n, 1)),
            column(laser::line_index, schema::ScalarType::u32, std::vector<uint32_t>(n, 0)),
            column(laser::line_valid, schema::ScalarType::u8, std::vector<uint8_t>(n, 1)),
            column(laser::confidence, schema::ScalarType::f32, std::vector<float>(n, 0.5f)),
            column(laser::confidence_valid, schema::ScalarType::u8, std::vector<uint8_t>(n, 1))};
        o.confidence_interpretation = impl();
    }
    return o;
}
} // namespace domain_fixture
