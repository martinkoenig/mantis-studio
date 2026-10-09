# Projected-light daemon control (v0.4 L5)

Status: implemented, acceptance pending. L0–L4 are FINAL ACCEPTED. L6–L8 remain
planned. No production projected-light hardware, extractor, triangulation or
remote network transport is introduced.

## Ownership and addressing

`services::ProjectedLightService` is separate from camera `CaptureService`.
`Runtime` implements projected discovery, pure validation, start, status, list,
and generation-fenced stop/cancel. A device selection is `(plugin_id, parent_id)`;
component IDs are scoped to their plugin. Discovery returns the accepted L2 graph:
parent/component relationships, controls, trigger endpoints, image stream and
physical identity/role/dimensions, supported emitter/capture/trigger states,
pattern identity/revision presence, evidence methods/scopes, FrameSet output
stream and every finite executor limit. Watchdog/interlock/fail-OFF presence is
advertised capability, never proof of observed or physical safety.

The daemon checks overlapping graph components/parent relationships before opening
an executor or camera capture. Independent resources may coexist. Device refresh
is suppressed while projected hardware or its recorder is active; plugin lifecycle
mutation and project switching reject relevant active ownership. Historical adapters
and mapped publications retain library/buffer owners. Switching projects also
requires idle jobs. A refused close keeps its graph exclusively reserved even
after cleanup_resolved; a later start or plugin lifecycle request cannot bypass
the quarantined ownership. Each project retains at most 64 projected captures and 64
projected replay jobs. Request identities occupy one entry per retained capture.

The control dispatcher is serialized; each recorder owns one session and a short
status mutex. It never holds that mutex across `next`, append, abort or finalization.
There is no Runtime-wide mutex across hardware, filesystem I/O or jobs. Status
uses recorder/job-owned cached byte/storage facts and a short L3 snapshot read,
so stop/status do not wait for Store's append lock. Complete record byte accounting
uses structural MANTIS03 length plus the frozen 40-byte MRAWREC3 envelope; it
adds no pixel traversal or per-record SQLite access. Outcome/finalization/recovery
refresh descriptor facts at their operation boundaries. Stop invokes
L3's priority path directly, independently of the recorder and its filesystem work.
Shutdown requests L3 stop, retires recorder/public invocations, then cancels/joins
jobs before registry teardown. Unfinished storage is explicitly abandoned as
RECOVERABLE. Compliant plugin calls have L3's finite deadlines; an in-process plugin
violating its synchronous contract cannot be forcibly preempted. The accepted
late-abort/quarantine and concurrent-call lifetime rules remain in force.

## Typed programs and pure validation

`ProjectedProgramSource` is oneof a typed inline `ProjectedAcquisitionProgram`
(maximum serialized inline DTO: **512 KiB**, checked before executor open), or a
finalized RawCapture schema-3 artifact ID. Artifact sources load exactly the
integrity-checked persisted MRUNHDR3 program; active calibration is never consulted.
No standalone program format is introduced.

The program DTO has explicit type/schema, complete ProgramReference with nested
hash/content presence, camera/emitter/controller participants, ordered sparse step
indices, complete OFF/ON vectors, capture/camera/trigger intent, evidence
requirement/scope, settle and maximum durations, repetitions and all RunBounds.
Durations are signed nanoseconds; resource counts are typed finite integers.
Daemon defaults are queue capacity 1, each timeout 100 ms and 4096 correlation
entries; explicit values reach L3 preflight unchanged and are persisted exactly.
L3 caps actual executor-call deadlines at the advertised executor limit.
Schema-1 terminal policy is fixed and has no configurable field. Optional enum
fields preserve construction omission for canonical validation. Invalid enum tags
or mismatched Presence/value are malformed RPC errors. Expected canonical, graph,
resource or executor validation rejection is a structured normal validation result.

Conversion yields `data::AcquisitionProgram`. `ProjectedRun::validate_program`
uses **the same Impl::preflight** as normal preparation, then only executor
`validate` and finite `close`. It never prepares, starts, triggers, aborts or creates
an artifact. Results distinguish host rejection, executor validation/rejection,
executor error and close error, and expose finite graph limits.

## Start, retries and failed starts

