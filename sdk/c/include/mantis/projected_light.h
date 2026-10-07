#ifndef MANTIS_PROJECTED_LIGHT_H
#define MANTIS_PROJECTED_LIGHT_H
#include <mantis/semantic_views.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MANTIS_MAX_TIMEOUT_MS 60000u
/* All calls are synchronous; timeout_ms is a relative total-call deadline in
 * milliseconds, 0=poll, 1..60000=finite wait. UINT32_MAX is never wait forever.
 * Producers must bound their callbacks and finish within the deadline. Hosts
 * must bound callback work too. Failure/not-ready/busy emits zero publications.
 */
enum {
    MANTIS_PL_OK = 0u,
    MANTIS_PL_ERROR = 1u,
    MANTIS_PL_NOT_READY = 2u,
    MANTIS_PL_BUSY = 3u,
    MANTIS_PL_INVALID = 4u,
    MANTIS_PL_INCOMPATIBLE = 5u,
    MANTIS_PL_TIMEOUT = 6u
};
enum {
    MANTIS_ERROR_NONE = 0u,
    MANTIS_ERROR_ARGUMENT = 1u,
    MANTIS_ERROR_CONTRACT = 2u,
    MANTIS_ERROR_RESOURCE = 3u,
    MANTIS_ERROR_DEVICE = 4u,
    MANTIS_ERROR_TRANSPORT = 5u,
    MANTIS_ERROR_DEADLINE = 6u,
    MANTIS_ERROR_CLEANUP = 7u
};
enum {
    MANTIS_PARTICIPANT_PARENT = 0u,
    MANTIS_PARTICIPANT_IMAGE = 1u,
    MANTIS_PARTICIPANT_EMITTER = 2u,
    MANTIS_PARTICIPANT_CONTROLLER = 3u
};
enum {
    MANTIS_RUN_OPEN = 0u,
    MANTIS_RUN_PREPARED = 1u,
    MANTIS_RUN_STARTED = 2u,
    MANTIS_RUN_STOPPED = 3u,
    MANTIS_RUN_FAILED = 4u
};
typedef struct MantisContractErrorV1 {
    uint32_t struct_size, abi_version, category, code;
} MantisContractErrorV1;
typedef struct MantisProjectedLimitsV1 {
    uint32_t struct_size, abi_version;
    uint32_t max_components, max_steps, max_bundle_members, max_cameras;
    uint64_t max_step_instances, max_commands, max_events, max_bytes, max_in_flight_captures;
    int64_t max_run_duration_ns, max_on_duration_ns, max_step_duration_ns;
    uint32_t max_pending_bundles, max_call_timeout_ms;
    /* Evidence<uint32_t> boolean: established 0/1, unknown, or unavailable. */
    MantisEvidenceUInt32V1 watchdog, interlock, fail_off;
} MantisProjectedLimitsV1;
/* A complete graph is one borrowed snapshot (<=64 components); no JSON authority.
 * controls and trigger_endpoints reference IDs within this selected parent's graph.
 * participants identify acquisition relationships, not presentation roles.
 * Capability advertisement describes accessible API support, never physical proof.
 */
