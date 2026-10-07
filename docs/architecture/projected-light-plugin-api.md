# Projected-light native plugin API (L2)

Status: **L2 implemented, acceptance pending**. L0 accepted; L1 **FINAL ACCEPTED**;
L3–L8 planned. This specifies an in-memory native contract, not a recorder codec,
sequencer, transport or physical safety certification. ADR-042 is unchanged.

## Headers and identifiers

Include `<mantis/projected_light.h>` in C or `<mantis/sdk.hpp>` in C++.
The C header includes the unchanged root SDK and `semantic_views.h`.

| Identifier | Meaning |
| --- | --- |
| `MANTIS_PROJECTED_LIGHT_V1` / `org.mantis.projected-light.v1` | Optional `MantisProjectedLightV1` queried table |
| `MANTIS_PROCESSOR_V2` / `org.mantis.processor.v2` | Optional full-semantic `MantisProcessorV2` queried table |
| `org.mantis.camera.image-stream.v1` | Accessible image source |
| `org.mantis.camera.frameset-stream.v1` | Accessible camera-only FrameSet source |
| `org.mantis.camera.exposure-control.v1` | Exposure control |
| `org.mantis.camera.gain-control.v1` | Gain control |
| `org.mantis.emitter.power-control.v1` | Accessible generic OFF/ON control |
| `org.mantis.emitter.state-feedback.v1` | Accessible state feedback |
| `org.mantis.trigger.hardware.v1` | Accessible hardware trigger control/endpoint |
| `org.mantis.acquisition.projected-light.v1` | Parent supports projected-light acquisition mode |

Interface versions do **not** change `MANTIS_ABI_V1` (1). All new tables and
views have `struct_size >= sizeof(the known structure)` and `abi_version == 1`.
All enum slots are `uint32_t`; the explicit `MANTIS_*` constants in the headers
are normative. Durations/timestamps are signed nanoseconds; counters are `uint64_t`.
Unknown interface queries return NULL. Missing projected-light support disables
that mode only. Semantic IDs (`org.mantis.AcquisitionProgram`, `AcquisitionBundle`,
`AcquisitionEvidence`, `TriggerEvent`, `LaserObservation`) are schema-1 **data**
identifiers and are independent of queried-interface IDs.

No fields, values or callback meanings in root/device/packet/observation/FrameSet/
acquisition/processor/exporter v1 were changed. Existing camera-only paths remain
on `MantisAcquisitionV1`; existing pipeline processing remains on `MantisProcessorV1`.

## Borrowing, bounds and bulk ownership

All strings, arrays and nested views are borrowed only during the synchronous
call/callback. A plugin must not retain a program/input view pointer after return.
A receiver copies strings and semantic values it keeps. Arrays require a NULL
pointer exactly when their count is zero; a nonzero count requires a valid array
of at least that many elements. Required strings are NUL-terminated UTF-8, bounded
by 1024 bytes, with stable IDs bounded by 256 bytes. Every nested structured view,
including typed evidence wrappers, has its own size/version prefix.

`MantisEvidence…V1` wrappers carry `presence` and a **typed** value pointer:

| Presence | Pointer | Meaning |
| --- | --- | --- |
| `MANTIS_PRESENCE_ESTABLISHED` (0) | Required non-NULL | The value is established, including valid zero/false/OFF |
| `MANTIS_PRESENCE_UNKNOWN` (1) | Required NULL | Value not established |
| `MANTIS_PRESENCE_UNAVAILABLE` (2) | Required NULL | Evidence unavailable |

No sentinel counter/time/value substitutes for presence. Required L1
construction-only `optional` enums also use typed presence wrappers, with only
ESTABLISHED/non-NULL accepted. Unknown, unavailable or omitted required values
fail explicitly, rather than becoming OFF, success, startup or no capture. A nullable `CaptureIntent.trigger` represents the
absence of a trigger **intent**, not missing physical evidence.

The L1 limits apply before array reads: 256 program steps, 1,000,000 expanded step
instances, 64 participants (16 cameras), 64 bundle members, 4096 generic semantic
entries per table, 128 attributes, and 1,000,000 observation samples. The adapter
also bounds an entire borrowed view to 65,536 charged entries (array entries and
structured prefixes), 4 MiB of string content, and 128 MiB of referenced attribute
bytes. Plugin resource limits may be smaller. These are software resource bounds,
not timing/optical guarantees. An invalid view fails explicitly; the adapter never
truncates, sorts, renumbers, fills missing values or normalizes contradictory facts.