Start resolves the graph and exclusive ownership, creates a service handle and
`ProjectedRun` with fresh RunId/GenerationId, and synchronously calls
`Store::begin_projected_capture` with the exact program and
`device::recorded_run_config`. Only after the immutable header is durable does it
launch the recorder and request `prepare`, then `start`. Start performs finite
lifecycle operations and returns; acquisition continues daemon-owned without clients.

Validation, preparation and start rejection after header creation retain the
handle, identity, program, artifact, typed root/cleanup errors and queue metrics.
The recorder persists the resolved final daemon outcome and finalizes a valid
zero-bundle failed capture. No first-frame requirement exists.

The top-level Request.request_id is the start idempotency key (nonempty, at most
256 bytes). The SDK preserves a supplied ID and generates one only when absent.
Retries compare plugin, parent, exact canonical program, source-artifact identity
and finite runtime config;
identical requests return the same handle/run/artifact without reopening or
preparing/starting. Reusing the key with changed semantics rejects explicitly.
Comparison uses the accepted typed header codec with fixed comparison identity;
Protobuf/JSON serialization is not treated as canonical. New IDs are new requests
and remain subject to ownership. No unbounded request cache exists.

Stop/cancel names capture, expected RunId and expected GenerationId. Missing,
wrong or stale tokens reject. NORMAL_STOP calls L3.stop; CANCEL calls L3.cancel.
The first mutation is latched; retries/conflicting later mutations return current
status without another abort, outcome or job. L3 fault > cancellation > completion
precedence and root/cleanup error separation are retained.

## Recording, status and diagnostics

Projected start snapshots the selected logical parent's project-local active
RigCalibration before executor opening/preparation. It verifies the finalized
artifact/hash and exact logical ID/schema/revision through the existing calibration
artifact reader. Declared image participants match typed graph component/stream,
physical identity, role and width/height; a subset of a stereo rig is supported.
Emitter/controller children are not cameras. Pure validation reuses this same check
before executor opening; expected incompatibility returns accepted=false with the
precise host_error and discovered limits, without prepare/start/abort or artifact
creation. Start rejects the same mismatch before hardware preparation and before
creating a capture artifact. Executor rejection/error remains a distinct field.

An internal calibration-bound ProjectedExecutor delegates all lifecycle calls and
stamps FrameSet/image headers, AcquisitionEvidence.rig_calibration and source-frame
rig references before L3 correlation/authoritative publication. The typed bound
reference contains both logical revision and exact immutable artifact/content hash.
Packet containers are copied; BufferViews and pixel backing are shared. Native
timing, trigger/effectiveness facts, camera-calibration and original-calibration
evidence remain unchanged. Abort delegates directly, without the binding mutex or
storage access.

The selected reference/dependency/hash enters provenance at durable capture
initialization. Source parent/child references must agree and remain stable;
packet headers compare logical ID/schema/revision only. Source acquisition/camera rig
references additionally agree on exact artifact ID, type/schema, revision and hash
presence/value whenever both establish content. Missing Unknown/Unavailable content
is not a conflicting identity and is never filled in. Same-camera source stability
remains checked; different CameraCalibration artifacts are independent of rig identity.
Original source headers and initial/first-established rig references, including exact
presence/content/hash and each camera's initial/first-established rig references,
are retained separately under source_* provenance fields. The recorder initializes these bounded, write-once audit
fields only when their corresponding bundle reaches the recording prefix, before
append. The schema-3 source-provenance operation permits late initialization while
OPEN, even after control-only records; it cannot change project binding/dependencies,
overwrite a field or run after outcome publication. Late camera evidence without a
FrameSet follows the same validation and audit rules. At most 3 + 2 × declared-camera
count small initialization groups are produced per run (64-camera semantic bound),
with at most 2048 bounded scalar provenance fields and no per-frame lookup/SQLite
transaction. No header, bundle, record, outcome format or metadata schema changes.

No active binding is valid: no project reference is invented and original semantic
Presence and packet references remain unchanged. Replay reads only recorded values;
later activation or clearing never replaces the historical revision.

The authoritative path is `ProjectedRun::next -> Store::append_bundle`, synchronously.
L3 owns the sole bounded authoritative queue. L5 retains one immutable latest bundle
and compact counters, never a frame/bundle history queue. Store backpressure applies
directly. After cleanup resolves the recorder drains every produced bundle, converts
`recorded_run_outcome(snapshot)` (expected count = **queue.produced**), persists
run.outcome through L4's exact count check, seals and submits final verification to
existing Jobs. The artifact is FINALIZED only when that job succeeds. Executor
terminal disposition is never substituted for final daemon outcome.

