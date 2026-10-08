#include <atomic>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/projected_run.hpp>
#include <mutex>
#include <thread>
using namespace mantis;
using namespace mantis::data;
using namespace mantis::device;
using namespace std::chrono_literals;
namespace {
std::atomic_size_t checks{};
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                   \
    } while (false)
const ComponentId emitter{{"amber"}}, camera{{"sensor"}}, controller_id{{"timer"}};
const ProjectedIdentity identity{RunId{{"run-new"}}, GenerationId{{"execution-new"}}};
const GenerationId source_generation{{"source-open"}}, controller_generation{{"controller-open"}};
const StreamId image_stream_id{{"image-output"}};
RuntimeTimestamp runtime(uint64_t n = 0) {
    return {{static_cast<int64_t>(n)}, {{{"runtime-clock"}, "runtime"}, {{"runtime-generation"}}}};
}
SemanticTimestamp sensor(int64_t n = 0) {
    return {n, {{{"sensor-clock"}, "sensor"}, source_generation}};
}
EvidenceSource proof(EvidenceMethod m = EvidenceMethod::controller_report) {
    EvidenceSource p{controller_id, m, Unavailable{}};
    if (m == EvidenceMethod::validated_executor)
        p.reference =
            ContentReference{{"synthetic-contract"}, {"org.example.Contract", 1}, Hash{"sha256", "aabb"}, 1};
    return p;
}
AcquisitionProgram program() {
    AcquisitionProgram p;
    p.identity = {{{"off-only"}}, Unknown{}, Unavailable{}};
    p.participants.emitters = {emitter};
    p.participants.controllers = {controller_id};
    p.repetitions = 1;
    AcquisitionStep s;
    s.index = 17;
    s.label = "dark";
    s.emitters = {{emitter, EmitterState::off}};
    s.capture.mode = CaptureMode::none;
    s.evidence_requirement = EvidenceRequirement::commanded_only;
    s.max_duration = 50ms;
    p.steps = {s};
    p.bounds = {2s, 1s, 100, 100, 100, 64 * 1024 * 1024, 16};
    CHECK(data::validate(p));
    return p;
}
ProjectedGraph graph() {
    ProjectedGraph g;
    g.parent = {"parent"};
    ProjectedComponent p;
    p.kind = ParticipantKind::parent;
    p.descriptor.id = g.parent;
    p.descriptor.capabilities = {std::string(projected_light)};
    p.participants = {emitter.id, camera.id, controller_id.id};
    ProjectedComponent e;
    e.kind = ParticipantKind::emitter;
    e.descriptor.id = emitter.id;
    e.descriptor.parent = g.parent;
    e.descriptor.capabilities = {"org.mantis.emitter.power-control.v1"};
    e.emitter_states = {EmitterState::off, EmitterState::on};
    e.evidence_methods = {EvidenceMethod::software_dispatch, EvidenceMethod::controller_report,
                          EvidenceMethod::validated_executor, EvidenceMethod::register_readback,
                          EvidenceMethod::electrical_readback, EvidenceMethod::optical_sensor};
    e.evidence_scopes = {EvidenceScope::controller_register, EvidenceScope::electrical_enable,
                         EvidenceScope::optical_emission};
    ProjectedComponent c;
    c.kind = ParticipantKind::image;
    c.descriptor.id = camera.id;
    c.descriptor.parent = g.parent;
    c.descriptor.capabilities = {std::string(image_stream), "org.mantis.trigger.hardware.v1"};
    c.role = "measurement";
    c.image_source = ProjectedImageSource{image_stream_id, "physical-camera", 2, 2};
    c.evidence_methods = {EvidenceMethod::camera_metadata, EvidenceMethod::validated_executor};
    c.evidence_scopes = e.evidence_scopes;
    c.capture_modes = {CaptureMode::free_running, CaptureMode::hardware_trigger};
    ProjectedComponent t;
    t.kind = ParticipantKind::controller;
    t.descriptor.id = controller_id.id;
    t.descriptor.parent = g.parent;
    t.descriptor.capabilities = {"org.mantis.trigger.hardware.v1", "org.mantis.emitter.power-control.v1"};
    t.controls = {emitter.id};
    t.evidence_methods = e.evidence_methods;
    t.evidence_scopes = e.evidence_scopes;
    t.trigger_endpoints = {camera.id};
    t.trigger_modes = {CaptureMode::hardware_trigger};
    g.components = {p, e, c, t};
    g.limits = {4,        256, 64,   16,        {10s, 5s, 1000000, 1000000, 1000000, 128 * 1024 * 1024, 32},
                100ms,    64,  1000, Unknown{}, Unavailable{},
                Unknown{}};
    g.frameset_stream = StreamId{{"parent-output"}};
    return g;
}
AcquisitionBundle bundle(const AcquisitionProgram &p, uint64_t seq, AcquisitionDisposition disposition,
                         size_t step = 0, uint64_t repetition = 0) {
    AcquisitionBundle b;
    b.key = {identity.run, {seq}};
    b.published = runtime(seq);
    auto &e = b.evidence;
    e.key = {identity.run, {seq}};
    e.program = p.identity;
    e.participants = p.participants;
    e.implementations = {{{"org.example.executor"}, {1, 0, 0}, "synthetic", Unavailable{}}};
    e.disposition = disposition;
    if (disposition == AcquisitionDisposition::control_only ||
        disposition == AcquisitionDisposition::captured)
        e.step = StepInstance{identity.run, repetition, p.steps[step].index};
    EmitterEvidence light;
    light.emitter = emitter;
    light.observed = Unavailable{};
    light.acknowledged = Unavailable{};
    if (e.step.get())
        light.commanded =
            EmitterCommand{RequestId{{"command-" + std::to_string(repetition) + "-" + std::to_string(step)}},
                           emitter, p.steps[step].emitters[0].state, runtime(seq)};
    e.emitters = {light};
    e.frameset = Unavailable{};
    e.rig_calibration = Unavailable{};
    return b;
}
AcquisitionProgram captured(EvidenceRequirement requirement = EvidenceRequirement::commanded_only,
                            bool hardware = false) {
    auto p = program();
    p.participants.cameras = {{camera, image_stream_id, "measurement"}};
    p.steps[0].capture.mode = hardware ? CaptureMode::hardware_trigger : CaptureMode::free_running;
    p.steps[0].capture.cameras = {camera};
    p.steps[0].evidence_requirement = requirement;
    if (hardware) {
        p.participants.controllers = {controller_id};
        p.steps[0].capture.trigger = TriggerIntent{controller_id, {{"trigger-request"}}, {camera}};
    }
    CHECK(data::validate(p));
    return p;
}
AcquisitionBundle capture(const AcquisitionProgram &p, uint64_t seq = 0) {
    auto b = bundle(p, seq, AcquisitionDisposition::captured);
    auto &e = b.evidence;
    CameraFrameEvidence f;
    f.frame = {camera, {image_stream_id, source_generation}, 42};
    f.camera_role = "measurement";
    f.width = 2;
    f.height = 2;
    ExposureEvidence exposure;
    exposure.evidence = {camera, EvidenceMethod::camera_metadata, Unavailable{}};
    exposure.interval = TimeInterval{sensor(0), sensor(10)};
    f.exposure = exposure;
    e.frames = {f};
    e.frameset = FrameSetKey{identity.run, {StreamId{{"parent-output"}}, source_generation}, 0};
    e.emitters[0].exposure_effective = {{f.frame, Unknown{}}};
    CHECK(data::validate(b));
    return b;
}
TriggerEvent trigger(const AcquisitionProgram &p, uint64_t sequence = 0) {
    TriggerEvent t;
    t.key = {identity.run, controller_id, controller_generation, {sequence}};
    t.step = {identity.run, 0, p.steps[0].index};
    t.request = p.steps[0].capture.trigger->request;
    t.kind = TriggerEvent::Kind::requested;
    t.evidence = proof(EvidenceMethod::software_dispatch);
    t.host_dispatched = runtime();
    t.intended_endpoints = {camera};
    return t;
}
void acknowledged(AcquisitionBundle &b, AcknowledgementStage stage = AcknowledgementStage::acceptance) {
    auto &e = b.evidence.emitters[0];
    e.acknowledged = Acknowledgement{
        e.commanded.get()->request,        stage, AcknowledgementResult::success, proof(), Unknown{},
        EvidenceScope::controller_register};
}
void effective(AcquisitionBundle &b, EvidenceScope scope = EvidenceScope::controller_register) {
    auto &f = b.evidence.frames[0];
    b.evidence.emitters[0].exposure_effective[0].state =
        ExposureEffectiveState{f.frame,
                               EmitterState::off,
                               scope,
                               proof(EvidenceMethod::validated_executor),
                               {sensor(0), sensor(10)}};
}
struct FakeState {
    std::mutex mutex;
    std::condition_variable cv;
    ProjectedGraph selected = graph();
    AcquisitionProgram prepared;
    std::deque<Result<std::optional<AcquisitionBundle>>> outputs;
    std::atomic_uint validations{}, preparations{}, starts{}, nexts{}, aborts{}, stops{}, closes{};
    std::atomic_bool next_active{}, abort_active{}, abort_overlapped_next{};
    bool reject_validation{}, reject_prepare{}, fail_validation{}, fail_prepare{}, fail_start{}, fail_abort{},
        fail_stop{}, fail_close{};
    bool block{}, entered{}, inhibit{}, abort_block{}, abort_entered{}, abort_release{};
    std::function<void(uint32_t)> on_next;
    std::function<void()> on_stop;
    std::vector<uint32_t> next_timeouts;
    AbortOutcome outcome;
    FakeState() {
        outcome.run = identity.run;
        outcome.fenced_generation = identity.generation;
        outcome.inhibited = true;
        outcome.stale_work_fenced = Unknown{};
        outcome.off_requested = true;
        EmitterEvidence e;
        e.emitter = emitter;
        e.commanded = Unknown{};
        e.acknowledged = Unavailable{};
        e.observed = Unavailable{};
        outcome.emitters = {e};
    }
    void emit(AcquisitionBundle b) {
        std::lock_guard lock(mutex);
        outputs.push_back(std::optional{std::move(b)});
        cv.notify_all();
    }
    bool await(std::function<bool()> predicate) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, 2s, predicate);
    }
};
Error injected() {
    return {Status::plugin_failed, "injected", "fake"};
}
class Fake final : public ProjectedExecutor {
    std::shared_ptr<FakeState> s;