Image/sample attributes reuse **unchanged `MantisAttributeV1` and `MantisBuffer`**.
Buffers must be published before emission. The producer releases its own references
when done. The receiver must call host `retain` for each reference it keeps, then
`release` exactly once when finished. The host adapter retains storage in immutable
BufferViews, without rewriting pixels or headers. SDK `RetainedBuffer` provides
RAII retention. Host-to-C `PacketView` owns its buffer handles and copies immutable
bulk bytes into published host buffers; it does not create serialization bytes.

## Descriptor graph and accessible functionality

`enumerate(timeout_ms, emit, context)` emits zero or more complete
`MantisProjectedGraphV1` snapshots, one per available selected parent, at most 64.
Each snapshot contains a parent ID, up to 64 `MantisProjectedComponentV1` entries,
and a `MantisProjectedLimitsV1` capability/resource snapshot. No JSON is authoritative.

Components have stable ID, parent ID (empty only for the selected root), name,
presentation role, participant kind, capability set, and separate ID-based
`controls`, `participants` and `trigger_endpoints` relationships. The selected
parent lists all owned participating children. Nested containment is supported;
parent references must resolve without cycles. A controller can be integrated in
the parent by advertising its accessible controller capabilities, with optional
parent self-participation. Resource identity is never a transient `/dev/videoX`.

Image participants require `image-stream`. Emitter/controller children are excluded
from image selection and existing calibration discovery by capability/participant
semantics, independent of product names or presentation roles. Emitters declare
both OFF and ON; controllers declare accessible control relationships, trigger
modes/endpoints. Control targets must be emitters; trigger endpoints must be image
participants with hardware-trigger capability. Duplicate control/trigger authority
is rejected. Known pattern ID/revision and optional qualified line identities are
explicitly distinct from unknown/unavailable pattern evidence. A pattern or line
hint establishes no calibration or laser geometry.

The graph declares camera capture modes and available evidence methods/scopes.
Those lists are availability descriptions, not claims that every combination
establishes physical proof. Limits include run/ON/step duration caps, expanded step,
command, event, byte, in-flight capture and pending bundle limits, and maximum call
timeout. Watchdog/interlock/fail-OFF availability uses three-way boolean evidence;
software capability advertisement is not an independently verified physical result.
A plugin must not advertise hardware functionality inaccessible through its contract.

## Program view

`MantisAcquisitionProgramV1` is a typed, bounded view of the accepted L1
`AcquisitionProgram`. It carries schema identity, program ID/hash/content-reference
presence, explicit camera/stream/role, emitter and controller participants, ordered
steps, repetitions and all `RunBounds` caps. Each step retains its index/label,
complete emitter OFF/ON vector, selected cameras/capture mode, controller/request/
endpoint trigger intent, evidence requirement/scope, settling and maximum duration.
`terminal_policy` must be `MANTIS_TERMINAL_INHIBIT_AND_ALL_OFF` (0).

The host converts through the L1 domain model and invokes the existing validator
**before** plugin validate/prepare. It also checks participants and relationships
against the selected graph. The plugin validates its actual accessible resources;
L3 owns scheduling/preflight policy. No acquisition program bytes, on-disk schema,
MANTIS03 encoding or alternative JSON program model are defined here.

## Full immutable semantic publication

`MantisSemanticPacketV1` has a bounded discriminant and exactly one matching typed
pointer, with all other pointers NULL. Its alternatives are:

- `MantisDataPacketV1`: complete legacy data header/metadata/attributes, with only
  FrameSet → ImageFrame children (1–16). Namespaced flat data extensions retain
  their schema identity. Typed acquisition/laser semantics cannot be flattened here.
- `MantisAcquisitionEvidenceV1`: all L1 evidence, causal keys, participants,
  implementation/configuration identity, commands/acknowledgements/observed/effective
  state, exact source-frame and calibration associations, timing/clock mappings,
  disposition/reason, unresolved requests and loss accounting.
- `MantisTriggerEventV1`: full L1 event stage, controller-generation scoped native
  identity, intended/actual endpoints, timing/mapping, acknowledgement and exact
  exposure association, preserving availability.
- `MantisAcquisitionBundleV1`: exactly one evidence value, zero or one actual
  unchanged immutable FrameSet, and bounded TriggerEvents. `member_count` equals
  `1 + !!frameset + trigger_count`, never greater than 64.
- `MantisLaserObservationV1`: complete L1 source/context/lineage, dictionary,
  validity/quality flags, producer/reference and bulk sample attribute model.