Recording errors are separately visible. They call the narrow internal
`ProjectedRun::recording_fault(Error)` using RESOURCE_LIMIT and the existing latch/
priority abort path; they never impersonate user stop/cancel. Failed recording is
RECOVERABLE; no missing final outcome is invented. A fault after already resolved
cleanup cannot rewrite the immutable L3 terminal snapshot. Finalization failures
remain job/storage errors. A cancelled queued finalization task releases its sealed
artifact as RECOVERABLE even if the task never executes; its recorded daemon outcome
remains unchanged and explicit recovery can verify/finalize the prefix.

Status contains capture/plugin/parent/run/generation/artifact identities, immutable
program reference, the seven daemon states, cleanup_resolved/active, separate typed
storage state, last evidence step and optional latest sequence, L3 produced/consumed/
occupancy/capacity/high-water/saturation, committed count, container bytes, finalization
job and optional recording error. Initiating/abort/stop/close errors have structural
message presence. Abort summary retains run/generation presence, inhibited/fenced/
OFF-request three-state booleans, per-emitter command/acknowledgement/observation
presence and typed state/stage/result/scope, effective presence counts and executor
error. An OFF request is not an observed/effective/optical OFF claim.

Counters update once per committed bundle: bundle/actual TriggerEvent counts,
evidence-only/captured counts, command presence, acknowledgement result/presence,
effective-state presence, unresolved request records and loss records. Counters count reported evidence
entries, including repeated reports; they do not claim distinct dispatches, currently
outstanding requests or physical loss. Late-evidence count
comes from L3. No status request rebuilds historical evidence or estimates unseen
physical losses. Diagnostics retain the existing 512-event budget; lifecycle events
are created/validation_failed/ready/started/stopping/completed/cancelled/failed/
recording_failed/finalizing/finalized, never per-frame histories. Events are diagnostic;
RawCapture remains authoritative.

## Data references, replay and SDK/CLI

`projected_bundle` returns the latest live or replay bundle as a leased immutable
MANTIS03 file in cache/previews: transport `local-mapped-file`, format_version 3,
lease ID and 60-second lease. Packet and bundle references share the existing
**eight outstanding lease** budget and preview_release. No publication yet returns
BUSY. Evidence-only bundles are valid; no fake FrameSet is created. The C++ helper
maps `read_bundle`, releases the lease and returns owned semantics; pixel BufferViews
retain the read-only mapping after unlink/helper return. No application pixel copy
occurs during decode. Temporary serialization writes original pixels unchanged.

Existing `replay` dispatches schema 1/2 to the unchanged camera source and schema 3
to `device::BundleReplay`. ASAP and receive/publication pacing alter delivery time
only. Jobs poll with 50-ms interruptible waits and check cancellation between polls/
records. Latest replay state holds one bundle. Verification streams two deterministic
passes over canonical capture-header bytes, canonical bundle bytes, one explicit final
outcome presence byte (0 absent / 1 present), and, when present, canonical MRUNOUT3
with the exact replay count. FNV-1a-64 is accidental-corruption/determinism detection,
not authentication. No executor, active calibration or ImageStream is opened for
schema-3 replay.

C++ Client adds projected_devices, validate_projected, start_projected(request, ID),
projected_status, projected_captures, stop_projected/cancel_projected(capture, run,
generation), projected_bundle_reference and mapped projected_bundle. Existing replay
returns a Job ID. CLI adds:

```
mantis-cli projected devices
mantis-cli projected validate|start PLUGIN PARENT --program FILE.json
mantis-cli projected validate|start PLUGIN PARENT --program-from-raw RAW_ARTIFACT
mantis-cli projected list
mantis-cli projected status CAPTURE
mantis-cli projected stop|cancel CAPTURE RUN GENERATION
mantis-cli projected bundle CAPTURE_OR_REPLAY
```

Start supports --request-id. Both validation/start accept --queue-capacity,
--operation-timeout-ms, --abort-timeout-ms, --cleanup-timeout-ms,
--publication-timeout-ms and --correlation-entries, all positive uint32.
Program JSON is strictly parsed by Protobuf with unknown fields rejected and a
512-KiB file bound. It is human input, not authoritative storage semantics. Bundle
CLI prints reference metadata, never pixels.