typedef struct MantisProjectedComponentV1 {
    uint32_t struct_size, abi_version;
    const char *id, *parent_id, *name, *role;
    uint32_t participant_kind;
    const char *const *capabilities;
    uint32_t capability_count;
    const char *const *controls;
    uint32_t control_count;
    const char *const *participants;
    uint32_t participant_count;
    const char *const *trigger_endpoints;
    uint32_t trigger_endpoint_count;
    const uint32_t *emitter_states, *capture_modes, *trigger_modes, *evidence_methods, *evidence_scopes;
    uint32_t emitter_state_count, capture_mode_count, trigger_mode_count, evidence_method_count,
        evidence_scope_count;
    MantisEvidencePatternIdV1 pattern;
    MantisEvidenceUInt64V1 pattern_revision;
    const MantisLineIdentityV1 *lines;
    uint32_t line_count;
} MantisProjectedComponentV1;
typedef struct MantisProjectedGraphV1 {
    uint32_t struct_size, abi_version;
    const char *parent_id;
    const MantisProjectedComponentV1 *components;
    uint32_t component_count;
    MantisProjectedLimitsV1 limits;
} MantisProjectedGraphV1;
typedef int (*MantisProjectedGraphEmitV1)(void *, const MantisProjectedGraphV1 *);
typedef struct MantisProgramValidationV1 {
    uint32_t struct_size, abi_version, accepted;
    MantisContractErrorV1 error;
    const char *diagnostic; /* optional bounded human text, never decision authority */
} MantisProgramValidationV1;
typedef int (*MantisProgramValidationEmitV1)(void *, const MantisProgramValidationV1 *);
typedef struct MantisProjectedStatusV1 {
    uint32_t struct_size, abi_version, state;
    MantisEvidenceRunIdV1 run;
    MantisEvidenceGenerationIdV1 generation;
    MantisEvidenceStepInstanceV1 step;
    MantisEvidenceUInt32V1 commands_available, evidence_available;
    MantisContractErrorV1 error;
} MantisProjectedStatusV1;
typedef int (*MantisProjectedStatusEmitV1)(void *, const MantisProjectedStatusV1 *);
typedef struct MantisAbortOutcomeV1 {
    uint32_t struct_size, abi_version;
    MantisEvidenceRunIdV1 run;
    MantisEvidenceGenerationIdV1 fenced_generation;
    MantisEvidenceUInt32V1 inhibited, stale_work_fenced, off_requested;
    const MantisEmitterEvidenceV1 *emitters;
    uint32_t emitter_count;
    MantisContractErrorV1 error;
} MantisAbortOutcomeV1;
typedef int (*MantisAbortEmitV1)(void *, const MantisAbortOutcomeV1 *);
/* Optional under root ABI 1. open owns the selected parent and all resources;
 * no separate MantisAcquisitionV1 open is needed or implied. Duplicate ownership
 * is BUSY, including conflicting camera-only opens of those resources.
 * Ordinary calls are serial per instance. abort is thread-safe concurrently with
 * any ordinary pending call, including next. First inhibit ON/triggers, then
 * fence stale generation work, then request all participating emitters OFF.
 * It never waits for recorder/normal callbacks/queues/UI. Software success does
 * not establish optical OFF; outcome carries actual availability/evidence.
 * stop/destroy quiesce callbacks within timeout or return BUSY/TIMEOUT; failed
 * destroy leaves the instance valid and owned. Successful destroy emits nothing,
 * releases ownership and forbids future callbacks. Unload only after success.
 * next: OK and exactly one BUNDLE semantic emit; NOT_READY and zero; else failure.
 */
typedef struct MantisProjectedLightV1 {
    uint32_t struct_size, abi_version;
    int (*enumerate)(uint32_t timeout_ms, MantisProjectedGraphEmitV1, void *);
    int (*open)(const MantisHostV1 *, const char *parent_id, uint32_t timeout_ms, void **instance);
    int (*validate)(void *, const MantisAcquisitionProgramV1 *, uint32_t timeout_ms,
                    MantisProgramValidationEmitV1, void *);
    int (*prepare)(void *, const MantisAcquisitionProgramV1 *, uint32_t timeout_ms,
                   MantisProgramValidationEmitV1, void *);
    int (*start)(void *, const char *run_id, const char *generation_id, uint32_t timeout_ms);
    int (*next)(void *, uint32_t timeout_ms, MantisSemanticEmitV1, void *);
    int (*status)(void *, uint32_t timeout_ms, MantisProjectedStatusEmitV1, void *);
    int (*abort)(void *, uint32_t reason, uint32_t timeout_ms, MantisAbortEmitV1, void *);
    int (*stop)(void *, uint32_t timeout_ms);
    int (*destroy)(void *, uint32_t timeout_ms);
    int (*diagnostics)(void *, uint32_t timeout_ms, MantisTextEmitV1, void *);
} MantisProjectedLightV1;
/* Additive full immutable semantic processing; root abi_version remains 1.
 * One OK process call emits exactly one full semantic output, errors emit none.
 * No input header replacement; output retains its own complete semantic values.
 * L6 owns runtime pipeline integration; MantisProcessorV1 remains unchanged.
 */
typedef struct MantisProcessorV2 {
    uint32_t struct_size, abi_version;
    int (*describe)(uint32_t timeout_ms, MantisNodeDescriptorV1 *);
    int (*process)(const MantisHostV1 *, const MantisSemanticPacketV1 *, uint32_t timeout_ms,
                   MantisSemanticEmitV1, void *);
} MantisProcessorV2;
#ifdef __cplusplus
}
#endif
#endif