There is no recursive semantic packet graph. FrameSet identity and rich headers
survive the boundary; the adapter never replaces output headers with input headers.
All converted semantic values undergo L1 validation, including trigger/evidence run
consistency, actual FrameSet validity and exposure-effective presence. Projected
`next` accepts only the bundle alternative and binds its run/program/participants
to the instance's explicit start/preparation. This is boundary validation, not
sequencing or event scheduling.

## Lifecycle, deadlines and errors

| Callback | Responsibility on success |
| --- | --- |
| `enumerate` | Borrowed complete graph/limits snapshots; no resource ownership |
| `open(host, parent, timeout, &instance)` | Own parent and all participating resources; return one non-NULL opaque handle |
| `validate(instance, program, timeout, emit, context)` | Exactly one structured `MantisProgramValidationV1` |
| `prepare(...)` | Reserve/prepare validated program/resources, exactly one validation result; rejection must not enable hardware |
| `start(instance, run_id, generation_id, timeout)` | Start the explicitly prepared run/generation |
| `next(instance, timeout, emit, context)` | Exactly one complete semantic bundle |
| `status(instance, timeout, emit, context)` | Exactly one `MantisProjectedStatusV1` |
| `abort(instance, reason, timeout, emit, context)` | Exactly one `MantisAbortOutcomeV1`, independent side path |
| `stop(instance, timeout)` | Inhibit/fence/request OFF and quiesce normal operation |
| `destroy(instance, timeout)` | Terminal cleanup/quiescence, release resources and handle |
| `diagnostics(instance, timeout, emit, context)` | Exactly one bounded human-readable diagnostic; NULL instance is permitted for discovery/open diagnostics |

Ordinary per-instance calls are serial. The plugin root/table/host must remain
alive through successful instance destruction and the end of all callbacks.
Potentially blocking calls take a **total relative deadline**: `timeout_ms == 0`
is a poll, 1–60,000 ms is finite waiting, and greater values are invalid. The plugin
must finish within that deadline, including callbacks and failure/not-ready paths;
the host must keep its synchronous callback work bounded too. Discovery/open must
also respect advertised smaller maximum call timeouts where applicable.

Returns are `MANTIS_PL_OK` (0), ERROR (1), NOT_READY (2), BUSY (3), INVALID (4),
INCOMPATIBLE (5), TIMEOUT (6). `next` returns OK with exactly one publication,
NOT_READY with none, or an explicit error with none. Other successful single-result
callbacks emit exactly one result. Rejected validation is OK with `accepted == 0`
and a structured error, distinct from a transport/call failure. `MantisContractErrorV1`
provides category and plugin-defined code; NONE requires zero code, other categories
require nonzero code. Diagnostic text is never a machine decision.

Status distinguishes OPEN/PREPARED/STARTED/STOPPED/FAILED, active run/execution
generation, current step when established, and command/evidence availability.
Execution generation is a fencing identity; sensor/controller clock generations
and native trigger generations remain separate L1 identities. A malformed view
returns host `Status::incompatible`; plugin operation failures return `plugin_failed`
(or `busy`/`invalid_argument` for those explicit outcomes). No C++ exception may
cross **any** plugin or host callback boundary.

## Abort concurrency and destruction

Abort is explicitly thread-safe while an ordinary `next` or another ordinary call
is pending. First inhibit future ON/trigger work, then invalidate/fence stale
execution-generation work, then request OFF for relevant emitters. Return structured
inhibited/fenced/request availability, scoped run/generation, per-emitter L1 evidence,
and structured initiating/cleanup outcome. A successful software OFF request never
establishes optical OFF. Unknown/unavailable feedback stays unknown/unavailable.
An already-entered synchronous callback may finish; abort cannot wait for it, normal
queue draining, recorder progress or UI/client activity. No new stale-generation
ON/trigger work is permitted after the fence.

Stop/destroy may wait for in-flight calls only within their deadline. BUSY/TIMEOUT
from destroy leaves the handle valid and owned; retry after calls finish. Successful
destroy guarantees no future callbacks. Callers prevent new calls during destruction.
Independent physical fail-OFF/interlocks/watchdogs remain necessary where hardware
requires them; this software interface provides no safety certification.

SDK `ProjectedLight` gates ordinary calls, lets abort bypass the data-plane gate,
tracks all calls, and refuses close while calls/callbacks remain active. Successful
close releases ownership. RAII destruction uses the explicit open timeout for its
bounded cleanup and keeps shared instance state alive through in-flight calls.
The runtime supplies a DSO/ownership lifetime pin. Canonical-library `Loaded`
wrappers share a single root initialize/shutdown lifetime; destroying an alias
cannot shut down a root still owned by another instance/callback. A plugin violating destroy by
refusing terminal cleanup after calls finish is quarantined with that pin/ownership
retained, rather than unloaded with live instance storage. Explicit close reports
refusal; in-process code cannot be forcibly preempted if it violates a deadline.
C++ object lifetimes still follow normal C++ caller synchronization rules.