## Protocol contract

Root remains mantis.wire.v1, version 1; legacy Request tags 10–40 and Response
fields 1–15 are unchanged. New Request oneof tags:

| Tag | Command |
|---|---|
|41|projected_devices_list|
|42|projected_validate|
|43|projected_start|
|44|projected_status|
|45|projected_captures_list|
|46|projected_stop (typed NORMAL_STOP/CANCEL)|
|47|projected_bundle|

New Response fields: 16 projected_devices (repeated), 17 projected_validation,
18 projected_captures (repeated). DataReference remains field 12. Every projected
message field and important enum numeric mapping is frozen by protocol contract
tests. Explicit enum mappings (ordered values start at 0):

| Enum | Numeric order |
|---|---|
|Presence|Unknown, Unavailable, Established|
|RunState|Validating, Ready, Running, Stopping, Completed, Cancelled, Failed|
|EmitterState|OFF, ON|
|CaptureMode|None, FreeRunning, HardwareTrigger|
|EvidenceRequirement|CommandedOnly, ControllerAcknowledged, ExposureEffective|
|EvidenceScope|ControllerRegister, ElectricalEnable, OpticalEmission|
|EvidenceMethod|SoftwareDispatch, ControllerReport, RegisterReadback, ElectricalReadback, OpticalSensor, ValidatedExecutor, CameraMetadata, SoftwareAssociation, Imported|
|StopMode|NormalStop, Cancel|
|Reason|None, Timeout, Rejected, DeviceFailure, TransportFailure, EvidenceMissing, ContradictoryEvidence, ResourceLimit, UserStop, UserCancel, CleanupFailure|
|ParticipantKind|Parent, Image, Emitter, Controller|
|TriggerMode|None, FreeRunning, HardwareTrigger|
|StorageState|Open, Finalizing, Finalized, Recoverable|
|AcknowledgementStage|Acceptance, Completion|
|AcknowledgementResult|Success, Rejected, Failed|

Ordinary projected DTOs have no bulk bundle/evidence tables, FrameSet/pixels,
TriggerEvent history, samples, bytes fields, arbitrary enum strings or authoritative
JSON/maps. The 4-MiB control frame/request/response bound remains; unknown commands
fail. Transport remains IPv4 **127.0.0.1 only**, with existing MANTIS_TOKEN local
policy. This is not production remote authentication, WAN streaming or TLS.

## L5 validation evidence

Local x86_64 Debug, Studio OFF: complete non-hardware CTest **47/47 passed**
(24.06 seconds). ASan + UBSan: the same complete suite **47/47 passed**
(97.22 seconds), with leak detection and halt-on-error enabled and no sanitizer
reports. `git diff --check` and architecture boundaries passed.

The new projected-controls test uses real protocol dispatch and the accepted test
plugin: exact discovery, pure validation without prepare/start/artifacts, typed
program round-trip, durable-header-before-prepare probe, zero-bundle host/executor/
prepare/start failures, idempotent starts, generation fences, both camera/projected
ownership directions, resource quarantine, storage failure/priority abort, three-state
abort summaries, bounded history/leases, replay verification and queued-finalization
cancellation/recovery. A real Store append is blocked on a mapped pixel fence while
stop/status reach L3 independently. Shutdown tests cover a pending projected run,
concurrent unrelated camera capture and queued finalization.

The projected-client-cli test uses actual loopback framing, the C++ SDK and CLI:
caller-supplied request correlation/retry, strict typed JSON and exact RawCapture
program sources, status/list/stop/cancel, two-pass verification, evidence-only
publications, paced replay cancellation and mapped pixels surviving lease release.
Existing protocol, legacy/golden formats, L3/L4, frozen L2 ABI/catalog, acquisition,
QoS and calibration-dataset regressions are included in both full suites.

Tests use worktree-local temporary storage. The sanitizer environment selects the
consistent system OpenCV library family and sets OPENCV_OPENCL_RUNTIME=disabled
to avoid the pre-existing NVIDIA OpenCL loader allocation leak. No sanitizer
suppression is installed. No physical hardware is used.