  public:
    explicit Fake(std::shared_ptr<FakeState> state) : s(std::move(state)) {}
    const ProjectedGraph &graph() const override {
        return s->selected;
    }
    Result<ProgramValidation> validate(const AcquisitionProgram &, uint32_t t) override {
        CHECK(t <= 1000);
        ++s->validations;
        if (s->fail_validation)
            return std::unexpected(injected());
        return ProgramValidation{!s->reject_validation,
                                 s->reject_validation ? ContractError{1, 71} : ContractError{},
                                 "synthetic validation"};
    }
    Result<ProgramValidation> prepare(const AcquisitionProgram &p, uint32_t t) override {
        CHECK(t <= 1000);
        ++s->preparations;
        s->prepared = p;
        if (s->fail_prepare)
            return std::unexpected(injected());
        return ProgramValidation{!s->reject_prepare,
                                 s->reject_prepare ? ContractError{1, 72} : ContractError{},
                                 "synthetic preparation"};
    }
    Result<void> start(const RunId &, const GenerationId &, uint32_t t) override {
        CHECK(t <= 1000);
        ++s->starts;
        if (s->fail_start)
            return std::unexpected(injected());
        return {};
    }
    Result<std::optional<AcquisitionBundle>> next(uint32_t timeout) override {
        s->next_active = true;
        struct Active {
            std::atomic_bool &value;
            ~Active() {
                value = false;
            }
        } active{s->next_active};
        ++s->nexts;
        if (s->on_next)
            s->on_next(timeout);
        std::unique_lock lock(s->mutex);
        s->next_timeouts.push_back(timeout);
        s->entered = true;
        s->cv.notify_all();
        if (s->block)
            s->cv.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return s->inhibit; });
        if (s->outputs.empty())
            return std::optional<AcquisitionBundle>{};
        auto out = std::move(s->outputs.front());
        s->outputs.pop_front();
        return out;
    }
    Result<ProjectedStatus> status(uint32_t) override {
        return ProjectedStatus{};
    }
    Result<AbortOutcome> abort(AcquisitionReason, uint32_t timeout) override {
        s->abort_overlapped_next = s->next_active.load();
        s->abort_active = true;
        struct Active {
            std::atomic_bool &value;
            ~Active() {
                value = false;
            }
        } active{s->abort_active};
        ++s->aborts;
        std::unique_lock lock(s->mutex);
        s->abort_entered = true;
        s->inhibit = true;
        s->cv.notify_all();
        if (s->abort_block)
            s->cv.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return s->abort_release; });
        if (s->fail_abort)
            return std::unexpected(injected());
        return s->outcome;
    }
    Result<void> stop(uint32_t timeout) override {
        CHECK(timeout <= 1000);
        CHECK(!s->next_active && !s->abort_active);
        if (s->on_stop)
            s->on_stop();
        ++s->stops;
        if (s->fail_stop)
            return std::unexpected(injected());
        return {};
    }
    Result<void> close(uint32_t timeout) override {
        CHECK(timeout <= 1000);
        CHECK(!s->next_active && !s->abort_active);
        ++s->closes;
        if (s->fail_close)
            return std::unexpected(injected());
        return {};
    }
    Result<std::string> diagnostics(uint32_t) override {
        return std::string("synthetic");
    }
};
ProjectedRunConfig config() {
    ProjectedRunConfig c;
    c.queue_capacity = 16;
    c.max_correlation_entries = 4096;
    c.publication_timeout_ms = 20;
    return c;
}
std::unique_ptr<ProjectedRun> run(std::shared_ptr<FakeState> s, AcquisitionProgram p = program(),
                                  ProjectedRunConfig c = config(), ProjectedClock clock = {}) {
    return std::make_unique<ProjectedRun>(
        std::make_unique<Fake>(s), std::move(p), c, [] { return identity; }, std::move(clock));
}
ProjectedRunSnapshot finish(ProjectedRun &r) {
    CHECK(r.wait_terminal(2000));
    return r.snapshot();
}
void fails(const AcquisitionProgram &p, AcquisitionBundle b, AcquisitionReason expected) {
    auto s = std::make_shared<FakeState>();
    s->emit(std::move(b));
    s->emit(bundle(p, 9, AcquisitionDisposition::completed));
    auto r = run(s, p);
    CHECK(r->prepare());
    (void)r->start();
    auto end = finish(*r);
    CHECK(end.state == ProjectedState::failed);
    CHECK(end.terminal.reason == expected);
    CHECK(s->aborts == 1);
    CHECK(s->closes == 1);
}
void preflight_tests() {
    {
        auto s = std::make_shared<FakeState>();
        auto r = run(s);
        CHECK(r->snapshot().state == ProjectedState::validating);
        CHECK(r->prepare());
        CHECK(r->snapshot().state == ProjectedState::ready);
        CHECK(s->starts == 0);
    }
    auto p = program();
    auto a = p.steps[0];
    a.index = 5;
    a.label = "A";
    a.emitters[0].state = EmitterState::on;
    auto d = p.steps[0];
    d.index = 99;
    d.label = "dark-again";
    auto b = a;
    b.index = 201;
    b.label = "B";
    p.steps = {p.steps[0], a, d, b};
    p.repetitions = 2;
    {
        auto s = std::make_shared<FakeState>();
        auto r = run(s, p);
        CHECK(r->prepare());
    }
    std::vector<std::function<void(ProjectedGraph &)>> lower = {
        [](auto &g) { g.limits.max_steps = 0; },
        [](auto &g) { g.limits.bounds.max_step_instances = 0; },
        [](auto &g) { g.limits.bounds.max_duration = 1ns; },
        [](auto &g) { g.limits.bounds.max_on_duration = 1ns; },
        [](auto &g) { g.limits.bounds.max_commands = 1; },
        [](auto &g) { g.limits.bounds.max_events = 1; },
        [](auto &g) { g.limits.bounds.max_bytes = 1; },
        [](auto &g) { g.limits.bounds.max_in_flight_captures = 1; },
        [](auto &g) { g.limits.max_step_duration = 1ns; }};
    for (auto edit : lower) {
        auto s = std::make_shared<FakeState>();
        edit(s->selected);
        auto r = run(s);
        CHECK(!r->prepare());
        CHECK(finish(*r).state == ProjectedState::failed);
        CHECK(s->validations == 0);
        CHECK(s->starts == 0);
    }
    {
        auto s = std::make_shared<FakeState>();
        s->selected.limits.max_cameras = 0;
        auto r = run(s, captured());
        CHECK(!r->prepare());
        CHECK(s->validations == 0);
    }
    {
        auto s = std::make_shared<FakeState>();
        s->selected.components[3].evidence_methods = {EvidenceMethod::software_dispatch};
        auto acknowledged_program = program();
        acknowledged_program.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
        auto r = run(s, acknowledged_program);
        CHECK(!r->prepare());
        CHECK(s->validations == 0);
    }
    for (int mode = 0; mode < 4; ++mode) {
        auto s = std::make_shared<FakeState>();
        s->reject_validation = mode == 0;
        s->fail_validation = mode == 1;
        s->reject_prepare = mode == 2;
        s->fail_prepare = mode == 3;
        auto r = run(s);
        CHECK(!r->prepare());
        CHECK(!r->start());
        CHECK(finish(*r).state == ProjectedState::failed);
        CHECK(s->starts == 0);
        CHECK(s->aborts == 0);
        if (mode == 0)
            CHECK(r->snapshot().validation->error.code == 71);
        if (mode == 2)
            CHECK(r->snapshot().preparation->error.code == 72);
    }
}
void lifecycle_tests() {
    auto p = program();
    {
        auto s = std::make_shared<FakeState>();
        s->emit(bundle(p, 0, AcquisitionDisposition::control_only));
        s->emit(bundle(p, 1, AcquisitionDisposition::completed));
        auto r = run(s, p);
        CHECK(r->prepare());
        CHECK(r->start());
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::completed);
        CHECK(end.covered_steps == 1);
        CHECK(end.transitions ==
              std::vector<ProjectedState>({ProjectedState::validating, ProjectedState::ready,
                                           ProjectedState::running, ProjectedState::stopping,
                                           ProjectedState::completed}));
        CHECK(end.terminal.executor_terminal);
        CHECK(s->aborts == 1);
        CHECK(s->stops == 1);
        CHECK(s->closes == 1);
        CHECK(!r->start());
        CHECK(r->next(0)->has_value());
        CHECK(r->next(0)->has_value());
        CHECK(!r->next(0)->has_value());
        CHECK(r->snapshot().queue.consumed == 2);
    }
    for (bool cancel : {false, true}) {
        auto s = std::make_shared<FakeState>();
        s->block = true;
        auto r = run(s, p);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(s->await([&] { return s->entered; }));
        if (cancel) {
            r->cancel();
            r->cancel();
        } else {
            r->stop();
            r->stop();
        }
        auto end = finish(*r);
        CHECK(end.state == (cancel ? ProjectedState::cancelled : ProjectedState::completed));
        CHECK(s->aborts == 1);
    }
    {
        auto s = std::make_shared<FakeState>();
        s->block = true;
        auto r = run(s);
        CHECK(r->prepare());
        CHECK(r->start());
        r->stop();
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::completed);
        CHECK(end.terminal.reason == AcquisitionReason::user_stop);
    }
    {
        auto s = std::make_shared<FakeState>();
        s->fail_start = true;
        auto r = run(s);
        CHECK(r->prepare());
        CHECK(!r->start());
        CHECK(finish(*r).state == ProjectedState::failed);
    }
    for (int mode = 0; mode < 3; ++mode) {
        auto s = std::make_shared<FakeState>();
        s->fail_abort = mode == 0;
        s->fail_stop = mode == 1;
        s->fail_close = mode == 2;
        auto r = run(s);
        CHECK(r->prepare());
        CHECK(r->start());
        r->cancel();
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::failed);
        CHECK(end.terminal.reason == AcquisitionReason::user_cancel);
        CHECK(!end.terminal.initiating_error);
        CHECK(mode == 0   ? end.terminal.abort_error.has_value()
              : mode == 1 ? end.terminal.stop_error.has_value()
                          : end.terminal.close_error.has_value());
    }
    {
        auto s = std::make_shared<FakeState>();
        s->outputs.push_back(std::unexpected(injected()));
        s->fail_close = true;
        auto r = run(s);
        CHECK(r->prepare());
        (void)r->start();
        auto end = finish(*r);
        CHECK(end.terminal.reason == AcquisitionReason::device_failure);
        CHECK(end.terminal.initiating_error);
        CHECK(end.terminal.close_error);
    }
    {
        auto s = std::make_shared<FakeState>();
        s->block = true;
        {
            auto r = run(s);
            CHECK(r->prepare());
            CHECK(r->start());
            CHECK(s->await([&] { return s->entered; }));
        }
        CHECK(s->aborts == 1 && s->closes == 1);
    }
}
void evidence_tests() {
    auto p = program();
    {
        auto s = std::make_shared<FakeState>();
        s->emit(bundle(p, 0, AcquisitionDisposition::completed));
        auto r = run(s, p);
        CHECK(r->prepare());
        (void)r->start();
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::failed);
        CHECK(end.terminal.reason == AcquisitionReason::evidence_missing);
    }
    {
        auto b = bundle(p, 0, AcquisitionDisposition::control_only);
        auto c = *b.evidence.emitters[0].commanded.get();
        c.state = EmitterState::on;
        b.evidence.emitters[0].commanded = c;
        fails(p, b, AcquisitionReason::contradictory_evidence);
    }
    for (bool unavailable : {false, true}) {
        auto b = bundle(p, 0, AcquisitionDisposition::control_only);
        b.evidence.emitters[0].commanded =
            unavailable ? Evidence<EmitterCommand>{Unavailable{}} : Evidence<EmitterCommand>{Unknown{}};
        fails(p, b, AcquisitionReason::evidence_missing);
    }
    for (auto stage : {AcknowledgementStage::acceptance, AcknowledgementStage::completion}) {
        auto q = p;
        q.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
        auto s = std::make_shared<FakeState>();
        auto b = bundle(q, 0, AcquisitionDisposition::control_only);
        acknowledged(b, stage);
        s->emit(b);
        s->emit(bundle(q, 1, AcquisitionDisposition::completed));
        auto r = run(s, q);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(finish(*r).state == ProjectedState::completed);
        auto value = r->next(0);
        CHECK((**value)->evidence.emitters[0].acknowledged.get()->stage == stage);
    }
    {
        auto q = p;
        q.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
        fails(q, bundle(q, 0, AcquisitionDisposition::control_only), AcquisitionReason::evidence_missing);
    }
    for (bool valid : {false, true}) {
        auto q = captured(EvidenceRequirement::exposure_effective);
        auto b = capture(q);
        if (valid)
            effective(b);
        auto s = std::make_shared<FakeState>();
        s->emit(b);
        s->emit(bundle(q, 1, AcquisitionDisposition::completed));
        auto r = run(s, q);
        CHECK(r->prepare());
        (void)r->start();
        auto final = finish(*r);
        if (final.state != (valid ? ProjectedState::completed : ProjectedState::failed))
            throw std::runtime_error(final.terminal.initiating_error
                                         ? final.terminal.initiating_error->message
                                         : "wrong terminal state");
        CHECK(final.state == (valid ? ProjectedState::completed : ProjectedState::failed));
    }
    {
        auto q = captured(EvidenceRequirement::exposure_effective);
        auto b = capture(q);
        effective(b);
        auto v = *b.evidence.emitters[0].exposure_effective[0].state.get();
        v.state = EmitterState::on;
        b.evidence.emitters[0].exposure_effective[0].state = v;
        fails(q, b, AcquisitionReason::contradictory_evidence);
    }
    {
        auto q = captured(EvidenceRequirement::exposure_effective);
        q.steps[0].required_scope = EvidenceScope::optical_emission;
        auto b = capture(q);
        effective(b);
        fails(q, b, AcquisitionReason::evidence_missing);
    }
    {
        auto q = captured();
        auto b = capture(q);
        b.evidence.frames[0].frame.camera = {{"foreign"}};
        fails(q, b, AcquisitionReason::contradictory_evidence);
    }
    {
        auto q = p;
        q.repetitions = 2;
        auto s = std::make_shared<FakeState>();
        auto a = q.steps[0];
        a.index = 300;
        a.label = "other";
        q.steps.push_back(a);
        uint64_t sequence = 0;
        for (uint64_t rep = 0; rep < 2; ++rep)
            for (size_t step = 0; step < 2; ++step)
                s->emit(bundle(q, sequence++, AcquisitionDisposition::control_only, step, rep));
        s->emit(bundle(q, sequence, AcquisitionDisposition::completed));
        auto r = run(s, q);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(finish(*r).covered_steps == 4);
    }
    {
        auto q = p;
        q.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
        auto s = std::make_shared<FakeState>();
        auto a = bundle(q, 0, AcquisitionDisposition::control_only);
        auto b = a;
        b.key.sequence = {1};
        b.evidence.key.ordinal = {1};
        b.published = runtime(1);
        acknowledged(b);
        s->emit(a);
        s->emit(b);
        s->emit(bundle(q, 2, AcquisitionDisposition::completed));
        auto r = run(s, q);
        CHECK(r->prepare());
        CHECK(r->start());
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::completed);
        CHECK(end.covered_steps == 1 && end.late_evidence == 1);
    }
}
void causal_tests() {
    auto p = program();
    for (bool valid : {false, true}) {
        auto s = std::make_shared<FakeState>();
        s->emit(bundle(p, 1, AcquisitionDisposition::control_only));
        auto end = bundle(p, 3, AcquisitionDisposition::completed);
        end.evidence.causal_predecessors = {{identity.run, {valid ? 1ull : 2ull}}};
        s->emit(end);
        auto r = run(s, p);
        CHECK(r->prepare());
        (void)r->start();
        auto final = finish(*r);
        if (final.state != (valid ? ProjectedState::completed : ProjectedState::failed))
            throw std::runtime_error(final.terminal.initiating_error
                                         ? final.terminal.initiating_error->message
                                         : "wrong terminal state");
        CHECK(final.state == (valid ? ProjectedState::completed : ProjectedState::failed));
    }
    {
        auto q = captured(EvidenceRequirement::commanded_only, true);
        auto s = std::make_shared<FakeState>();
        auto a = capture(q);
        auto t = trigger(q);
        a.evidence.triggers = {t.key};
        s->emit(a);
        auto b = bundle(q, 1, AcquisitionDisposition::startup);
        b.evidence.triggers = {t.key};
        b.triggers = {t};
        s->emit(b);
        s->emit(bundle(q, 2, AcquisitionDisposition::completed));
        auto r = run(s, q);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(finish(*r).state == ProjectedState::completed);
    }
    {
        auto q = captured(EvidenceRequirement::commanded_only, true);
        auto b = capture(q);
        auto t = trigger(q);
        t.request = {{"wrong"}};
        b.triggers = {t};
        b.evidence.triggers = {t.key};
        fails(q, b, AcquisitionReason::contradictory_evidence);
    }
    {
        auto q = captured();
        auto s = std::make_shared<FakeState>();
        auto a = capture(q);
        auto b = a;
        b.key.sequence = {1};
        b.evidence.key.ordinal = {1};
        b.published = runtime(1);
        b.evidence.frames[0].width = 7;
        s->emit(a);
        s->emit(b);
        auto r = run(s, q);
        CHECK(r->prepare());
        (void)r->start();
        CHECK(finish(*r).terminal.reason == AcquisitionReason::contradictory_evidence);
    }
}
void deadline_queue_tests() {
    std::atomic_int64_t ticks{};
    auto clock = [&] { return std::chrono::steady_clock::time_point{Duration{ticks.load()}}; };
    {
        auto s = std::make_shared<FakeState>();
        auto p = program();
        p.bounds.max_duration = 20ms;
        s->on_next = [&](uint32_t t) {
            CHECK(t <= 20);
            ticks.fetch_add(5'000'000);
        };
        auto r = run(s, p, config(), clock);
        CHECK(r->prepare());
        (void)r->start();
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::failed);
        CHECK(end.terminal.reason == AcquisitionReason::timeout);
        CHECK(s->nexts <= 5);
        CHECK(s->aborts == 1);
    }
    ticks = 0;
    {
        auto s = std::make_shared<FakeState>();
        auto p = program();
        p.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
        s->emit(bundle(p, 0, AcquisitionDisposition::control_only));
        auto r = run(s, p, config(), clock);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(r->next(100)->has_value());
        ticks = 60'000'000;
        r->notify_clock_advanced();
        auto end = finish(*r);
        CHECK(end.terminal.reason == AcquisitionReason::evidence_missing);
    }
    {
        auto s = std::make_shared<FakeState>();
        auto p = program();
        auto c = config();
        c.queue_capacity = 1;
        s->emit(bundle(p, 0, AcquisitionDisposition::control_only));
        s->emit(bundle(p, 1, AcquisitionDisposition::completed));
        auto r = run(s, p, c);
        CHECK(r->prepare());
        CHECK(r->start());
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::failed);
        CHECK(end.terminal.reason == AcquisitionReason::resource_limit);
        CHECK(end.queue.high_water == 1 && end.queue.produced == 1);
        CHECK(end.queue.saturation_failures == 1);
        CHECK(end.terminal.unqueued_bundle);
        CHECK(s->aborts == 1);
        CHECK(r->next(0)->has_value());
    }
    {
        auto s = std::make_shared<FakeState>();
        auto p = program();
        auto c = config();
        c.queue_capacity = 1;
        c.publication_timeout_ms = 1000;
        s->emit(bundle(p, 0, AcquisitionDisposition::control_only));
        auto r = run(s, p, c);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(r->next(100)->has_value());
        s->emit(bundle(p, 1, AcquisitionDisposition::completed));
        CHECK(r->next(100)->has_value());
        CHECK(finish(*r).state == ProjectedState::completed);
    }
}

