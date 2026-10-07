#include <cmath>
#include <iostream>
#include <limits>
#include <mantis/data_io.hpp>
#include <mantis/projected_light.hpp>
#include <sstream>
#include <type_traits>
using namespace mantis;
using namespace mantis::data;
using namespace std::chrono_literals;
namespace {
int checks = 0;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__));              \
    } while (false)
template <class T> void valid(const T &v) {
    auto r = validate(v);
    if (!r)
        throw std::runtime_error(r.error().message);
    CHECK(r);
}
template <class T, class F> void rejects(T v, F edit, std::string_view message = {}) {
    edit(v);
    auto r = validate(v);
    CHECK(!r);
    CHECK(r.error().code == Status::invalid_argument);
    CHECK(!r.error().message.empty());
    if (!message.empty())
        CHECK(r.error().message.find(message) != std::string::npos);
}
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
template <class F> void rejects_frameset(AcquisitionBundle b, F edit) {
    rejects(b, [&](auto &x) {
        Packet packet = *x.frameset;
        edit(packet);
        // Deliberately bypass publish(): immutable ownership alone cannot prove packet validity.
        x.frameset = std::make_shared<const Packet>(std::move(packet));
    });
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
void resolved_calibration_references() {
    for (bool rig : {false, true}) {
        auto o = observation();
        auto field = rig ? &CameraFrameEvidence::rig_calibration : &CameraFrameEvidence::camera_calibration;
        o.context.source.*field = ExactCalibrationReference{
            {{"logical-calibration"}, 1, 7},
            ContentReference{{"immutable-calibration"},
                             {rig ? "org.mantis.RigCalibration" : "org.mantis.CameraCalibration", 1},
                             Hash{"sha256", "abcd"},
                             7}};
        valid(o);
        auto invalid = [&](auto edit) {
            rejects(o, [&](auto &x) {
                auto exact = *(x.context.source.*field).get();
                auto resolved = *exact.content.get();
                edit(resolved);
                exact.content = std::move(resolved);
                x.context.source.*field = std::move(exact);
            });
        };
        invalid([](auto &r) { ++r.revision; });
        invalid([](auto &r) { ++r.type.version; });
        invalid([](auto &r) { r.type.name = "org.mantis.LaserModel"; });
        invalid([](auto &r) { r.hash = Unknown{}; });
        invalid([](auto &r) { r.hash = Unavailable{}; });
        for (auto unresolved :
             {Evidence<ContentReference>{Unknown{}}, Evidence<ContentReference>{Unavailable{}}}) {
            auto imported = o;
            auto exact = *(imported.context.source.*field).get();
            exact.content = unresolved;
            imported.context.source.*field = exact;
            imported.context.origin = ObservationOrigin::imported;
            valid(imported);
            CHECK((imported.context.source.*field).get()->content.presence() == unresolved.presence());
        }
    }
    auto e = evidence();
    e.rig_calibration = ExactCalibrationReference{
        {{"rig-logical"}, 1, 7},
        ContentReference{{"rig-immutable"}, {"org.mantis.RigCalibration", 1}, Hash{"sha256", "abcd"}, 7}};
    valid(e);
    rejects(e, [](auto &x) {
        auto exact = *x.rig_calibration.get();
        auto resolved = *exact.content.get();
        resolved.revision = 8;
        exact.content = resolved;
        x.rig_calibration = exact;
    });
    rejects(e, [](auto &x) {
        auto exact = *x.rig_calibration.get();
        auto resolved = *exact.content.get();
        resolved.hash = Unknown{};
        exact.content = resolved;
        x.rig_calibration = exact;
    });
}
void observation_run_identity() {
    for (auto origin : {ObservationOrigin::real, ObservationOrigin::synthetic}) {
        for (uint64_t n : {0u, 1u}) {
            auto o = observation(n);
            o.context.origin = origin;
            valid(o);
            rejects(o, [](auto &x) { x.key.run_id = Unknown{}; }, "established run identity");
            rejects(o, [](auto &x) { x.key.run_id = Unavailable{}; }, "established run identity");
        }
    }
    for (auto absent : {Evidence<RunId>{Unknown{}}, Evidence<RunId>{Unavailable{}}}) {
        auto imported = observation();
        imported.context.origin = ObservationOrigin::imported;
        imported.key.run_id = absent;
        valid(imported);
        CHECK(imported.key.run_id.presence() == absent.presence());
    }
    auto correlations = [&](auto attach) {
        auto o = observation();
        attach(o);
        valid(o);
        rejects(o, [](auto &x) { x.key.run_id = RunId{{"other-run"}}; }, "matching established run");
        o.context.origin = ObservationOrigin::imported;
        rejects(o, [](auto &x) { x.key.run_id = Unknown{}; }, "matching established run");
        rejects(o, [](auto &x) { x.key.run_id = Unavailable{}; }, "matching established run");
    };
    correlations([](auto &o) { o.context.bundle = BundleKey{run, {0}}; });
    correlations([](auto &o) { o.context.frameset = FrameSetKey{run, {{{"set-stream"}}, generation}, 10}; });
    correlations([](auto &o) { o.context.correlation = ProgramCorrelation{program_ref(), {run, 0, 0}}; });
    correlations([](auto &o) { o.context.triggers = {request().key}; });
    correlations([](auto &o) { o.context.acquisition_evidence = EvidenceKey{run, {0}}; });
}
void quality_mask_consistency() {
    auto known = observation();
    for (auto bit : {laser::emitter_unknown, laser::line_unknown})
        rejects(
            known,
            [bit](auto &o) {
                o.attributes[1] = attr(laser::quality_flags_descriptor(1), std::vector<uint32_t>{bit});
            },
            "contradicts validity mask");
    auto unknown = observation();
    unknown.attributes[3] = column(laser::emitter_valid, schema::ScalarType::u8, std::vector<uint8_t>{0});
    unknown.attributes[5] = column(laser::line_valid, schema::ScalarType::u8, std::vector<uint8_t>{0});
    unknown.attributes[1] =
        attr(laser::quality_flags_descriptor(1),
             std::vector<uint32_t>{laser::emitter_unknown | laser::line_unknown | 0x8000000cu});
    valid(unknown);
    for (uint32_t flags : {0u, laser::emitter_unknown, laser::line_unknown})
        rejects(
            unknown,
            [flags](auto &o) {
                o.attributes[1] = attr(laser::quality_flags_descriptor(1), std::vector<uint32_t>{flags});
            },
            "contradicts validity mask");
    auto mixed = observation(3);
    mixed.attributes[3] = column(laser::emitter_valid, schema::ScalarType::u8, std::vector<uint8_t>{1, 1, 0});
    mixed.attributes[5] = column(laser::line_valid, schema::ScalarType::u8, std::vector<uint8_t>{1, 0, 0});
    const std::vector<uint32_t> flags{0x4000000c, laser::line_unknown,
                                      laser::emitter_unknown | laser::line_unknown | 0x8000000c};
    mixed.attributes[1] = attr(laser::quality_flags_descriptor(3), flags);
    valid(mixed);
    std::vector<uint32_t> preserved(3);
    std::memcpy(preserved.data(), mixed.attributes[1].buffer.map_read()->data(), 3 * sizeof(uint32_t));
    CHECK(preserved == flags);
    rejects(
        mixed,
        [](auto &o) {
            o.attributes[1] = attr(laser::quality_flags_descriptor(3), std::vector<uint32_t>{0, 0, 0});
        },
        "contradicts validity mask");
}
void identities() {
    CHECK(schema::acquisition_program == (schema::DataTypeId{"org.mantis.AcquisitionProgram", 1}));
    CHECK(schema::acquisition_bundle == (schema::DataTypeId{"org.mantis.AcquisitionBundle", 1}));
    CHECK(schema::acquisition_evidence == (schema::DataTypeId{"org.mantis.AcquisitionEvidence", 1}));
    CHECK(schema::trigger_event == (schema::DataTypeId{"org.mantis.TriggerEvent", 1}));
    CHECK(schema::laser_observation == (schema::DataTypeId{"org.mantis.LaserObservation", 1}));
    CHECK(schema::image == (schema::DataTypeId{"org.mantis.ImageFrame", 1}));
    CHECK(schema::frameset == (schema::DataTypeId{"org.mantis.FrameSet", 1}));
    CHECK(schema::points == (schema::DataTypeId{"org.mantis.PointCloud", 1}));
    CHECK(schema::mesh == (schema::DataTypeId{"org.mantis.Mesh", 1}));
    CHECK(schema::tensor == (schema::DataTypeId{"org.mantis.Tensor", 1}));
    static_assert(!std::is_convertible_v<RequestId, NativeTriggerIdentity>);
    static_assert(!std::is_convertible_v<ComponentId, StreamId>);
    static_assert(!std::is_convertible_v<ObservationSequence, EventSequence>);
    CHECK((StepInstance{run, 2, 7}).repetition_index == 2);
}
void programs() {
    auto p = off_program();
    valid(p);
    rejects(p, [](auto &x) { x.steps[0].capture.mode.reset(); });
    rejects(p, [](auto &x) { x.steps[0].evidence_requirement.reset(); });
    rejects(p, [](auto &x) { x.steps.clear(); });
    rejects(p, [](auto &x) { x.participants.emitters.push_back(emitter_a); });
    rejects(p, [](auto &x) {
        x.participants.cameras = {{camera, {{"stream"}}, "left"}, {camera, {{"other"}}, "right"}};
    });
    rejects(p, [](auto &x) { x.steps.push_back(x.steps.front()); });
    rejects(p, [](auto &x) {
        auto s = x.steps.front();
        s.index = 1;
        x.steps.push_back(s);
    });
    rejects(p, [](auto &x) {
        auto s = x.steps.front();
        s.label = "other";
        x.steps.push_back(s);
    });
    rejects(p, [](auto &x) { x.steps.front().emitters.clear(); });
    rejects(p, [](auto &x) { x.steps.front().emitters.front().emitter = emitter_b; });
    rejects(p, [](auto &x) { x.repetitions = 0; });
    rejects(p, [](auto &x) { x.steps.resize(257); });
    rejects(p, [](auto &x) { x.repetitions = 1'000'001; });
    rejects(
        p,
        [](auto &x) {
            auto s = x.steps.front();
            s.index = 1;
            s.label = "other";
            x.steps.push_back(s);
            x.repetitions = UINT64_MAX;
        },
        "multiplication overflow");
    rejects(p, [](auto &x) { x.bounds.max_commands = 0; });
    rejects(p, [](auto &x) { x.bounds.max_events = 0; });
    rejects(p, [](auto &x) { x.bounds.max_bytes = 0; });
    rejects(p, [](auto &x) { x.bounds.max_in_flight_captures = 0; });
    rejects(p, [](auto &x) { x.bounds.max_step_instances = 0; });
    rejects(p, [](auto &x) { x.bounds.max_duration = 0ns; });
    rejects(p, [](auto &x) { x.bounds.max_on_duration = 0ns; });
    rejects(p, [](auto &x) { x.steps[0].max_duration = -1ns; });
    rejects(p, [](auto &x) { x.steps[0].max_duration = 0ns; });
    rejects(p, [](auto &x) { x.steps[0].settle = -1ns; });
    rejects(p, [](auto &x) { x.steps[0].settle = x.steps[0].max_duration; });
    rejects(p, [](auto &x) { x.steps[0].emitters[0].state = static_cast<EmitterState>(99); });
    rejects(p, [](auto &x) { x.steps[0].evidence_requirement = EvidenceRequirement::exposure_effective; });
    rejects(p, [](auto &x) { x.steps[0].label = std::string(max_semantic_string + 1, 'a'); });
    auto generic = p;
    generic.participants.emitters.push_back(emitter_b);
    generic.participants.cameras = {{camera, {{"image-stream"}}, "left"}};
    generic.participants.controllers = {controller};
    generic.steps.clear();
    generic.repetitions = 3;
    for (uint32_t i = 0; i < 4; ++i) {
        AcquisitionStep s;
        s.evidence_requirement = EvidenceRequirement::commanded_only;
        s.index = i;
        s.label = "step-" + std::to_string(i);
        s.max_duration = 10ms;
        s.settle = 1ms;
        s.emitters = {{emitter_a, i == 1 ? EmitterState::on : EmitterState::off},
                      {emitter_b, i == 3 ? EmitterState::on : EmitterState::off}};
        s.capture = {CaptureMode::free_running, {camera}, std::nullopt};
        generic.steps.push_back(s);
    }
    valid(generic);
    CHECK(!generic.steps[1].capture.trigger);
    rejects(generic, [](auto &x) { x.steps[0].capture.mode = CaptureMode::hardware_trigger; });
    auto hw = generic;
    hw.steps[1].capture.mode = CaptureMode::hardware_trigger;
    hw.steps[1].capture.trigger = TriggerIntent{controller, {{"logical-request"}}, {camera}};
    valid(hw);
    rejects(hw, [](auto &x) { x.steps[1].capture.trigger->request.id.value.clear(); });
    rejects(hw, [](auto &x) { x.steps[1].capture.trigger->endpoints.clear(); });
    rejects(hw, [](auto &x) { x.steps[1].capture.trigger->controller = emitter_b; });
    rejects(hw, [](auto &x) { x.steps[1].capture.mode = CaptureMode::free_running; });
    auto edge = p;
    edge.repetitions = max_executed_steps;
    edge.bounds.max_duration = Duration{10'000'000'000'000};
    valid(edge);
    auto steps_edge = p;
    steps_edge.steps.clear();
    for (uint32_t i = 0; i < 256; ++i) {
        auto s = p.steps[0];
        s.evidence_requirement = EvidenceRequirement::commanded_only;
        s.index = i;
        s.label = std::to_string(i);
        steps_edge.steps.push_back(s);
    }
    valid(steps_edge);
    rejects(generic, [](auto &x) { x.bounds.max_step_instances = 1; });
    rejects(generic, [](auto &x) { x.bounds.max_duration = 1ns; });
    // Deadlines are independent caps, not prescribed step durations. L3 enforces elapsed time.
    auto on = p;
    on.steps[0].emitters[0].state = EmitterState::on;
    on.repetitions = 2;
    on.bounds.max_on_duration = 15ms;
    valid(on);
    auto across = generic;
    across.steps[0].emitters[0].state = EmitterState::on;
    across.steps[3].emitters[0].state = EmitterState::on;
    across.bounds.max_on_duration = 25ms;
    valid(across);
    rejects(generic, [](auto &x) { x.bounds.max_on_duration = 1ms; }, "ON settle");
}
void evidence_states() {
    Evidence<EmitterState> unknown, unavailable{Unavailable{}}, off{EmitterState::off};
    CHECK(unknown.presence() == Presence::unknown && !unknown.get());
    CHECK(unavailable.presence() == Presence::unavailable && !unavailable.get());
    CHECK(off.presence() == Presence::established && *off.get() == EmitterState::off);
    CHECK(unknown != unavailable && unknown != off && unavailable != off);
    CHECK(Evidence<uint64_t>{uint64_t{0}}.get());
    CHECK(Evidence<SemanticTimestamp>{ts(0)}.get());
    auto e = evidence();
    valid(e);
    rejects(e, [](auto &x) { x.disposition.reset(); });
    CHECK(!e.emitters[0].commanded.get() && !e.emitters[0].acknowledged.get());
    e.emitters[0].commanded = EmitterCommand{{{"emitter-request"}}, emitter_a, EmitterState::on, host_ts()};
    valid(e);
    CHECK(!e.emitters[0].observed.get());
    CHECK(e.emitters[0].exposure_effective.empty());
    e.emitters[0].acknowledged = Acknowledgement{{{"emitter-request"}},
                                                 AcknowledgementStage::acceptance,
                                                 AcknowledgementResult::success,
                                                 proof(),
                                                 ts()};
    valid(e);
    CHECK(!e.emitters[0].observed.get());
    e.emitters[0].observed = StateObservation{EmitterState::off, EvidenceScope::electrical_enable,
                                              proof(EvidenceMethod::electrical_readback), ts(), Unknown{}};
    valid(e);
    rejects(e, [](auto &x) {
        auto s = *x.emitters[0].observed.get();
        s.state.reset();
        x.emitters[0].observed = s;
    });
    // Contradiction is retained, never normalized to commanded ON.
    CHECK(e.emitters[0].observed.get()->state == EmitterState::off);
    rejects(
        e,
        [](auto &x) {
            auto o = *x.emitters[0].observed.get();
            o.scope = EvidenceScope::optical_emission;
            x.emitters[0].observed = o;
        },
        "Electrical");
    auto unclocked = e;
    auto point = *unclocked.emitters[0].observed.get();
    point.time = Unavailable{};
    point.coverage = Unavailable{};
    unclocked.emitters[0].observed = point;
    valid(unclocked);
    CHECK(unclocked.emitters[0].exposure_effective.empty());
    e.frames = {source_frame()};
    ExposureEffectiveState effective{frame_key(),
                                     EmitterState::off,
                                     EvidenceScope::electrical_enable,
                                     proof(EvidenceMethod::electrical_readback),
                                     {ts(-1), ts(11)}};
    e.emitters[0].exposure_effective = {{frame_key(), effective}};
    valid(e);
    CHECK(e.emitters[0].exposure_effective[0].state.get()->scope != EvidenceScope::optical_emission);
    rejects(
        e,
        [](auto &x) {
            auto s = *x.emitters[0].exposure_effective[0].state.get();
            s.coverage.end = ts(5);
            x.emitters[0].exposure_effective[0].state = s;
        },
        "entire established exposure");
    rejects(e, [](auto &x) { x.frames[0].exposure = Unavailable{}; });
    rejects(e, [](auto &x) {
        auto s = *x.emitters[0].exposure_effective[0].state.get();
        s.coverage.end.clock.generation = {{"other"}};
        x.emitters[0].exposure_effective[0].state = s;
    });
    rejects(e, [](auto &x) {
        x.key.run_id = {{"other-run"}};
        x.step = StepInstance{run, 0, 0};
    });
    rejects(e, [](auto &x) { x.emitters.clear(); });
    rejects(e, [](auto &x) { x.disposition = AcquisitionDisposition::failed; });
    auto failed = evidence();
    failed.disposition = AcquisitionDisposition::failed;
    failed.reason = AcquisitionReason::timeout;
    failed.unresolved_requests = {{{{"pending"}}, camera, AcquisitionReason::timeout}};
    failed.losses = {{LossKind::exposure, camera, Unknown{}, AcquisitionReason::timeout, Unknown{},
                      RequestId{{"pending"}}}};
    valid(failed);
    CHECK(!failed.losses[0].count.get());
    for (auto d : {AcquisitionDisposition::startup, AcquisitionDisposition::completed,
                   AcquisitionDisposition::stopped, AcquisitionDisposition::cancelled}) {
        auto terminal = evidence();
        terminal.disposition = d;
        valid(terminal);
    }
    auto control = evidence();
    control.disposition = AcquisitionDisposition::control_only;
    control.step = StepInstance{run, 0, 0};
    valid(control);
    rejects(control, [](auto &x) { x.frames = {source_frame()}; });
    auto causal = evidence();
    causal.key.ordinal = {2};
    causal.causal_predecessors = {{run, {0}}, {run, {1}}};
    valid(causal);
    rejects(causal, [](auto &x) { x.causal_predecessors.push_back(x.key); });
    rejects(causal, [](auto &x) { x.implementations[0].build.clear(); });
    rejects(causal, [](auto &x) {
        x.program.content = ContentReference{{"program-A"}, schema::points, Unknown{}, 0};
    });
    ClockMappingEvidence clock_map{
        {{{"sensor-clock"}, "sensor"}, {{"host-monotonic"}, "host"}, 1.0, 3.0, 0.5},
        generation,
        generation,
        content("clock-mapping")};
    auto mapped = evidence();
    mapped.clock_mappings = {clock_map};
    valid(mapped);
    rejects(mapped,
            [](auto &x) { x.clock_mappings[0].mapping.scale = std::numeric_limits<double>::infinity(); });
    rejects(mapped, [](auto &x) { x.clock_mappings[0].mapping.uncertainty_ns = -1; });
    auto calibrated = evidence();
    calibrated.rig_calibration = ExactCalibrationReference{
        {{"rig-immutable"}, 1, 7},
        ContentReference{{"rig-artifact"}, {"org.mantis.RigCalibration", 1}, Hash{"sha256", "abcd"}, 7}};
    valid(calibrated);
}
void triggers_and_bundles() {
    auto t = request();
    valid(t);
    auto physical = observed();
    valid(physical);
    rejects(t, [](auto &x) { x.kind.reset(); });
    CHECK(t.kind != physical.kind && !t.native_trigger.get());
    auto endpoint_unavailable = physical;
    endpoint_unavailable.actual_endpoints = Unavailable{};
    valid(endpoint_unavailable);
    CHECK(!endpoint_unavailable.actual_endpoints.get());
    auto unclocked = physical;
    unclocked.device_time = Unavailable{};
    unclocked.host_received = Unavailable{};
    valid(unclocked);
    CHECK(physical.native_trigger.get()->value == 0); // Native zero is a real ID, scoped by generation.
    CHECK(physical.exposure_association.get()->frame == frame_key());
    auto copy = physical;
    valid(copy);
    CHECK(copy.exposure_association.get()->trigger == physical.key);
    rejects(physical, [](auto &x) {
        auto n = *x.native_trigger.get();
        n.generation = {{"other"}};
        x.native_trigger = n;
    });
    rejects(physical, [](auto &x) {
        auto n = *x.native_trigger.get();
        n.generation.id.value.clear();
        x.native_trigger = n;
    });
    rejects(t, [](auto &x) { x.native_trigger = NativeTriggerIdentity{controller, generation, 0}; });
    rejects(t, [](auto &x) { x.actual_endpoints = std::vector<ComponentId>{camera}; });
    rejects(t, [](auto &x) { x.host_dispatched = Unknown{}; });
    rejects(physical, [](auto &x) { x.evidence.method = EvidenceMethod::software_dispatch; });
    auto accepted = request(2);
    accepted.kind = TriggerEvent::Kind::acknowledged_accepted;
    accepted.evidence = proof();
    accepted.acknowledgement = Acknowledgement{accepted.request, AcknowledgementStage::acceptance,
                                               AcknowledgementResult::success, proof(), ts()};
    valid(accepted);
    auto completed = accepted;
    completed.kind = TriggerEvent::Kind::acknowledged_completed;
    completed.key.sequence = {3};
    auto ack = *completed.acknowledgement.get();
    ack.stage = AcknowledgementStage::completion;
    completed.acknowledgement = ack;
    valid(completed);
    CHECK(accepted.acknowledgement.get()->stage != completed.acknowledgement.get()->stage);
    rejects(accepted, [](auto &x) { x.kind = TriggerEvent::Kind::acknowledged_completed; });
    rejects(accepted, [](auto &x) {
        auto a = *x.acknowledgement.get();
        a.result.reset();
        x.acknowledgement = a;
    });
    rejects(accepted, [](auto &x) { x.acknowledgement = Unknown{}; });
    rejects(accepted, [](auto &x) { x.evidence.method = EvidenceMethod::software_dispatch; });
    auto rejected = accepted;
    rejected.kind = TriggerEvent::Kind::rejected;
    ack = *rejected.acknowledgement.get();
    ack.result = AcknowledgementResult::rejected;
    rejected.acknowledgement = ack;
    valid(rejected);
    for (auto kind : {TriggerEvent::Kind::timed_out, TriggerEvent::Kind::cancelled}) {
        auto outcome = t;
        outcome.kind = kind;
        valid(outcome);
    }
    auto b = bundle();
    valid(b);
    CHECK(!b.frameset); // No empty FrameSet required.
    b.triggers = {t, physical};
    b.evidence.triggers = {t.key, physical.key};
    valid(b);
    rejects(b, [](auto &x) { std::swap(x.triggers[0], x.triggers[1]); });
    rejects(b, [](auto &x) { x.evidence.triggers.clear(); });
    rejects(b, [](auto &x) {
        x.triggers[1].key.controller_generation = {{"other-generation"}};
        x.evidence.triggers[1] = x.triggers[1].key;
    });
    auto full = bundle();
    for (uint64_t i = 0; i < 63; ++i) {
        auto event = request(i);
        full.evidence.triggers.push_back(event.key);
        full.triggers.push_back(event);
    }
    valid(full);
    rejects(
        full,
        [](auto &x) {
            auto event = request(63);
            x.evidence.triggers.push_back(event.key);
            x.triggers.push_back(event);
        },
        "64 total");
    FrameSetKey fs{run, {{{"frameset-stream"}}, generation}, 10};
    auto captured = bundle();
    auto images = frameset_packet();
    captured.frameset = images;
    captured.evidence.frameset = fs;
    captured.evidence.frames = {source_frame()};
    captured.evidence.step = StepInstance{run, 0, 0};
    captured.evidence.disposition = AcquisitionDisposition::captured;
    valid(captured);
    CHECK(captured.published.time.nanoseconds !=
          captured.evidence.frames[0].source_timestamp.get()->nanoseconds);
    CHECK(captured.frameset == images);
    auto retained = captured;
    CHECK(retained.frameset->frames[0] == images->frames[0]);
    CHECK(retained.frameset->frames[0]->attributes[0].buffer.identity() ==
          images->frames[0]->attributes[0].buffer.identity());
    CHECK(retained.frameset->frames[0]->attributes[0].buffer.map_read()->data() ==
          images->frames[0]->attributes[0].buffer.map_read()->data());
    rejects_frameset(captured, [](auto &p) { p.type = schema::image; });
    rejects_frameset(captured, [](auto &p) { p.type.version = 2; });
    rejects_frameset(captured, [](auto &p) { p.frames.clear(); });
    rejects_frameset(captured, [](auto &p) { p.frames.resize(17, p.frames[0]); });
    rejects_frameset(captured, [](auto &p) { p.attributes = p.frames[0]->attributes; });
    rejects_frameset(captured, [](auto &p) { p.frames[0].reset(); });
    rejects_frameset(captured, [](auto &p) {
        Packet image = *p.frames[0];
        image.type = schema::points;
        p.frames[0] = std::make_shared<const Packet>(std::move(image));
    });
    rejects_frameset(captured, [](auto &p) {
        Packet image = *p.frames[0];
        image.type.version = 2;
        p.frames[0] = std::make_shared<const Packet>(std::move(image));
    });
    rejects_frameset(captured, [](auto &p) {
        Packet image = *p.frames[0];
        image.frames = {p.frames[0]};
        p.frames[0] = std::make_shared<const Packet>(std::move(image));
    });
    rejects_frameset(captured, [](auto &p) {
        Packet image = *p.frames[0];
        image.attributes[0].descriptor.shape[0] = 0;
        p.frames[0] = std::make_shared<const Packet>(std::move(image));
    });
    rejects_frameset(captured, [](auto &p) { ++p.header.sequence.value; });
    rejects_frameset(captured, [](auto &p) {
        Packet image = *p.frames[0];
        ++image.header.sequence.value;
        p.frames[0] = std::make_shared<const Packet>(std::move(image));
    });
    rejects(captured, [](auto &x) { x.evidence.frameset = Unknown{}; });
    rejects(captured, [](auto &x) { x.evidence.frames.clear(); });
    for (uint64_t i = 0; i < 62; ++i) {
        auto event = request(i);
        captured.evidence.triggers.push_back(event.key);
        captured.triggers.push_back(event);
    }
    valid(captured); // Evidence + actual FrameSet + 62 triggers = 64 members.
    rejects(
        captured,
        [](auto &x) {
            auto event = request(62);
            x.evidence.triggers.push_back(event.key);
            x.triggers.push_back(event);
        },
        "64 total");
    static_assert(std::is_same_v<decltype(captured.frameset), Published>);
    auto next = bundle();
    next.key.sequence = {1};
    next.evidence.key.ordinal = {1};
    CHECK(validate_successor(bundle(), next));
    auto backwards = next;
    backwards.published = host_ts(99);
    CHECK(!validate_successor(bundle(), backwards));
    CHECK(!validate_successor(next, next));
    next.key.run_id = {{"other"}};
    CHECK(!validate_successor(bundle(), next));
}
void observations() {
    auto o = observation();
    valid(o);
    rejects(o, [](auto &x) { x.disposition.reset(); });
    valid(observation(3));
    auto next = o;
    next.key.sequence = {1};
    CHECK(validate_successor(o, next));
    CHECK(!validate_successor(o, o));
    next.key.producer_generation = {{"other"}};
    CHECK(!validate_successor(o, next));
    static_assert(!std::is_convertible_v<RuntimeTimestamp, SemanticTimestamp>);
    auto zero = observation(0);
    valid(zero);
    CHECK(zero.disposition == ObservationDisposition::success && zero.attributes.empty());
    auto failed = zero;
    failed.disposition = ObservationDisposition::extractor_failed;
    valid(failed);
    auto unavailable = zero;
    unavailable.disposition = ObservationDisposition::extractor_unavailable;
    valid(unavailable);
    CHECK(zero.disposition != failed.disposition && failed.disposition != unavailable.disposition);
    rejects(zero, [](auto &x) {
        x.attributes.push_back(attr(laser::quality_flags_descriptor(0), std::vector<uint32_t>{}));
    });
    rejects(o, [](auto &x) { x.disposition = ObservationDisposition::extractor_failed; });
    auto d = laser::source_pixel_descriptor(3);
    CHECK(d.name == laser::source_pixel && d.scalar == schema::ScalarType::f32);
    CHECK((d.shape == std::vector<uint64_t>{3, 2}) && (d.stride == std::vector<uint64_t>{8, 4}) &&
          d.unit == "pixel");
    auto q = laser::quality_flags_descriptor(3);
    CHECK(q.name == laser::quality_flags && q.scalar == schema::ScalarType::u32);
    CHECK((q.shape == std::vector<uint64_t>{3}) && (q.stride == std::vector<uint64_t>{4}) && q.unit.empty());
    CHECK(laser::pixel_convention == "x right; y down; origin at center of original pixel (0,0)");
    CHECK(laser::emitter_unknown == 1 && laser::line_unknown == 2 && laser::ambiguous == 4 &&
          laser::producer_rejected == 8);
    auto future = o;
    future.attributes[1] = attr(laser::quality_flags_descriptor(1), std::vector<uint32_t>{0x8000000c});
    valid(future);
    uint32_t preserved = 0;
    std::memcpy(&preserved, future.attributes[1].buffer.map_read()->data(), 4);
    CHECK(preserved == 0x8000000c);
    rejects(o, [](auto &x) {
        x.attributes[1] = attr(laser::quality_flags_descriptor(2), std::vector<uint32_t>{0, 0});
    });
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        rejects(o, [bad](auto &x) {
            x.attributes[0] = attr(laser::source_pixel_descriptor(1), std::vector<float>{bad, 0});
        });
        rejects(o, [bad](auto &x) {
            x.attributes[0] = attr(laser::source_pixel_descriptor(1), std::vector<float>{0, bad});
        });
    }
    rejects(o, [](auto &x) { x.attributes[0].descriptor.unit = "mm"; });
    rejects(o, [](auto &x) { x.attributes[1].descriptor.scalar = schema::ScalarType::f32; });
    for (size_t mask : {3u, 5u, 7u})
        rejects(o, [mask](auto &x) {
            auto name = x.attributes[mask].descriptor.name;
            x.attributes[mask] = column(name, schema::ScalarType::u8, std::vector<uint8_t>{2});
        });
    for (size_t member : {2u, 3u, 4u, 5u, 6u, 7u})
        rejects(o, [member](auto &x) {
            x.attributes.erase(x.attributes.begin() + static_cast<std::ptrdiff_t>(member));
        });
    rejects(o, [](auto &x) {
        x.attributes[2] = column(laser::emitter_index, schema::ScalarType::u32, std::vector<uint32_t>{1});
    });
    rejects(o, [](auto &x) {
        x.attributes[4] = column(laser::line_index, schema::ScalarType::u32, std::vector<uint32_t>{1});
    });
    rejects(o, [](auto &x) {
        x.attributes[3] = column(laser::emitter_valid, schema::ScalarType::u8, std::vector<uint8_t>{0});
        x.attributes[1] =
            attr(laser::quality_flags_descriptor(1), std::vector<uint32_t>{laser::emitter_unknown});
    });
    rejects(o, [](auto &x) {
        x.emitter_dictionary.push_back(emitter_b);
        x.line_dictionary[0].emitter = emitter_b;
    });
    for (float bad :
         {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
        rejects(o, [bad](auto &x) {
            x.attributes[6] = column(laser::confidence, schema::ScalarType::f32, std::vector<float>{bad});
        });
    for (float edge : {0.0f, 1.0f}) {
        auto x = o;
        x.attributes[6] = column(laser::confidence, schema::ScalarType::f32, std::vector<float>{edge});
        valid(x);
    }
    auto unknown = o;
    for (size_t mask : {3u, 5u, 7u})
        unknown.attributes[mask] =
            column(unknown.attributes[mask].descriptor.name, schema::ScalarType::u8, std::vector<uint8_t>{0});
    unknown.attributes[6] = column(laser::confidence, schema::ScalarType::f32, std::vector<float>{0});
    unknown.attributes[1] = attr(laser::quality_flags_descriptor(1),
                                 std::vector<uint32_t>{laser::emitter_unknown | laser::line_unknown});
    valid(unknown);
    auto absent = o;
    absent.attributes.resize(2);
    absent.emitter_dictionary.clear();
    absent.line_dictionary.clear();
    valid(absent);
    auto pattern_only = absent;
    pattern_only.context.emitter_patterns = {{emitter_a, {{"pattern"}}, 1}};
    valid(pattern_only);
    rejects(pattern_only,
            [](auto &x) { x.context.emitter_patterns.push_back(x.context.emitter_patterns[0]); });
    rejects(o, [](auto &x) { x.context.origin.reset(); });
    auto imported = absent;
    imported.key.run_id = Unavailable{};
    imported.context.origin = ObservationOrigin::imported;
    valid(imported);
    rejects(o, [](auto &x) { x.emitter_dictionary.push_back(emitter_a); });
    rejects(o, [](auto &x) { x.line_dictionary.push_back(x.line_dictionary[0]); });
    auto qualified = o;
    auto line = qualified.line_dictionary[0];
    line.pattern_revision = 2;
    qualified.line_dictionary.push_back(line);
    valid(qualified);
    rejects(o, [](auto &x) {
        x.attributes.push_back(column("org.mantis.position", schema::ScalarType::f32, std::vector<float>{0}));
    });
    auto extension = o;
    extension.attributes.push_back(
        column("org.example.width", schema::ScalarType::f32, std::vector<float>{2}));
    valid(extension);
    rejects(extension, [](auto &x) { x.attributes.back().descriptor.name = "unnamespaced"; });
    rejects(o, [](auto &x) { x.context.source.width = 0; });
    rejects(o, [](auto &x) { x.sample_count = max_observation_samples + 1; });
    rejects(o, [](auto &x) { x.confidence_interpretation = Unknown{}; });
    rejects(o, [](auto &x) {
        x.context.source.camera_calibration = ExactCalibrationReference{
            {{"logical"}, 1, 1}, ContentReference{{"artifact"}, {"org.mantis.LaserModel", 1}, Unknown{}, 1}};
    });
    auto calibrated = o;
    calibrated.context.source.camera_calibration = ExactCalibrationReference{
        {{"logical"}, 1, 1},
        ContentReference{{"artifact"}, {"org.mantis.CameraCalibration", 1}, Hash{"sha256", "abcd"}, 1}};
    valid(calibrated);
    rejects(o, [](auto &x) { x.emitter_dictionary.resize(max_semantic_entries + 1); });
    rejects(o, [](auto &x) { x.attributes.resize(max_semantic_attributes + 1); });
    auto strided = o;
    strided.attributes[0] =
        attr({std::string(laser::source_pixel), schema::ScalarType::f32, {1, 2}, {16, 8}, "pixel"},
             std::vector<float>{0.5f, 999, 1.5f});
    valid(strided);
    rejects(o, [](auto &x) {
        x.context.source.sync = SyncEvidence{{{"group"}, 0}, time::SyncQuality::hardware, Unknown{}};
    });
    auto synced = o;
    auto t = observed();
    synced.context.source.sync =
        SyncEvidence{{{"group"}, 0}, time::SyncQuality::hardware, *t.exposure_association.get()};
    valid(synced);
    rejects(synced, [](auto &x) {
        auto s = *x.context.source.sync.get();
        auto a = *s.hardware_association.get();
        a.native_trigger = Unavailable{};
        s.hardware_association = a;
        x.context.source.sync = s;
    });
    rejects(synced, [](auto &x) {
        auto s = *x.context.source.sync.get();
        auto a = *s.hardware_association.get();
        a.method = AssociationMethod::software_correspondence;
        s.hardware_association = a;
        x.context.source.sync = s;
    });
    rejects(o, [](auto &x) {
        x.context.preprocessing = PreprocessingTransform{{0, 0, 0, 0, 0, 0, 0, 0, 0}, Unknown{}};
    });
    auto wrong_schema = o;
    wrong_schema.type.version = 2;
    rejects(wrong_schema, [](auto &) {});
}
void legacy_data() {
    Packet image;
    image.type = schema::image;
    image.attributes = {column("org.mantis.pixels", schema::ScalarType::u8, std::vector<uint8_t>{7})};
    auto published = publish(image);
    Packet fs;
    fs.type = schema::frameset;
    fs.frames = {published};
    auto frames = publish(fs);
    CHECK(frames->frames.size() == 1 && frames->type == schema::frameset);
    std::ostringstream out(std::ios::binary);
    write_packet(out, *frames);
    CHECK(out.str().substr(0, 8) == "MANTIS02");
    auto encoded = out.str();
    auto decoded = read_packet(memory::copy(std::as_bytes(std::span{encoded.data(), encoded.size()})));
    CHECK(decoded->frames.size() == 1);
    Packet xyz;
    xyz.type = schema::points;
    xyz.attributes = {column("org.mantis.position", schema::ScalarType::f32, std::vector<float>{1})};
    CHECK(point_cloud(xyz));
    CHECK(!point_cloud(image));
    Packet tensor;
    tensor.type = schema::tensor;
    tensor.attributes = image.attributes;
    CHECK(publish(tensor)->type == schema::tensor);
    bool rejected = false;
    try {
        Packet bad;
        bad.type = schema::frameset;
        publish(bad);
    } catch (const Failure &) {
        rejected = true;
    }
    CHECK(rejected);
}
} // namespace
int main() {
    try {
        identities();
        programs();
        evidence_states();
        triggers_and_bundles();
        resolved_calibration_references();
        observation_run_identity();
        quality_mask_consistency();
        observations();
        legacy_data();
        std::cout << checks << " canonical projected-light checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
