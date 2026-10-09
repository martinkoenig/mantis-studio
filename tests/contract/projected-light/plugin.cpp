// Dedicated contract DSO. Public SDK only; no runtime/domain headers or hardware.
#include "fixture.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mantis/sdk.hpp>
#include <mutex>
#include <string>
#include <thread>

namespace {
using mantis::sdk::boundary;
std::atomic_uint fault{}, pending_calls{}, destroyed_instances{}, live_instances{}, initializations{},
    shutdowns{};
std::atomic_uint selected_shape{UINT32_MAX}, selected_publication{UINT32_MAX};
std::mutex ownership;
bool owned{};
std::string image_role{"imaging"}, image_identity{"physical-camera-alpha"}, source_calibration_id;
uint32_t image_width = 2, image_height = 2;
uint64_t source_calibration_revision{};
template <class T> T view() {
    T out{};
    out.struct_size = sizeof(T);
    out.abi_version = MANTIS_ABI_V1;
    return out;
}
template <class T> T absent(uint32_t presence = MANTIS_PRESENCE_UNKNOWN) {
    auto out = view<T>();
    out.presence = presence;
    return out;
}
template <class T, class V> T present(const V *v) {
    auto out = absent<T>(MANTIS_PRESENCE_ESTABLISHED);
    out.value = v;
    return out;
}
MantisDataTypeV1 type(const char *name) {
    auto out = view<MantisDataTypeV1>();
    out.name = name;
    out.version = 1;
    return out;
}
MantisStreamIdentityV1 stream() {
    auto out = view<MantisStreamIdentityV1>();
    out.id = "image-stream";
    out.generation = "camera-generation";
    return out;
}
MantisRuntimeTimestampV1 host_time() {
    auto out = view<MantisRuntimeTimestampV1>();
    out.time = 0;
    out.clock = view<MantisClockIdentityV1>();
    out.clock.domain = view<MantisClockDomainV1>();
    out.clock.domain.id = "host-monotonic";
    out.clock.domain.name = "host";
    out.clock.generation = "host-generation";
    return out;
}
MantisSourceFrameKeyV1 source_frame() {
    auto out = view<MantisSourceFrameKeyV1>();
    out.camera = "camera-alpha";
    out.stream = stream();
    out.native_sequence = 7;
    return out;
}
MantisEvidenceSourceV1 source(uint32_t method = MANTIS_EVIDENCE_METHOD_SOFTWARE_DISPATCH) {
    auto out = view<MantisEvidenceSourceV1>();
    out.source = "controller-alpha";
    out.method = method;
    out.reference = absent<MantisEvidenceContentReferenceV1>(MANTIS_PRESENCE_UNAVAILABLE);
    return out;
}
MantisCameraFrameEvidenceV1 camera_evidence() {
    auto out = view<MantisCameraFrameEvidenceV1>();
    out.frame = source_frame();
    out.camera_role = image_role.c_str();
    out.width = image_width;
    out.height = image_height;
    out.source_timestamp = absent<MantisEvidenceSemanticTimestampV1>();
    out.host_received = absent<MantisEvidenceRuntimeTimestampV1>(MANTIS_PRESENCE_UNAVAILABLE);
    out.timestamp_meaning = absent<MantisEvidenceTimestampMeaningV1>();
    out.exposure = absent<MantisEvidenceExposureEvidenceV1>(MANTIS_PRESENCE_UNAVAILABLE);
    out.sync = absent<MantisEvidenceSyncEvidenceV1>();
    out.camera_calibration = absent<MantisEvidenceExactCalibrationReferenceV1>(MANTIS_PRESENCE_UNAVAILABLE);
    out.rig_calibration = absent<MantisEvidenceExactCalibrationReferenceV1>();
    out.original_calibration = absent<MantisEvidenceExactCalibrationReferenceV1>(MANTIS_PRESENCE_UNAVAILABLE);
    return out;
}
MantisEmitterEvidenceV1 emitter_evidence() {
    auto out = view<MantisEmitterEvidenceV1>();
    out.emitter = "emitter-alpha";
    out.commanded = absent<MantisEvidenceEmitterCommandV1>();
    out.acknowledged = absent<MantisEvidenceAcknowledgementV1>(MANTIS_PRESENCE_UNAVAILABLE);
    out.observed = absent<MantisEvidenceStateObservationV1>(MANTIS_PRESENCE_UNAVAILABLE);
    return out;
}
MantisPacketHeaderV1 packet_header(uint64_t sequence) {
    auto out = view<MantisPacketHeaderV1>();
    out.sequence = sequence;
    out.timestamp = view<MantisDeviceTimestampV1>();
    out.timestamp.domain = view<MantisClockDomainV1>();
    out.timestamp.domain.id = "camera-clock";
    out.timestamp.domain.name = "camera";
    out.sync = view<MantisSyncGroupV1>();
    out.sync.id = "";
    out.calibration = view<MantisCalibrationReferenceV1>();
    out.calibration.id = source_calibration_id.c_str();
    out.calibration.schema_version = 1;
    out.calibration.revision = source_calibration_revision;
    out.frame = view<MantisCoordinateFrameV1>();
    out.frame.id = "camera-optical";
    out.frame.name = "optical";
    out.metadata = view<MantisMetadataV1>();
    return out;
}
struct Graph {
    std::array<MantisProjectedComponentV1, 5> components;
    MantisProjectedImageSourceV1 image_source = view<MantisProjectedImageSourceV1>();
    const char *frameset_stream = "frameset-stream";
    MantisProjectedGraphV1 graph = view<MantisProjectedGraphV1>();
    Graph() {
        static const char *parent_caps[] = {MANTIS_PROJECTED_LIGHT_ACQUISITION_V1, MANTIS_FRAMESET_STREAM_V1};
        static const char *camera_caps[] = {MANTIS_IMAGE_STREAM_V1, MANTIS_HARDWARE_TRIGGER_V1};
        static const char *emitter_caps[] = {MANTIS_EMITTER_POWER_CONTROL_V1};
        static const char *controller_caps[] = {MANTIS_HARDWARE_TRIGGER_V1, MANTIS_EMITTER_POWER_CONTROL_V1};
        static const char *participants[] = {"camera-alpha", "emitter-alpha", "controller-alpha"};
        static const char *controls[] = {"emitter-alpha"};
        static const char *endpoints[] = {"camera-alpha"};
        static const uint32_t states[] = {MANTIS_EMITTER_STATE_OFF, MANTIS_EMITTER_STATE_ON};
        static const uint32_t captures[] = {MANTIS_CAPTURE_MODE_FREE_RUNNING,
                                            MANTIS_CAPTURE_MODE_HARDWARE_TRIGGER};
        static const uint32_t triggers[] = {MANTIS_CAPTURE_MODE_HARDWARE_TRIGGER};
        static const uint32_t methods[] = {MANTIS_EVIDENCE_METHOD_SOFTWARE_DISPATCH};
        static const uint32_t scopes[] = {MANTIS_EVIDENCE_SCOPE_CONTROLLER_REGISTER};
        const char *ids[] = {"parent-alpha", "camera-alpha", "emitter-alpha", "controller-alpha"};
        for (size_t i = 0; i < 4; ++i) {
            auto &c = components[i];
            c = view<MantisProjectedComponentV1>();
            c.id = ids[i];
            c.parent_id = i ? ids[0] : "";
            c.name = ids[i];
            c.role = i == 1 ? image_role.c_str() : "presentation";
            c.participant_kind = static_cast<uint32_t>(i);
            c.pattern = absent<MantisEvidencePatternIdV1>(MANTIS_PRESENCE_UNAVAILABLE);
            c.pattern_revision = absent<MantisEvidenceUInt64V1>(MANTIS_PRESENCE_UNAVAILABLE);
            c.evidence_methods = methods;
            c.evidence_method_count = 1;
            c.evidence_scopes = scopes;
            c.evidence_scope_count = 1;
        }
        auto &p = components[0];
        p.capabilities = parent_caps;
        p.capability_count = 2;
        p.participants = participants;
        p.participant_count = 3;
        auto &c = components[1];
        c.capabilities = camera_caps;
        c.capability_count = 2;
        c.capture_modes = captures;
        c.capture_mode_count = 2;
        image_source.stream_id = "image-stream";
        image_source.physical_identity = image_identity.c_str();
        image_source.width = image_width;
        image_source.height = image_height;
        c.image_source = &image_source;
        auto &e = components[2];
        e.capabilities = emitter_caps;
        e.capability_count = 1;
        e.emitter_states = states;
        e.emitter_state_count = 2;
        auto &t = components[3];
        t.capabilities = controller_caps;
        t.capability_count = 2;
        t.controls = controls;
        t.control_count = 1;
        t.trigger_endpoints = endpoints;
        t.trigger_endpoint_count = 1;
        t.trigger_modes = triggers;
        t.trigger_mode_count = 1;
        graph.parent_id = "parent-alpha";
        graph.components = components.data();
        graph.component_count = 4;
        graph.frameset_stream = present<MantisEvidenceStreamIdV1>(&frameset_stream);
        auto &l = graph.limits;
        l = view<MantisProjectedLimitsV1>();
        l.max_components = 4;
        l.max_steps = 256;
        l.max_bundle_members = 64;
        l.max_cameras = 1;
        l.max_step_instances = 1000000;
        l.max_commands = 1000000;
        l.max_events = 1000000;
        l.max_bytes = 134217728;
        l.max_in_flight_captures = 1;
        l.max_run_duration_ns = 10000000000;
        l.max_on_duration_ns = 1000000000;
        l.max_step_duration_ns = 1000000000;
        l.max_pending_bundles = 1;
        l.max_call_timeout_ms = 1000;
        l.watchdog = absent<MantisEvidenceUInt32V1>(MANTIS_PRESENCE_UNAVAILABLE);
        l.interlock = absent<MantisEvidenceUInt32V1>();
        l.fail_off = absent<MantisEvidenceUInt32V1>(MANTIS_PRESENCE_UNAVAILABLE);
    }
};
// Own only semantic metadata needed to echo the prepared reference in this fixture.
struct Reference {
    std::string algorithm, hex, content_id, content_type, content_algorithm, content_hex;
    MantisProgramReferenceV1 reference{};
    MantisHashV1 hash = view<MantisHashV1>(), content_hash = view<MantisHashV1>();
    MantisContentReferenceV1 content = view<MantisContentReferenceV1>();
    void copy(const MantisProgramReferenceV1 &p) {
        reference = p;
        if (p.hash.value) {
            algorithm = p.hash.value->algorithm;
            hex = p.hash.value->hex;
        }
        if (p.content.value) {
            content = *p.content.value;
            content_id = content.id;
            content_type = content.type.name;
            if (content.hash.value) {
                content_algorithm = content.hash.value->algorithm;
                content_hex = content.hash.value->hex;
            }
        }
        // Never keep even unused borrowed pointers after the prepare callback.
        reference.id = nullptr;
        reference.hash.value = nullptr;
        reference.content.value = nullptr;
        content.id = nullptr;
        content.type.name = nullptr;
        content.hash.value = nullptr;
    }
    MantisProgramReferenceV1 get(const char *id) {
        auto out = reference;
        out.id = id;
        if (out.hash.presence == MANTIS_PRESENCE_ESTABLISHED) {
            hash.algorithm = algorithm.c_str();
            hash.hex = hex.c_str();
            out.hash.value = &hash;
        }
        if (out.content.presence == MANTIS_PRESENCE_ESTABLISHED) {
            content.id = content_id.c_str();
            content.type.name = content_type.c_str();
            if (content.hash.presence == MANTIS_PRESENCE_ESTABLISHED) {
                content_hash.algorithm = content_algorithm.c_str();
                content_hash.hex = content_hex.c_str();
                content.hash.value = &content_hash;
            }
            out.content.value = &content;
        }
        return out;
    }
};
std::atomic_uint opens{}, validations{}, prepares{}, starts{}, aborts{};
void (*prepare_probe)(void *){};
void *prepare_context{};
struct Instance {
    const MantisHostV1 *host;
    std::mutex mutex;
    std::condition_variable wake;
    bool prepared{}, started{}, inhibited{}, two_emitters{};
    uint32_t active{}, publication{};
    std::string run, generation, program = "program-alpha";
    Reference reference;
    uint32_t step_index{};
    uint64_t repetitions{};
    explicit Instance(const MantisHostV1 *h) : host(h), two_emitters(fault == TEST_TWO_EMITTERS) {}
    struct Call {
        Instance &instance;
        explicit Call(Instance &s) : instance(s) {
            std::lock_guard lock(s.mutex);
            ++s.active;
        }
        ~Call() {
            std::lock_guard lock(instance.mutex);
            --instance.active;
            instance.wake.notify_all();
        }
    };
};
int enumerate(uint32_t t, MantisProjectedGraphEmitV1 emit, void *ctx) {
    if (t > MANTIS_MAX_TIMEOUT_MS || !emit)
        return MANTIS_PL_INVALID;
    int rc = MANTIS_PL_ERROR;
    auto code = boundary([&] {
        Graph g;
        switch (fault.load()) {
        case TEST_GRAPH_NULL:
            g.graph.components = nullptr;
            break;
        case TEST_GRAPH_SIZE:
            g.components[0].struct_size = 0;
            break;
        case TEST_GRAPH_VERSION:
            g.graph.abi_version = 2;
            break;
        case TEST_GRAPH_COUNT:
            g.graph.component_count = 65;
            break;
        case TEST_GRAPH_DUPLICATE:
            g.components[2].id = g.components[1].id;
            break;
        case TEST_GRAPH_PARENT:
            g.components[2].parent_id = "missing-parent";
            break;
        case TEST_GRAPH_RELATION:
            g.components[1].controls = g.components[3].controls;
            g.components[1].control_count = 1;
            break;
        case TEST_GRAPH_PRESENCE:
            g.graph.limits.watchdog.presence = 99;
            break;
        case TEST_IMAGE_NULL:
            g.components[1].image_source = nullptr;
            break;
        case TEST_IMAGE_STREAM:
            g.image_source.stream_id = "";
            break;
        case TEST_IMAGE_IDENTITY:
            g.image_source.physical_identity = "";
            break;
        case TEST_IMAGE_WIDTH:
            g.image_source.width = 0;
            break;
        case TEST_IMAGE_HEIGHT:
            g.image_source.height = 0;
            break;
        case TEST_IMAGE_PREFIX:
            g.image_source.abi_version = 2;
            break;
        case TEST_FRAMESET_STREAM:
            g.graph.frameset_stream = absent<MantisEvidenceStreamIdV1>();
            break;
        case TEST_GRAPH_NO_FRAMESET:
            g.components[0].capability_count = 1;
            g.graph.frameset_stream = absent<MantisEvidenceStreamIdV1>(MANTIS_PRESENCE_UNAVAILABLE);
            break;
        case TEST_TWO_EMITTERS: {
            static const char *participants[] = {"camera-alpha", "emitter-alpha", "controller-alpha",
                                                 "emitter-beta"};
            static const char *controls[] = {"emitter-alpha", "emitter-beta"};
            g.components[4] = g.components[2];
            g.components[4].id = "emitter-beta";
            g.components[0].participants = participants;
            g.components[0].participant_count = 4;
            g.components[3].controls = controls;
            g.components[3].control_count = 2;
            g.graph.component_count = g.graph.limits.max_components = 5;
            break;
        }
        default:
            break;
        }
        rc = emit(ctx, &g.graph);
    });
    return code ? code : rc;
}
int open(const MantisHostV1 *host, const char *parent, uint32_t t, void **out) {
    if (!mantis::sdk::compatible(host) || !parent || std::strcmp(parent, "parent-alpha") ||
        t > MANTIS_MAX_TIMEOUT_MS || !out)
        return MANTIS_PL_INVALID;
    ++opens;
    std::lock_guard lock(ownership);
    *out = nullptr;
    if (owned)
        return MANTIS_PL_BUSY;
    auto rc = boundary([&] { *out = new Instance(host); });
    if (!rc) {
        owned = true;
        ++live_instances;
    }
    return rc;
}
int validate(void *ptr, const MantisAcquisitionProgramV1 *p, uint32_t t, MantisProgramValidationEmitV1 emit,
             void *ctx) {
    if (!ptr || t > MANTIS_MAX_TIMEOUT_MS || !emit)
        return MANTIS_PL_INVALID;
    ++validations;
    if (fault == TEST_SERVICE_VALIDATE_FAILURE)
        return MANTIS_PL_ERROR;
    Instance::Call active(*static_cast<Instance *>(ptr));
    auto v = view<MantisProgramValidationV1>();
    v.error = view<MantisContractErrorV1>();
    v.accepted = mantis::sdk::compatible_table(p) && p->type.name &&
                 !std::strcmp(p->type.name, MANTIS_ACQUISITION_PROGRAM) && p->type.version == 1 &&
                 p->identity.id && p->steps_count == 1 && p->steps && p->repetitions > 0 &&
                 p->terminal_policy == MANTIS_TERMINAL_INHIBIT_AND_ALL_OFF && fault != TEST_REJECT_PROGRAM;
    if (!v.accepted) {
        v.error.category = MANTIS_ERROR_ARGUMENT;
        v.error.code = 1;
    }
    v.diagnostic = v.accepted ? "accepted deterministic fixture program" : "unsupported fixture program";
    int rc = MANTIS_PL_ERROR;
    auto code = boundary([&] { rc = emit(ctx, &v); });
    return code ? code : rc;
}
int prepare(void *ptr, const MantisAcquisitionProgramV1 *p, uint32_t t, MantisProgramValidationEmitV1 emit,
            void *ctx) {
    if (!ptr || !emit || t > MANTIS_MAX_TIMEOUT_MS)
        return MANTIS_PL_INVALID;
    Instance::Call active(*static_cast<Instance *>(ptr));
    ++prepares;
    if (prepare_probe)
        prepare_probe(prepare_context);
    if (fault == TEST_FAILURE)
        return MANTIS_PL_ERROR;
    struct Validation {
        MantisProgramValidationEmitV1 emit;
        void *context;
        bool accepted{};
    } validation{emit, ctx};
    auto capture = [](void *context, const MantisProgramValidationV1 *result) {
        auto &state = *static_cast<Validation *>(context);
        state.accepted = result->accepted != 0;
        return state.emit(state.context, result);
    };
    int rc = validate(ptr, p, t, capture, &validation);
    if (!rc && validation.accepted) {
        auto &s = *static_cast<Instance *>(ptr);
        rc = boundary([&] {
            std::lock_guard lock(s.mutex);
            s.prepared = true;
            s.program = p->identity.id;
            s.reference.copy(p->identity);
            s.step_index = p->steps[0].index;
            s.repetitions = p->repetitions;
        });
    }
    return rc;
}
int start(void *ptr, const char *run, const char *generation, uint32_t t) {
    if (!ptr || !run || !*run || !generation || !*generation || t > MANTIS_MAX_TIMEOUT_MS)
        return MANTIS_PL_INVALID;
    ++starts;
    if (fault == TEST_SERVICE_START_FAILURE)
        return MANTIS_PL_ERROR;
    auto &s = *static_cast<Instance *>(ptr);
    int rc = MANTIS_PL_OK;
    auto code = boundary([&] {
        std::lock_guard lock(s.mutex);
        if (!s.prepared || s.started) {
            rc = MANTIS_PL_INVALID;
            return;
        }
        s.run = run;
        s.generation = generation;
        s.inhibited = false;
        s.started = true;
        s.publication = 0;
    });
    return code ? code : rc;
}
struct Bundle {
    mantis::sdk::Buffer pixels;
    MantisAcquisitionBundleV1 bundle = view<MantisAcquisitionBundleV1>();
    MantisSemanticPacketV1 semantic = view<MantisSemanticPacketV1>();
    MantisEmitterCommandV1 command = view<MantisEmitterCommandV1>();
    MantisExactCalibrationReferenceV1 source_calibration = view<MantisExactCalibrationReferenceV1>();
    MantisContentReferenceV1 source_content = view<MantisContentReferenceV1>();
    MantisHashV1 source_hash = view<MantisHashV1>();
    MantisCameraParticipantV1 camera = view<MantisCameraParticipantV1>();
    MantisEmitterEvidenceV1 emitter = emitter_evidence();
    MantisImplementationIdentityV1 implementation = view<MantisImplementationIdentityV1>();
    MantisCameraFrameEvidenceV1 frame = camera_evidence();
    MantisCameraEffectiveStateV1 effective = view<MantisCameraEffectiveStateV1>();
    MantisFrameSetKeyV1 frameset_key = view<MantisFrameSetKeyV1>();
    MantisStepInstanceV1 step = view<MantisStepInstanceV1>();
    MantisTriggerEventV1 trigger = view<MantisTriggerEventV1>();
    MantisRuntimeTimestampV1 dispatched = host_time();
    MantisDataPacketV1 frameset = view<MantisDataPacketV1>(), image = view<MantisDataPacketV1>();
    MantisAttributeV1 attribute = view<MantisAttributeV1>();
    uint32_t disposition = MANTIS_ACQUISITION_DISPOSITION_STARTUP;
    uint32_t trigger_kind = MANTIS_TRIGGER_KIND_REQUESTED;
    const char *emitters[1] = {"emitter-alpha"}, *controllers[1] = {"controller-alpha"},
               *endpoints[1] = {"camera-alpha"};
    Bundle(Instance &s, uint32_t publication, uint32_t shape)
        : pixels(s.host, size_t(image_width) * image_height) {
        std::fill(pixels.writable().begin(), pixels.writable().end(), std::byte{42});
        pixels.publish();
        auto &b = bundle;
        b.type = type(MANTIS_ACQUISITION_BUNDLE);
        b.key = view<MantisBundleKeyV1>();
        b.key.run_id = s.run.c_str();
        b.key.sequence = publication;
        b.published = host_time();
        b.published.time = 100 + (fault == TEST_SERVICE_CAPTURE ? 500000000LL * publication : publication);
        auto &e = b.evidence;
        e = view<MantisAcquisitionEvidenceV1>();
        e.type = type(MANTIS_ACQUISITION_EVIDENCE);
        e.key = view<MantisEvidenceKeyV1>();
        e.key.run_id = s.run.c_str();
        e.key.ordinal = publication;
        e.program = s.reference.get(s.program.c_str());
        e.step = absent<MantisEvidenceStepInstanceV1>();
        e.frameset = absent<MantisEvidenceFrameSetKeyV1>(MANTIS_PRESENCE_UNAVAILABLE);
        e.rig_calibration = absent<MantisEvidenceExactCalibrationReferenceV1>();
        e.participants = view<MantisParticipantsV1>();
        camera.component = "camera-alpha";
        camera.stream = "image-stream";
        camera.role = image_role.c_str();
        e.participants.cameras = &camera;
        e.participants.cameras_count = 1;
        e.participants.emitters = emitters;
        e.participants.emitters_count = 1;
        e.participants.controllers = controllers;
        e.participants.controllers_count = 1;
        implementation.implementation = "org.example.contract-fixture";
        implementation.version = view<MantisVersionV1>();
        implementation.version.major = 1;
        implementation.build = "test-only";
        implementation.configuration = absent<MantisEvidenceContentReferenceV1>(MANTIS_PRESENCE_UNAVAILABLE);
        e.implementations = &implementation;
        e.implementations_count = 1;
        e.emitters = &emitter;
        e.emitters_count = 1;
        e.disposition = present<MantisEvidenceAcquisitionDispositionV1>(&disposition);
        e.diagnostic = "fixture";
        step.step_index = s.step_index;
        if (shape == 1) {
            frameset_key.run_id = s.run.c_str();
            frameset_key.stream = stream();
            frameset_key.stream.id = "frameset-stream";
            frameset_key.sequence = 9;
            e.frameset = present<MantisEvidenceFrameSetKeyV1>(&frameset_key);
            e.frames = &frame;
            e.frames_count = 1;
            step.run_id = s.run.c_str();
            e.step = present<MantisEvidenceStepInstanceV1>(&step);
            disposition = MANTIS_ACQUISITION_DISPOSITION_CAPTURED;
            effective.frame = source_frame();
            effective.state = absent<MantisEvidenceExposureEffectiveStateV1>();
            emitter.exposure_effective = &effective;
            emitter.exposure_effective_count = 1;
            image.type = type(MANTIS_IMAGE);
            image.header = packet_header(7);
            attribute.name = "org.mantis.pixels";
            attribute.unit = "intensity";
            attribute.scalar_type = 1;
            attribute.rank = 2;
            attribute.shape[0] = image_height;
            attribute.shape[1] = image_width;
            attribute.stride[0] = image_width;
            attribute.stride[1] = 1;
            attribute.buffer = pixels.get();
            attribute.bytes = size_t(image_width) * image_height;
            image.attributes = &attribute;
            image.attribute_count = 1;
            frameset.type = type(MANTIS_FRAMESET);
            frameset.header = packet_header(9);
            frameset.frames = &image;
            frameset.frame_count = 1;
            b.frameset = &frameset;
            if (fault == TEST_SERVICE_CALIBRATION_MISMATCH)
                ++image.header.calibration.revision;
            if (fault == TEST_SERVICE_CALIBRATION_CHANGE && publication == 1) {
                ++image.header.calibration.revision;
                ++frameset.header.calibration.revision;
                frameset_key.sequence += publication;
                frame.frame.native_sequence += publication;
                effective.frame = frame.frame;
                image.header.sequence += publication;
                frameset.header.sequence += publication;
            }
            if (fault == TEST_SERVICE_CALIBRATION_EXACT) {
                source_calibration.calibration = frameset.header.calibration;
                source_content.id = "source-rig-artifact";
                source_content.type = type("org.mantis.RigCalibration");
                source_content.revision = source_calibration_revision;
                source_hash.algorithm = "sha256";
                source_hash.hex = "abcd";
                source_content.hash = present<MantisEvidenceHashV1>(&source_hash);
                source_calibration.content = present<MantisEvidenceContentReferenceV1>(&source_content);
                e.rig_calibration = present<MantisEvidenceExactCalibrationReferenceV1>(&source_calibration);
                frame.rig_calibration = e.rig_calibration;
            }
        }
        if (shape == 2) {
            trigger.type = type(MANTIS_TRIGGER_EVENT);
            trigger.key = view<MantisTriggerKeyV1>();
            trigger.key.run_id = s.run.c_str();
            trigger.key.source = "controller-alpha";
            trigger.key.controller_generation = s.generation.c_str();
            trigger.key.sequence = publication;
            trigger.step = view<MantisStepInstanceV1>();
            trigger.step.run_id = s.run.c_str();
            trigger.step.step_index = s.step_index;
            trigger.request = "trigger-request";
            trigger.kind = present<MantisEvidenceTriggerKindV1>(&trigger_kind);
            trigger.native_trigger =
                absent<MantisEvidenceNativeTriggerIdentityV1>(MANTIS_PRESENCE_UNAVAILABLE);
            trigger.acknowledgement = absent<MantisEvidenceAcknowledgementV1>(MANTIS_PRESENCE_UNAVAILABLE);
            trigger.evidence = source();
            trigger.device_time = absent<MantisEvidenceSemanticTimestampV1>();
            trigger.host_received = absent<MantisEvidenceRuntimeTimestampV1>();
            trigger.host_dispatched = present<MantisEvidenceRuntimeTimestampV1>(&dispatched);
            trigger.uncertainty_ns = absent<MantisEvidenceFloat64V1>();
            trigger.clock_mapping = absent<MantisEvidenceClockMappingEvidenceV1>();
            trigger.actual_endpoints = absent<MantisEvidenceComponentListV1>(MANTIS_PRESENCE_UNAVAILABLE);
            trigger.requested_exposure = absent<MantisEvidenceSourceFrameKeyV1>();
            trigger.exposure_association = absent<MantisEvidenceExposureAssociationV1>();
            trigger.intended_endpoints = endpoints;
            trigger.intended_endpoints_count = 1;
            b.triggers = &trigger;
            b.trigger_count = 1;
            e.triggers = &trigger.key;
            e.triggers_count = 1;
        }
        if (fault >= TEST_SERVICE_COMPLETE) {
            disposition = publication == 0 ? (shape == 1 ? MANTIS_ACQUISITION_DISPOSITION_CAPTURED
                                                         : MANTIS_ACQUISITION_DISPOSITION_CONTROL_ONLY)
                                           : MANTIS_ACQUISITION_DISPOSITION_COMPLETED;
            step.run_id = s.run.c_str();
            e.step = present<MantisEvidenceStepInstanceV1>(&step);
            if (publication == 0) {
                command.request = "service-off";
                command.target = "emitter-alpha";
                command.state = MANTIS_EMITTER_STATE_OFF;
                command.dispatched = host_time();
                emitter.commanded = present<MantisEvidenceEmitterCommandV1>(&command);
            }
        }
        if (fault == TEST_SERVICE_TRIGGER && publication == 1) {
            disposition = MANTIS_ACQUISITION_DISPOSITION_FAILED;
            e.reason = MANTIS_ACQUISITION_REASON_DEVICE_FAILURE;
        }
        b.member_count = 1 + uint32_t(b.frameset != nullptr) + b.trigger_count;
        semantic.kind = MANTIS_SEMANTIC_BUNDLE;
        semantic.bundle = &b;
    }
};
int next(void *ptr, uint32_t t, MantisSemanticEmitV1 emit, void *ctx) {
    if (!ptr || !emit || t > MANTIS_MAX_TIMEOUT_MS)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    const auto f = fault.load();
    uint32_t publication{};
    {
        std::lock_guard lock(s.mutex);
        if (!s.started || s.inhibited)
            return MANTIS_PL_NOT_READY;
        ++s.active;
        publication = s.publication++;
        const auto selected = selected_publication.exchange(UINT32_MAX);
        if (selected != UINT32_MAX)
            publication = selected;
    }
    struct Guard {
        Instance &s;
        ~Guard() {
            std::lock_guard lock(s.mutex);
            --s.active;
            s.wake.notify_all();
        }
    } guard{s};
    if (f == TEST_PENDING || f == TEST_SERVICE_PENDING || f == TEST_SERVICE_ABORT_STATES ||
        f == TEST_SERVICE_ABORT_FALSE || (f == TEST_SERVICE_RECORDING_FAILURE && publication > 0)) {
        std::unique_lock lock(s.mutex);
        if (f == TEST_SERVICE_PENDING || f == TEST_SERVICE_ABORT_STATES || f == TEST_SERVICE_ABORT_FALSE)
            --s.publication;
        ++pending_calls;
        s.wake.wait_for(lock, std::chrono::milliseconds(t), [&] { return s.inhibited; });
        --pending_calls;
        return MANTIS_PL_NOT_READY;
    }
    if (f == TEST_NOT_READY || (publication > 2 && selected_shape == UINT32_MAX))
        return MANTIS_PL_NOT_READY;
    if (f == TEST_FAILURE)
        return MANTIS_PL_ERROR;
    if (f == TEST_ZERO_EMIT)
        return MANTIS_PL_OK;
    int rc = MANTIS_PL_ERROR;
    auto code = boundary([&] {
        auto shape = selected_shape.load();
        if (shape == UINT32_MAX)
            shape = (f == TEST_BAD_FRAMESET || f == TEST_DOUBLE_EMIT || f == TEST_EMIT_AFTER_FAILURE) ? 1u
                    : (f == TEST_RUN_MISMATCH) ? 2u
                                               : publication;
        if (f >= TEST_SERVICE_COMPLETE)
            shape = publication == 0
                        ? (f == TEST_SERVICE_CAPTURE || f == TEST_SERVICE_CALIBRATION_MISMATCH ||
                                   f == TEST_SERVICE_CALIBRATION_CHANGE || f == TEST_SERVICE_CALIBRATION_EXACT
                               ? 1
                           : f == TEST_SERVICE_TRIGGER ? 2
                                                       : 0)
                        : 0;
        if (f == TEST_SERVICE_CALIBRATION_CHANGE && publication == 1)
            shape = 1;
        Bundle b(s, publication, shape);
        MantisHashV1 wrong_hash = view<MantisHashV1>();
        wrong_hash.algorithm = "sha256";
        wrong_hash.hex = "dead";
        auto wrong_content = view<MantisContentReferenceV1>();
        wrong_content.id = s.program.c_str();
        wrong_content.type = type(MANTIS_ACQUISITION_PROGRAM);
        wrong_content.hash = absent<MantisEvidenceHashV1>(MANTIS_PRESENCE_UNAVAILABLE);
        if (b.bundle.evidence.program.content.value)
            wrong_content = *b.bundle.evidence.program.content.value;
        switch (f) {
        case TEST_WRONG_MEMBER:
            b.semantic.kind = MANTIS_SEMANTIC_DATA;
            break;
        case TEST_TOO_MANY_MEMBERS:
            b.bundle.member_count = 65;
            b.bundle.trigger_count = 64;
            break;
        case TEST_BAD_FRAMESET:
            b.frameset.frame_count = 0;
            break;
        case TEST_BAD_EVIDENCE:
            b.bundle.evidence.emitters_count = 0;
            b.bundle.evidence.emitters = nullptr;
            break;
        case TEST_RUN_MISMATCH:
            b.trigger.key.run_id = "other-run";
            break;
        case TEST_ACTIVE_RUN:
            b.bundle.key.run_id = "unrelated-run";
            b.bundle.evidence.key.run_id = "unrelated-run";
            break;
        case TEST_PROGRAM_MISMATCH:
            b.bundle.evidence.program.id = "unrelated-program";
            break;
        case TEST_SUCCESSOR_DUPLICATE:
            b.bundle.key.sequence = publication - 1;
            break;
        case TEST_SUCCESSOR_BACKWARD:
            b.bundle.key.sequence = publication - 2;
            break;
        case TEST_SUCCESSOR_ORDINAL:
            b.bundle.evidence.key.ordinal = publication - 1;
            break;
        case TEST_SUCCESSOR_CLOCK:
            b.bundle.published.clock.domain.id = "other-publication-clock";
            break;
        case TEST_SUCCESSOR_CLOCK_GENERATION:
            b.bundle.published.clock.generation = "other-publication-generation";
            break;
        case TEST_SUCCESSOR_TIME:
            b.bundle.published.time = 0;
            break;
        case TEST_SUCCESSOR_TRIGGER_REUSE:
            b.trigger.key.sequence = publication - 1;
            break;
        case TEST_SUCCESSOR_TRIGGER_BACKWARD:
            b.trigger.key.sequence = publication - 2;
            break;
        case TEST_SUCCESSOR_CONTROLLER:
            b.trigger.key.controller_generation = "another-controller-generation";
            break;
        case TEST_SUCCESSOR_STREAM:
            b.frame.frame.stream.generation = "another-camera-generation";
            b.effective.frame.stream.generation = "another-camera-generation";
            break;
        case TEST_HASH_MISMATCH:
        case TEST_HASH_ESTABLISHED:
            b.bundle.evidence.program.hash = present<MantisEvidenceHashV1>(&wrong_hash);
            break;
        case TEST_HASH_UNAVAILABLE:
            b.bundle.evidence.program.hash = absent<MantisEvidenceHashV1>(MANTIS_PRESENCE_UNAVAILABLE);
            break;
        case TEST_CONTENT_ESTABLISHED:
            b.bundle.evidence.program.content = present<MantisEvidenceContentReferenceV1>(&wrong_content);
            break;
        case TEST_CONTENT_UNAVAILABLE:
            b.bundle.evidence.program.content =
                absent<MantisEvidenceContentReferenceV1>(MANTIS_PRESENCE_UNAVAILABLE);
            break;
        case TEST_SOURCE_WIDTH:
            ++b.frame.width;
            break;
        case TEST_SOURCE_HEIGHT:
            ++b.frame.height;
            break;
        case TEST_OUTPUT_FRAMESET_STREAM:
            b.frameset_key.stream.id = "unadvertised-frameset-stream";
            break;
        case TEST_HASH_DOWNGRADE:
            b.bundle.evidence.program.hash = absent<MantisEvidenceHashV1>();
            break;
        case TEST_CONTENT_MISMATCH:
            ++wrong_content.revision;
            b.bundle.evidence.program.content = present<MantisEvidenceContentReferenceV1>(&wrong_content);
            break;
        case TEST_CONTENT_DOWNGRADE:
            b.bundle.evidence.program.content = absent<MantisEvidenceContentReferenceV1>();
            break;
        case TEST_CONTENT_HASH:
            wrong_content.hash = present<MantisEvidenceHashV1>(&wrong_hash);
            b.bundle.evidence.program.content = present<MantisEvidenceContentReferenceV1>(&wrong_content);
            break;
        case TEST_CONTENT_HASH_UNKNOWN:
        case TEST_CONTENT_HASH_UNAVAILABLE:
            wrong_content.hash = absent<MantisEvidenceHashV1>(
                f == TEST_CONTENT_HASH_UNKNOWN ? MANTIS_PRESENCE_UNKNOWN : MANTIS_PRESENCE_UNAVAILABLE);
            b.bundle.evidence.program.content = present<MantisEvidenceContentReferenceV1>(&wrong_content);
            break;
        case TEST_EVIDENCE_STEP:
            b.step.step_index = s.step_index + 1;
            b.step.run_id = s.run.c_str();
            b.bundle.evidence.step = present<MantisEvidenceStepInstanceV1>(&b.step);
            break;
        case TEST_EVIDENCE_REPETITION:
            b.step.repetition_index = s.repetitions;
            b.step.run_id = s.run.c_str();
            b.bundle.evidence.step = present<MantisEvidenceStepInstanceV1>(&b.step);
            break;
        case TEST_TRIGGER_STEP:
            b.trigger.step.step_index = s.step_index + 1;
            break;
        case TEST_TRIGGER_REPETITION:
            b.trigger.step.repetition_index = s.repetitions;
            break;
        case TEST_BUNDLE_SIZE:
            b.bundle.struct_size = 0;
            break;
        case TEST_BUNDLE_VERSION:
            b.bundle.abi_version = 99;
            break;
        case TEST_BUNDLE_PRESENCE:
            b.emitter.observed.presence = 99;
            break;
        default:
            break;
        }
        rc = emit(ctx, f == TEST_BUNDLE_NULL ? nullptr : &b.semantic);
        if (f == TEST_DOUBLE_EMIT) {
            (void)emit(ctx, &b.semantic);
            rc = MANTIS_PL_OK;
        }
        if (f == TEST_EMIT_AFTER_FAILURE)
            rc = MANTIS_PL_ERROR;
    });
    return code ? code : rc;
}
int status(void *ptr, uint32_t t, MantisProjectedStatusEmitV1 emit, void *ctx) {
    if (!ptr || !emit || t > MANTIS_MAX_TIMEOUT_MS)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    Instance::Call active(s);
    std::string run_value, generation_value;
    uint32_t state;
    uint32_t step_index;
    uint64_t repetitions;
    {
        std::lock_guard lock(s.mutex);
        state = s.started ? MANTIS_RUN_STARTED : s.prepared ? MANTIS_RUN_PREPARED : MANTIS_RUN_OPEN;
        run_value = s.run;
        generation_value = s.generation;
        step_index = s.step_index;
        repetitions = s.repetitions;
    }
    auto out = view<MantisProjectedStatusV1>();
    out.state = state;
    const char *run = run_value.c_str(), *gen = generation_value.c_str();
    out.run = run_value.empty() ? absent<MantisEvidenceRunIdV1>(MANTIS_PRESENCE_UNAVAILABLE)
                                : present<MantisEvidenceRunIdV1>(&run);
    out.generation = generation_value.empty()
                         ? absent<MantisEvidenceGenerationIdV1>(MANTIS_PRESENCE_UNAVAILABLE)
                         : present<MantisEvidenceGenerationIdV1>(&gen);
    out.step = absent<MantisEvidenceStepInstanceV1>();
    auto step = view<MantisStepInstanceV1>();
    if (state == MANTIS_RUN_STARTED) {
        step.run_id = run;
        step.step_index = step_index;
        if (fault == TEST_STATUS_STEP)
            ++step.step_index;
        if (fault == TEST_STATUS_REPETITION)
            step.repetition_index = repetitions;
        out.step = present<MantisEvidenceStepInstanceV1>(&step);
    }
    out.commands_available = absent<MantisEvidenceUInt32V1>();
    out.evidence_available = absent<MantisEvidenceUInt32V1>(MANTIS_PRESENCE_UNAVAILABLE);
    out.error = view<MantisContractErrorV1>();
    if (fault == TEST_BAD_STATUS)
        out.state = 99;
    int rc = MANTIS_PL_ERROR;
    auto code = boundary([&] { rc = emit(ctx, &out); });
    return code ? code : rc;
}
int abort(void *ptr, uint32_t reason, uint32_t t, MantisAbortEmitV1 emit, void *ctx) {
    if (!ptr || !emit || t > MANTIS_MAX_TIMEOUT_MS || reason > MANTIS_ACQUISITION_REASON_CLEANUP_FAILURE)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    ++aborts;
    Instance::Call active(s);
    // This lock is never held by pending next or its callback. Fence first, then OFF.
    std::string run, gen;
    {
        std::lock_guard lock(s.mutex);
        s.inhibited = true;
        run = s.run;
        gen = s.generation;
        s.wake.notify_all();
    }
    int rc = MANTIS_PL_ERROR;
    auto code = boundary([&] {
        auto out = view<MantisAbortOutcomeV1>();
        const char *run_id = run.c_str(), *generation = gen.c_str();
        out.run = run.empty() ? absent<MantisEvidenceRunIdV1>(MANTIS_PRESENCE_UNAVAILABLE)
                              : present<MantisEvidenceRunIdV1>(&run_id);
        out.fenced_generation = gen.empty()
                                    ? absent<MantisEvidenceGenerationIdV1>(MANTIS_PRESENCE_UNAVAILABLE)
                                    : present<MantisEvidenceGenerationIdV1>(&generation);
        uint32_t yes = 1;
        out.inhibited = present<MantisEvidenceUInt32V1>(&yes);
        out.stale_work_fenced = present<MantisEvidenceUInt32V1>(&yes);
        out.off_requested = present<MantisEvidenceUInt32V1>(&yes);
        std::array<MantisEmitterEvidenceV1, 2> emitters{emitter_evidence(), emitter_evidence()};
        emitters[1].emitter = "emitter-beta";
        auto &emitter = emitters[0];
        auto command = view<MantisEmitterCommandV1>();
        command.request = "abort-off";
        command.target = "emitter-alpha";
        command.state = MANTIS_EMITTER_STATE_OFF;
        command.dispatched = host_time();
        emitter.commanded = present<MantisEvidenceEmitterCommandV1>(&command);
        auto beta_command = command;
        beta_command.request = "abort-off-beta";
        beta_command.target = "emitter-beta";
        emitters[1].commanded = present<MantisEvidenceEmitterCommandV1>(&beta_command);
        out.emitters = emitters.data();
        out.emitter_count = s.two_emitters ? 2 : 1;
        out.error = view<MantisContractErrorV1>();
        uint32_t no = 0;
        if (fault == TEST_SERVICE_ABORT_FALSE)
            out.stale_work_fenced = present<MantisEvidenceUInt32V1>(&no);
        if (fault == TEST_SERVICE_ABORT_STATES) {
            out.stale_work_fenced = absent<MantisEvidenceUInt32V1>();
            out.off_requested = absent<MantisEvidenceUInt32V1>(MANTIS_PRESENCE_UNAVAILABLE);
        }
        if (fault == TEST_BAD_ABORT)
            yes = 9;
        if (fault == TEST_ABORT_EMPTY) {
            out.emitters = nullptr;
            out.emitter_count = 0;
        }
        if (fault == TEST_ABORT_MISSING)
            out.emitter_count = 1;
        if (fault == TEST_ABORT_DUPLICATE) {
            emitters[1] = emitter;
            out.emitter_count = 2;
        }
        if (fault == TEST_ABORT_FOREIGN) {
            emitters[1].emitter = "foreign-emitter";
            emitters[1].commanded = absent<MantisEvidenceEmitterCommandV1>();
            out.emitter_count = 2;
        }
        if (fault == TEST_ABORT_UNKNOWN) {
            emitter.commanded = absent<MantisEvidenceEmitterCommandV1>();
            emitters[1].commanded = absent<MantisEvidenceEmitterCommandV1>(MANTIS_PRESENCE_UNAVAILABLE);
        }
        if (fault == TEST_ABORT_REVERSE)
            std::swap(emitters[0], emitters[1]);
        rc = emit(ctx, &out);
    });
    return code ? code : rc;
}
int stop(void *ptr, uint32_t t) {
    if (!ptr || t > MANTIS_MAX_TIMEOUT_MS)
        return MANTIS_PL_INVALID;
    auto &s = *static_cast<Instance *>(ptr);
    std::unique_lock lock(s.mutex);
    s.inhibited = true;
    s.wake.notify_all();
    if (!s.wake.wait_for(lock, std::chrono::milliseconds(t), [&] { return s.active == 0; }))
        return MANTIS_PL_BUSY;
    s.started = false;
    if (fault == TEST_SERVICE_STOP_FAILURE)
        return MANTIS_PL_ERROR;
    return MANTIS_PL_OK;
}
int destroy(void *ptr, uint32_t t) {
    if (fault == TEST_DESTROY_REFUSE || fault == TEST_SERVICE_CLOSE_FAILURE)
        return MANTIS_PL_BUSY;
    auto rc = stop(ptr, t);
    if (rc && fault != TEST_SERVICE_STOP_FAILURE)
        return rc;
    delete static_cast<Instance *>(ptr);
    std::lock_guard lock(ownership);
    owned = false;
    --live_instances;
    ++destroyed_instances;
    return MANTIS_PL_OK;
}
int diagnostics(void *ptr, uint32_t t, MantisTextEmitV1 emit, void *ctx) {
    if (!emit || t > MANTIS_MAX_TIMEOUT_MS)
        return MANTIS_PL_INVALID;
    std::unique_ptr<Instance::Call> active;
    if (ptr)
        active = std::make_unique<Instance::Call>(*static_cast<Instance *>(ptr));
    int rc = MANTIS_PL_ERROR;
    auto code =
        boundary([&] { rc = emit(ctx, "deterministic contract fixture; optical state unavailable"); });
    return code ? code : rc;
}
// Last exception boundary includes allocation/locking before inner callbacks.
template <auto Function> struct Safe;
template <class R, class... Args, R (*Function)(Args...)> struct Safe<Function> {
    static R call(Args... args) noexcept {
        try {
            return Function(args...);
        } catch (...) {
            if constexpr (std::is_void_v<R>)
                return;
            else if constexpr (std::is_pointer_v<R>)
                return nullptr;
            else
                return MANTIS_PL_ERROR;
        }
    }
};
const MantisProjectedLightV1 projected = {sizeof(projected),      1,
                                          Safe<enumerate>::call,  Safe<open>::call,
                                          Safe<validate>::call,   Safe<prepare>::call,
                                          Safe<start>::call,      Safe<next>::call,
                                          Safe<status>::call,     Safe<abort>::call,
                                          Safe<stop>::call,       Safe<destroy>::call,
                                          Safe<diagnostics>::call};
// Camera-only mode shares parent ownership but never opens the projected interface.
int camera_enumerate(MantisDiscoverEmitV1 emit, void *ctx) {
    static const char *caps[] = {MANTIS_FRAMESET_STREAM_V1};
    MantisDiscoveredDeviceV1 d{sizeof(d), 1, "parent-alpha", "", "Camera-only fixture", caps, 1, "{}"};
    int rc = 1;
    auto code = boundary([&] { rc = emit(ctx, &d); });
    return code ? code : rc;
}
int camera_open(const MantisHostV1 *h, const char *id, void **p) {
    return open(h, id, 0, p);
}
void camera_destroy(void *p) {
    (void)destroy(p, 1000);
}
int camera_start(void *p) {
    auto &s = *static_cast<Instance *>(p);
    s.started = true;
    return 0;
}
int camera_stop(void *p) {
    return stop(p, 1000);
}
int camera_next(void *p, uint32_t t, MantisFrameSetEmitV1 emit, void *ctx) {
    if (!p || !emit || t > MANTIS_MAX_TIMEOUT_MS)
        return 1;
    int rc = 1;
    auto code = boundary([&] {
        auto &s = *static_cast<Instance *>(p);
        if (!s.started)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        mantis::sdk::Buffer buffer(s.host, 4);
        std::fill(buffer.writable().begin(), buffer.writable().end(), std::byte{5});
        buffer.publish();
        MantisAttributeV1 a{sizeof(a), 1,      "org.mantis.pixels", "intensity", 1, 2,
                            {2, 2},    {2, 1}, buffer.get(),        0,           4};
        MantisObservationV1 image{};
        image.struct_size = sizeof(image);
        image.abi_version = 1;
        image.packet = {sizeof(MantisPacketV1),
                        1,
                        MANTIS_IMAGE,
                        1,
                        7 + s.publication++,
                        0,
                        "clock",
                        "",
                        0,
                        "optical",
                        &a,
                        1};
        image.metadata_json = "{}";
        MantisFrameSetV1 set{};
        set.struct_size = sizeof(set);
        set.abi_version = 1;
        set.observation = image;
        set.observation.packet.type_id = MANTIS_FRAMESET;
        set.observation.packet.attributes = nullptr;
        set.observation.packet.attribute_count = 0;
        set.frames = &image;
        set.frame_count = 1;
        rc = emit(ctx, &set);
    });
    return code ? code : rc;
}
int camera_diagnostics(void *, MantisTextEmitV1 emit, void *ctx) {
    return emit ? emit(ctx, "{}") : MANTIS_PL_INVALID;
}
const MantisAcquisitionV1 acquisition = {sizeof(acquisition),           1,
                                         Safe<camera_enumerate>::call,  Safe<camera_open>::call,
                                         Safe<camera_destroy>::call,    Safe<camera_start>::call,
                                         Safe<camera_next>::call,       Safe<camera_stop>::call,
                                         Safe<camera_diagnostics>::call};
int processor_describe(uint32_t timeout_ms, MantisNodeDescriptorV1 *out) {
    if (timeout_ms > MANTIS_MAX_TIMEOUT_MS || !mantis::sdk::compatible_table(out))
        return 1;
    *out = {
        sizeof(*out), 1, "semantic-identity", MANTIS_ACQUISITION_BUNDLE, MANTIS_ACQUISITION_BUNDLE, 1, 1, 1,
        "cpu"};
    return 0;
}
int processor_process(const MantisHostV1 *, const MantisSemanticPacketV1 *p, uint32_t t,
                      MantisSemanticEmitV1 emit, void *ctx) {
    if (!mantis::sdk::compatible_table(p) || t > MANTIS_MAX_TIMEOUT_MS || !emit)
        return MANTIS_PL_INVALID;
    int rc = 1;
    auto code = boundary([&] { rc = emit(ctx, p); });
    return code ? code : rc;
}
const MantisProcessorV2 processor = {sizeof(processor), 1, Safe<processor_describe>::call,
                                     Safe<processor_process>::call};
const TestProjectedControl control = {
    [](uint32_t f) { fault = f; },
    [] { return pending_calls.load(); },
    [] { return destroyed_instances.load(); },
    [] { return live_instances.load(); },
    [] { return initializations.load(); },
    [] { return shutdowns.load(); },
    [](uint32_t shape) { selected_shape = shape; },
    [](uint32_t publication) { selected_publication = publication; },
    [] { return opens.load(); },
    [] { return validations.load(); },
    [] { return prepares.load(); },
    [] { return starts.load(); },
    [] { return aborts.load(); },
    [](void (*probe)(void *), void *ctx) {
        prepare_probe = probe;
        prepare_context = ctx;
    },
    [](const char *role, const char *identity, uint32_t width, uint32_t height) {
        image_role = role;
        image_identity = identity;
        image_width = width;
        image_height = height;
    },
    [](const char *id, uint64_t revision) {
        source_calibration_id = id;
        source_calibration_revision = revision;
    }};
int initialize(const MantisHostV1 *h) {
    if (!mantis::sdk::compatible(h))
        return 1;
    ++initializations;
    return 0;
}
void shutdown() {
    ++shutdowns;
}
const void *query(const char *id) {
    if (!id)
        return nullptr;
    if (!std::strcmp(id, MANTIS_PROJECTED_LIGHT_V1))
        return &projected;
    if (!std::strcmp(id, MANTIS_ACQUISITION_V1))
        return &acquisition;
    if (!std::strcmp(id, MANTIS_PROCESSOR_V2))
        return &processor;
    if (!std::strcmp(id, TEST_PROJECTED_CONTROL))
        return &control;
    return nullptr;
}
const MantisPluginV1 plugin = {sizeof(plugin),
                               1,
                               "org.example.projected-contract",
                               "1.0.0",
                               Safe<initialize>::call,
                               Safe<shutdown>::call,
                               Safe<query>::call};
} // namespace
extern "C" MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t abi) {
    return abi == 1 ? &plugin : nullptr;
}
