// Dedicated TEST/BENCH DSO. No linux.cpp, UART, GPIO, or physical backend exists.
#define MANTIS_X1_PROJECTED_FIXTURE 1
#include "../../../plugins/first-party/devices/x1/acquisition.hpp"
#include "../x1-projected/views.hpp"
#include "host.hpp"
#include <fstream>
#include <set>

namespace {
namespace camera = x1::acquisition;
namespace f2 = x1::f2;
namespace sim = x1::f2::simulation;
using namespace x1_fixture_views;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
constexpr uint32_t call_limit = 1000;
constexpr uint64_t byte_limit = 1024 * 1024, command_limit = 32;
bool enabled() {
    auto opt = std::getenv("MANTIS_X1_F2_SIMULATION"), fake = std::getenv("MANTIS_X1_FAKE");
    return opt && std::string_view(opt) == "1" && fake && *fake && std::getenv("MANTIS_X1_PROFILE") &&
           std::getenv("MANTIS_X1_F2_CONFIG");
}
sim::BenchConfig configuration() {
    const char *path = std::getenv("MANTIS_X1_F2_CONFIG");
    if (!path)
        throw std::invalid_argument("Explicit F2 simulation configuration required");
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
        throw std::invalid_argument("F2 configuration must be a regular JSON file");
    std::ifstream file(path);
    std::array<char, 4097> bytes{};
    file.read(bytes.data(), bytes.size());
    auto count = file.gcount();
    if (!file.eof() || count <= 0 || count > 4096)
        throw std::invalid_argument("Bounded F2 configuration file");
    auto j = camera::Json::parse(bytes.data(), bytes.data() + count);
    const std::set<std::string> keys{"schema",
                                     "backend",
                                     "controller_id",
                                     "board_revision",
                                     "channel_uid",
                                     "channel_index",
                                     "emitter_suffix",
                                     "controller_suffix",
                                     "rig_revision",
                                     "current_ua",
                                     "period_us",
                                     "high_us",
                                     "pulse_count",
                                     "max_current_ua",
                                     "max_pulses",
                                     "max_run_ms",
                                     "max_continuous_on_us"};
    if (!j.is_object() || j.size() != keys.size())
        throw std::invalid_argument("F2 configuration schema");
    for (auto i = j.begin(); i != j.end(); ++i)
        if (!keys.contains(i.key()))
            throw std::invalid_argument("F2 configuration selector");
    auto number = [&](const char *name, uint64_t expected) {
        if (!j.at(name).is_number_unsigned() || j.at(name).get<uint64_t>() != expected)
            throw std::invalid_argument("F2 mapping/limit mismatch");
    };
    number("schema", 1);
    number("controller_id", sim::BenchConfig::controller);
    number("board_revision", sim::BenchConfig::board);
    number("channel_uid", sim::BenchConfig::channel_uid);
    number("channel_index", 0);
    number("rig_revision", 1);
    number("max_current_ua", sim::BenchConfig::max_current);
    number("max_pulses", sim::BenchConfig::max_pulses);
    number("max_run_ms", sim::BenchConfig::max_run_ms);
    number("max_continuous_on_us", sim::BenchConfig::max_on);
    if (j.at("backend") != "SimulationOnly" || j.at("emitter_suffix") != "/simulation-f2/L1" ||
        j.at("controller_suffix") != "/simulation-f2/esp8266")
        throw std::invalid_argument("Synthetic mapping required");
    auto value = [&](const char *name) {
        auto &v = j.at(name);
        if (!v.is_number_unsigned() || v.get<uint64_t>() > UINT32_MAX)
            throw std::invalid_argument("F2 finite parameter");
        return v.get<uint32_t>();
    };
    sim::BenchConfig config;
    config.current_ua = value("current_ua");
    config.period_us = value("period_us");
    config.high_us = value("high_us");
    config.pulses = value("pulse_count");
    if (!config.valid())
        throw std::invalid_argument("F2 finite limits");
    return config;
}
struct Graph {
    std::array<std::string, 5> ids;
    std::array<std::string, 2> physical;
    std::string frameset;
    const char *frameset_ptr{}, *emitter{}, *controller{};
    std::array<const char *, 4> participants;
    std::array<MantisProjectedImageSourceV1, 2> images;
    std::array<MantisProjectedComponentV1, 5> components;
    MantisProjectedGraphV1 graph = view<MantisProjectedGraphV1>();
    Graph(const x1::Profile &p, const std::array<x1::CameraInfo, 2> &c) {
        ids[0] = camera::parent_id(c);
        ids[1] = ids[0] + "/left";
        ids[2] = ids[0] + "/right";
        ids[3] = ids[0] + "/simulation-f2/L1";
        ids[4] = ids[0] + "/simulation-f2/esp8266";
        emitter = ids[3].c_str();
        controller = ids[4].c_str();
        frameset = ids[0] + "/framesets";
        frameset_ptr = frameset.c_str();
        static const char *root_caps[]{MANTIS_FRAMESET_STREAM_V1, MANTIS_PROJECTED_LIGHT_ACQUISITION_V1};
        static const char *image_caps[]{MANTIS_IMAGE_STREAM_V1};
        static const char *power[]{MANTIS_EMITTER_POWER_CONTROL_V1};
        static const uint32_t states[]{MANTIS_EMITTER_STATE_OFF, MANTIS_EMITTER_STATE_ON};
        static const uint32_t modes[]{MANTIS_CAPTURE_MODE_FREE_RUNNING};
        static const uint32_t methods[]{MANTIS_EVIDENCE_METHOD_SOFTWARE_DISPATCH};
        static const uint32_t scopes[]{MANTIS_EVIDENCE_SCOPE_CONTROLLER_REGISTER};
        for (size_t i = 0; i < 5; ++i) {
            auto &v = components[i];
            v = view<MantisProjectedComponentV1>();
            v.id = ids[i].c_str();
            v.parent_id = i ? ids[0].c_str() : "";
            v.name = i == 0   ? "X1 F2 SimulationOnly TEST/BENCH"
                     : i < 3  ? "Fixture camera"
                     : i == 3 ? "Synthetic mapped L1"
                              : "Simulated ESP8266 F2 v1";
            v.role = i == 1 ? "left" : i == 2 ? "right" : i == 3 ? "synthetic-L1" : "simulation-f2";
            v.participant_kind = i == 0   ? MANTIS_PARTICIPANT_PARENT
                                 : i < 3  ? MANTIS_PARTICIPANT_IMAGE
                                 : i == 3 ? MANTIS_PARTICIPANT_EMITTER
                                          : MANTIS_PARTICIPANT_CONTROLLER;
            v.pattern = absent<MantisEvidencePatternIdV1>();
            v.pattern_revision = absent<MantisEvidenceUInt64V1>();
            if (i)
                participants[i - 1] = v.id;
        }
        auto &root = components[0];
        root.capabilities = root_caps;
        root.capability_count = 2;
        root.participants = participants.data();
        root.participant_count = 4;
        for (size_t i = 0; i < 2; ++i) {
            physical[i] = camera::identity(c[i]);
            auto &image = images[i];
            image = view<MantisProjectedImageSourceV1>();
            image.stream_id = ids[i + 1].c_str();
            image.physical_identity = physical[i].c_str();
            image.width = p.mode.width;
            image.height = p.mode.height;
            auto &v = components[i + 1];
            v.image_source = &image;
            v.capabilities = image_caps;
            v.capability_count = 1;
            v.capture_modes = modes;
            v.capture_mode_count = 1;
        }
        for (size_t i = 3; i < 5; ++i) {
            auto &v = components[i];
            v.capabilities = power;
            v.capability_count = 1;
            v.evidence_methods = methods;
            v.evidence_method_count = 1;
            v.evidence_scopes = scopes;
            v.evidence_scope_count = 1;
        }
        components[3].emitter_states = states;
        components[3].emitter_state_count = 2;
        components[4].controls = &emitter;
        components[4].control_count = 1;
        graph.parent_id = ids[0].c_str();
        graph.components = components.data();
        graph.component_count = 5;
        graph.frameset_stream = present<MantisEvidenceStreamIdV1>(&frameset_ptr);
        auto &l = graph.limits;
        l = view<MantisProjectedLimitsV1>();
        l.max_components = 5;
        l.max_cameras = 2;
        l.max_steps = 1;
        l.max_bundle_members = 1;
        l.max_step_instances = 1;
        l.max_commands = command_limit;
        l.max_events = command_limit;
        l.max_bytes = byte_limit;
        l.max_in_flight_captures = 1;
        l.max_pending_bundles = 1;
        l.max_call_timeout_ms = call_limit;
        l.max_run_duration_ns = 2'000'000'000;
        l.max_on_duration_ns = 2'000'000'000;
        l.max_step_duration_ns = 2'000'000'000;
        l.watchdog = absent<MantisEvidenceUInt32V1>();
        l.interlock = absent<MantisEvidenceUInt32V1>();
        l.fail_off = absent<MantisEvidenceUInt32V1>();
    }
};
struct Instance {
    std::unique_ptr<camera::Device> camera;
    Graph graph;
    sim::BenchConfig config;
    sim::Link link;
    sim::Host host;
    sim::Snapshot snapshot;
    Reference reference;
    std::mutex control;
    std::condition_variable wake;
    uint32_t active{}, state = MANTIS_RUN_OPEN, reason = MANTIS_ACQUISITION_REASON_NONE, step{}, intent{};
    uint64_t fence{}, prepared_fence{}, sequence{}, host_fence{};
    std::string run, generation, request, diagnostic, release_file, terminal_bytes;
    uint32_t terminal_reason{}, terminal_cleanup{};
    bool inhibited = true, done{}, control_published{}, stop_failed{};
    int64_t dispatch_ns{};
    explicit Instance(std::unique_ptr<camera::Device> d)
        : camera(std::move(d)), graph(camera->config.profile, camera->cameras), config(configuration()),
          host(link) {
        sim::Injection f;
        if (auto fault = std::getenv("MANTIS_X1_F2_FAULT")) {
            std::string_view v = fault;
            if (v == "configure")
                f.reject_message = 0x10;
            else if (v == "arm")
                f.reject_message = 0x11;
            else if (v == "lost-ack") {
                f.drop_message = 0x13;
                f.drops = 1;
            } else if (v == "lost-event")
                f.lose_terminal = true;
            else if (v == "blocked")
                f.block_next = true;
            else if (v == "stop")
                f.fail_stop = true;
            else if (v == "cleanup")
                f.cleanup_error = true;
            else if (v == "lease") {
                f.disable_heartbeat = true;
                config.pulses = 100;
            } else if (v == "link")
                f.fail_link = true;
            else if (v == "reboot")
                f.reboot_after_message = 0x13;
            else if (v == "recorder" || v == "hold") {
                auto release = std::getenv("MANTIS_X1_F2_RELEASE_FILE");
                if (!release || !*release)
                    throw std::invalid_argument("Explicit fixture release file required");
                release_file = release;
            } else if (!v.empty() && v != "prepare")
                throw std::invalid_argument("Unknown F2 fixture fault");
            diagnostic = std::string(v);
        }
        link.inject(f);
    }
    struct Call {
        Instance &s;
        explicit Call(Instance &i) : s(i) {
            std::lock_guard lock(s.control);
            ++s.active;
        }
        ~Call() {
            std::lock_guard lock(s.control);
            --s.active;
            s.wake.notify_all();
        }
    };
    void fence_work(uint32_t why) {
        ++fence;
        inhibited = true;
        if (!reason)
            reason = why;
        wake.notify_all();
    }
    MantisRuntimeTimestampV1 time(int64_t ns, const char *clock_generation) const {
        auto v = view<MantisRuntimeTimestampV1>();
        v.time = ns;
        v.clock = view<MantisClockIdentityV1>();
        v.clock.domain = view<MantisClockDomainV1>();
        v.clock.domain.id = "linux.monotonic";
        v.clock.domain.name = "runtime";
        v.clock.generation = clock_generation;
        return v;
    }
};
bool same(const char *v, const std::string &s) {
    return v && v == s;
}
bool accepts(const Instance &s, const MantisAcquisitionProgramV1 *p) {
    if (!mantis::sdk::compatible_table(p) || !p->identity.id || !p->type.name ||
        std::strcmp(p->type.name, MANTIS_ACQUISITION_PROGRAM) || p->type.version != 1 ||
        p->repetitions != 1 || p->steps_count != 1 || !p->steps ||
        p->terminal_policy != MANTIS_TERMINAL_INHIBIT_AND_ALL_OFF)
        return false;
    auto &part = p->participants;
    if (part.cameras_count || part.cameras || part.emitters_count != 1 || !part.emitters ||
        !same(part.emitters[0], s.graph.ids[3]) || part.controllers_count != 1 || !part.controllers ||
        !same(part.controllers[0], s.graph.ids[4]))
        return false;
    auto &v = p->steps[0];
    auto &b = p->bounds;
    if (!mantis::sdk::compatible_table(&v) || v.emitters_count != 1 || !v.emitters ||
        !same(v.emitters[0].emitter, s.graph.ids[3]) || v.emitters[0].state > MANTIS_EMITTER_STATE_ON ||
        v.settle || v.required_scope != MANTIS_EVIDENCE_SCOPE_CONTROLLER_REGISTER || v.max_duration <= 0 ||
        v.max_duration > 2'000'000'000 || v.capture.cameras_count || v.capture.cameras || v.capture.trigger ||
        !v.capture.mode.value || v.capture.mode.presence != MANTIS_PRESENCE_ESTABLISHED ||
        *v.capture.mode.value != MANTIS_CAPTURE_MODE_NONE || !v.evidence_requirement.value ||
        v.evidence_requirement.presence != MANTIS_PRESENCE_ESTABLISHED ||
        *v.evidence_requirement.value != MANTIS_EVIDENCE_REQUIREMENT_COMMANDED_ONLY)
        return false;
    if (b.max_duration <= 0 || b.max_duration > 2'000'000'000 || b.max_on_duration <= 0 ||
        b.max_on_duration > 2'000'000'000 || b.max_step_instances != 1 || b.max_commands < 2 ||
        b.max_commands > command_limit || b.max_events < 1 || b.max_events > command_limit ||
        b.max_bytes < 65536 || b.max_bytes > byte_limit || b.max_in_flight_captures != 1)
        return false;
    auto duration = s.config.duration_us() * 1'000;
    return v.emitters[0].state == MANTIS_EMITTER_STATE_OFF ||
           (duration <= uint64_t(v.max_duration) && duration <= uint64_t(b.max_duration) &&
            duration <= uint64_t(b.max_on_duration));
}
int enumerate(uint32_t t, MantisProjectedGraphEmitV1 emit, void *ctx) {
    if (!emit || t > call_limit)
        return MANTIS_PL_INVALID;
    if (!enabled())
        return MANTIS_PL_OK;
    configuration();
    sim::Link link;
    sim::Host host(link);
    host.discover(Clock::now() + std::chrono::milliseconds(t));
    auto config = camera::configuration();
    auto cameras = x1::assign(config.profile, config.backend->discover(config.profile));
    Graph g(config.profile, cameras);
    return emit(ctx, &g.graph);
}
int open(const MantisHostV1 *h, const char *id, uint32_t t, void **out) {
    if (!out || !enabled() || t > call_limit)
        return MANTIS_PL_INVALID;
    *out = nullptr;
    void *d{};
    auto rc = camera::open_device(h, id, &d);
    if (rc)
        return rc;
    std::unique_ptr<camera::Device> owned(static_cast<camera::Device *>(d));
    auto instance = std::make_unique<Instance>(std::move(owned));
    instance->snapshot = instance->host.discover(Clock::now() + std::chrono::milliseconds(t));
    *out = instance.release();
    return MANTIS_PL_OK;
}
int validate(void *ptr, const MantisAcquisitionProgramV1 *p, uint32_t t, MantisProgramValidationEmitV1 emit,
             void *ctx) {
    if (!ptr || !emit || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    auto v = view<MantisProgramValidationV1>();
    v.error = view<MantisContractErrorV1>();
    v.accepted = accepts(s, p) && sim::configuration_fits(s.snapshot, s.config);
    if (v.accepted)
        try {
            auto current = s.host.discover(Clock::now() + std::chrono::milliseconds(t));
            v.accepted = current.boot == s.snapshot.boot && current.generation == s.snapshot.generation &&
                         current.calibration == s.snapshot.calibration;
        } catch (...) {
            v.accepted = 0;
        }
    if (!v.accepted) {
        v.error.category = MANTIS_ERROR_ARGUMENT;
        v.error.code = 1;
    }
    v.diagnostic = v.accepted ? "F2 SimulationOnly one-step control-only; no physical evidence"
                              : "Unsupported/stale F2 simulation program";
    return emit(ctx, &v);
}
int prepare(void *ptr, const MantisAcquisitionProgramV1 *p, uint32_t t, MantisProgramValidationEmitV1 emit,
            void *ctx) {
    if (!ptr || !emit || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    uint64_t attempt;
    {
        std::lock_guard lock(s.control);
        if (s.state == MANTIS_RUN_STARTED)
            return MANTIS_PL_BUSY;
        s.state = MANTIS_RUN_OPEN;
        s.fence_work(0);
        attempt = s.fence;
    }
    auto v = view<MantisProgramValidationV1>();
    v.error = view<MantisContractErrorV1>();
    v.accepted = accepts(s, p) && sim::configuration_fits(s.snapshot, s.config) && s.diagnostic != "prepare";
    if (v.accepted)
        try {
            auto current = s.host.discover(Clock::now() + std::chrono::milliseconds(t));
            v.accepted = current.boot == s.snapshot.boot && current.generation == s.snapshot.generation &&
                         current.calibration == s.snapshot.calibration;
        } catch (...) {
            v.accepted = 0;
        }
    if (v.accepted) {
        s.reference.copy(p->identity);
        std::lock_guard lock(s.control);
        if (s.fence != attempt)
            v.accepted = 0;
        else {
            s.prepared_fence = attempt;
            s.step = p->steps[0].index;
            s.intent = p->steps[0].emitters[0].state;
            s.state = MANTIS_RUN_PREPARED;
        }
    }
    if (!v.accepted) {
        v.error.category = MANTIS_ERROR_ARGUMENT;
        v.error.code = 1;
    }
    v.diagnostic = v.accepted ? "F2 snapshot reserved without CLAIM/CONFIGURE/ARM"
                              : "F2 prepare rejected; no startable state";
    auto rc = emit(ctx, &v);
    if (rc) {
        std::lock_guard lock(s.control);
        s.state = MANTIS_RUN_OPEN;
    }
    return rc;
}
int start(void *ptr, const char *run, const char *generation, uint32_t t) {
    if (!ptr || !run || !*run || !generation || !*generation || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    uint64_t f, hf;
    {
        std::lock_guard lock(s.control);
        if (s.state == MANTIS_RUN_STARTED)
            return MANTIS_PL_BUSY;
        if (s.state != MANTIS_RUN_PREPARED || s.prepared_fence != s.fence)
            return MANTIS_PL_INVALID;
        s.run = run;
        s.generation = generation;
        s.request = s.run + "/f2/step-command";
        s.reason = 0;
        s.terminal_reason = s.terminal_cleanup = 0;
        s.terminal_bytes.clear();
        s.stop_failed = false;
        s.inhibited = false;
        s.done = s.control_published = false;
        s.sequence = 0;
        s.state = MANTIS_RUN_STARTED;
        f = s.fence;
        hf = s.host.fence();
    }
    try {
        s.host.start(s.snapshot, s.config, s.intent == MANTIS_EMITTER_STATE_ON,
                     Clock::now() + std::chrono::milliseconds(t), hf);
        std::lock_guard lock(s.control);
        if (s.fence != f || s.inhibited)
            return MANTIS_PL_NOT_READY;
        s.dispatch_ns = s.host.dispatched(s.intent == MANTIS_EMITTER_STATE_ON);
        s.host_fence = s.host.fence();
        return MANTIS_PL_OK;
    } catch (const std::exception &e) {
        std::lock_guard lock(s.control);
        s.state = MANTIS_RUN_FAILED;
        s.fence_work(MANTIS_ACQUISITION_REASON_DEVICE_FAILURE);
        s.diagnostic = e.what();
        return MANTIS_PL_ERROR;
    }
}
struct Publication {
    MantisAcquisitionBundleV1 bundle = view<MantisAcquisitionBundleV1>();
    MantisEmitterEvidenceV1 emitter = view<MantisEmitterEvidenceV1>();
    MantisEmitterCommandV1 command = view<MantisEmitterCommandV1>();
    MantisImplementationIdentityV1 implementation = view<MantisImplementationIdentityV1>();
    MantisStepInstanceV1 step = view<MantisStepInstanceV1>();
    MantisEvidenceKeyV1 predecessor = view<MantisEvidenceKeyV1>();
    uint32_t disposition;
    Publication(Instance &s, bool terminal) {
        bundle.type = type(MANTIS_ACQUISITION_BUNDLE);
        bundle.key = view<MantisBundleKeyV1>();
        bundle.key.run_id = s.run.c_str();
        bundle.key.sequence = s.sequence;
        bundle.published = s.time(x1::monotonic_ns(), s.generation.c_str());
        bundle.member_count = 1;
        auto &e = bundle.evidence;
        e = view<MantisAcquisitionEvidenceV1>();
        e.type = type(MANTIS_ACQUISITION_EVIDENCE);
        e.key = view<MantisEvidenceKeyV1>();
        e.key.run_id = s.run.c_str();
        e.key.ordinal = s.sequence;
        e.program = s.reference.get();
        step.run_id = s.run.c_str();
        step.step_index = s.step;
        e.step =
            terminal ? absent<MantisEvidenceStepInstanceV1>() : present<MantisEvidenceStepInstanceV1>(&step);
        if (s.sequence) {
            predecessor.run_id = s.run.c_str();
            predecessor.ordinal = s.sequence - 1;
            e.causal_predecessors = &predecessor;
            e.causal_predecessors_count = 1;
        }
        e.participants = view<MantisParticipantsV1>();
        e.participants.emitters = &s.graph.emitter;
        e.participants.emitters_count = 1;
        e.participants.controllers = &s.graph.controller;
        e.participants.controllers_count = 1;
        command.request = s.request.c_str();
        command.target = s.graph.emitter;
        command.state = s.intent;
        command.dispatched = s.time(s.dispatch_ns, s.generation.c_str());
        emitter.emitter = s.graph.emitter;
        emitter.commanded = terminal ? absent<MantisEvidenceEmitterCommandV1>(MANTIS_PRESENCE_UNKNOWN)
                                     : present<MantisEvidenceEmitterCommandV1>(&command);
        emitter.acknowledged = absent<MantisEvidenceAcknowledgementV1>();
        emitter.observed = absent<MantisEvidenceStateObservationV1>();
        e.emitters = &emitter;
        e.emitters_count = 1;
        implementation.implementation = "org.mantis.x1.f2.simulation-only";
        implementation.version = view<MantisVersionV1>();
        implementation.version.major = 1;
        implementation.build = "L7b-1 framed F2 synthetic ESP8266; rig revision 1";
        implementation.configuration = absent<MantisEvidenceContentReferenceV1>();
        e.implementations = &implementation;
        e.implementations_count = 1;
        e.frameset = absent<MantisEvidenceFrameSetKeyV1>();
        e.rig_calibration = absent<MantisEvidenceExactCalibrationReferenceV1>();
        disposition = terminal ? (s.terminal_reason ? MANTIS_ACQUISITION_DISPOSITION_FAILED
                                                    : MANTIS_ACQUISITION_DISPOSITION_COMPLETED)
                               : MANTIS_ACQUISITION_DISPOSITION_CONTROL_ONLY;
        e.disposition = present<MantisEvidenceAcquisitionDispositionV1>(&disposition);
        e.reason = terminal ? s.terminal_reason : 0;
        e.diagnostic =
            terminal && !s.terminal_bytes.empty()
                ? s.terminal_bytes.c_str()
                : "Synthetic F2 software command only; electrical/optical/exposure evidence unavailable";
    }
    int emit(MantisSemanticEmitV1 callback, void *ctx) {
        auto v = view<MantisSemanticPacketV1>();
        v.kind = MANTIS_SEMANTIC_BUNDLE;
        v.bundle = &bundle;
        return callback(ctx, &v);
    }
};
int next(void *ptr, uint32_t t, MantisSemanticEmitV1 emit, void *ctx) {
    if (!ptr || !emit || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    bool terminal;
    uint64_t f;
    {
        std::lock_guard lock(s.control);
        if (s.state != MANTIS_RUN_STARTED || s.inhibited || s.done)
            return MANTIS_PL_NOT_READY;
        terminal = s.control_published;
        f = s.fence;
    }
    if (!terminal && !s.release_file.empty()) {
        auto deadline = Clock::now() + std::chrono::milliseconds(t);
        while (!std::filesystem::exists(s.release_file)) {
            std::unique_lock lock(s.control);
            if (s.inhibited || s.fence != f)
                return MANTIS_PL_NOT_READY;
            if (Clock::now() >= deadline)
                return MANTIS_PL_NOT_READY;
            s.wake.wait_until(lock, std::min(deadline, Clock::now() + 5ms), [&] { return s.inhibited; });
        }
    }
    if (terminal && s.intent == MANTIS_EMITTER_STATE_ON)
        try {
            auto record = s.host.terminal(Clock::now() + std::chrono::milliseconds(t));
            if (!record)
                return MANTIS_PL_NOT_READY;
            auto a = s.host.association();
            {
                std::lock_guard lock(s.control);
                if (s.fence != f || s.inhibited)
                    return MANTIS_PL_NOT_READY;
                s.terminal_cleanup = static_cast<uint32_t>(f2::get(*record, 52, 2));
                if (record->bytes[28] != 1 || f2::get(*record, 29, 2) || f2::get(*record, 50, 2) ||
                    s.terminal_cleanup)
                    s.terminal_reason = s.terminal_cleanup && !f2::get(*record, 50, 2)
                                            ? MANTIS_ACQUISITION_REASON_CLEANUP_FAILURE
                                            : MANTIS_ACQUISITION_REASON_DEVICE_FAILURE;
                // Exact private controller record retained as bounded diagnostic provenance, not physical
                // evidence.
                s.terminal_bytes = "F2 v1 synthetic terminal record hex=";
                static constexpr char hex[] = "0123456789abcdef";
                for (auto byte : record->data()) {
                    s.terminal_bytes += hex[byte >> 4];
                    s.terminal_bytes += hex[byte & 15];
                }
                s.diagnostic = "F2 completed synthetic pulses=" + std::to_string(f2::get(*record, 31, 4)) +
                               " boot=" + std::to_string(a.boot) + " arm=" + std::to_string(a.arm) +
                               " execution=" + std::to_string(a.execution);
            }
        } catch (const std::exception &e) {
            std::lock_guard lock(s.control);
            if (s.fence != f || s.inhibited)
                return MANTIS_PL_NOT_READY;
            s.diagnostic = e.what();
            s.state = MANTIS_RUN_FAILED;
            s.fence_work(MANTIS_ACQUISITION_REASON_DEVICE_FAILURE);
            return MANTIS_PL_ERROR;
        }
    {
        std::lock_guard lock(s.control);
        if (s.fence != f || s.inhibited)
            return MANTIS_PL_NOT_READY;
    }
    Publication publication(s, terminal);
    auto rc = publication.emit(emit, ctx);
    if (!rc) {
        std::lock_guard lock(s.control);
        ++s.sequence;
        if (terminal)
            s.done = true;
        else
            s.control_published = true;
    }
    return rc;
}
int status(void *ptr, uint32_t t, MantisProjectedStatusEmitV1 emit, void *ctx) {
    if (!ptr || !emit || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    auto v = view<MantisProjectedStatusV1>();
    std::string run, generation;
    uint32_t yes = 1;
    {
        std::lock_guard lock(s.control);
        v.state = s.state;
        run = s.run;
        generation = s.generation;
    }
    const char *r = run.c_str(), *g = generation.c_str();
    v.run = run.empty() ? absent<MantisEvidenceRunIdV1>() : present<MantisEvidenceRunIdV1>(&r);
    v.generation = generation.empty() ? absent<MantisEvidenceGenerationIdV1>()
                                      : present<MantisEvidenceGenerationIdV1>(&g);
    v.step = absent<MantisEvidenceStepInstanceV1>();
    v.commands_available = present<MantisEvidenceUInt32V1>(&yes);
    v.evidence_available = present<MantisEvidenceUInt32V1>(&yes);
    v.error = view<MantisContractErrorV1>();
    return emit(ctx, &v);
}
int abort(void *ptr, uint32_t why, uint32_t t, MantisAbortEmitV1 emit, void *ctx) {
    if (!ptr || !emit || t > call_limit || why > MANTIS_ACQUISITION_REASON_CLEANUP_FAILURE)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    std::string run, generation;
    {
        std::lock_guard lock(s.control);
        s.fence_work(why);
        run = s.run;
        generation = s.generation;
    }
    uint32_t cleanup;
    {
        std::lock_guard lock(s.control);
        cleanup = s.terminal_cleanup;
    }
    auto deadline = Clock::now() + std::chrono::milliseconds(t);
    bool stopped = s.host.stop(deadline);
    const auto stop_error = s.host.stop_error();
    const bool retired = s.host.retire_heartbeat(deadline);
    {
        std::lock_guard lock(s.control);
        s.stop_failed |= !stopped || !retired;
    }
    auto v = view<MantisAbortOutcomeV1>();
    uint32_t yes = 1;
    const char *r = run.c_str(), *g = generation.c_str();
    v.run = run.empty() ? absent<MantisEvidenceRunIdV1>() : present<MantisEvidenceRunIdV1>(&r);
    v.fenced_generation = generation.empty() ? absent<MantisEvidenceGenerationIdV1>()
                                             : present<MantisEvidenceGenerationIdV1>(&g);
    v.inhibited = present<MantisEvidenceUInt32V1>(&yes);
    v.stale_work_fenced = present<MantisEvidenceUInt32V1>(&yes);
    v.off_requested = stopped ? present<MantisEvidenceUInt32V1>(&yes)
                              : absent<MantisEvidenceUInt32V1>(MANTIS_PRESENCE_UNKNOWN);
    std::string request = "f2-priority-stop";
    auto command = view<MantisEmitterCommandV1>();
    command.request = request.c_str();
    command.target = s.graph.emitter;
    command.state = MANTIS_EMITTER_STATE_OFF;
    command.dispatched =
        s.time(s.host.dispatched(false), generation.empty() ? "f2-unstarted" : generation.c_str());
    auto e = view<MantisEmitterEvidenceV1>();
    e.emitter = s.graph.emitter;
    e.commanded = stopped ? present<MantisEvidenceEmitterCommandV1>(&command)
                          : absent<MantisEvidenceEmitterCommandV1>(MANTIS_PRESENCE_UNKNOWN);
    e.acknowledged = absent<MantisEvidenceAcknowledgementV1>();
    e.observed = absent<MantisEvidenceStateObservationV1>();
    v.emitters = &e;
    v.emitter_count = 1;
    v.error = view<MantisContractErrorV1>();
    if (!stopped || !retired || cleanup) {
        v.error.category = MANTIS_ERROR_DEVICE;
        v.error.code = cleanup ? cleanup : static_cast<uint16_t>(!retired ? f2::Result::timeout : stop_error);
    }
    return emit(ctx, &v);
}
int stop(void *ptr, uint32_t t) {
    if (!ptr || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    {
        std::lock_guard lock(s.control);
        s.fence_work(MANTIS_ACQUISITION_REASON_USER_STOP);
        s.state = MANTIS_RUN_STOPPED;
    }
    auto deadline = Clock::now() + std::chrono::milliseconds(t);
    auto ok = s.host.stop(deadline);
    auto heartbeat_retired = s.host.retire_heartbeat(deadline);
    std::unique_lock lock(s.control);
    s.stop_failed |= !ok;
    auto retired = s.wake.wait_until(lock, deadline, [&] { return s.active <= 1; });
    return ok && heartbeat_retired && retired && !s.terminal_cleanup ? MANTIS_PL_OK : MANTIS_PL_ERROR;
}
int destroy(void *ptr, uint32_t t) {
    if (!ptr || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    auto deadline = Clock::now() + std::chrono::milliseconds(t);
    {
        std::unique_lock lock(s.control);
        if (!s.wake.wait_until(lock, deadline, [&] { return !s.active; }))
            return MANTIS_PL_BUSY;
    }
    // STOP may be uncertain, but shutdown must retire the heartbeat before deleting callback state.
    if (!s.host.shutdown(deadline))
        return MANTIS_PL_BUSY;
    delete &s;
    return MANTIS_PL_OK;
}
int diagnostics(void *ptr, uint32_t t, MantisTextEmitV1 emit, void *ctx) {
    if (!emit || t > call_limit)
        return MANTIS_PL_INVALID;
    if (!ptr)
        return emit(ctx, "X1 F2 SimulationOnly; no physical UART/GPIO/output implementation");
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    auto m = s.link.metrics();
    camera::Json j;
    {
        std::lock_guard lock(s.control);
        j["diagnostic"] = s.diagnostic;
        j["first_reason"] = s.reason;
        j["stop_uncertain"] = s.stop_failed;
    }
    j["frames"] = m.frames;
    j["malformed"] = m.malformed;
    j["run_admissions"] = m.runs;
    j["synthetic_completed_pulses"] = m.pulses;
    j["stop_requests"] = m.stops;
    j["collector_high_water"] = m.collector;
    j["reply_high_water"] = m.reply_high_water;
    j["event_high_water"] = m.event_high_water;
    j["image_copies"] = 0;
    j["physical_evidence"] = "unavailable";
    auto text = j.dump();
    return emit(ctx, text.c_str());
}
template <auto F> struct Safe;
template <class R, class... A, R (*F)(A...)> struct Safe<F> {
    static R call(A... a) noexcept {
        try {
            return F(a...);
        } catch (...) {
            return MANTIS_PL_ERROR;
        }
    }
};
const MantisProjectedLightV1 projected{sizeof(projected),      1,
                                       Safe<enumerate>::call,  Safe<open>::call,
                                       Safe<validate>::call,   Safe<prepare>::call,
                                       Safe<start>::call,      Safe<next>::call,
                                       Safe<status>::call,     Safe<abort>::call,
                                       Safe<stop>::call,       Safe<destroy>::call,
                                       Safe<diagnostics>::call};
const MantisAcquisitionV1 acquisition{sizeof(acquisition), 1,
                                      camera::enumerate,   camera::open_device,
                                      camera::destroy,     camera::start,
                                      camera::next,        camera::stop,
                                      camera::diagnostics};
int initialize(const MantisHostV1 *h) {
    return mantis::sdk::compatible(h) ? 0 : 1;
}
void shutdown() {}
const void *query(const char *id) {
    if (!id)
        return nullptr;
    if (!std::strcmp(id, MANTIS_PROJECTED_LIGHT_V1))
        return enabled() ? &projected : nullptr;
    return !std::strcmp(id, MANTIS_ACQUISITION_V1) ? &acquisition : nullptr;
}
const MantisPluginV1 plugin{sizeof(plugin), 1, "org.mantis.x1", "1.1.0", initialize, shutdown, query};
} // namespace
extern "C" MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t abi) {
    return abi == 1 ? &plugin : nullptr;
}