void additional_contract_tests() {
    auto p = program();
    for (int mode = 0; mode < 5; ++mode) {
        auto state = std::make_shared<FakeState>();
        auto cfg = config();
        if (mode == 0)
            cfg.queue_capacity = 0;
        if (mode == 1)
            cfg.queue_capacity = 65;
        if (mode == 2)
            cfg.operation_timeout_ms = 0;
        if (mode == 3)
            cfg.max_correlation_entries = 0;
        if (mode == 4)
            p.repetitions = 0;
        auto r = run(state, p, cfg);
        CHECK(!r->prepare());
        CHECK(finish(*r).state == ProjectedState::failed);
        CHECK(state->validations == 0 && state->starts == 0);
    }
    p = program();
    // Default identities are fresh; injected factories own uniqueness in deterministic tests.
    {
        ProjectedRun first(std::make_unique<Fake>(std::make_shared<FakeState>()), p);
        ProjectedRun second(std::make_unique<Fake>(std::make_shared<FakeState>()), p);
        CHECK(first.snapshot().identity.run != second.snapshot().identity.run);
        CHECK(first.snapshot().identity.generation != second.snapshot().identity.generation);
    }
    // OFF/DARK/A/DARK/B with two generic emitters is preflightable without product roles.
    {
        auto state = std::make_shared<FakeState>();
        auto q = p;
        ComponentId other{{"violet"}};
        q.participants.emitters.push_back(other);
        q.steps[0].emitters.push_back({other, EmitterState::off});
        auto a = q.steps[0];
        a.index = 80;
        a.label = "A";
        a.emitters[0].state = EmitterState::on;
        auto dark = q.steps[0];
        dark.index = 81;
        dark.label = "dark-again";
        auto b = q.steps[0];
        b.index = 90;
        b.label = "B";
        b.emitters[1].state = EmitterState::on;
        q.steps = {q.steps[0], a, dark, b};
        auto e = state->selected.components[1];
        e.descriptor.id = other.id;
        state->selected.components.push_back(e);
        auto r = run(state, q);
        CHECK(r->prepare());
        CHECK(r->snapshot().state == ProjectedState::ready);
    }
    // A parent can act as an integrated controller only through accessible
    // controller capabilities; its parent kind alone supplies no such function.
    {
        auto state = std::make_shared<FakeState>();
        auto q = p;
        q.participants.controllers = {ComponentId{state->selected.parent}};
        auto r = run(state, q);
        CHECK(!r->prepare());
        CHECK(state->validations == 0);
    }
    // Scope and capability rejection happen before plugin validation.
    for (int mode = 0; mode < 3; ++mode) {
        auto state = std::make_shared<FakeState>();
        auto q = captured(EvidenceRequirement::exposure_effective, true);
        if (mode == 0)
            for (auto &component : state->selected.components)
                component.evidence_scopes.clear();
        if (mode == 1)
            for (auto &component : state->selected.components)
                component.evidence_methods = {EvidenceMethod::software_dispatch};
        if (mode == 2)
            state->selected.components[3].trigger_endpoints.clear();
        auto r = run(state, q);
        CHECK(!r->prepare());
        CHECK(state->validations == 0);
    }
    // Missing/Unavailable never meets a stronger requirement.
    {
        auto q = captured(EvidenceRequirement::exposure_effective);
        auto b = capture(q);
        b.evidence.emitters[0].exposure_effective[0].state = Unavailable{};
        fails(q, b, AcquisitionReason::evidence_missing);
        auto ack = p;
        ack.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
        auto a = bundle(ack, 0, AcquisitionDisposition::control_only);
        a.evidence.emitters[0].acknowledged = Unknown{};
        fails(ack, a, AcquisitionReason::evidence_missing);
    }
    // Exact capture camera set, independent of graph child ordering.
    {
        auto state = std::make_shared<FakeState>();
        auto q = captured();
        ComponentId second{{"second-camera"}};
        q.participants.cameras.push_back({second, {{"second-output"}}, "auxiliary"});
        q.steps[0].capture.cameras.push_back(second);
        auto source = state->selected.components[2];
        source.descriptor.id = second.id;
        source.role = "auxiliary";
        source.image_source->stream = {{"second-output"}};
        state->selected.components.push_back(source);
        state->emit(capture(q));
        auto r = run(state, q);
        CHECK(r->prepare());
        (void)r->start();
        CHECK(finish(*r).terminal.reason == AcquisitionReason::contradictory_evidence);
    }
    for (int mode = 0; mode < 4; ++mode) {
        auto q = captured(EvidenceRequirement::commanded_only, true);
        auto b = capture(q);
        auto t = trigger(q);
        if (mode == 0)
            t.key.source = {{"different-controller"}};
        if (mode == 1)
            t.intended_endpoints = {{{"different-endpoint"}}};
        if (mode == 2)
            t.step.step_index = 800;
        if (mode == 3)
            t.step.repetition_index = 1;
        b.triggers = {t};
        b.evidence.triggers = {t.key};
        fails(q, b, AcquisitionReason::contradictory_evidence);
    }
    {
        auto q = captured();
        q.participants.controllers = {controller_id};
        auto b = capture(q);
        auto hardware = captured(EvidenceRequirement::commanded_only, true);
        auto t = trigger(hardware);
        b.triggers = {t};
        b.evidence.triggers = {t.key};
        fails(q, b, AcquisitionReason::contradictory_evidence);
    }
    // Older-step evidence after newer-step evidence adds proof, never a second execution.
    {
        auto q = p;
        q.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
        auto second = q.steps[0];
        second.index = 400;
        second.label = "later";
        q.steps.push_back(second);
        auto state = std::make_shared<FakeState>();
        auto early = bundle(q, 0, AcquisitionDisposition::control_only);
        auto newer = bundle(q, 1, AcquisitionDisposition::control_only, 1);
        acknowledged(newer);
        auto late = early;
        late.key.sequence = {2};
        late.evidence.key.ordinal = {2};
        late.published = runtime(2);
        acknowledged(late);
        state->emit(early);
        state->emit(newer);
        state->emit(late);
        state->emit(bundle(q, 3, AcquisitionDisposition::completed));
        auto r = run(state, q);
        CHECK(r->prepare());
        CHECK(r->start());
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::completed && end.covered_steps == 2 && end.late_evidence == 1);
    }
    // Late acknowledgement-only proof can resolve a captured step without
    // republishing FrameSet context or substituting any source frame.
    {
        auto q = captured(EvidenceRequirement::controller_acknowledged);
        auto state = std::make_shared<FakeState>();
        auto first = capture(q);
        auto late = bundle(q, 1, AcquisitionDisposition::startup);
        late.evidence.step = first.evidence.step;
        late.evidence.emitters[0].acknowledged =
            Acknowledgement{first.evidence.emitters[0].commanded.get()->request,
                            AcknowledgementStage::acceptance,
                            AcknowledgementResult::success,
                            proof(),
                            Unknown{},
                            EvidenceScope::controller_register};
        CHECK(data::validate(late));
        state->emit(first);
        state->emit(late);
        state->emit(bundle(q, 2, AcquisitionDisposition::completed));
        auto r = run(state, q);
        CHECK(r->prepare());
        CHECK(r->start());
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::completed && end.covered_steps == 1 && end.late_evidence == 1);
        CHECK((**r->next(0))->evidence.emitters[0].acknowledged.presence() == Presence::unavailable);
    }
    // Step-correlated command proof may precede its authoritative capture.
    {
        auto q = captured(EvidenceRequirement::controller_acknowledged);
        auto state = std::make_shared<FakeState>();
        auto captured_step = capture(q, 1);
        auto early = bundle(q, 0, AcquisitionDisposition::startup);
        early.evidence.step = captured_step.evidence.step;
        early.evidence.emitters[0].commanded = captured_step.evidence.emitters[0].commanded;
        acknowledged(early);
        captured_step.evidence.emitters[0].commanded = Unknown{};
        state->emit(early);
        state->emit(captured_step);
        state->emit(bundle(q, 2, AcquisitionDisposition::completed));
        auto r = run(state, q);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(finish(*r).state == ProjectedState::completed);
    }
    // Startup frames with Unknown step context remain unknown and do not count
    // execution; later exact step association can establish their context.
    {
        auto q = captured();
        auto state = std::make_shared<FakeState>();
        auto first = capture(q);
        first.evidence.disposition = AcquisitionDisposition::startup;
        first.evidence.step = Unknown{};
        auto later = capture(q, 1);
        later.evidence.emitters[0].commanded = first.evidence.emitters[0].commanded;
        state->emit(first);
        state->emit(later);
        state->emit(bundle(q, 2, AcquisitionDisposition::completed));
        auto r = run(state, q);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(finish(*r).covered_steps == 1);
        CHECK((**r->next(0))->evidence.step.presence() == Presence::unknown);
    }
    // Exposure proof can resolve late without mutating the original publication.
    {
        auto q = captured(EvidenceRequirement::exposure_effective);
        auto state = std::make_shared<FakeState>();
        auto initial = capture(q);
        auto late = initial;
        late.key.sequence = {1};
        late.evidence.key.ordinal = {1};
        late.published = runtime(1);
        effective(late);
        state->emit(initial);
        state->emit(late);
        state->emit(bundle(q, 2, AcquisitionDisposition::completed));
        auto r = run(state, q);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(finish(*r).state == ProjectedState::completed);
        CHECK((**r->next(0))->evidence.emitters[0].exposure_effective[0].state.presence() ==
              Presence::unknown);
        CHECK((**r->next(0))->evidence.emitters[0].exposure_effective[0].state.presence() ==
              Presence::established);
    }
    // Across an intervening evidence-only publication, a generation still cannot reset.
    for (int mode = 0; mode < 3; ++mode) {
        auto q = captured(EvidenceRequirement::commanded_only, true);
        auto state = std::make_shared<FakeState>();
        auto first = capture(q);
        auto t = trigger(q);
        first.triggers = {t};
        first.evidence.triggers = {t.key};
        state->emit(first);
        state->emit(bundle(q, 1, AcquisitionDisposition::startup));
        auto next = first;
        next.key.sequence = {2};
        next.evidence.key.ordinal = {2};
        next.published = runtime(2);
        if (mode == 0) {
            next.triggers[0].key.controller_generation = {{"reset"}};
            next.triggers[0].key.sequence = {1};
            next.evidence.triggers = {next.triggers[0].key};
        }
        if (mode == 1) {
            next.triggers.clear();
            next.evidence.triggers.clear();
            next.evidence.frames[0].frame.stream.generation = {{"reset"}};
            next.evidence.emitters[0].exposure_effective[0].frame = next.evidence.frames[0].frame;
        }
        if (mode == 2) {
            next.triggers.clear();
            next.evidence.triggers.clear();
            auto key = *next.evidence.frameset.get();
            key.stream.generation = {{"reset"}};
            next.evidence.frameset = key;
        }
        state->emit(next);
        auto r = run(state, q);
        CHECK(r->prepare());
        (void)r->start();
        CHECK(finish(*r).terminal.reason == AcquisitionReason::contradictory_evidence);
    }
    // A contradictory duplicate request, and a reused event key, fail explicitly.
    {
        auto q = p;
        auto second = q.steps[0];
        second.index = 800;
        second.label = "other";
        second.emitters[0].state = EmitterState::on;
        q.steps.push_back(second);
        auto state = std::make_shared<FakeState>();
        auto first = bundle(q, 0, AcquisitionDisposition::control_only);
        auto other = bundle(q, 1, AcquisitionDisposition::control_only, 1);
        auto command = *other.evidence.emitters[0].commanded.get();
        command.request = first.evidence.emitters[0].commanded.get()->request;
        other.evidence.emitters[0].commanded = command;
        state->emit(first);
        state->emit(other);
        auto r = run(state, q);
        CHECK(r->prepare());
        (void)r->start();
        CHECK(finish(*r).terminal.reason == AcquisitionReason::contradictory_evidence);
    }
    {
        auto q = captured(EvidenceRequirement::commanded_only, true);
        auto state = std::make_shared<FakeState>();
        auto first = capture(q);
        auto t = trigger(q);
        first.triggers = {t};
        first.evidence.triggers = {t.key};
        state->emit(first);
        state->emit(bundle(q, 1, AcquisitionDisposition::startup));
        auto duplicate = bundle(q, 2, AcquisitionDisposition::startup);
        duplicate.triggers = {t};
        duplicate.evidence.triggers = {t.key};
        state->emit(duplicate);
        auto r = run(state, q);
        CHECK(r->prepare());
        (void)r->start();
        CHECK(finish(*r).state == ProjectedState::failed);
    }
    // Frame identity reuse cannot conceal a second capture of the same step.
    {
        auto q = captured();
        auto state = std::make_shared<FakeState>();
        auto first = capture(q);
        auto second = first;
        second.key.sequence = {1};
        second.evidence.key.ordinal = {1};
        second.published = runtime(1);
        second.evidence.frames[0].frame.native_sequence = 43;
        second.evidence.emitters[0].exposure_effective[0].frame = second.evidence.frames[0].frame;
        state->emit(first);
        state->emit(second);
        auto r = run(state, q);
        CHECK(r->prepare());
        (void)r->start();
        CHECK(finish(*r).terminal.reason == AcquisitionReason::contradictory_evidence);
    }
    // Clock-domain timestamps are not used to prove a predecessor exists.
    {
        auto state = std::make_shared<FakeState>();
        auto b = bundle(p, 2, AcquisitionDisposition::control_only);
        b.evidence.causal_predecessors = {{identity.run, {1}}};
        b.published = runtime(1000000000);
        state->emit(b);
        auto r = run(state, p);
        CHECK(r->prepare());
        (void)r->start();
        CHECK(finish(*r).state == ProjectedState::failed);
    }
    // Executor terminal faults preserve their original typed reason and bundle.
    {
        auto state = std::make_shared<FakeState>();
        auto b = bundle(p, 0, AcquisitionDisposition::failed);
        b.evidence.reason = AcquisitionReason::transport_failure;
        state->emit(b);
        auto r = run(state, p);
        CHECK(r->prepare());
        (void)r->start();
        auto end = finish(*r);
        CHECK(end.terminal.reason == AcquisitionReason::transport_failure && end.terminal.executor_terminal);
    }
    // Worker exceptions are contained and priority abort still executes.
    {
        auto state = std::make_shared<FakeState>();
        state->on_next = [](uint32_t) { throw std::runtime_error("worker fault"); };
        auto r = run(state, p);
        CHECK(r->prepare());
        (void)r->start();
        CHECK(finish(*r).state == ProjectedState::failed);
        CHECK(state->aborts == 1 && state->closes == 1);
    }
    // Stop while the publication path is full must not wait for a consumer.
    {
        auto state = std::make_shared<FakeState>();
        auto cfg = config();
        cfg.queue_capacity = 1;
        cfg.publication_timeout_ms = 1000;
        state->emit(bundle(p, 0, AcquisitionDisposition::control_only));
        state->emit(bundle(p, 1, AcquisitionDisposition::startup));
        auto r = run(state, p, cfg);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(state->await([&] { return state->nexts >= 2; }));
        r->cancel();
        CHECK(state->aborts == 1);
        auto end = finish(*r);
        CHECK(end.queue.high_water == 1);
        CHECK(end.terminal.unqueued_bundle);
    }
    // Established negative software outcomes are cleanup faults; presence stays
    // exact and no observed/effective OFF is manufactured.
    for (int action = 0; action < 3; ++action) {
        auto state = std::make_shared<FakeState>();
        if (action == 0)
            state->outcome.inhibited = false;
        if (action == 1)
            state->outcome.stale_work_fenced = false;
        if (action == 2)
            state->outcome.off_requested = false;
        auto r = run(state, p);
        CHECK(r->prepare());
        CHECK(r->start());
        r->cancel();
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::failed && end.terminal.abort_error);
        CHECK(end.terminal.reason == AcquisitionReason::user_cancel);
        CHECK(end.terminal.abort_outcome->emitters[0].observed.presence() == Presence::unavailable);
    }
    // Complete synthetic command-OFF feedback remains command feedback only.
    {
        auto state = std::make_shared<FakeState>();
        state->outcome.emitters[0].commanded =
            EmitterCommand{{{"abort-off"}}, emitter, EmitterState::off, runtime()};
        auto r = run(state, p);
        CHECK(r->prepare());
        CHECK(r->start());
        r->stop();
        auto end = finish(*r);
        CHECK(end.terminal.abort_outcome->emitters[0].commanded.get()->state == EmitterState::off);
        CHECK(end.terminal.abort_outcome->emitters[0].observed.presence() == Presence::unavailable);
    }
}