The [starting commit CI](https://github.com/martinkoenig/mantis-studio/actions/runs/37791356298)
is green for x86_64 and ARM64, each Studio ON/OFF, and ASan/UBSan. The new L5
commit's CI remains pending its push; those baseline results are not claimed for
the new SHA. Frozen C ABI/storage fixtures and their implementations are unchanged.

The calibration-binding follow-up passes the complete non-hardware suite **48/48**
(30.74 seconds) and the complete ASan + UBSan suite **48/48** (106.21 seconds),
with the same leak-detection/OpenCV environment and no sanitizer reports. The new
projected-calibration-binding test covers no active binding, a camera subset of a
stereo rig, exact revision/artifact/hash binding before L3 publication, shared pixel
backing, identity/role/dimension rejection before executor opening, contradictory
and changing source references, late source evidence without a FrameSet, bounded
write-once provenance, and unchanged replay after activating a newer revision or
clearing the active binding. Idempotent retries preserve the original snapshot.
The existing camera-only binding, projected concurrency/count-binding, protocol,
legacy storage golden and frozen ABI/catalog tests remain green. Boundaries and
`git diff --check` pass.

The [follow-up baseline CI at 7171b740](https://github.com/martinkoenig/mantis-studio/actions/runs/37917677111)
is green for x86_64 and ARM64 Studio ON/OFF and sanitizers. The subsequent
a13adf3 ARM64 failure is investigated below.
No frozen ABI, protocol numbering, accepted storage format, hardware, extraction,
triangulation or L6 implementation changes are included.

The calibration-validation follow-up also covers pure validation of compatible,
absent, physically/role/dimension-incompatible and corrupted active calibration;
malformed-program host errors and executor errors retain separate results. Exact
source tests reject acquisition/camera and camera/camera rig content conflicts,
accept matching references and nested Unknown/Unavailable content, and confirm that
an inconsistent native bundle produces zero authoritative L3 publications.

### Acquisition integration CI investigation

[Run 37923585814, ARM64 Studio OFF](https://github.com/martinkoenig/mantis-studio/actions/runs/37923585814/job/113797127401)
failed at acquisition.py's phase-jitter-left public validator start with
`LOSSLESS writer queue saturated; capture failed explicitly`. The corresponding
camera Session, source fixture and integration test were unchanged from 7171b740;
ProjectedCalibrationBinding is called only by projected start/validation, never
that camera path. CI uses sequential CTest; resource pressure was not measured on
the failing runner, so a specific scheduler/storage stall cannot be asserted.

The same pre-existing mechanism is present in
[run 37672328140 at 876b303c, x86_64 Studio OFF](https://github.com/martinkoenig/mantis-studio/actions/runs/37672328140/job/112966960147):
acquisition-y10p-integration failed during phase-drift-left with the same lossless
saturation error, before L1–L5 implementations. Phase fixtures deliberately emit
without wall-clock pacing for fast algorithm unit tests. Successful live recording
of that CPU-speed producer depends on uncontended writer scheduling; the recorder
correctly refuses an enqueue after its existing finite admission deadline.

Ten unchanged local acquisition runs and five under two-core/three-burner CPU
contention passed. A worktree-local LD_PRELOAD experiment delaying the second
phase-jitter-left `.segment.part` write by 150 ms reproduced the exact CI failure
at the same validator start. This demonstrates storage/scheduler-stall sensitivity;
it does not claim an observed 150 ms stall on GitHub's runner.

Integration tests now explicitly set `MANTIS_X1_FAKE_PACE=1`. This option applies
only to the fake backend's phase fixture delivery and uses the profile's finite
frame period. Native timestamps, sequence values and pixel bytes are unchanged;
scheduling delays do not generate a catch-up burst or discard an observation.
Default algorithm unit fixtures remain unpaced. The Linux camera backend, camera
Session, 32-entry queue, 50 ms admission deadline, lossless failures and all existing
assertions remain unchanged. Ten corrected repetitions with the identical injected
150 ms write stall passed. No fault injection library is installed or used by CI.

Five GREY and five Y10P repetitions also passed with the same injected write stall
and two-core/three-burner CPU contention. The final complete non-hardware suite
passes **48/48** (30.43 seconds); the complete ASan + UBSan suite passes **48/48**
(108.46 seconds), with leak detection and halt-on-error enabled. Architecture
boundaries and `git diff --check` pass. CI acceptance additionally requires all five
jobs to succeed for the exact final pushed SHA; local results alone are insufficient.
