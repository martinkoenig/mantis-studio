// TEST/BENCH ONLY. Linked exclusively to fake camera/media sources, never linux.cpp.
#define MANTIS_X1_PROJECTED_FIXTURE 1
#include "../../../plugins/first-party/devices/x1/acquisition.hpp"
#include <condition_variable>
#include <filesystem>
#include <mantis/sdk.hpp>

namespace {
namespace camera = x1::acquisition;
using namespace std::chrono_literals;
template <class T> T view() {
    T v{};
    v.struct_size = sizeof(v);
    v.abi_version = 1;
    return v;
}
template <class T> T absent(uint32_t p = MANTIS_PRESENCE_UNAVAILABLE) {
    auto v = view<T>();
    v.presence = p;
    return v;
}
template <class T, class V> T present(const V *p) {
    auto v = absent<T>(MANTIS_PRESENCE_ESTABLISHED);
    v.value = p;
    return v;
}
MantisDataTypeV1 type(const char *name) {
    auto v = view<MantisDataTypeV1>();
    v.name = name;
    v.version = 1;
    return v;
}
constexpr uint32_t call_limit = 1000, step_limit = 32;
constexpr uint64_t instance_limit = 128, command_limit = 256, byte_limit = 64 * 1024 * 1024;
bool enabled() {
    auto opt = std::getenv("MANTIS_X1_PROJECTED_FIXTURE");
    auto fake = std::getenv("MANTIS_X1_FAKE");
    return opt && std::string_view(opt) == "1" && fake && *fake && std::getenv("MANTIS_X1_PROFILE");
}
struct Graph {
    std::array<std::string, 6> ids;
    std::array<std::string, 2> streams, physical;
    std::string frameset;
    const char *frameset_ptr{};
    std::array<const char *, 5> participants;
    std::array<const char *, 2> emitters;
    std::array<MantisProjectedImageSourceV1, 2> images;
    std::array<MantisProjectedComponentV1, 6> components;
    MantisProjectedGraphV1 graph = view<MantisProjectedGraphV1>();
    Graph(const x1::Profile &profile, const std::array<x1::CameraInfo, 2> &c) {
        ids[0] = camera::parent_id(c);
        ids[1] = ids[0] + "/left";
        ids[2] = ids[0] + "/right";
        ids[3] = ids[0] + "/fixture/L1";
        ids[4] = ids[0] + "/fixture/L7";
        ids[5] = ids[0] + "/fixture/controller";
        frameset = ids[0] + "/framesets";
        frameset_ptr = frameset.c_str();
        static const char *parent_caps[]{MANTIS_FRAMESET_STREAM_V1, MANTIS_PROJECTED_LIGHT_ACQUISITION_V1};
        static const char *image_caps[]{MANTIS_IMAGE_STREAM_V1};
        static const char *power_caps[]{MANTIS_EMITTER_POWER_CONTROL_V1};
        static const uint32_t states[]{MANTIS_EMITTER_STATE_OFF, MANTIS_EMITTER_STATE_ON};
        static const uint32_t modes[]{MANTIS_CAPTURE_MODE_FREE_RUNNING};
        static const uint32_t methods[]{MANTIS_EVIDENCE_METHOD_SOFTWARE_DISPATCH};
        static const uint32_t scopes[]{MANTIS_EVIDENCE_SCOPE_CONTROLLER_REGISTER};
        for (size_t i = 0; i < ids.size(); ++i) {
            auto &v = components[i];
            v = view<MantisProjectedComponentV1>();
            v.id = ids[i].c_str();
            v.parent_id = i ? ids[0].c_str() : "";
            v.name = i == 0  ? "X1 TEST/BENCH fixture"
                     : i < 3 ? "Fixture measurement camera"
                             : "Synthetic fixture control";
            v.role = i == 1 ? "left" : i == 2 ? "right" : i == 3 ? "L1" : i == 4 ? "L7" : "fixture";
            v.participant_kind = i == 0  ? MANTIS_PARTICIPANT_PARENT
                                 : i < 3 ? MANTIS_PARTICIPANT_IMAGE
                                 : i < 5 ? MANTIS_PARTICIPANT_EMITTER
                                         : MANTIS_PARTICIPANT_CONTROLLER;
            v.pattern = absent<MantisEvidencePatternIdV1>();
            v.pattern_revision = absent<MantisEvidenceUInt64V1>();
            if (i)
                participants[i - 1] = ids[i].c_str();
        }
        auto &root = components[0];
        root.capabilities = parent_caps;
        root.capability_count = 2;
        root.participants = participants.data();
        root.participant_count = 5;
        for (size_t i = 0; i < 2; ++i) {
            streams[i] = ids[i + 1]; // stable selected camera stream; generations are runtime facts
            physical[i] = camera::identity(c[i]);
            images[i] = view<MantisProjectedImageSourceV1>();
            images[i].stream_id = streams[i].c_str();
            images[i].physical_identity = physical[i].c_str();
            images[i].width = profile.mode.width;
            images[i].height = profile.mode.height;
            auto &v = components[i + 1];
            v.image_source = &images[i];
            v.capabilities = image_caps;
            v.capability_count = 1;
            v.capture_modes = modes;
            v.capture_mode_count = 1;
            auto &e = components[i + 3];
            e.capabilities = power_caps;
            e.capability_count = 1;
            e.emitter_states = states;
            e.emitter_state_count = 2;
            e.evidence_methods = methods;
            e.evidence_method_count = 1;
            e.evidence_scopes = scopes;
            e.evidence_scope_count = 1;
            emitters[i] = ids[i + 3].c_str();
        }
        auto &control = components[5];
        control.capabilities = power_caps;
        control.capability_count = 1;
        control.controls = emitters.data();
        control.control_count = 2;
        control.evidence_methods = methods;
        control.evidence_method_count = 1;
        control.evidence_scopes = scopes;
        control.evidence_scope_count = 1;
        graph.parent_id = ids[0].c_str();
        graph.components = components.data();
        graph.component_count = 6;
        graph.frameset_stream = present<MantisEvidenceStreamIdV1>(&frameset_ptr);
        auto &l = graph.limits;
        l = view<MantisProjectedLimitsV1>();
        l.max_components = 6;
        l.max_cameras = 2;
        l.max_steps = step_limit;
        l.max_bundle_members = 2;
        l.max_step_instances = instance_limit;
        l.max_commands = command_limit;
        l.max_events = command_limit;
        l.max_bytes = byte_limit;
        l.max_in_flight_captures = 1;
        l.max_pending_bundles = 1;
        l.max_call_timeout_ms = call_limit;
        l.max_run_duration_ns = 30'000'000'000;
        l.max_on_duration_ns = 5'000'000'000;
        l.max_step_duration_ns = 2'000'000'000;
        l.watchdog = absent<MantisEvidenceUInt32V1>();
        l.interlock = absent<MantisEvidenceUInt32V1>();
        l.fail_off = absent<MantisEvidenceUInt32V1>();
    }
};
// Borrowed reference metadata is owned exactly, including all presence values.
struct Reference {
    MantisProgramReferenceV1 ref{};
    MantisHashV1 hash{}, content_hash{};
    MantisContentReferenceV1 content{};
    std::string id, algorithm, hex, content_id, content_type, content_algorithm, content_hex;
    void copy(const MantisProgramReferenceV1 &v) {
        ref = v;
        id = v.id;
        if (v.hash.value) {
            hash = *v.hash.value;
            algorithm = hash.algorithm;
            hex = hash.hex;
        }
        if (v.content.value) {
            content = *v.content.value;
            content_id = content.id;
            content_type = content.type.name;
            if (content.hash.value) {
                content_hash = *content.hash.value;
                content_algorithm = content_hash.algorithm;
                content_hex = content_hash.hex;
            }
        }
        ref.id = nullptr;
        ref.hash.value = nullptr;
        ref.content.value = nullptr;
        hash.algorithm = hash.hex = nullptr;
        content.id = content.type.name = nullptr;
        content.hash.value = nullptr;
        content_hash.algorithm = content_hash.hex = nullptr;
    }
    MantisProgramReferenceV1 get() {
        auto v = ref;
        v.id = id.c_str();
        if (v.hash.presence == MANTIS_PRESENCE_ESTABLISHED) {
            hash.algorithm = algorithm.c_str();
            hash.hex = hex.c_str();
            v.hash.value = &hash;
        }
        if (v.content.presence == MANTIS_PRESENCE_ESTABLISHED) {
            content.id = content_id.c_str();
            content.type.name = content_type.c_str();
            if (content.hash.presence == MANTIS_PRESENCE_ESTABLISHED) {
                content_hash.algorithm = content_algorithm.c_str();
                content_hash.hex = content_hex.c_str();
                content.hash.value = &content_hash;
            }
            v.content.value = &content;
        }
        return v;
    }
};
struct Step {
    uint32_t index{}, mode{};
    std::array<uint32_t, 2> states{};
};
struct Instance {
    std::unique_ptr<camera::Device> camera;
    Graph graph;
    Reference reference;
    std::vector<Step> steps;
    std::array<size_t, 2> camera_order{};
    std::array<const char *, 2> participant_emitters{};
    uint64_t repetitions{}, cursor{}, sequence{}, fence_version{}, prepared_fence{};
    std::string fault, run, generation, camera_generation, release_file;
    std::mutex control;
    std::condition_variable wake;
    uint32_t active{}, state = MANTIS_RUN_OPEN, first_reason = MANTIS_ACQUISITION_REASON_NONE;
    bool inhibited = true, dispatched{}, completed{}, close_refused{};
    bool camera_started{};
    std::array<uint32_t, 2> logical{};
    std::array<std::string, 2> requests;
    MantisRuntimeTimestampV1 dispatch_time{};
    explicit Instance(std::unique_ptr<camera::Device> d)
        : camera(std::move(d)), graph(camera->config.profile, camera->cameras) {
        if (auto f = std::getenv("MANTIS_X1_PROJECTED_FAULT"))
            fault = f;
        if (auto f = std::getenv("MANTIS_X1_PROJECTED_RELEASE_FILE"))
            release_file = f;
        if (fault == "delay" && release_file.empty())
            throw std::runtime_error("Fixture delay requires explicit release file");
    }
    struct Call {
        Instance &s;
        explicit Call(Instance &v) : s(v) {
            std::lock_guard lock(s.control);
            ++s.active;
        }
        ~Call() {
            std::lock_guard lock(s.control);
            --s.active;
            s.wake.notify_all();
        }
    };
    MantisRuntimeTimestampV1 time(int64_t n = x1::monotonic_ns()) const {
        auto v = view<MantisRuntimeTimestampV1>();
        v.time = n;
        v.clock = view<MantisClockIdentityV1>();
        v.clock.domain = view<MantisClockDomainV1>();
        v.clock.domain.id = "linux.monotonic";
        v.clock.domain.name = "runtime";
        v.clock.generation = generation.c_str();
        return v;
    }
    void fence(uint32_t reason) {
        // Never called under a camera/recorder/publication mutex.
        ++fence_version;
        inhibited = true;
        logical.fill(MANTIS_EMITTER_STATE_OFF);
        if (first_reason == MANTIS_ACQUISITION_REASON_NONE)
            first_reason = reason;
        wake.notify_all();
    }
};
bool same(const char *a, const std::string &b) {
    return a && a == b;
}
bool accepts(const Instance &s, const MantisAcquisitionProgramV1 *p) {
    if (!mantis::sdk::compatible_table(p) || !p->steps || !p->steps_count || p->steps_count > step_limit ||
        !p->repetitions || p->repetitions > instance_limit / p->steps_count || !p->identity.id ||
        !p->type.name || std::strcmp(p->type.name, MANTIS_ACQUISITION_PROGRAM) || p->type.version != 1 ||
        p->terminal_policy != MANTIS_TERMINAL_INHIBIT_AND_ALL_OFF)
        return false;
    const auto &part = p->participants;
    if (part.cameras_count != 2 || !part.cameras || part.emitters_count != 2 || !part.emitters ||
        part.controllers_count != 1 || !part.controllers || !same(part.controllers[0], s.graph.ids[5]))
        return false;
    std::array<bool, 2> seen{}, seen_emitters{};
    for (size_t i = 0; i < 2; ++i) {
        auto c = same(part.cameras[i].component, s.graph.ids[1])   ? 0u
                 : same(part.cameras[i].component, s.graph.ids[2]) ? 1u
                                                                   : 2u;
        if (c == 2 || seen[c] || !same(part.cameras[i].stream, s.graph.streams[c]) ||
            !same(part.cameras[i].role, c ? "right" : "left"))
            return false;
        seen[c] = true;
        auto e = same(part.emitters[i], s.graph.ids[3])   ? 0u
                 : same(part.emitters[i], s.graph.ids[4]) ? 1u
                                                          : 2u;
        if (e == 2 || seen_emitters[e])
            return false;
        seen_emitters[e] = true;
    }
    const auto &b = p->bounds;
    if (b.max_duration <= 0 || b.max_duration > 30'000'000'000 || b.max_on_duration <= 0 ||
        b.max_on_duration > 5'000'000'000 || b.max_step_instances > instance_limit ||
        b.max_commands > command_limit || b.max_events > command_limit || b.max_bytes > byte_limit ||
        b.max_in_flight_captures != 1)
        return false;
    uint64_t captures{};
    for (uint32_t i = 0; i < p->steps_count; ++i) {
        const auto &v = p->steps[i];
        if (!mantis::sdk::compatible_table(&v) || v.emitters_count != 2 || !v.emitters ||
            !v.capture.mode.value || v.capture.mode.presence != MANTIS_PRESENCE_ESTABLISHED ||
            !v.evidence_requirement.value || v.evidence_requirement.presence != MANTIS_PRESENCE_ESTABLISHED ||
            *v.evidence_requirement.value != MANTIS_EVIDENCE_REQUIREMENT_COMMANDED_ONLY || v.settle != 0 ||
            v.max_duration <= 0 || v.max_duration > 2'000'000'000 || v.capture.trigger)
            return false;
        std::array<bool, 2> emitters{};
        for (size_t j = 0; j < 2; ++j) {
            auto e = same(v.emitters[j].emitter, s.graph.ids[3])   ? 0u
                     : same(v.emitters[j].emitter, s.graph.ids[4]) ? 1u
                                                                   : 2u;
            if (e == 2 || emitters[e] || v.emitters[j].state > MANTIS_EMITTER_STATE_ON)
                return false;
            emitters[e] = true;
        }
        auto mode = *v.capture.mode.value;
        if (mode == MANTIS_CAPTURE_MODE_NONE) {
            if (v.capture.cameras_count || v.capture.cameras)
                return false;
        } else if (mode == MANTIS_CAPTURE_MODE_FREE_RUNNING) {
            if (v.capture.cameras_count != 2 || !v.capture.cameras ||
                !((same(v.capture.cameras[0], s.graph.ids[1]) &&
                   same(v.capture.cameras[1], s.graph.ids[2])) ||
                  (same(v.capture.cameras[1], s.graph.ids[1]) && same(v.capture.cameras[0], s.graph.ids[2]))))
                return false;
            ++captures;
        } else
            return false;
    }
    // Conservative encoded-size reservation, including bounded semantic metadata.
    auto pixels = uint64_t(s.camera->config.profile.mode.width) * s.camera->config.profile.mode.height *
                  (s.camera->config.profile.mode.fourcc == "Y10P" ? 5u : 4u) / 4u * 2u;
    return b.max_step_instances >= p->repetitions * p->steps_count &&
           b.max_commands >= 2 * p->repetitions * p->steps_count &&
           b.max_bytes >= p->repetitions * (captures * pixels + (p->steps_count + 1) * 65536ull);
}
int enumerate(uint32_t t, MantisProjectedGraphEmitV1 emit, void *ctx) {
    if (t > call_limit || !emit)
        return MANTIS_PL_INVALID;
    if (!enabled())
        return MANTIS_PL_OK;
    auto config = camera::configuration();
    auto c = x1::assign(config.profile, config.backend->discover(config.profile));
    Graph g(config.profile, c);
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
    *out = new Instance(std::move(owned));
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
    v.accepted = accepts(s, p);
    if (!v.accepted) {
        v.error.category = MANTIS_ERROR_ARGUMENT;
        v.error.code = 1;
    }
    v.diagnostic = v.accepted ? "X1 synthetic free-running fixture; physical evidence unavailable"
                              : "Unsupported X1 fixture selection/mode/bounds/settle";
    return emit(ctx, &v);
}
int prepare(void *ptr, const MantisAcquisitionProgramV1 *p, uint32_t t, MantisProgramValidationEmitV1 emit,
            void *ctx) {
    if (!ptr || !emit || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    uint64_t attempt_fence{};
    {
        std::lock_guard lock(s.control);
        if (s.state == MANTIS_RUN_STARTED)
            return MANTIS_PL_BUSY;
        s.state = MANTIS_RUN_OPEN;
        s.steps.clear();
        s.fence(MANTIS_ACQUISITION_REASON_NONE);
        attempt_fence = s.fence_version;
    }
    auto v = view<MantisProgramValidationV1>();
    v.error = view<MantisContractErrorV1>();
    v.accepted = accepts(s, p) && s.fault != "prepare";
    if (!v.accepted) {
        v.error.category = MANTIS_ERROR_ARGUMENT;
        v.error.code = 1;
    }
    if (v.accepted) {
        s.reference.copy(p->identity);
        for (size_t i = 0; i < 2; ++i) {
            s.camera_order[i] = same(p->participants.cameras[i].component, s.graph.ids[1]) ? 0 : 1;
            s.participant_emitters[i] =
                s.graph.emitters[same(p->participants.emitters[i], s.graph.ids[3]) ? 0 : 1];
        }
        s.repetitions = p->repetitions;
        for (uint32_t i = 0; i < p->steps_count; ++i) {
            Step step;
            step.index = p->steps[i].index;
            step.mode = *p->steps[i].capture.mode.value;
            for (const auto &intent : std::span(p->steps[i].emitters, 2))
                step.states[same(intent.emitter, s.graph.ids[3]) ? 0 : 1] = intent.state;
            s.steps.push_back(step);
        }
        std::lock_guard lock(s.control);
        if (s.fence_version != attempt_fence) {
            v.accepted = 0;
            v.error.category = MANTIS_ERROR_RESOURCE;
            v.error.code = 2;
        } else {
            s.prepared_fence = attempt_fence;
            s.state = MANTIS_RUN_PREPARED;
        }
    }
    v.diagnostic = v.accepted ? "prepared synthetic fixture" : "fixture prepare rejected";
    auto rc = emit(ctx, &v);
    if (rc) {
        std::lock_guard lock(s.control);
        s.state = MANTIS_RUN_OPEN;
    }
    return rc;
}
int start(void *ptr, const char *r, const char *g, uint32_t t) {
    if (!ptr || !r || !*r || !g || !*g || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    {
        std::lock_guard lock(s.control);
        if (s.state == MANTIS_RUN_STARTED)
            return MANTIS_PL_BUSY;
        if (s.state != MANTIS_RUN_PREPARED || s.prepared_fence != s.fence_version)
            return MANTIS_PL_INVALID;
        s.run = r;
        s.generation = g;
        s.camera_generation = s.generation + "/camera";
        s.cursor = s.sequence = 0;
        s.dispatched = s.completed = false;
        s.inhibited = false;
        s.first_reason = MANTIS_ACQUISITION_REASON_NONE;
        s.state = MANTIS_RUN_STARTED;
        s.camera_started = false;
    }
    auto rc = s.fault == "start"   ? MANTIS_PL_ERROR
              : s.fault == "delay" ? MANTIS_PL_OK
                                   : camera::start(s.camera.get());
    s.camera_started = rc == MANTIS_PL_OK && s.fault != "delay";
    {
        std::lock_guard lock(s.control);
        if (rc) {
            s.state = MANTIS_RUN_FAILED;
            s.fence(MANTIS_ACQUISITION_REASON_DEVICE_FAILURE);
        }
    }
    return static_cast<int>(rc);
}
struct DataView {
    MantisDataPacketV1 packet = view<MantisDataPacketV1>();
    camera::Json metadata;
    std::vector<std::string> keys, values;
    std::vector<MantisMetadataEntryV1> entries;
    explicit DataView(const MantisObservationV1 &v) {
        packet.type = type(!std::strcmp(v.packet.type_id, MANTIS_FRAMESET) ? MANTIS_FRAMESET : MANTIS_IMAGE);
        auto &h = packet.header;
        h = view<MantisPacketHeaderV1>();
        h.sequence = v.packet.sequence;
        h.timestamp = view<MantisDeviceTimestampV1>();
        h.timestamp.nanoseconds = v.packet.device_time_ns;
        h.timestamp.domain = view<MantisClockDomainV1>();
        h.timestamp.domain.id = v.packet.clock_id;
        h.timestamp.domain.name = "plugin clock";
        h.received = v.host_receive_ns;
        h.sync = view<MantisSyncGroupV1>();
        h.sync.id = v.sync_group;
        h.sync.trigger = v.sync_trigger;
        h.sync_quality = v.sync_quality;
        h.calibration = view<MantisCalibrationReferenceV1>();
        h.calibration.id = v.packet.calibration_id;
        h.calibration.schema_version = 1;
        h.calibration.revision = v.packet.calibration_revision;
        h.frame = view<MantisCoordinateFrameV1>();
        h.frame.id = v.packet.coordinate_frame;
        h.frame.name = "plugin frame";
        metadata = camera::Json::parse(v.metadata_json);
        if (metadata.size() > 256)
            throw std::runtime_error("X1 metadata bound exceeded");
        for (auto it = metadata.begin(); it != metadata.end(); ++it) {
            keys.push_back(it.key());
            values.push_back(it.value().get<std::string>());
        }
        for (size_t i = 0; i < keys.size(); ++i) {
            auto e = view<MantisMetadataEntryV1>();
            e.key = keys[i].c_str();
            e.value = values[i].c_str();
            entries.push_back(e);
        }
        h.metadata = view<MantisMetadataV1>();
        h.metadata.items = entries.empty() ? nullptr : entries.data();
        h.metadata.count = static_cast<uint32_t>(entries.size());
        packet.attributes = v.packet.attributes;
        packet.attribute_count = v.packet.attribute_count;
    }
};
struct Publication {
    Instance &s;
    Step step;
    uint64_t ordinal{}, repetition{};
    bool terminal{};
    std::array<MantisCameraParticipantV1, 2> cameras;
    const char *controller;
    std::array<MantisEmitterCommandV1, 2> commands;
    std::array<MantisEmitterEvidenceV1, 2> emitters;
    std::array<MantisCameraFrameEvidenceV1, 2> sources;
    std::array<MantisSemanticTimestampV1, 2> device_times;
    std::array<MantisRuntimeTimestampV1, 2> received_times;
    std::array<MantisSyncEvidenceV1, 2> sync;
    std::array<MantisExactCalibrationReferenceV1, 2> calibration;
    std::array<std::array<MantisCameraEffectiveStateV1, 2>, 2> effective;
    MantisFrameSetKeyV1 frameset_key = view<MantisFrameSetKeyV1>();
    MantisStepInstanceV1 instance = view<MantisStepInstanceV1>();
    MantisImplementationIdentityV1 implementation = view<MantisImplementationIdentityV1>();
    MantisEvidenceKeyV1 predecessor = view<MantisEvidenceKeyV1>();
    MantisAcquisitionBundleV1 bundle = view<MantisAcquisitionBundleV1>();
    Publication(Instance &v, Step st, uint64_t n, uint64_t rep, bool done)
        : s(v), step(st), ordinal(n), repetition(rep), terminal(done), controller(s.graph.ids[5].c_str()) {
        bundle.type = type(MANTIS_ACQUISITION_BUNDLE);
        bundle.key = view<MantisBundleKeyV1>();
        bundle.key.run_id = s.run.c_str();
        bundle.key.sequence = n;
        bundle.published = s.time();
        auto &e = bundle.evidence;
        e = view<MantisAcquisitionEvidenceV1>();
        e.type = type(MANTIS_ACQUISITION_EVIDENCE);
        e.key = view<MantisEvidenceKeyV1>();
        e.key.run_id = s.run.c_str();
        e.key.ordinal = n;
        e.program = s.reference.get();
        instance.run_id = s.run.c_str();
        instance.repetition_index = rep;
        instance.step_index = step.index;
        e.step =
            done ? absent<MantisEvidenceStepInstanceV1>() : present<MantisEvidenceStepInstanceV1>(&instance);
        if (n) {
            predecessor.run_id = s.run.c_str();
            predecessor.ordinal = n - 1;
            e.causal_predecessors = &predecessor;
            e.causal_predecessors_count = 1;
        }
        e.participants = view<MantisParticipantsV1>();
        for (size_t i = 0; i < 2; ++i) {
            cameras[i] = view<MantisCameraParticipantV1>();
            auto source_index = s.camera_order[i];
            cameras[i].component = s.graph.ids[source_index + 1].c_str();
            cameras[i].stream = s.graph.streams[source_index].c_str();
            cameras[i].role = source_index ? "right" : "left";
            commands[i] = view<MantisEmitterCommandV1>();
            commands[i].request = s.requests[i].c_str();
            commands[i].target = s.graph.emitters[i];
            commands[i].state = step.states[i];
            commands[i].dispatched = s.dispatch_time;
            emitters[i] = view<MantisEmitterEvidenceV1>();
            emitters[i].emitter = s.graph.emitters[i];
            emitters[i].commanded = done ? absent<MantisEvidenceEmitterCommandV1>(MANTIS_PRESENCE_UNKNOWN)
                                         : present<MantisEvidenceEmitterCommandV1>(&commands[i]);
            emitters[i].acknowledged = absent<MantisEvidenceAcknowledgementV1>();
            emitters[i].observed = absent<MantisEvidenceStateObservationV1>();
        }
        e.participants.cameras = cameras.data();
        e.participants.cameras_count = 2;
        e.participants.emitters = s.participant_emitters.data();
        e.participants.emitters_count = 2;
        e.participants.controllers = &controller;
        e.participants.controllers_count = 1;
        implementation.implementation = "org.mantis.x1.projected-fixture";
        implementation.version = view<MantisVersionV1>();
        implementation.version.major = 1;
        implementation.build = "L7a TEST/BENCH synthetic controller and cameras";
        implementation.configuration = absent<MantisEvidenceContentReferenceV1>();
        e.implementations = &implementation;
        e.implementations_count = 1;
        e.emitters = emitters.data();
        e.emitters_count = 2;
        e.frameset = absent<MantisEvidenceFrameSetKeyV1>();
        e.rig_calibration = absent<MantisEvidenceExactCalibrationReferenceV1>();
        static const uint32_t control_only = MANTIS_ACQUISITION_DISPOSITION_CONTROL_ONLY,
                              completed = MANTIS_ACQUISITION_DISPOSITION_COMPLETED;
        e.disposition = present<MantisEvidenceAcquisitionDispositionV1>(done ? &completed : &control_only);
        e.diagnostic =
            "Synthetic software dispatch only; optical state/exposure/trigger evidence unavailable";
        bundle.member_count = 1;
    }
    int emit(MantisSemanticEmitV1 callback, void *ctx, const MantisFrameSetV1 *fs = nullptr) {
        std::optional<DataView> parent;
        std::array<std::optional<DataView>, 2> images;
        std::array<MantisDataPacketV1, 2> image_packets;
        auto &e = bundle.evidence;
        if (fs) {
            if (fs->frame_count != 2)
                return MANTIS_PL_INCOMPATIBLE;
            parent.emplace(fs->observation);
            for (size_t i = 0; i < 2; ++i) {
                images[i].emplace(fs->frames[i]);
                image_packets[i] = images[i]->packet;
                const auto &h = image_packets[i].header;
                auto &v = sources[i];
                v = view<MantisCameraFrameEvidenceV1>();
                v.frame = view<MantisSourceFrameKeyV1>();
                v.frame.camera = s.graph.ids[i + 1].c_str();
                v.frame.stream = view<MantisStreamIdentityV1>();
                v.frame.stream.id = s.graph.streams[i].c_str();
                v.frame.stream.generation = s.camera_generation.c_str();
                v.frame.native_sequence = h.sequence;
                v.camera_role = i ? "right" : "left";
                v.width = s.camera->config.profile.mode.width;
                v.height = s.camera->config.profile.mode.height;
                device_times[i] = view<MantisSemanticTimestampV1>();
                device_times[i].nanoseconds = h.timestamp.nanoseconds;
                device_times[i].clock = view<MantisClockIdentityV1>();
                device_times[i].clock.domain = h.timestamp.domain;
                device_times[i].clock.generation = s.camera_generation.c_str();
                v.source_timestamp = present<MantisEvidenceSemanticTimestampV1>(&device_times[i]);
                // Fake camera received timestamps use its synthetic native clock, not host monotonic.
                received_times[i] = s.time(h.received);
                received_times[i].clock = device_times[i].clock;
                v.host_received = present<MantisEvidenceRuntimeTimestampV1>(&received_times[i]);
                v.timestamp_meaning = absent<MantisEvidenceTimestampMeaningV1>(MANTIS_PRESENCE_UNKNOWN);
                v.exposure = absent<MantisEvidenceExposureEvidenceV1>();
                sync[i] = view<MantisSyncEvidenceV1>();
                sync[i].group = h.sync;
                sync[i].quality = h.sync_quality;
                sync[i].hardware_association = absent<MantisEvidenceExposureAssociationV1>();
                v.sync = present<MantisEvidenceSyncEvidenceV1>(&sync[i]);
                v.camera_calibration = absent<MantisEvidenceExactCalibrationReferenceV1>();
                v.rig_calibration = absent<MantisEvidenceExactCalibrationReferenceV1>();
                v.original_calibration = absent<MantisEvidenceExactCalibrationReferenceV1>();
                if (*h.calibration.id) {
                    calibration[i] = view<MantisExactCalibrationReferenceV1>();
                    calibration[i].calibration = h.calibration;
                    calibration[i].content = absent<MantisEvidenceContentReferenceV1>();
                    v.original_calibration =
                        present<MantisEvidenceExactCalibrationReferenceV1>(&calibration[i]);
                }
                for (size_t j = 0; j < 2; ++j) {
                    effective[j][i] = view<MantisCameraEffectiveStateV1>();
                    effective[j][i].frame = v.frame;
                    effective[j][i].state = absent<MantisEvidenceExposureEffectiveStateV1>();
                }
            }
            parent->packet.frames = image_packets.data();
            parent->packet.frame_count = 2;
            bundle.frameset = &parent->packet;
            bundle.member_count = 2;
            frameset_key.run_id = s.run.c_str();
            frameset_key.stream = view<MantisStreamIdentityV1>();
            frameset_key.stream.id = s.graph.frameset.c_str();
            frameset_key.stream.generation = s.camera_generation.c_str();
            frameset_key.sequence = fs->observation.packet.sequence;
            e.frameset = present<MantisEvidenceFrameSetKeyV1>(&frameset_key);
            e.frames = sources.data();
            e.frames_count = 2;
            for (size_t j = 0; j < 2; ++j) {
                emitters[j].exposure_effective = effective[j].data();
                emitters[j].exposure_effective_count = 2;
            }
            static const uint32_t captured = MANTIS_ACQUISITION_DISPOSITION_CAPTURED;
            e.disposition = present<MantisEvidenceAcquisitionDispositionV1>(&captured);
        }
        {
            std::lock_guard lock(s.control);
            if (s.inhibited)
                return MANTIS_PL_NOT_READY;
        }
        // An entered synchronous callback may finish after abort; no post-fence command can enter.
        auto packet = view<MantisSemanticPacketV1>();
        packet.kind = MANTIS_SEMANTIC_BUNDLE;
        packet.bundle = &bundle;
        return callback(ctx, &packet);
    }
};
int next(void *ptr, uint32_t t, MantisSemanticEmitV1 emit, void *ctx) {
    if (!ptr || !emit || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(t);
    Step step{};
    uint64_t n{}, rep{};
    bool terminal{};
    {
        std::unique_lock lock(s.control);
        if (s.state != MANTIS_RUN_STARTED || s.inhibited || s.completed)
            return MANTIS_PL_NOT_READY;
        if (s.fault == "block") {
            s.wake.wait_until(lock, deadline, [&] { return s.inhibited; });
            return MANTIS_PL_NOT_READY;
        }
        if (!s.camera_started) {
            // Fault injection only: test releases the first capture after installing
            // a real filesystem failure. Never check storage under the abort mutex.
            lock.unlock();
            const bool released = std::filesystem::exists(s.release_file);
            lock.lock();
            if (s.inhibited)
                return MANTIS_PL_NOT_READY;
            if (!released) {
                s.wake.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + 5ms),
                                  [&] { return s.inhibited; });
                return MANTIS_PL_NOT_READY;
            }
            lock.unlock();
            auto rc = camera::start(s.camera.get());
            lock.lock();
            if (rc) {
                s.state = MANTIS_RUN_FAILED;
                s.fence(MANTIS_ACQUISITION_REASON_DEVICE_FAILURE);
                return rc;
            }
            s.camera_started = true;
            if (s.inhibited)
                return MANTIS_PL_NOT_READY;
        }
        terminal = s.cursor == s.steps.size() * s.repetitions;
        if (!terminal) {
            step = s.steps[s.cursor % s.steps.size()];
            rep = s.cursor / s.steps.size();
        }
        n = s.sequence;
        if (!terminal && !s.dispatched) {
            s.logical = step.states;
            s.dispatch_time = s.time();
            for (size_t i = 0; i < 2; ++i)
                s.requests[i] = s.run + "/fixture/" + std::to_string(s.cursor) + "/" + std::to_string(i);
            s.dispatched = true;
        }
    }
    Publication publication(s, step, n, rep, terminal);
    int rc{};
    if (!terminal && step.mode == MANTIS_CAPTURE_MODE_FREE_RUNNING) {
        struct Context {
            Publication &p;
            MantisSemanticEmitV1 emit;
            void *ctx;
            bool emitted{};
            int result{};
        } context{publication, emit, ctx};
        auto callback = [](void *v, const MantisFrameSetV1 *fs) noexcept {
            auto &c = *static_cast<Context *>(v);
            auto code = mantis::sdk::boundary([&] {
                c.result = c.p.emit(c.emit, c.ctx, fs);
                c.emitted = c.result == MANTIS_PL_OK;
            });
            return code ? code : c.result;
        };
        do {
            {
                std::lock_guard lock(s.control);
                if (s.inhibited)
                    return MANTIS_PL_NOT_READY;
            }
            // Reuse exactly the camera next/pairing path; short waits bound abort responsiveness.
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 deadline - std::chrono::steady_clock::now())
                                 .count();
            auto slice = static_cast<uint32_t>(std::clamp<int64_t>(remaining, 0, 2));
            rc = camera::next(s.camera.get(), slice, callback, &context);
            if (context.emitted)
                break;
            {
                std::lock_guard lock(s.control);
                if (s.inhibited)
                    return MANTIS_PL_NOT_READY;
            }
            if (rc != MANTIS_PL_NOT_READY) {
                std::lock_guard lock(s.control);
                s.state = MANTIS_RUN_FAILED;
                s.fence(MANTIS_ACQUISITION_REASON_DEVICE_FAILURE);
                return rc ? rc : MANTIS_PL_ERROR;
            }
            if (std::chrono::steady_clock::now() >= deadline)
                return MANTIS_PL_NOT_READY;
            // Finite interruptible readiness yield, no busy polling and no bridge queue.
            std::unique_lock lock(s.control);
            s.wake.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + 1ms),
                              [&] { return s.inhibited; });
        } while (true);
        rc = context.result;
    } else
        rc = publication.emit(emit, ctx);
    if (rc)
        return rc;
    {
        std::lock_guard lock(s.control);
        ++s.sequence;
        if (terminal)
            s.completed = true;
        else {
            ++s.cursor;
            s.dispatched = false;
        }
    }
    return MANTIS_PL_OK;
}
int status(void *ptr, uint32_t t, MantisProjectedStatusEmitV1 emit, void *ctx) {
    if (!ptr || !emit || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    auto v = view<MantisProjectedStatusV1>();
    std::string run, gen;
    uint32_t yes = 1;
    {
        std::lock_guard lock(s.control);
        v.state = s.state;
        run = s.run;
        gen = s.generation;
    }
    const char *r = run.c_str(), *g = gen.c_str();
    v.run = run.empty() ? absent<MantisEvidenceRunIdV1>() : present<MantisEvidenceRunIdV1>(&r);
    v.generation =
        gen.empty() ? absent<MantisEvidenceGenerationIdV1>() : present<MantisEvidenceGenerationIdV1>(&g);
    v.step = absent<MantisEvidenceStepInstanceV1>();
    v.commands_available = present<MantisEvidenceUInt32V1>(&yes);
    v.evidence_available = present<MantisEvidenceUInt32V1>(&yes);
    v.error = view<MantisContractErrorV1>();
    return emit(ctx, &v);
}
int abort(void *ptr, uint32_t reason, uint32_t t, MantisAbortEmitV1 emit, void *ctx) {
    if (!ptr || !emit || t > call_limit || reason > MANTIS_ACQUISITION_REASON_CLEANUP_FAILURE)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    std::string run, gen;
    MantisRuntimeTimestampV1 dispatched;
    {
        std::lock_guard lock(s.control);
        s.fence(reason);
        run = s.run;
        gen = s.generation;
        dispatched = s.time();
    }
    dispatched.clock.generation = gen.empty() ? "fixture-unstarted" : gen.c_str();
    auto v = view<MantisAbortOutcomeV1>();
    const char *r = run.c_str(), *g = gen.c_str();
    uint32_t yes = 1;
    v.run = run.empty() ? absent<MantisEvidenceRunIdV1>() : present<MantisEvidenceRunIdV1>(&r);
    v.fenced_generation =
        gen.empty() ? absent<MantisEvidenceGenerationIdV1>() : present<MantisEvidenceGenerationIdV1>(&g);
    v.inhibited = present<MantisEvidenceUInt32V1>(&yes);
    v.stale_work_fenced = present<MantisEvidenceUInt32V1>(&yes);
    v.off_requested = present<MantisEvidenceUInt32V1>(&yes);
    std::array<MantisEmitterEvidenceV1, 2> emitters;
    std::array<MantisEmitterCommandV1, 2> commands;
    std::array<std::string, 2> requests;
    for (size_t i = 0; i < 2; ++i) {
        requests[i] = "fixture-abort-off-" + std::to_string(i);
        commands[i] = view<MantisEmitterCommandV1>();
        commands[i].request = requests[i].c_str();
        commands[i].target = s.graph.emitters[i];
        commands[i].state = MANTIS_EMITTER_STATE_OFF;
        commands[i].dispatched = dispatched;
        emitters[i] = view<MantisEmitterEvidenceV1>();
        emitters[i].emitter = s.graph.emitters[i];
        emitters[i].commanded = present<MantisEvidenceEmitterCommandV1>(&commands[i]);
        emitters[i].acknowledged = absent<MantisEvidenceAcknowledgementV1>();
        emitters[i].observed = absent<MantisEvidenceStateObservationV1>();
    }
    v.emitters = emitters.data();
    v.emitter_count = 2;
    v.error = view<MantisContractErrorV1>();
    if (s.fault == "cleanup") {
        v.error.category = MANTIS_ERROR_CLEANUP;
        v.error.code = 1;
    }
    return emit(ctx, &v);
}
int stop(void *ptr, uint32_t t) {
    if (!ptr || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    {
        std::unique_lock lock(s.control);
        s.fence(MANTIS_ACQUISITION_REASON_NONE);
        if (!s.wake.wait_for(lock, std::chrono::milliseconds(t), [&] { return s.active == 0; }))
            return MANTIS_PL_BUSY;
        s.state = MANTIS_RUN_STOPPED;
    }
    return camera::stop(s.camera.get());
}
int destroy(void *ptr, uint32_t t) {
    if (!ptr || t > call_limit)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    auto rc = stop(ptr, t);
    if (rc)
        return rc;
    if (s.fault == "close" && !s.close_refused) {
        s.close_refused = true;
        return MANTIS_PL_BUSY;
    }
    delete &s;
    return MANTIS_PL_OK;
}
int diagnostics(void *ptr, uint32_t t, MantisTextEmitV1 emit, void *ctx) {
    if (!emit || t > call_limit)
        return MANTIS_PL_INVALID;
    if (!ptr)
        return emit(ctx, "X1 TEST/BENCH ONLY; explicit fake backend required; no physical controller");
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call call(s);
    auto json = camera::Json::object();
    json["acquisition_copy_frames"] = std::to_string(s.camera->copied_frames);
    json["acquisition_copy_bytes"] = std::to_string(s.camera->copied_bytes);
    for (const char *key : {"copy_count", "pending_high_water_left", "pending_high_water_right",
                            "pairing_mode", "max_v4l2_delta_ns", "error"}) {
        auto found = s.camera->metrics.find(key);
        if (found != s.camera->metrics.end())
            json[key] = found->second;
    }
    {
        std::lock_guard lock(s.control);
        json["fixture"] = "synthetic";
        json["first_abort_reason"] = std::to_string(s.first_reason);
        json["projected_pixel_copies"] = "0";
        json["physical_evidence"] = "unavailable";
    }
    auto text = json.dump();
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
const MantisPluginV1 plugin{sizeof(plugin), 1, "org.mantis.x1", "1.0.0", initialize, shutdown, query};
} // namespace
extern "C" MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t abi) {
    return abi == 1 ? &plugin : nullptr;
}