void reservation_and_terminal_tests() {
    auto p = program();
    for (int mode = 0; mode < 3; ++mode) {
        auto state = std::make_shared<FakeState>();
        auto q = p;
        auto cfg = config();
        if (mode == 0)
            q.bounds.max_events = 1;
        if (mode == 1) {
            q.repetitions = 2;
            cfg.max_correlation_entries = 1;
        }
        if (mode == 2) {
            q = captured();
            q.repetitions = 2;
            cfg.max_correlation_entries = 1;
        }
        auto r = run(state, q, cfg);
        CHECK(!r->prepare());
        CHECK(state->starts == 0);
    }
    for (bool cancel : {false, true})
        for (int failure = 0; failure < 3; ++failure) {
            auto state = std::make_shared<FakeState>();
            state->fail_abort = failure == 0;
            state->fail_stop = failure == 1;
            state->fail_close = failure == 2;
            if (!cancel) {
                state->emit(bundle(p, 0, AcquisitionDisposition::control_only));
                state->emit(bundle(p, 1, AcquisitionDisposition::completed));
            }
            auto r = run(state, p);
            CHECK(r->prepare());
            (void)r->start();
            if (cancel)
                r->cancel();
            auto end = finish(*r);
            CHECK(end.state == ProjectedState::failed);
            CHECK(state->aborts == 1 && state->stops == 1 && state->closes == 1);
        }
    // A retained command can establish the same requested state in another control
    // step; the program declares complete intent without requiring redundant writes.
    {
        auto state = std::make_shared<FakeState>();
        auto q = p;
        q.repetitions = 2;
        q.bounds.max_commands = 1;
        auto first = bundle(q, 0, AcquisitionDisposition::control_only);
        auto second = bundle(q, 1, AcquisitionDisposition::control_only, 0, 1);
        second.evidence.emitters[0].commanded = first.evidence.emitters[0].commanded;
        state->emit(first);
        state->emit(second);
        state->emit(bundle(q, 2, AcquisitionDisposition::completed));
        auto r = run(state, q);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(finish(*r).state == ProjectedState::completed);
    }
    // Late acknowledgements still bind to the established command after coverage.
    {
        auto state = std::make_shared<FakeState>();
        auto q = p;
        auto first = bundle(q, 0, AcquisitionDisposition::control_only);
        auto late = first;
        late.key.sequence = {1};
        late.evidence.key.ordinal = {1};
        late.published = runtime(1);
        acknowledged(late);
        auto ack = *late.evidence.emitters[0].acknowledged.get();
        ack.request = {{"foreign-command"}};
        late.evidence.emitters[0].commanded = Unknown{};
        late.evidence.emitters[0].acknowledged = ack;
        state->emit(first);
        state->emit(late);
        auto r = run(state, q);
        CHECK(r->prepare());
        (void)r->start();
        CHECK(finish(*r).terminal.reason == AcquisitionReason::contradictory_evidence);
    }
    // A late completion resolves the actual request for this step, not every future
    // occurrence of the program's logical TriggerIntent request ID.
    {
        auto state = std::make_shared<FakeState>();
        auto q = p;
        q.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
        auto initial = bundle(q, 0, AcquisitionDisposition::control_only);
        initial.evidence.unresolved_requests = {{initial.evidence.emitters[0].commanded.get()->request,
                                                 emitter, AcquisitionReason::evidence_missing}};
        auto late = initial;
        late.key.sequence = {1};
        late.evidence.key.ordinal = {1};
        late.published = runtime(1);
        late.evidence.unresolved_requests.clear();
        acknowledged(late, AcknowledgementStage::completion);
        state->emit(initial);
        state->emit(late);
        state->emit(bundle(q, 2, AcquisitionDisposition::completed));
        auto r = run(state, q);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(finish(*r).state == ProjectedState::completed);
    }
    {
        auto state = std::make_shared<FakeState>();
        auto q = captured(EvidenceRequirement::commanded_only, true);
        q.repetitions = 2;
        auto first = capture(q);
        auto requested = trigger(q);
        first.triggers = {requested};
        first.evidence.triggers = {requested.key};
        state->emit(first);
        auto completed = bundle(q, 1, AcquisitionDisposition::startup);
        auto ack = requested;
        ack.key.sequence = {1};
        ack.kind = TriggerEvent::Kind::acknowledged_completed;
        ack.evidence = proof();
        ack.acknowledgement = Acknowledgement{
            ack.request, AcknowledgementStage::completion,  AcknowledgementResult::success, proof(),
            Unknown{},   EvidenceScope::controller_register};
        completed.triggers = {ack};
        completed.evidence.triggers = {ack.key};
        state->emit(completed);
        auto second = capture(q, 2);
        second.evidence.step = StepInstance{identity.run, 1, q.steps[0].index};
        second.evidence.frames[0].frame.native_sequence = 43;
        second.evidence.emitters[0].exposure_effective[0].frame = second.evidence.frames[0].frame;
        auto command = *second.evidence.emitters[0].commanded.get();
        command.request = {{"command-next-repetition"}};
        second.evidence.emitters[0].commanded = command;
        auto next_request = requested;
        next_request.key.sequence = {2};
        next_request.step.repetition_index = 1;
        second.triggers = {next_request};
        second.evidence.triggers = {next_request.key};
        second.evidence.unresolved_requests = {
            {next_request.request, controller_id, AcquisitionReason::evidence_missing}};
        state->emit(second);
        state->emit(bundle(q, 3, AcquisitionDisposition::completed));
        auto r = run(state, q);
        CHECK(r->prepare());
        (void)r->start();
        CHECK(finish(*r).terminal.reason == AcquisitionReason::evidence_missing);
    }
    // The authoritative queue shares the exact immutable synthetic pixel storage.
    {
        auto q = captured();
        auto b = capture(q);
        Packet image;
        image.type = schema::image;
        image.header.sequence.value = 42;
        memory::BufferBuilder pixels(4);
        pixels.writable()[0] = std::byte{7};
        auto storage = std::move(pixels).publish();
        auto backing = storage.identity();
        image.attributes.push_back(
            {{"org.mantis.pixels", schema::ScalarType::u8, {2, 2}, {2, 1}, "intensity"}, storage});
        Packet set;
        set.type = schema::frameset;
        set.header.sequence.value = 0;
        set.frames = {data::publish(std::move(image))};
        b.frameset = data::publish(std::move(set));
        auto state = std::make_shared<FakeState>();
        state->emit(b);
        state->emit(bundle(q, 1, AcquisitionDisposition::completed));
        std::shared_ptr<const AcquisitionBundle> retained;
        {
            auto r = run(state, q);
            CHECK(r->prepare());
            CHECK(r->start());
            CHECK(finish(*r).state == ProjectedState::completed);
            retained = **r->next(0);
        }
        CHECK(retained->frameset->frames[0]->attributes[0].buffer.identity() == backing);
        CHECK(retained->frameset->frames[0]->attributes[0].buffer.map_read()->front() == std::byte{7});
    }
    // An independently advanced fake clock wakes priority abort during blocked next.
    {
        std::atomic_int64_t clock{};
        auto state = std::make_shared<FakeState>();
        state->block = true;
        auto q = p;
        q.bounds.max_duration = 50ms;
        auto r = run(state, q, config(),
                     [&] { return std::chrono::steady_clock::time_point{Duration{clock.load()}}; });
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(state->await([&] { return state->entered; }));
        clock = 50'000'000;
        r->notify_clock_advanced();
        auto end = finish(*r);
        CHECK(end.terminal.reason == AcquisitionReason::timeout);
        CHECK(state->aborts == 1);
        for (auto timeout : state->next_timeouts)
            CHECK(timeout <= 50);
    }
    // Run expiration wins when the publication wait is longer than the run.
    {
        std::atomic_int64_t clock{};
        auto state = std::make_shared<FakeState>();
        auto q = p;
        q.bounds.max_duration = 50ms;
        auto cfg = config();
        cfg.queue_capacity = 1;
        cfg.publication_timeout_ms = 1000;
        state->emit(bundle(q, 0, AcquisitionDisposition::control_only));
        state->emit(bundle(q, 1, AcquisitionDisposition::startup));
        auto r =
            run(state, q, cfg, [&] { return std::chrono::steady_clock::time_point{Duration{clock.load()}}; });
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(state->await([&] { return state->nexts >= 2; }));
        clock = 50'000'000;
        r->notify_clock_advanced();
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::failed && end.terminal.reason == AcquisitionReason::timeout);
        CHECK(state->aborts == 1 && end.terminal.unqueued_bundle);
    }
    // A monotonic clock may have a negative epoch; an extreme forward jump must
    // expire the run without overflowing time-point subtraction.
    {
        std::atomic_int64_t clock{std::chrono::steady_clock::time_point::min().time_since_epoch().count() +
                                  1};
        auto state = std::make_shared<FakeState>();
        state->on_next = [&](uint32_t) {
            clock = std::chrono::steady_clock::time_point::max().time_since_epoch().count();
        };
        auto r = run(state, p, config(),
                     [&] { return std::chrono::steady_clock::time_point{Duration{clock.load()}}; });
        CHECK(r->prepare());
        (void)r->start();
        CHECK(finish(*r).terminal.reason == AcquisitionReason::timeout);
    }
    // Explicit finite consumer timeouts, plus run-deadline arithmetic overflow.
    {
        auto state = std::make_shared<FakeState>();
        auto r = run(state, p);
        CHECK(!r->next(60001));
        CHECK(!r->wait_terminal(60001));
    }
    {
        auto state = std::make_shared<FakeState>();
        auto r = run(state, p, config(), [] { return std::chrono::steady_clock::time_point::max() - 1ns; });
        CHECK(r->prepare());
        CHECK(!r->start());
        CHECK(finish(*r).state == ProjectedState::failed);
    }
}
void review_queue_tests() {
    auto p = program();
    // A full queue and an in-flight executor publication are synchronized by next.
    // Stop/cancel must retain it without treating an external interruption as saturation.
    for (bool cancel : {false, true}) {
        auto state = std::make_shared<FakeState>();
        auto cfg = config();
        cfg.queue_capacity = 1;
        cfg.publication_timeout_ms = 1000;
        state->emit(bundle(p, 0, AcquisitionDisposition::control_only));
        state->emit(bundle(p, 1, AcquisitionDisposition::startup));
        auto r = run(state, p, cfg);
        CHECK(r->prepare()); CHECK(r->start());
        CHECK(state->await([&] { return state->nexts >= 2; }));
        if (cancel) r->cancel(); else r->stop();
        auto end = finish(*r);
        CHECK(end.state == (cancel ? ProjectedState::cancelled : ProjectedState::completed));
        CHECK(end.terminal.reason == (cancel ? AcquisitionReason::user_cancel : AcquisitionReason::user_stop));
        CHECK(!end.terminal.initiating_error);
        CHECK(end.queue.saturation_failures == 0 && end.queue.produced == 1);
        CHECK(end.terminal.unqueued_bundle && end.terminal.unqueued_bundle->key.sequence.value == 1);
        CHECK(state->aborts == 1);
    }
    // A previously latched evidence fault wins while publication is blocked.
    {
        auto q = p;
        q.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
        std::atomic_int64_t ticks{};
        auto state = std::make_shared<FakeState>();
        auto cfg = config(); cfg.queue_capacity = 1; cfg.publication_timeout_ms = 1000;
        state->emit(bundle(q, 0, AcquisitionDisposition::control_only));
        state->emit(bundle(q, 1, AcquisitionDisposition::startup));
        auto r = run(state, q, cfg, [&] { return std::chrono::steady_clock::time_point{std::chrono::nanoseconds{ticks.load()}}; });
        CHECK(r->prepare()); CHECK(r->start());
        CHECK(state->await([&] { return state->nexts >= 2; }));
        ticks = 60'000'000; r->notify_clock_advanced();
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::failed && end.terminal.reason == AcquisitionReason::evidence_missing);
        CHECK(end.queue.saturation_failures == 0);
        CHECK(end.terminal.unqueued_bundle);
    }
}
void review_source_tests() {
    auto q = program(); q.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
    for (int mode = 0; mode < 6; ++mode) {
        auto state = std::make_shared<FakeState>();
        auto p = q;
        if (mode == 2) p.participants.controllers.clear();
        auto b = bundle(p, 0, AcquisitionDisposition::control_only); acknowledged(b);
        auto ack = *b.evidence.emitters[0].acknowledged.get();
        if (mode == 0) ack.evidence.source = {{"foreign-authority"}};
        if (mode == 1) state->selected.components[3].controls.clear();
        if (mode == 3) state->selected.components[3].evidence_methods = {EvidenceMethod::software_dispatch};
        if (mode == 4) state->selected.components[3].evidence_scopes = {EvidenceScope::electrical_enable};
        if (mode >= 1 && mode <= 4) {
            // Preflight has a legitimate authority; publication still names the
            // wrong/unadvertised/non-participating timer instead of this controller.
            auto authority = graph().components[3];
            authority.descriptor.id = {"control-authority"};
            authority.descriptor.capabilities = {"org.mantis.emitter.power-control.v1"};
            authority.trigger_modes.clear(); authority.trigger_endpoints.clear();
            state->selected.components[3].controls.clear();
            state->selected.components[0].participants.push_back(authority.descriptor.id);
            state->selected.components.push_back(authority); ++state->selected.limits.max_components;
            p.participants.controllers.push_back(ComponentId{authority.descriptor.id});
            b.evidence.participants = p.participants;
        }
        b.evidence.emitters[0].acknowledged = ack;
        state->emit(b); state->emit(bundle(p, 1, AcquisitionDisposition::completed));
        auto r = run(state, p); CHECK(r->prepare()); (void)r->start();
        auto end = finish(*r);
        CHECK(end.state == (mode == 5 ? ProjectedState::completed : ProjectedState::failed));
        if (mode != 5) CHECK(end.terminal.reason == AcquisitionReason::contradictory_evidence);
    }
    // Integrated parent control authority is an explicit controller participant;
    // it uses the parent ID rather than inventing a separate child.
    {
        auto state = std::make_shared<FakeState>();
        auto p = q; p.participants.controllers = {ComponentId{state->selected.parent}};
        auto &parent = state->selected.components[0];
        parent.descriptor.capabilities.push_back("org.mantis.emitter.power-control.v1");
        parent.controls = {emitter.id};
        parent.evidence_methods = {EvidenceMethod::controller_report};
        parent.evidence_scopes = {EvidenceScope::controller_register};
        state->selected.components[3].controls.clear(); // No duplicate control authority.
        auto b = bundle(p, 0, AcquisitionDisposition::control_only); acknowledged(b);
        auto ack = *b.evidence.emitters[0].acknowledged.get(); ack.evidence.source = ComponentId{parent.descriptor.id};
        b.evidence.emitters[0].acknowledged = ack;
        state->emit(b); state->emit(bundle(p, 1, AcquisitionDisposition::completed));
        auto r = run(state, p); CHECK(r->prepare()); CHECK(r->start());
        CHECK(finish(*r).state == ProjectedState::completed);
    }
    for (int mode = 0; mode < 7; ++mode) {
        auto p = captured(EvidenceRequirement::exposure_effective);
        auto state = std::make_shared<FakeState>(); auto b = capture(p); effective(b);
        auto value = *b.evidence.emitters[0].exposure_effective[0].state.get();
        if (mode == 0) value.evidence.source = {{"foreign-source"}};
        if (mode == 1) state->selected.components[3].evidence_methods.clear();
        if (mode == 2) state->selected.components[3].evidence_scopes.clear();
        if (mode == 3) {
            value.evidence = {emitter, EvidenceMethod::electrical_readback, Unavailable{}};
            value.scope = EvidenceScope::electrical_enable; p.steps[0].required_scope = value.scope;
        }
        if (mode == 4) {
            ProjectedComponent sensor;
            sensor.kind = ParticipantKind::controller; sensor.descriptor.id = {"optical-monitor"};
            sensor.descriptor.parent = state->selected.parent;
            sensor.evidence_methods = {EvidenceMethod::optical_sensor};
            sensor.evidence_scopes = {EvidenceScope::optical_emission};
            state->selected.components[0].participants.push_back(sensor.descriptor.id);
            state->selected.components.push_back(sensor); ++state->selected.limits.max_components;
            value.evidence = {ComponentId{sensor.descriptor.id}, EvidenceMethod::optical_sensor, Unavailable{}};
            value.scope = EvidenceScope::optical_emission; p.steps[0].required_scope = value.scope;
        }
        if (mode == 6) state->selected.components[3].controls.clear();
        b.evidence.program = p.identity;
        b.evidence.emitters[0].exposure_effective[0].state = value;
        CHECK(data::validate(b));
        state->emit(b); state->emit(bundle(p, 1, AcquisitionDisposition::completed));
        auto r = run(state, p); CHECK(r->prepare()); (void)r->start();
        auto end = finish(*r);
        CHECK(end.state == ((mode >= 3 && mode <= 5) ? ProjectedState::completed : ProjectedState::failed));
        if (mode < 3 || mode == 6) CHECK(end.terminal.reason == AcquisitionReason::contradictory_evidence);
    }
    // Even optional observed facts and trigger event provenance require graph membership.
    {
        auto p = program(); auto b = bundle(p, 0, AcquisitionDisposition::control_only);
        b.evidence.emitters[0].observed = StateObservation{EmitterState::off, EvidenceScope::controller_register,
            {ComponentId{{"foreign"}}, EvidenceMethod::controller_report, Unavailable{}}, Unknown{}, Unknown{}};
        fails(p, b, AcquisitionReason::contradictory_evidence);
    }
    {
        auto p = captured(EvidenceRequirement::commanded_only, true); auto b = capture(p); auto t = trigger(p);
        t.evidence.source = {{"foreign"}}; b.triggers = {t}; b.evidence.triggers = {t.key};
        fails(p, b, AcquisitionReason::contradictory_evidence);
    }
}
void review_outcome_tests() {
    for (auto result : {AcknowledgementResult::rejected, AcknowledgementResult::failed}) {
        auto p = program(); p.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
        auto b = bundle(p, 0, AcquisitionDisposition::control_only); acknowledged(b);
        auto ack = *b.evidence.emitters[0].acknowledged.get(); ack.result = result;
        b.evidence.emitters[0].acknowledged = ack; CHECK(data::validate(b));
        auto state = std::make_shared<FakeState>(); state->emit(b);
        auto r = run(state, p); CHECK(r->prepare()); (void)r->start(); auto end = finish(*r);
        CHECK(end.state == ProjectedState::failed);
        CHECK(end.terminal.reason == (result == AcknowledgementResult::rejected ? AcquisitionReason::rejected : AcquisitionReason::device_failure));
        CHECK(end.terminal.unqueued_bundle->evidence.emitters[0].acknowledged.get()->result == result);
    }
    for (auto kind : {TriggerEvent::Kind::rejected, TriggerEvent::Kind::timed_out, TriggerEvent::Kind::cancelled}) {
        auto p = captured(EvidenceRequirement::commanded_only, true);
        auto b = capture(p); auto t = trigger(p); t.kind = kind;
        if (kind == TriggerEvent::Kind::rejected) {
            t.evidence = proof();
            t.acknowledgement = Acknowledgement{t.request, AcknowledgementStage::acceptance,
                AcknowledgementResult::rejected, proof(), Unknown{}, EvidenceScope::controller_register};
        }
        b.triggers = {t}; b.evidence.triggers = {t.key}; CHECK(data::validate(b));
        auto state = std::make_shared<FakeState>(); state->emit(b);
        auto r = run(state, p); CHECK(r->prepare()); (void)r->start(); auto end = finish(*r);
        auto reason = kind == TriggerEvent::Kind::rejected ? AcquisitionReason::rejected :
                      kind == TriggerEvent::Kind::timed_out ? AcquisitionReason::timeout : AcquisitionReason::device_failure;
        CHECK(end.state == ProjectedState::failed && end.terminal.reason == reason);
        CHECK(end.terminal.unqueued_bundle->triggers[0].kind == kind);
    }
    for (bool cancel : {false, true}) {
        auto p = captured(EvidenceRequirement::commanded_only, true);
        auto b = bundle(p, 0, AcquisitionDisposition::startup); auto t = trigger(p); t.kind = TriggerEvent::Kind::cancelled;
        b.triggers = {t}; b.evidence.triggers = {t.key}; CHECK(data::validate(b));
        auto state = std::make_shared<FakeState>(); state->block = true; state->emit(b);
        auto cfg = config(); cfg.operation_timeout_ms = 1000;
        auto r = run(state, p, cfg); CHECK(r->prepare()); CHECK(r->start());
        CHECK(state->await([&] { return state->entered; }));
        if (cancel) r->cancel(); else r->stop();
        auto end = finish(*r);
        CHECK(end.state == (cancel ? ProjectedState::cancelled : ProjectedState::completed));
        CHECK(end.terminal.reason == (cancel ? AcquisitionReason::user_cancel : AcquisitionReason::user_stop));
        CHECK(end.terminal.unqueued_bundle->triggers[0].kind == TriggerEvent::Kind::cancelled);
    }
}
void review_resource_tests() {
    // Control-only evidence occupies generic correlation slots, never capture slots.
    {
        auto p = program(); p.repetitions = 2; p.bounds.max_in_flight_captures = 1;
        p.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
        auto first = bundle(p, 0, AcquisitionDisposition::control_only, 0, 0);
        auto second = bundle(p, 1, AcquisitionDisposition::control_only, 0, 1);
        auto late_first = first; late_first.key.sequence = {2}; late_first.evidence.key.ordinal = {2}; late_first.published = runtime(2); acknowledged(late_first);
        auto late_second = second; late_second.key.sequence = {3}; late_second.evidence.key.ordinal = {3}; late_second.published = runtime(3); acknowledged(late_second);
        auto state = std::make_shared<FakeState>();
        state->emit(first); state->emit(second); state->emit(late_first); state->emit(late_second); state->emit(bundle(p, 4, AcquisitionDisposition::completed));
        auto r = run(state, p); CHECK(r->prepare()); CHECK(r->start());
        auto end = finish(*r); CHECK(end.state == ProjectedState::completed && end.covered_steps == 2);
    }
    {
        auto p = captured(EvidenceRequirement::controller_acknowledged); p.repetitions = 2; p.bounds.max_in_flight_captures = 1;
        auto first = capture(p); auto second = first;
        second.key.sequence = {1}; second.published = runtime(1); second.evidence.key.ordinal = {1};
        second.evidence.step = StepInstance{identity.run, 1, p.steps[0].index};
        second.evidence.frames[0].frame.native_sequence = 43;
        second.evidence.emitters[0].exposure_effective[0].frame = second.evidence.frames[0].frame;
        second.evidence.frameset = FrameSetKey{identity.run, {StreamId{{"parent-output"}}, source_generation}, 1};
        second.evidence.emitters[0].commanded = EmitterCommand{{{"second-command"}}, emitter, EmitterState::off, runtime(1)};
        auto state = std::make_shared<FakeState>(); state->emit(first); state->emit(second);
        auto r = run(state, p); CHECK(r->prepare()); (void)r->start();
        CHECK(finish(*r).terminal.reason == AcquisitionReason::resource_limit);
    }
    for (bool commands : {false, true}) {
        auto p = program(); auto cfg = config(); cfg.max_correlation_entries = 128;
        if (commands) p.bounds.max_commands = 129; else p.bounds.max_events = 129;
        auto state = std::make_shared<FakeState>(); auto r = run(state, p, cfg);
        CHECK(!r->prepare()); CHECK(state->validations == 0 && state->starts == 0);
        CHECK(finish(*r).terminal.reason == AcquisitionReason::resource_limit);
    }
    // Declared identity arithmetic is checked before allocation or plugin calls.
    {
        auto p = program(); p.bounds.max_events = UINT64_MAX;
        auto state = std::make_shared<FakeState>(); state->selected.limits.bounds.max_events = UINT64_MAX;
        auto r = run(state, p); CHECK(!r->prepare());
        CHECK(state->validations == 0 && state->starts == 0);
        CHECK(finish(*r).terminal.reason == AcquisitionReason::resource_limit);
    }
    // Exact declared event and command ceilings remain usable after preflight.
    {
        auto p = program(); p.repetitions = 12; p.bounds.max_commands = 12; p.bounds.max_events = 25;
        auto cfg = config(); cfg.queue_capacity = 32; cfg.max_correlation_entries = 128;
        auto state = std::make_shared<FakeState>();
        for (uint64_t i = 0; i < 12; ++i) {
            state->emit(bundle(p, 2*i, AcquisitionDisposition::control_only, 0, i));
            state->emit(bundle(p, 2*i+1, AcquisitionDisposition::startup));
        }
        state->emit(bundle(p, 24, AcquisitionDisposition::completed));
        auto r = run(state, p, cfg); CHECK(r->prepare()); CHECK(r->start());
        auto end = finish(*r); CHECK(end.state == ProjectedState::completed);
        CHECK(end.covered_steps == 12 && end.queue.produced == p.bounds.max_events);
    }
    // Four pixel bytes cannot hide descriptor or header allocations behind payload accounting.
    for (int where = 0; where < 8; ++where) {
        auto p = captured(); p.bounds.max_bytes = 8 * 1024 * 1024;
        auto b = capture(p); Packet image; image.type = schema::image; image.header.sequence.value = 42;
        memory::BufferBuilder pixels(4); auto storage = std::move(pixels).publish();
        image.attributes.push_back({{"org.mantis.pixels", schema::ScalarType::u8, {2,2}, {2,1}, "intensity"}, storage});
        Packet set; set.type = schema::frameset; set.header.sequence.value = 0;
        const size_t huge = p.bounds.max_bytes + 1;
        if (where == 0) set.header.metadata["org.example.large"] = std::string(huge, 'x');
        if (where == 1) image.header.metadata["org.example.large"] = std::string(huge, 'x');
        if (where == 2) image.attributes[0].descriptor.name = "org.example." + std::string(huge, 'x');
        if (where == 3) image.attributes[0].descriptor.unit = std::string(huge, 'x');
        if (where == 4) image.attributes[0].descriptor.shape.reserve(huge / sizeof(uint64_t) + 1);
        if (where == 5) image.header.frame.name = std::string(huge, 'x');
        if (where == 6) image.attributes[0].descriptor.stride.reserve(huge / sizeof(uint64_t) + 1);
        set.frames = {data::publish(std::move(image))};
        if (where == 7) set.frames.reserve(huge / sizeof(data::Published) + 1);
        b.frameset = data::publish(std::move(set)); CHECK(data::validate(b));
        auto state = std::make_shared<FakeState>(); state->emit(b); auto r = run(state, p);
        CHECK(r->prepare()); (void)r->start(); auto end = finish(*r);
        CHECK(end.state == ProjectedState::failed && end.terminal.reason == AcquisitionReason::resource_limit);
        CHECK(end.queue.produced == 0 && !end.terminal.unqueued_bundle);
        CHECK(!end.terminal.executor_terminal);
    }
}