## Explicit mode selection and runtime/SDK surfaces

A plugin may expose both interfaces. Camera-only mode explicitly opens
`MantisAcquisitionV1`; projected-light mode explicitly opens `MantisProjectedLightV1`.
The latter must never internally require a separately opened acquisition handle.
Both modes must enforce exclusive ownership of overlapping parent resources.

`Registry::devices()` keeps its image/camera behavior. The new
`Registry::projected_light_parents(timeout)` discovers graph snapshots and
`Registry::open_projected_light(plugin_id, parent, timeout)` selects projected mode.
The host prevents duplicate parent opens across adapter instances/Loaded objects
for the same canonical library path; the plugin arbitrates conflicts with its
camera-only/other handles. Isolated device streaming is still deferred.

`device::ProjectedExecutor` is a separate domain abstraction exposing graph,
validation/preparation, start, optional next bundle, status, abort, stop, close and
diagnostics. It does not derive from `ImageStream`. It owns no daemon state machine,
program scheduler, queue/backpressure policy, recorder or service DTOs.

The standalone C++ SDK provides size/version checks, `query_optional`,
`processor_v2`, `ProjectedLight`, `RetainedBuffer`, and exception-contained borrowed
callback helpers. Runtime `semantic::ProgramView`/`PacketView` own conversion
storage; decoders copy semantic values and retain bulk buffers. These runtime
converters depend downward on device/data APIs; the public SDK depends only on C ABI
concepts, never runtime/domain implementations.

`MantisProcessorV2` is defined and query/compile/identity-tested: `describe(timeout, descriptor)` uses the
unchanged node descriptor; `process(host, input, timeout, emit, context)` consumes a
full immutable semantic view and emits exactly one full semantic output on success.
L6 owns the pipeline adapter, full-packet processing integration and synthetic
LaserObservation producer. The v1 processing path is unchanged.

## Contract evidence

`tests/contract/projected-light/plugin.cpp` is a test-only SDK DSO with arbitrary
parent/camera/emitter/controller IDs. It implements both acquisition modes, finite
limits, prepare/start/status, evidence-only/FrameSet/trigger publications, explicit
Unknown/Unavailable, not-ready/failure, concurrent abort and bounded cleanup.
It scripts publications to prove the interface and implements no timing sequencer.
It is built outside the production plugin discovery directory.

Tests cover malformed prefixes/pointers/counts/graphs/presence/programs, publication
cardinality, wrong semantic kinds, invalid FrameSets/L1 evidence, run mismatches,
retention/release balance, mode ownership, deadlines, abort during pending next and
destroy refusal during active callbacks. Frozen v0.1 and pre-L2 acquisition C DSOs
are compiled independently; portable comparisons check every frozen table field's
size/alignment/offset. `tests/contract/v1/plugin.h` remains unchanged.

`generate_views.py` is an explicit reviewed L2 field catalog, not a parser that
silently tracks future domain changes. Regenerating its checked-in C declarations
and converters must not change published ABI layouts or values without an additive
versioned interface decision. It defines no on-disk bytes.

## Local validation evidence

On x86_64 with GCC 15.2, the complete headless non-hardware CTest suite passed
**38/38**. This includes projected-light-contract, c-abi, legacy-abi,
projected-light-semantics, core, acquisition, acquisition-qos and existing camera/
calibration/recording regressions. `git diff --check` and
`python3 tests/contract/boundaries.py .` passed.

ASan + UBSan with leak detection and halt-on-error passed **6/6** focused tests:
projected-light-contract, c-abi, legacy-abi, projected-light-semantics, acquisition
and acquisition-qos. The final fixture changes were retested under both ordinary
and sanitizer builds. No use-after-free, undefined behavior or leak was reported.
Native ARM64/cross-toolchain execution was unavailable locally; those results are
not claimed. L2 acceptance remains pending.

Build/test configuration used `MANTIS_BUILD_STUDIO=OFF`, existing OpenCV 4.10 SDK/
runtime dependencies, and `/usr/bin/python3` with protobuf. Missing imgcodecs SDK
headers were staged under the ignored worktree build directory; library search
used the matching existing OpenCV build. Test temporary directories stayed under
the worktree. No external dependency/user files were modified.