void final_preflight_tests() {
    // Dispatch feasibility follows accessible power control, not a dispatch-proof
    // advertisement on the emitter itself.
    {
        auto state = std::make_shared<FakeState>();
        for (auto &component : state->selected.components)
            component.evidence_methods.clear();
        auto r = run(state);
        CHECK(r->prepare());
        CHECK(r->snapshot().state == ProjectedState::ready);
        CHECK(state->validations == 1 && state->preparations == 1 && state->starts == 0);
    }
    for (int mode = 0; mode < 7; ++mode) {
        auto p = program();
        p.steps[0].evidence_requirement = EvidenceRequirement::controller_acknowledged;
        auto state = std::make_shared<FakeState>();
        state->selected.components[1].evidence_methods.clear();
        auto &controller = state->selected.components[3];
        controller.evidence_methods = {EvidenceMethod::controller_report};
        controller.evidence_scopes = {EvidenceScope::controller_register};
        if (mode == 1)
            controller.controls.clear();
        if (mode == 2)
            p.participants.controllers.clear();
        if (mode == 3 || mode == 4) {
            auto &parent = state->selected.components[0];
            parent.descriptor.capabilities.push_back("org.mantis.emitter.power-control.v1");
            parent.controls = {emitter.id};
            parent.evidence_methods = controller.evidence_methods;
            parent.evidence_scopes = controller.evidence_scopes;
            controller.controls.clear();
            p.participants.controllers = mode == 3 ? std::vector<ComponentId>{ComponentId{parent.descriptor.id}}
                                                 : std::vector<ComponentId>{};
        }
        if (mode == 5)
            controller.evidence_scopes = {EvidenceScope::electrical_enable};
        if (mode == 6)
            controller.evidence_methods = {EvidenceMethod::camera_metadata};
        auto r = run(state, p);
        bool feasible = mode == 0 || mode == 3;
        CHECK(static_cast<bool>(r->prepare()) == feasible);
        CHECK(state->validations == (feasible ? 1u : 0u));
        CHECK(state->preparations == (feasible ? 1u : 0u));
        CHECK(state->starts == 0);
        if (feasible)
            CHECK(r->snapshot().state == ProjectedState::ready);
        else
            CHECK(finish(*r).terminal.reason == AcquisitionReason::rejected);
    }
    // A graph-owned optical source supplies feasibility and synthetic proof;
    // no optical evidence method is needed on the emitter or its controller.
    {
        auto p = captured(EvidenceRequirement::exposure_effective);
        p.steps[0].required_scope = EvidenceScope::optical_emission;
        auto state = std::make_shared<FakeState>();
        for (auto &component : state->selected.components)
            component.evidence_methods.clear();
        state->selected.components[2].evidence_methods = {EvidenceMethod::camera_metadata};
        ProjectedComponent optical;
        optical.kind = ParticipantKind::controller;
        optical.descriptor.id = {"optical-proof-source"};
        optical.descriptor.parent = state->selected.parent;
        optical.evidence_methods = {EvidenceMethod::optical_sensor};
        optical.evidence_scopes = {EvidenceScope::optical_emission};
        state->selected.components[0].participants.push_back(optical.descriptor.id);
        state->selected.components.push_back(optical);
        ++state->selected.limits.max_components;
        auto b = capture(p);
        effective(b, EvidenceScope::optical_emission);
        auto value = *b.evidence.emitters[0].exposure_effective[0].state.get();
        value.evidence = {ComponentId{optical.descriptor.id}, EvidenceMethod::optical_sensor, Unavailable{}};
        b.evidence.emitters[0].exposure_effective[0].state = value;
        CHECK(data::validate(b));
        state->emit(b);
        state->emit(bundle(p, 1, AcquisitionDisposition::completed));
        auto r = run(state, p);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(finish(*r).state == ProjectedState::completed);
    }
}
const ComponentId other_camera{{"other-sensor"}};
void add_other_camera(FakeState &state, AcquisitionProgram &p) {
    auto source = state.selected.components[2];
    source.descriptor.id = other_camera.id;
    source.role = "other-measurement";
    source.image_source->stream = {{"other-image-output"}};
    source.image_source->physical_identity = "other-physical-camera";
    source.evidence_methods.push_back(EvidenceMethod::controller_report);
    state.selected.components[0].participants.push_back(other_camera.id);
    state.selected.components.push_back(source);
    state.selected.components[3].trigger_endpoints.push_back(other_camera.id);
    ++state.selected.limits.max_components;
    p.participants.cameras.push_back({other_camera, source.image_source->stream, source.role});
}
void final_provenance_tests() {
    for (bool correct : {false, true}) {
        auto state = std::make_shared<FakeState>();
        auto p = captured();
        add_other_camera(*state, p);
        auto b = capture(p);
        auto exposure = *b.evidence.frames[0].exposure.get();
        exposure.evidence.source = correct ? camera : other_camera;
        b.evidence.frames[0].exposure = exposure;
        CHECK(data::validate(b));
        state->emit(b);
        state->emit(bundle(p, 1, AcquisitionDisposition::completed));
        auto r = run(state, p);
        CHECK(r->prepare());
        (void)r->start();
        auto end = finish(*r);
        CHECK(end.state == (correct ? ProjectedState::completed : ProjectedState::failed));
        if (!correct) {
            CHECK(end.terminal.reason == AcquisitionReason::contradictory_evidence);
            CHECK(end.terminal.unqueued_bundle->evidence.frames[0].exposure.get()->evidence.source == other_camera);
        }
    }
    // Both frame hardware-sync and TriggerEvent association paths bind camera
    // metadata to that exact source. Controller reports bind to the trigger's
    // controller; immutable executor proof may come from the graph-owned parent.
    for (bool in_trigger : {false, true})
        for (int source = 0; source < 5; ++source) {
            auto state = std::make_shared<FakeState>();
            auto p = captured(EvidenceRequirement::commanded_only, true);
            add_other_camera(*state, p);
            state->selected.components[0].evidence_methods = {EvidenceMethod::validated_executor};
            auto b = capture(p);
            auto requested = trigger(p, 0);
            auto observed = trigger(p, 1);
            observed.kind = TriggerEvent::Kind::observed;
            observed.evidence = proof();
            ExposureAssociation association;
            association.frame = b.evidence.frames[0].frame;
            association.trigger = observed.key;
            association.method = AssociationMethod::native_trigger;
            association.native_trigger = NativeTriggerIdentity{controller_id, controller_generation, 12};
            association.evidence = {source == 1 || source == 3 ? other_camera : camera,
                                    EvidenceMethod::camera_metadata, Unavailable{}};
            if (source == 2 || source == 3)
                association.evidence = {source == 2 ? controller_id : other_camera,
                                        EvidenceMethod::controller_report, Unavailable{}};
            if (source == 4) {
                association.method = AssociationMethod::validated_executor;
                association.evidence = proof(EvidenceMethod::validated_executor);
                association.evidence.source = ComponentId{state->selected.parent};
            }
            if (in_trigger)
                observed.exposure_association = association;
            else
                b.evidence.frames[0].sync = SyncEvidence{{{"synthetic-sync"}, 0}, time::SyncQuality::hardware,
                                                        association};
            b.triggers = {requested, observed};
            b.evidence.triggers = {requested.key, observed.key};
            CHECK(data::validate(b));
            state->emit(b);
            state->emit(bundle(p, 1, AcquisitionDisposition::completed));
            auto r = run(state, p);
            CHECK(r->prepare());
            (void)r->start();
            auto end = finish(*r);
            bool valid = source == 0 || source == 2 || source == 4;
            CHECK(end.state == (valid ? ProjectedState::completed : ProjectedState::failed));
            if (!valid) {
                CHECK(end.terminal.reason == AcquisitionReason::contradictory_evidence);
                CHECK(end.terminal.unqueued_bundle);
            }
        }
}
void attach_frame(AcquisitionBundle &b, size_t bytes, size_t metadata = 0) {
    Packet image;
    image.type = schema::image;
    image.header.sequence.value = b.evidence.frames[0].frame.native_sequence;
    memory::BufferBuilder pixels(bytes);
    image.attributes.push_back({{"org.mantis.pixels", schema::ScalarType::u8, {2, 2}, {2, 1}, "intensity"},
                                std::move(pixels).publish()});
    Packet set;
    set.type = schema::frameset;
    set.header.sequence.value = b.evidence.frameset.get()->sequence;
    if (metadata)
        set.header.metadata["org.example.retention-test"] = std::string(metadata, 'x');
    set.frames = {data::publish(std::move(image))};
    b.frameset = data::publish(std::move(set));
}
void final_admission_tests() {
    for (bool oversized : {false, true})
        for (bool rejected : {false, true}) {
            auto p = captured(EvidenceRequirement::controller_acknowledged);
            p.bounds.max_bytes = 8 * 1024 * 1024;
            auto b = capture(p);
            acknowledged(b);
            if (rejected) {
                auto ack = *b.evidence.emitters[0].acknowledged.get();
                ack.result = AcknowledgementResult::rejected;
                b.evidence.emitters[0].acknowledged = ack;
            } else {
                auto exposure = *b.evidence.frames[0].exposure.get();
                exposure.evidence.source = {{"foreign-exposure-source"}};
                b.evidence.frames[0].exposure = exposure;
            }
            attach_frame(b, oversized ? p.bounds.max_bytes + 1 : 4);
            CHECK(data::validate(b));
            auto original = b.frameset.get();
            auto backing = b.frameset->frames[0]->attributes[0].buffer.identity();
            std::weak_ptr<const Packet> weak_set = b.frameset, weak_image = b.frameset->frames[0];
            auto state = std::make_shared<FakeState>();
            state->emit(std::move(b));
            auto r = run(state, p);
            CHECK(r->prepare());
            (void)r->start();
            auto end = finish(*r);
            CHECK(end.state == ProjectedState::failed);
            CHECK(end.queue.produced == 0 && end.queue.saturation_failures == 0);
            CHECK(state->aborts == 1);
            if (oversized) {
                CHECK(end.terminal.reason == AcquisitionReason::resource_limit);
                CHECK(end.terminal.initiating_error && end.terminal.initiating_error->message.size() < 256);
                CHECK(!end.terminal.unqueued_bundle && !end.terminal.executor_terminal);
                CHECK(weak_set.expired() && weak_image.expired());
            } else {
                CHECK(end.terminal.reason == (rejected ? AcquisitionReason::rejected
                                                      : AcquisitionReason::contradictory_evidence));
                CHECK(end.terminal.unqueued_bundle->frameset.get() == original);
                CHECK(end.terminal.unqueued_bundle->frameset->frames[0]->attributes[0].buffer.identity() == backing);
                if (rejected)
                    CHECK(end.terminal.unqueued_bundle->evidence.emitters[0].acknowledged.get()->result == AcknowledgementResult::rejected);
            }
        }
    // Metadata also precedes negative-outcome validation in admission.
    {
        auto p = captured(EvidenceRequirement::controller_acknowledged);
        p.bounds.max_bytes = 8 * 1024 * 1024;
        auto b = capture(p);
        acknowledged(b);
        auto ack = *b.evidence.emitters[0].acknowledged.get();
        ack.result = AcknowledgementResult::rejected;
        b.evidence.emitters[0].acknowledged = ack;
        attach_frame(b, 4, p.bounds.max_bytes + 1);
        CHECK(data::validate(b));
        std::weak_ptr<const Packet> weak_set = b.frameset;
        auto state = std::make_shared<FakeState>();
        state->emit(std::move(b));
        auto r = run(state, p);
        CHECK(r->prepare());
        (void)r->start();
        auto end = finish(*r);
        CHECK(end.terminal.reason == AcquisitionReason::resource_limit);
        CHECK(!end.terminal.unqueued_bundle && weak_set.expired());
    }
    // The budget fits one 4-MiB payload and structural charges, but would not fit
    // two payload charges. Successful capture proves exactly-once admission.
    {
        auto p = captured();
        p.bounds.max_bytes = 8 * 1024 * 1024;
        auto b = capture(p);
        attach_frame(b, 4 * 1024 * 1024);
        auto backing = b.frameset->frames[0]->attributes[0].buffer.identity();
        auto state = std::make_shared<FakeState>();
        state->emit(std::move(b));
        state->emit(bundle(p, 1, AcquisitionDisposition::completed));
        auto r = run(state, p);
        CHECK(r->prepare());
        CHECK(r->start());
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::completed && end.queue.produced == 2);
        auto retained = **r->next(0);
        CHECK(retained->frameset->frames[0]->attributes[0].buffer.identity() == backing);
    }
    // An admitted publication can exhaust the space for private correlation copies.
    // Those partial copies are discarded; the original admitted bundle stays available.
    {
        auto p = captured(EvidenceRequirement::controller_acknowledged);
        p.bounds.max_commands = 1;
        p.bounds.max_events = 2;
        p.bounds.max_bytes = 768 * 1024;
        auto b = capture(p);
        b.evidence.implementations.clear();
        for (size_t i = 0; i < max_participants; ++i)
            b.evidence.implementations.push_back({
                {"producer-" + std::to_string(i) + std::string(200, 'p')}, {1, 0, 0},
                std::string(1024, 'b'), ContentReference{
                    {"configuration-" + std::string(200, 'c')},
                    {"org.example." + std::string(1000, 't'), 1}, Hash{"sha256", "aabb"}, 1}});
        CHECK(data::validate(b));
        auto state = std::make_shared<FakeState>();
        state->emit(std::move(b));
        state->emit(bundle(p, 1, AcquisitionDisposition::completed));
        auto r = run(state, p);
        CHECK(r->prepare());
        (void)r->start();
        auto end = finish(*r);
        CHECK(end.terminal.reason == AcquisitionReason::resource_limit);
        CHECK(end.queue.produced == 0 && end.terminal.unqueued_bundle);
        CHECK(end.terminal.unqueued_bundle->evidence.implementations.size() == max_participants);
    }
    // Admission bounds malformed packet traversal before any recursive accounting.
    {
        auto p = captured();
        auto b = capture(p);
        Packet malformed;
        malformed.type = schema::frameset;
        malformed.frames = {nullptr};
        b.frameset = std::make_shared<const Packet>(std::move(malformed));
        std::weak_ptr<const Packet> weak_set = b.frameset;
        auto state = std::make_shared<FakeState>();
        state->emit(std::move(b));
        auto r = run(state, p);
        CHECK(r->prepare());
        (void)r->start();
        auto end = finish(*r);
        CHECK(end.terminal.reason == AcquisitionReason::contradictory_evidence);
        CHECK(!end.terminal.unqueued_bundle && weak_set.expired());
    }
}

void concurrency_tests() {
    {
        auto s = std::make_shared<FakeState>();
        s->block = true;
        s->abort_block = true;
        auto cfg = config();
        cfg.operation_timeout_ms = 1000;
        auto r = run(s, program(), cfg);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(s->await([&] { return s->entered; }));
        auto cancel = std::async(std::launch::async, [&] { r->cancel(); });
        CHECK(s->await([&] { return s->abort_entered; }));
        CHECK(s->abort_overlapped_next);
        CHECK(r->snapshot().state == ProjectedState::stopping);
        r->stop();
        r->cancel();
        CHECK(s->aborts == 1);
        {
            std::lock_guard lock(s->mutex);
            s->abort_release = true;
            s->cv.notify_all();
        }
        cancel.get();
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::cancelled);
        CHECK(end.terminal.abort_outcome->emitters[0].commanded.presence() == Presence::unknown);
        CHECK(end.terminal.abort_outcome->emitters[0].observed.presence() == Presence::unavailable);
    }
    {
        auto s = std::make_shared<FakeState>();
        s->block = true;
        s->outputs.push_back(std::unexpected(injected()));
        auto r = run(s);
        CHECK(r->prepare());
        CHECK(r->start());
        CHECK(s->await([&] { return s->entered; }));
        r->cancel();
        auto end = finish(*r);
        CHECK(end.state == ProjectedState::failed);
        CHECK(end.terminal.reason == AcquisitionReason::device_failure);
    }
}
} // namespace
void terminal_recording_tests() {
    auto root = std::filesystem::temp_directory_path() / Id::random().value;
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() { std::filesystem::remove_all(p); }
    } cleanup{root};
    auto store = std::make_shared<artifact::Store>(root);
    // Actual L3 execution: preflight/prepare failure, pending-next cancellation,
    // normal executor completion, stop/close failure, cancellation with cleanup fault.
    for (unsigned mode = 0; mode < 7; ++mode) {
        auto s = std::make_shared<FakeState>();
        std::promise<void> stop_entered, release_stop;
        auto released = release_stop.get_future().share();
        if (mode == 0)
            s->on_stop = [&] {
                stop_entered.set_value();
                CHECK(released.wait_for(1s) == std::future_status::ready);
            };
        const bool cancel = mode == 1 || mode == 5;
        s->fail_prepare = mode == 0;
        s->fail_stop = mode == 3 || mode == 5;
        s->fail_close = mode == 4;
        s->block = cancel;
        auto p = program();
        if (mode >= 2 && !cancel) {
            s->emit(bundle(p, 0, AcquisitionDisposition::control_only));
            s->emit(bundle(p, 1, AcquisitionDisposition::completed));
        }
        if (mode == 6) {
            s->outcome.inhibited = false;
            s->outcome.stale_work_fenced = Unavailable{};
        }
        auto r = run(s, p);
        auto id = store->begin_projected_capture(
            {p, identity.run, identity.generation, recorded_run_config(config())});
        bool rejected = false;
        try {
            (void)recorded_run_outcome(r->snapshot());
        } catch (const Failure &) {
            rejected = true;
        }
        CHECK(rejected);
        if (mode == 0) {
            auto preparing = std::async(std::launch::async, [&] { return r->prepare(); });
            CHECK(stop_entered.get_future().wait_for(1s) == std::future_status::ready);
            auto pending = r->snapshot();
            CHECK(pending.state == ProjectedState::failed && !pending.cleanup_resolved);
            bool pending_rejected = false;
            try {
                (void)recorded_run_outcome(pending);
            } catch (const Failure &) {
                pending_rejected = true;
            }
            CHECK(pending_rejected);
            release_stop.set_value();
            CHECK(preparing.wait_for(1s) == std::future_status::ready && !preparing.get());
        } else {
            CHECK(r->prepare());
            (void)r->start();
            if (cancel) {
                CHECK(s->await([&] { return s->entered; }));
                r->cancel();
            }
        }
        auto end = finish(*r);
        CHECK(end.cleanup_resolved);
        uint64_t records{};
        for (;;) {
            auto next = r->next(0);
            CHECK(next);
            if (!*next)
                break;
            store->append_bundle(id, ***next);
            ++records;
        }
        auto out = recorded_run_outcome(end);
        store->record_run_outcome(id, out);
        CHECK(store->finalize(id).state == artifact::ArtifactState::finalized);
        auto summary = store->bundle_summary(id);
        CHECK(summary.records == records && summary.final_outcome);
        auto recorded = summary.final_outcome;
        CHECK(recorded->reason == end.terminal.reason);
        if (mode == 2)
            CHECK(recorded->disposition == RecordedRunDisposition::completed);
        else if (mode == 1)
            CHECK(recorded->disposition == RecordedRunDisposition::cancelled);
        else
            CHECK(recorded->disposition == RecordedRunDisposition::failed);
        if (mode == 3 || mode == 4) {
            CHECK(end.terminal.executor_terminal && records == 2);
            CHECK(store->bundle(id, 1).evidence.disposition == AcquisitionDisposition::completed);
            CHECK(recorded->reason == AcquisitionReason::cleanup_failure);
            CHECK(mode == 3 ? recorded->stop_error.get() != nullptr : recorded->close_error.get() != nullptr);
        }
        if (cancel)
            CHECK(!end.terminal.executor_terminal && records == 0 &&
                  recorded->reason == AcquisitionReason::user_cancel);
        if (mode == 0)
            CHECK(records == 0 && s->starts == 0 && s->aborts == 0 && recorded->initiating_error.get());
        if (mode != 0) {
            CHECK(recorded->abort_outcome.get());
            CHECK(recorded->abort_outcome.get()->stale_work_fenced.presence() ==
                  (mode == 6 ? Presence::unavailable : Presence::unknown));
        }
    }
}
int main() {
    try {
        final_preflight_tests();
        final_provenance_tests();
        final_admission_tests();
        review_queue_tests();
        review_source_tests();
        review_outcome_tests();
        review_resource_tests();
        preflight_tests();
        lifecycle_tests();
        evidence_tests();
        causal_tests();
        deadline_queue_tests();
        concurrency_tests();
        additional_contract_tests();
        reservation_and_terminal_tests();
        terminal_recording_tests();
        std::cout << checks << " deterministic sequencer checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
