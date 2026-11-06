# CAL-01 proposal: versioned live ChArUco observation sources

**Status: PROPOSAL — NOT ACCEPTED; design only.** Tracks [issue #65](https://github.com/martinkoenig/mantis-studio/issues/65),
CAL-M8-1. No interface described as proposed below exists by virtue of this document.
No accepted ADR is amended, no production behavior changes, and M8 remains pending.
Conflicting implementation requires independently accepted architecture updates first.

## 1. Evidence baseline and constraints

All CONFIRMED source claims in this proposal refer to commit
[`c11dd3dc6204872720b061668fa4d769e3b62389`](https://github.com/martinkoenig/mantis-studio/commit/c11dd3dc6204872720b061668fa4d769e3b62389).
The repository paths in the following inventory resolve at that commit. They are
evidence, not declarations that a proposed replacement has been delivered.

| Evidence | Exact source path at the baseline / confirmed behavior |
| --- | --- |
| S1 | [services.cpp](../../src/services/services.cpp), `Runtime::start_capture`, lines 169–318: checks ordinary/projected camera ownership, validates/snapshots active calibration, creates RawCapture before constructing Session. `Runtime::build_calibration_dataset`, lines 785–816, requires finalized schema-2 RawCapture inputs and schedules M3/M5 through Jobs. |
| S2 | [device_runtime.hpp](../../src/device-runtime/include/mantis/device_runtime.hpp), `Session`; [device-runtime.cpp](../../src/device-runtime/device-runtime.cpp), constructor/stop: daemon-owned producer, 32-entry LOSSLESS writer queue, one-entry latest-only preview, immutable Published buffers. Admission waits up to 50 ms then fails explicitly; first readiness currently means first writer callback success, not durable finalization. |
| S3 | [X1 acquisition](../../plugins/first-party/devices/x1/acquisition.hpp), `start`, `observation`, `next`, and [profile](../../profiles/x1-q6a.json): composite parent, exact physical role/identity metadata, native counters/clocks, bounded pairing/lookahead, one acquisition copy, Y10P packing. Profile requests paired 1280×720, 120 FPS, software correspondence ≤5 ms. |
| S4 | [capture_calibration.cpp](../../src/services/capture_calibration.cpp), `discovered_calibration_component` / `CaptureCalibrationBinding::stamp`: strict role/identity/dimension parsing, source parent/child agreement, original source-reference stability, snapshot stamping with shared pixel storage. Its current provenance destination requires a RawCapture artifact. |
| S5 | [calibration_observation.hpp](../../src/calibration/include/mantis/calibration_observation.hpp), `ObservationSource`, `TargetObservation`, `intersect_observations`; [M2 adapter](../../src/calibration-opencv/calibration-opencv.cpp), `detect_target`: successful absence differs from error; physical ChArUco IDs and millimeter geometry are preserved. Existing ObservationSource requires a real RawCapture ID. |
| S6 | [calibration_dataset.hpp](../../src/calibration/include/mantis/calibration_dataset.hpp), `FrameSetKey`, `DatasetAnalysisConfig`, `select_dataset_samples`; [M3 builder](../../src/calibration-dataset/calibration-dataset.cpp), `build_calibration_dataset`: finalized RawCapture schema 2 only, sequential CaptureReader, exact role/camera/optical-frame consistency, complete compact records, deterministic per-camera maximin selection. |
| S7 | [calibration_solve.hpp](../../src/calibration/include/mantis/calibration_solve.hpp) and [solver](../../src/calibration-solver-opencv/calibration-solver-opencv.cpp): independent mono, selected-population held-out split, fixed-intrinsic ChArUco-ID stereo and separate training/held-out/final evidence. Solver consumes corners, not pixels. |
| S8 | [artifact API](../../src/calibration-artifacts/include/mantis/calibration_artifacts.hpp), [codecs](../../src/calibration-artifacts/codecs.hpp), [persistence](../../src/calibration-artifacts/calibration-artifacts.cpp): four schema-1 artifact types, strict canonical CalibrationDocument schema 1, exact upstream hashes, immutable reserved revisions and typed graph validation. Current readers reject other schemas. |
| S9 | [service DTOs](../../src/services/include/mantis/calibration_service.hpp), [protocol](../../protocol/protobuf/mantis.proto), [adapter](../../src/protocol/calibration_adapter.cpp), [C++ client](../../src/client/include/mantis/client.hpp), [Python client](../../sdk/python/mantis/calibration.py): DatasetBuild carries raw_capture_artifact_ids; compact controls/inspection exclude full populations/pixels. |
| S10 | [Studio controller](../../apps/studio/calibration_controller.cpp), [public client seam](../../apps/studio/calibration_client.hpp), [workspace](../../ui/workspaces/CalibrationWorkspace.qml): record/finalize RawCaptures before dataset building, public API only, transient UI state, asynchronous requests and separate activation. |
| S11 | [replay.cpp](../../src/device-runtime/replay.cpp), `RecordedSource`, and [CaptureReader API](../../src/artifact-store/include/mantis/artifact_store.hpp): historical image-only RawCapture 1/2 replay reads recorded packets; no detector rerun, camera re-pairing or current active-binding lookup. |
| S12 | [M8 procedure](../hardware/x1-geometric-calibration-validation.md), [report schema](../validation/x1-calibration-report-schema.md), [validation harness](../../tools/validate_x1_calibration.py): real evidence, unchanged measured target/rig, independent captures, daemon restarts and immutable lineage; fixture results cannot establish real M8 acceptance. |

**ACCEPTED contracts:** [Architecture authority](../../ARCHITECTURE.md), frozen
[Architecture v1](../../MANTIS_STUDIO_ARCHITECTURE.md),
[ADR-021](../adr/021-enumerated-acquisition-and-frameset-contract.md),
[023](../adr/023-acquisition-preview-leases-and-storage-jobs.md),
[025](../adr/025-packed-monochrome-image-byte-layout.md),
[026](../adr/026-bounded-software-observation-pairing.md),
[027](../adr/027-calibration-target-geometry-and-physical-scale.md),
[028](../adr/028-target-observations-and-deterministic-calibration-datasets.md),
[029](../adr/029-calibration-solve-validation-and-stereo-policy.md),
[030](../adr/030-calibration-artifacts-revisioning-and-active-binding.md),
[033](../adr/033-reliability-performance-release-gates.md),
[035](../adr/035-calibration-control-api-and-job-orchestration.md),
[036](../adr/036-studio-calibration-workspace-uses-public-calibration-api.md), and
[037](../adr/037-real-hardware-calibration-acceptance-is-evidence-driven.md).
The [v0.3 baseline](v0.3-geometric-calibration.md) and M2–M7 implementation contracts
remain authoritative. Software correspondence is not synchronized exposure;
clock uncertainty, physical trigger accuracy and optical quality must not be invented.

**REPORTED:** the operator observed a Studio freeze after approximately 1–2 seconds
of recording on Q6A microSD and has printed A3 ChArUco targets. **HYPOTHESIS / OPEN:**
storage throughput is the freeze's complete cause. S1/S6 confirm the RawCapture-only
calibration path; they do not diagnose the reported freeze. [SIM-01B #93](https://github.com/martinkoenig/mantis-studio/issues/93)
qualified explicit software overload/failure recovery, while sustained 120 FrameSets/s /
276.48 MB/s remains **NOT QUALIFIED**. That is neither microSD causal evidence nor
UI responsiveness, laser safety or physical calibration evidence.

[PR #97](https://github.com/martinkoenig/mantis-studio/pull/97), inspected at
`fcd7e1ca8f52e7763096aa68ea619216c18c232c`, is **UNMERGED / PROVISIONAL**.
Only its public `docs/plugin-development/virtual-rig-scene.md` diff was inspected
for future integration context. Its synthetic scene does not establish a ChArUco
fixture or hardware acceptance. This proposal depends only on the baseline above.

## 2. Alternatives and recommendation

| Alternative | Feasibility / benefits | Costs and limits |
| --- | --- | --- |
| A — bounded observation-source session, then dataset | Acquire through the existing daemon device runtime in an additive observation consumer mode. Keep all admitted compact corner records in bounded RAM; finalize an immutable source artifact on explicit Finish; build/select a Dataset separately. Removes continuous RawCapture writes and retains a complete admitted population for selection audit. | Unfinished RAM evidence is lost on crash; loss must be explicit. Requires new source identity, versioned dataset/solve lineage and observation-mode lifecycle. Detector cannot be rerun without optional pixels. |
| B — bounded compact observation journal, then dataset | Same daemon acquisition/admission/detection, but append compact records to a separately typed, checksummed journal on a bounded writer branch. Final immutable source manifest references committed chunks. Useful for longer sessions and crash recovery without RAW images. | New durable-prefix protocol, journal reader, storage queues and interrupted-commit evidence. Slow storage stops observation collection explicitly when bounded backlog fills; it never blocks the camera producer. Greater storage/recovery implementation surface. |
| C — sparse selected-pair image source | Persist only explicitly admitted pairs in a new sparse image-source type, then reuse detection offline through a typed new reader. Feasible diagnostic option; permits detector reruns. | Persists RAW images, so does not meet normal observation-only mode. Cannot reuse a RawCapture ID/schema to imply continuous acquisition. Only a separately authorized forensic mode. |

**Recommendation (not adoption): A**, with an immutable compact source separate
from selection/solving. The short stationary-pose M8 workflow can use bounded sessions;
loss of unfinished work is preferable to introducing an unnecessary journal contract
in the first package. B is viable if recovery of unfinished observations becomes
an accepted requirement. Neither alternative changes camera ownership or RawCapture.
Do not implement both persistence strategies behind a hidden fallback.

## 3. Ownership and lifecycle

Propose an explicit `observation_only_v1` consumer mode in the **same** daemon
`device::Session` ownership path (S1/S2), not a second acquisition runtime or a UI
camera opener. Refactor only the consumer policy/readiness seam after decision
approval: existing recording mode retains its 32-entry LOSSLESS queue, 50 ms failure
semantics, startup, diagnostics and RawCapture finalization behavior unchanged.
An observation-only start never calls Store::begin for RawCapture and never supplies
a dummy RawCapture ID or a no-op recorder to simulate the mode.

The runtime must check ordinary captures, live observation sessions and projected
camera reservations symmetrically using the actual parent/component resources.
Resolve/snapshot discovered role/physical identity/geometry, selected target and
active calibration before acquiring the lease. Start exactly the selected composite
parent through the existing acquisition interface; reject separately opened children
as a means to synthesize a pair. Device refresh/project close cannot invalidate
streams with active owners. A client disconnect never transfers or cancels ownership.

Proposed observable state machines (names are new):

```text
source:      RESERVED -> STARTING -> READY -> DRAINING -> RETIRED
analysis:    WAITING -> ADMITTING -> DRAINING -> SEALED -> RETIRED
publication: NOT_ENTERED -> PERSISTING -> FINALIZED | FAILED_RECOVERABLE
operation:   QUEUED -> RUNNING -> COMPLETED | FAILED | CANCELLED
```

READY means a first structurally valid paired FrameSet was received, not that a
target was found, persisted or accepted. Finish closes admission at a producer
barrier, records the last parent sequence, stops/drains the owned source, processes
the bounded admitted tail, seals counters, validates, checks cancellation, then
enters persistence. Stop acceptance is not completion. COMPLETED/ACK includes the
final artifact ID/hash only after Store finalization and readback validation.
Dataset creation is a separate Job; no automatic mono/rig solve or activation.

Cancel before persistence closes admission and discards queued/in-flight results
as explicitly counted cancellations; no finalized source is published. Detection
may be non-preemptive within one call. To contain a hung/crashed detector, recommend
one daemon-supervised **analysis helper process**, with no device/Store access:
one bounded paired request and response, immutable input mapping, explicit request
generation, and termination/reaping on a fixed deadline. This is an internal analysis
boundary, not an alternative capture owner or public Plugin SDK change. Copy/export
details and deadlines require qualification (section 9); do not claim the current
BufferView already exports process handles. M4 solves keep ADR-035 semantics.

Cancellation observed before the single persistence-entry linearization point
prevents publication. Once entered, M5 persistence is non-preemptive: completion or
write failure wins, even if cancellation arrives later; the UI shows finishing.
Timeout/disconnect is not cancellation. Original acquisition/detector/integrity/I/O
failure wins over a later Cancel/cleanup failure. A serialized terminal-state latch
records the first failure; concurrent reports use a fixed source→analysis→persistence
phase order at sealing. Record phase and bounded secondary diagnostics, never replace
the initiating error with an assertion, logging, termination or unlink error.

All failure/unwind paths close admission, release test/IPC gates, stop only owned
source resources, retire callbacks, terminate/reap the owned helper, drop mappings
and return leases exactly once. Helper callbacks cannot access a destroyed session.
An outstanding entered Store operation remains owned until it returns; a storage
stall is shown as incomplete, never falsely ACKed or declared cleaned up. A finite
worst-case OS I/O completion bound is **OPEN**, not supplied by ADR-035. Tests bound
hangs and prove cleanup for controlled faults; they cannot certify arbitrary kernel
or hardware failures. No retry-until-success or silent restart is allowed.

## 4. Proposed versioned identity

Introduce pure `CalibrationSourceRefV2` as a tagged union, never overload
`raw_capture_id`: `{kind: raw_capture, artifact_id, hash}` or
`{kind: live_observation, source_identity, artifact_id, hash}`. The live artifact
ID/hash become available only on finalization; an ephemeral operation handle cannot
serve as an immutable reference. Existing FrameSetKey/ObservationSource remain v1.
New `FrameSetKeyV2 = (source_kind, source_identity, parent_frameset_sequence)` and
`ObservationKeyV2 = (FrameSetKeyV2, exact_camera_role, physical_camera_id)` use
lexicographic UTF-8 byte order then unsigned sequence order. RawCapture adapters
use their real artifact ID as source_identity and preserve v1 order exactly.

Live identity version 1 contains:

- A daemon-incarnation random 128-bit nonce, a new per-acquisition random 128-bit
  nonce, and a monotonically increasing generation within that daemon. Start uses
  a client idempotency nonce: a retry returns the same operation; changed parameters
  with that nonce fail. Restart allocates a new incarnation, even after PID/time reuse.
  CSPRNG failure is fatal. Neither wall time, PID, project path nor counter alone
  establishes uniqueness. Reject repeated identities in retained sources/imports;
  an identical immutable import may be referenced, but never counted as new evidence.
- Exact acquisition parent logical ID/plugin ID/version, parent descriptor hash,
  ordered child IDs and physical `left`/`right` identities/roles, profile hash,
  mode and geometry. Preserve exact role strings; uppercase labels are UI text.
  Video-node paths and discovery order are not identities.
- Original source calibration reference and captured active-binding snapshot,
  including schema/revision and backing artifact hash when present. Parent/child
  source references must agree and remain stable. Missing ID stays absent; no
  current-binding lookup may fill it later. Reuse S4 validation, with the new source
  document as provenance destination, leaving the RawCapture path unchanged.
- Parent FrameSet sequence and each child native sequence; parent and per-child
  DeviceTimestamp (signed nanoseconds, exact clock-domain ID/name), received time
  with daemon-incarnation host clock scope, sync group/quality, pairing policy/mode,
  tolerance, selected delta and exclusion records/counters from the producer.
  Preserve timestamp-source/flags where supplied. Clock-mapping/uncertainty and
  optical exposure skew are optional typed evidence with `unknown`/`unavailable`
  reason, never numeric zero as a substitute for unknown. No inferred hardware sync.
- Image width/height, exact optical frame ID/name, packing/layout version, row
  stride and delivered byte extent. The original packet and child geometry must
  match discovery; a crop/mode identity supplied by a device is retained rather
  than guessed from equal dimensions.
- Exact Target artifact ID/hash and target identity/revision, nominal pattern,
  dictionary/layout, measured active extents, optional provenance/uncertainties and
  effective millimeter geometry. Record generator/vector-target hash and operator
  session evidence separately; do not infer physical measurements from pixels.
- Detector implementation ID/version, actual linked OpenCV version, Mantis build,
  effective options, preprocessing/layout conversion version and analysis-config
  hash. For existing ChArUco: CORNER_REFINE_NONE, minMarkers=2, homography interpolation
  with no supplied intrinsics/distortion, exact pattern-layout capability. Record
  effective defaults, not just user overrides. Unsupported layout is an error.
- Admission-policy version/configuration, descriptor/selection/split/solver versions,
  source seal/finalization status, retained-evidence inventory and provenance hashes.

A mutable counter snapshot is operational evidence only. Final source evidence
is immutable and hash-verified. Hashes establish byte integrity and reuse detection,
not authenticity of a physical camera or proof that an operator moved a board.

## 5. Minimal additive artifacts and validity

Proposed storage versions are independent from app, protocol and project versions:

| Proposed object | Fields / meaning |
| --- | --- |
| `org.mantis.CalibrationObservationSource`, artifact schema 1 (new type) | One immutable bounded live session: source identity v1; exact target reference/content; device/camera/clock/calibration snapshots; detector/preprocessing manifest; admission config/ledger; every admitted per-role analysis record; terminal counters/boundary evidence; optional explicit forensic references. No pixels in normal mode. |
| `TargetObservationV2` (embedded pure value, no separate per-frame artifact) | ObservationKeyV2, target identity/revision/type, dimensions, sorted physical point IDs, paired image points px / object points mm, detection evidence, per-image pixel-evidence hash and timestamp/geometry provenance reference. No image ownership or mutable current-state reference. |
| `org.mantis.CalibrationDataset`, artifact schema 2 (new documents only) | Reserved logical revision, exact target reference, sorted typed source references, analysis config v2, exact cameras, complete compact admitted analysis population, descriptors and per-camera selection ranks. Links each record to its source record digest. No spoofed raw_capture_ids field. |
| `org.mantis.CameraCalibration` / `org.mantis.RigCalibration`, artifact schema 2 | Same numerical models and three-stage M4 evidence, but typed source keys in partitions/poses/residuals and exact schema-2 dataset dependencies; Rig retains exact camera/target refs and explicit LEFT/RIGHT slots. |
| `org.mantis.CalibrationDocument`, packet schema 2 | Canonical version-dispatched envelope for new source and schema-2 calibration documents. Target remains artifact/document schema 1 and is referenced without rewriting. |

The new source is required: a Dataset alone cannot audit admission/restarts, source
reuse and failure counters independently of a particular selection configuration.
One source may produce multiple analysis datasets; that creates **no independence**.
Initial scope accepts live-only sources in a v2 Dataset; mixed live/RawCapture
datasets are rejected until their selection ordering and evidence semantics are
separately qualified. A pure v1 adapter is for equivalence tests/read-only views,
not a migration that writes fictional sources.

Per admitted FrameSet retain exactly one record for each requested role. Distinguish
`detected`, `no_target`, `not_processed_cancelled`, `detector_error`, `missing_frame`,
`inconsistent_source` and `unknown` with typed reason. Only detected records have
nonempty points/descriptors; zero corners means successful absence. Detector/layout/
clock/identity errors never become no_target. Missing required frames, duplicate roles,
inconsistent source and detector errors fail the session; diagnostic evidence can
remain incomplete/recoverable, but cannot be solved. Unknown essential identity,
target geometry or time correspondence also prevents a finalized usable source.
Unknown optical uncertainty/quality alone does not invent an additional quality gate.

Validators check finite/range-safe arithmetic, matching array lengths and counts,
unique IDs within the exact target, M1-scaled object points, matching source/camera/
target references, canonical strict key order, exact role coverage, stable geometry,
clock consistency and observed parent/native continuity. Native exclusions remain
ADR-026 exclusions, not native loss. A parent gap before admission is source loss;
expected gaps between admitted samples are explained by the ledger. Repeats/reversals
are failures. Bad discovery geometry fails before lease/start. Strict mode remains
strict; neither absence handling nor deliberate sampling relaxes source integrity.

Hash scope: SHA-256 plus algorithm identifier for canonical source metadata/records,
target/config manifests and per-admitted-image evidence. Image evidence hashes a
domain-separated canonical layout tuple plus **all delivered packed/RAW8 row bytes,
including padding**, computed while mapped before release. The exact detector u8
input also gets a separately named hash (Y10P top-eight-bit conversion v1); never
confuse it with the original-pixel hash. Hash work runs only on admitted images,
outside the producer. Unknown/unprocessed images have a reason and null hash.

Canonical documents extend S8 rules: sorted unique UTF-8 object keys, strict known
fields/enums, finite round-trip doubles, explicit null absence, exact ID/frame names,
canonical source/record/point ordering and checked counts. Store hashes cover the
whole immutable packet; no self-hash. Component digests exclude themselves using
an explicit domain/version and listed field scope. The session identity is content
here; document packet header stays neutral. Full-source hashes and image hashes
are retained; neither hashes alone nor equal corners imply physical independence.

At Finish, seal and validate RAM evidence before reserving/writing source revision
and a single document through generic Store. Reserve once, append once, finalize
once; acknowledge only validated finalized state. Failed allocation creates no
success; a reserved number is never reused. EIO/ENOSPC/short-write/interrupted finalization
leaves the existing incomplete/recoverable state and exact initiating error.
Recovery may finalize only a complete, hash-verified **sealed** document. A crash
before seal loses RAM observations explicitly and cannot turn a prefix into a
completed session. Recovery never invents counters, source end or target detection.
No automatic resume across daemon incarnations. Upstream artifacts stay immutable.

## 6. Admission, selection and solving

Propose admission `paired_stride_latest_v1`, independent of diversity selection:
first valid parent sequence `s0` anchors eligibility; `(s-s0) mod stride == 0` is
eligible, with stride 60 as an initial explicit 120-FPS experiment. Parent sequence,
not wall time/UI polling, controls eligibility. Producer continuously validates
continuity and updates constant-space counters; ineligible pairs retain no pixels.
An eligible pair replaces the one pending candidate atomically; the evicted key
and reason `candidate_replaced` enter a bounded ledger. Never evict an in-flight pair.
When the helper is ready, dispatch the latest pending pair with a monotonic request
ordinal. Retain each admission/dispatch/drop decision; stop before the fixed ledger
limit rather than silently truncating history. This is declared lossy **candidate**
admission, not lossless acquisition or a claim to detect every board appearance.

Scheduling affects which candidates are processed. Do not promise that a second
live run, or a differently scheduled replay of input pixels, produces the same
admitted population. Determinism means: the same finalized source population,
target and config yields exactly the same descriptors, selected keys/ranks and
partitions. Persisted scheduling decisions explain differences. A new online
reservoir or selection on only a moving retained subset would require a new
selection policy and proof; it is not selection v1.

Retain **every completed admitted detection and absence**, including unselected
observations, until the configured finite source bound. Dataset schema/config v2
uses descriptor v1 (eight components), checked llround×1,000,000 quantization,
overflow-checked squared distances, per-camera greedy maximin seed/ties and ranks.
Because key type changes, name selection policy **v2** explicitly: numerical rule
equals v1, canonical tie key is ObservationKeyV2. Approval requires exact key/rank
equivalence under order-preserving legacy embedding, plus tie/permutation/overflow
and cross-architecture fixtures. Never assert compatibility just from equal counts.
There is no automatic quality PASS or recommended core selection budget.

M4 numerical/eligibility/split policies remain version 1 after a key-generic pure
adaptation: independent mono uses all eligible selected observations per camera;
held-out formula and no-training-leakage stay unchanged. Stereo considers the
FrameSet union selected by either side, ordered by min/max ranks then FrameSetKeyV2;
both sides' **unselected** completed detections must therefore remain available.
Join ChArUco physical IDs, never vector positions. Retain missing-detection and
insufficient/degenerate dispositions. Keep fixed final intrinsics for stereo;
retain training, held-out and final refit separately. Plain Checkerboard remains
mono-only; initial live collection accepts ChArUco only and does not remove legacy
Checkerboard support.

Saved corners support reproducible **re-solving**, subject to recorded solver build
and existing floating tolerances. Normal mode cannot rerun the **detector**, reconstruct
pixels, verify image hashes against absent originals or replay camera imagery.
That limitation must appear in inspection/export/UI. Separately explicit forensic
retention would need a byte/frame quota, consent before collection, immutable image
references and cleanup/failure contract; it is off and outside the initial proposal.

## 7. Compatibility and exact module migration map

| Affected boundary (future work only) | Minimum additive change / protected behavior |
| --- | --- |
| `src/device-runtime/{include/mantis/device_runtime.hpp,device-runtime.cpp}`; `src/services/{services.cpp,projected_sessions.cpp}` | Consumer mode/readiness seam and symmetric shared camera reservation checks; bounded sampler branch. Existing recording mode and projected ownership semantics unchanged. No plugin acquires cameras for the detector. |
| `src/services/capture_calibration.cpp` and its public internal header | Extract/reuse pure snapshot consistency validation; new source provenance sink, not RawCapture-only initialization. Stamp/check once per session without producer DB work or pixel copies. |
| `src/calibration/include/mantis/{calibration_observation.hpp,calibration_dataset.hpp,calibration_solve.hpp}` | New v2 key/source/observation/dataset/result values and strict validators. Factor source-independent numerical policy without changing v1 models/meaning. Foundation remains Qt/OpenCV/Store/JSON/Protobuf/filesystem-free. |
| `src/calibration-opencv` and `src/calibration-dataset` | New v2 observation entry/adaptation and bounded helper wrapper; effective detector manifest. Reuse layout/GrayImageView and geometry logic. Existing RawCapture builder and v1 detector API remain. No fabricated RawCapture input to old validate_observation_source. |
| `src/calibration-solver-opencv` | Explicit v2 dataset/result overloads; same mono/stereo math/splits. No Store, detector, acquisition or ArUco dependency enters solver. |
| `src/calibration-artifacts/{include/mantis/calibration_artifacts.hpp,codecs.hpp,calibration-artifacts.cpp}` | Version-dispatched new type/schema codecs, source graph/hash validation and schema-2 create/load APIs. Existing strict schema-1 readers/encoders remain byte-compatible; unknown versions fail explicitly. |
| `src/artifact-store` | Generic immutable Store is reused, not a new pixel database. Confirm type/schema-aware revision reservation and source import/reuse validation. Any uniqueness index or migration needed after inspection requires an explicit transactional migration proposal; do not silently repurpose current tables. |
| `src/services/include/mantis/calibration_service.hpp`; Runtime | Add live operation DTOs, capability and compact inspection; separate v2 Dataset build/solve routing. Existing DatasetBuild semantics stay RawCapture-only. |
| `protocol/protobuf/mantis.proto`; `src/protocol/calibration_adapter.cpp` | New messages/unused command/response tags and version-capability fields; compact bounded evidence only. Do not renumber, reinterpret or reuse any existing tags, including projected-light additions. |
| `src/client`; `sdk/python/mantis/calibration.py`; `apps/cli/calibration_commands.*` | Add explicit live-source start/status/finish/cancel and v2 build methods with bounded collections and typed presence. No Store/OpenCV/runtime linkage. |
| `apps/studio/calibration_{client.hpp,controller.hpp,controller.cpp}`; `ui/workspaces/CalibrationWorkspace.qml` and `ui/calibration` | Public client mode choice, asynchronous live state/counters, immutable-source selection and exact-reference resume. No detector or lineage store in QML. |
| `tools/validate_x1_calibration.py`, `tests/integration/x1_calibration_evaluator.cpp`, report validators and `scripts/validate-x1-calibration.sh` | Separately versioned live evidence mode, independent source verification and explicit unknown replay result; existing RawCapture M8 validation unchanged until equivalent contract accepted. |

No rewrite/migration of old schema-1 CalibrationDatasets, Targets, Camera/Rig objects,
RawCapture schema 1/2, historical packet headers or hashes. (Schema-1 **dataset** is
not schema-1 RawCapture ingestion: M3 accepts RawCapture 2 only.) Preserve existing
project metadata schema 2 and manifest 1 unless a separately reviewed additive
index migration is necessary. Start new schema-2 calibration series initially;
do not reuse a v1 series across schemas without explicit reservation proof.

Activation of a new schema-2 Rig needs explicit version-dispatched graph/device
validation and schema-aware calibration references in future packet snapshots.
Existing active-binding records and captures keep their exact ID/schema/revision.
Old clients/readers must reject unsupported schema-2 info/references, never decode
them as schema 1. Version capability prevents offering live mode to an old daemon.
Export/import preserves identity and hashes; changing a container path/ID cannot
manufacture independent source content. RecordedSource remains an image source;
observation-only artifacts are not advertised as RawCapture or image replay.

## 8. Proposed public API and Studio workflow

API names below are **proposed**, not currently callable:

| Operation | Compact contract |
| --- | --- |
| `start_calibration_observations(LiveObservationStartV1)` | Exact device/Target IDs, exact roles, admission/bounds/config, idempotency nonce; reserve capacity before acquisition, return operation ID and generation-scoped handle. Start is not readiness or persistence ACK. |
| `calibration_observation_status(handle)` | Source/analysis/publication states, source-ready flag, queue high-water, phase/error and counters; no points or pixels. Stale incarnation/generation rejected. |
| `finish_calibration_observations(handle)` | Idempotently close admission and request sealed-source publication; return/retain normal Job ID, whose result is exactly one source artifact. No synchronous wait for CV/storage. |
| `cancel_calibration_observations(handle)` / ordinary Job cancellation | Same cancellation latch and persistence-entry semantics; no second cancellation authority. Client timeout changes no daemon state. |
| `build_observation_calibration_dataset(DatasetBuildV2)` | Finalized typed live source references, exact Target and analysis config v2/selection v2; ordinary Job produces one Dataset schema 2. |
| `calibration_info` / solve operations | Version-capability-aware schema-2 compact info and explicit overload/message routing; exact dependency IDs, separate camera and rig Jobs, explicit activation. |

Reuse normal bounded Job execution/history, but reserve the single live analysis
slot before device start; queued Jobs cannot hold camera leases. Wire allocation
must select genuinely unused tags after inspecting current descriptors, update the
protocol compatibility test and preserve the 4 MiB control-frame limit. Propose
≤16 KiB compact status, coalesced at ≤2 Hz; full records/ledgers travel through
artifact data-plane access, never status RPCs. Unknown enums/versions fail; absent
uncertainties/provenance remain absent across C++/Python/Protobuf.

In Devices/System → Calibration, retain seven freely navigable stages. Captures
offers explicit **Live observations (no images saved)** versus **Recorded captures**,
gated by daemon capability. Target form preserves measured active extents, dictionary/
layout and provenance presence; require explicit operator target/physical-role review.
Start shows readiness and admitted/detected/no-target/drop counts descriptively.
Finish shows draining → persisting → finalized; Dataset becomes selectable only
after finalization. Cancel shows its acknowledged terminal outcome. Returning to
another stage or disconnecting does not erase/cancel daemon work. Reconnect queries
the exact handle; restart shows interrupted work and immutable finalized sources,
not fictional continuation. Dataset, independent cameras, Rig and explicit activation
then compose the existing public workflow.

Use one outstanding status request, coalesced refresh, bounded counters and small
overlays/coverage metadata. Do not add a copied full-resolution UI stream or an
unbounded per-frame UI queue. Local preview may reuse ADR-023 leases only when
explicitly requested; remote sampling does not require it. Current local preview
file transport is not a remote transport promise. Show storage/detector errors and
loss categories distinctly, keyboard/focus/scroll behavior and accessible status
text; no autoquality green badge, license gate or claim of measured accuracy.

## 9. Proposed budgets, observability and failure containment

These are **proposed engineering budgets**, not measured Q6A guarantees or quality
defaults. Caller-selected positive selection budgets remain independent.

| Boundary | Initial bounded proposal / rationale |
| --- | --- |
| Input | 2×1280×720×1.25×120 = 276,480,000 bytes/s packed payload (276.48 MB/s decimal); one pair is 2,304,000 bytes before padding/metadata. Driver strides/extents determine actual allocation. Acquisition/pairing costs still exist without recording. |
| Admission | Explicit stride 60, nominal 2 candidate pairs/s; one pending + one in-flight pair. Replacements counted by key. No producer waits for detector/UI/storage, hashes, conversion or helper IPC. Atomics/bounded nonblocking handoff only; failure to hand off is a counted candidate drop. |
| Helper | One supervised request, one bounded response; selected-pair export may make one extra 2.304 MB copy per dispatch, about 4.608 MB/s at nominal admission. Proposed 2 s request deadline, one helper per daemon, 512 MiB process address-space ceiling subject to runtime-library/ARM qualification. No automatic retries. |
| Pixel ownership | Sampler retains ≤2 parent pairs in daemon plus ≤1 exported pair in helper (6.912 MB packed payload total), one reusable sequential 0.9216 MB u8 scratch, bounded IPC headers. These exclude existing producer/lookahead/preview and OpenCV internal memory; measure their total separately. Input padding/extent caps must be validated before retention. |
| Session evidence | Max 300 s, 600 eligible pairs, 600 processed pairs, 1,200 per-camera records; ≤512 points/record, ≤32 KiB encoded record, ≤64 MiB compact source RAM, ≤128 MiB encoded document. Reject oversized target/config before start. Stop admission at the first bound; never drop older compact evidence to make room. |
| Ledger | At most 600 eligible decisions and 4,096 compressed continuity/exclusion entries; bounded reason counters, uint64 checked overflow. Continuous ineligible runs are ranges; alternating gaps cannot allocate indefinitely. Overflow stops/fails with inspectable incomplete evidence. |
| Storage/UI | No normal acquisition image writes. Finalize one compact source outside acquisition; reserve encoding capacity before starting. One persistence operation per live slot, bounded status payload/refresh. Slow Store cannot accumulate more sessions/queues; slot remains unavailable until entered persistence retires. |

Producer validates every received paired packet's sequence and required source
metadata even when not admitted. Use cached validated descriptors/hashes, not
per-frame JSON serialization/DB queries; validate changed supplied metadata
explicitly. Resource limits are reserved before acquisition. If the existing
nonblocking queue primitive cannot support truthful bounded ledger handoff, choose
a fixed ring with reservation proof in implementation, not an unbounded log.

Accounting must reconcile at seal:

```text
published_pairs = policy_skipped + eligible_pairs
eligible_pairs = dispatched + candidate_replaced + candidate_rejected + cancelled_pending
dispatched = completed_pairs + cancelled_inflight + analysis_failed_pairs
completed_camera_records = detected + no_target
detected = selected + detected_unselected                 (per Dataset, per role)
```

Before sealing, include current pending and in-flight gauges in these equations;
the terminal equations above apply only after each gauge has reached zero. A normal
Finish drains the pending tail; Cancel records it as cancelled_pending. A detected
pair may have no_target on one or both sides; completed_pairs counts processing,
not successful target detection. Source/native
loss, pairing exclusions and unobserved intervals are separate; a missing native
frame cannot be hidden in policy_skipped. Persisted-record rejects/write failures
are separate from candidate drops. Source integrity failures preclude usable source
publication; candidate sampling loss is declared, visible and subject to M8 review.
Counter snapshots are monotonic with generation and sample time; never imply all
physical exposures were observed. Incomplete evidence says which equalities are
not yet closed.

Record bounded phase times (ready/admit/dispatch/detect/seal/persist/finalize), queue
high-water, copy/byte counts and primary/secondary failure. Later owner-authorized
hardware characterization should compare acquisition-only, observation sampling
and explicit recording under controlled conditions, observing UI heartbeat, source
continuity, latency, CPU/RAM, thermal state and write latency without privileged
instrumentation. This package performs **none** of those operations. Removing
RawCapture writes is a design response to load, not a proven fix for the reported
freeze; detector, locks, UI work and storage are still separate hypotheses.

## 10. Executable adversarial test specification

This is a **test specification, not implemented tests or observed PASS results**.
Future registration names/tags require review; reuse existing suites rather than
inventing test-count targets. The production boundaries below must actually execute
through public Runtime/service/adapter/client paths and real Store/codecs where
specified. Controlled fake acquisition and detector IPC are test inputs, not mocked
assertions of their own correctness. Source tests must still exercise X1 pairing
and ordinary LOSSLESS behavior separately.

Every test has a finite watchdog, explicit entry/completion observables and RAII
cleanup: release barriers before joins, transport worker exceptions, retire callbacks,
close mappings, reap owned children and prove lease return. No sleep/retry oracle.
`C` below means that common cleanup, plus unchanged upstream artifact hashes and
no unintended activation. For persistence tests, release the fault gate and await
the actual Store result before C; a watchdog expiry is failure, never cancellation
success. Assert both primary error/phase and terminal publication state.

| ID / production boundary | Controlled stimulus / decisive independent oracle | Pass/fail and bounded cleanup |
| --- | --- | --- |
| T01 — live start + Store import | Force duplicate acquisition identity with different content; import the same sealed source under a new artifact/container ID; retry identical start nonce, then alter config with same nonce. | Collision/conflicting retry rejected; identical retry starts only one source. Imported content cannot count twice. Inspect actual operation count, source identity/hash and Store graph, not returned labels. C. |
| T02 — Runtime restart + status/finish | Hold an old-generation callback, stop/restart daemon with sequence and PID reuse, then deliver stale result/finish handle. | New incarnation differs; stale operations fail and old callback cannot modify new counters/records. No cross-generation publication. Release stale gate, reap both owned processes, C. |
| T03 — producer continuity | Emit parent 10,11,13, repeat 13 or reverse 12 before admission; separately native gap and ADR-026 legitimate exclusion. | Parent/native integrity faults fail before usable source; legitimate pairing exclusion retains exact reason and is not recorder/candidate loss. Compare independently scripted complete sequence trace and ledger. C. |
| T04 — discovery/paired-image boundary | Swap physical LEFT/RIGHT IDs while preserving array positions, duplicate role, change optical frame or source calibration on one child. | Preflight incompatibility before acquisition when discoverable; otherwise source failure with exact offending role/reference. Explicit rig slots do not repair physical swap. Assert no finalized source/RawCapture. C. |
| T05 — timestamp boundary | Supply incomparable clock IDs, backward timestamp, absent required clock, fabricated zero uncertainty, changed clock mid-session; retain valid software-paired trace. | Invalid pairing provenance rejected; absent optical uncertainty remains unknown in valid source, never hardware sync/zero skew. Compare recorded parent/child clocks/delta to fixture trace. C. |
| T06 — FrameSet validation | Missing RIGHT, duplicate requested child, truncated packed extent, partial pair and unrelated auxiliary child. | Required-frame/layout faults fail, diagnostic reason survives; auxiliary child is ignored only under explicit role policy. Partial **target visibility** remains distinct from partial frame. No detector call on invalid bytes. C. |
| T07 — M2 + helper result boundary | Actual blank image, partial ChArUco with known ID sets, unsupported layout, detector exception and malformed IPC empty-success observation. | Blank is no_target; partial preserves independent expected IDs/geometry; exception/invalid observation fails, never counted no_target. Assert exact dispatch/result accounting and no empty TargetObservation. C. |
| T08 — target/config validation | Wrong target revision/hash, swapped measured X/Y extents, changed dictionary/layout, mismatched resolution. | Identity/hash/dimension faults rejected; valid anisotropic geometry checked against independently computed millimeter points. Deliberately wrong physical input may be internally consistent: require operator evidence, never assert software detected a mismeasured board. C. |
| T09 — pure selector + real dataset codec | Permute source/record/role order; exact descriptor ties, half-away rounding and overflow inputs; vary helper completion scheduling with the same sealed population. | Golden selected keys/ranks and serialized canonical content agree, overflow fails. Legacy embedding yields v1 ranks exactly; new live schedules may change admission and must expose differing ledgers. C. |
| T10 — M4 public solve | ChArUco sides with different/unsorted ID sets and one unselected side needed by stereo; perturb held-out-only pixels and keep training fixed. | Independent expected mono/stereo union/order/common IDs/split; preliminary mono fit unchanged by held-out-only mutation, fixed intrinsics unchanged in stereo. Preserve ineligible reasons and all three evidence stages. Checkerboard rig incompatible. C. |
| T11 — producer→sampler overload | Barrier-block helper after request entry, send more eligible pairs than pending capacity, then release; exhaust compact/ledger cap. | Producer processes scripted source without detector wait, ≤1 pending/1 in-flight, evicted keys and counters match fixture, no native/source loss relabeling; bounds stop admission visibly. Assert actual buffer lifetime/maximum allocation and cleanup counts. C. |
| T12 — finalization/slow Store | Gate append/finalize after entry; send Finish twice, query status and attempt competing source/ordinary/projected start. | No completion ACK before real finalization; one write/reservation only; device lease/analysis slot states match lifecycle, camera conflicts rejected symmetrically. UI heartbeat/control reply remains observable while Store is gated. Release gate, inspect actual finalized bytes, C. |
| T13 — cancel during acquisition/detection | Gate source next or helper processing after explicit entry; cancel before persistence, send late detector result. | No finalized source; source stops once, helper reaped within configured deadline, late result discarded/count retained, no callback after owner destruction. Assertion-failure injection releases all gates before joins. C. |
| T14 — persistence-entry race | Barrier just before entry, Cancel then release; second run Cancel just after entered persistence, test both success and EIO. | Before-entry cancellation prevents any published source. After-entry success returns actual completed artifact; EIO remains failed with original I/O error despite Cancel. Never report Cancelled for a finalized result or ACK an incomplete write. C. |
| T15 — Store durability/recovery | Inject short write, ENOSPC, EIO, crash before seal, after reserve, mid-append and around finalize commit. | Only complete sealed/hash-verified documents may finalize; prefix stays incomplete/recoverable, consumed revision not reused, upstream bytes unchanged. Reopen real Store and load graph, not session JSON assertions. C. |
| T16 — strict source/dataset readers | Corrupt bytes/hash, duplicate JSON key, noncanonical order, unknown schema, missing target/source ref, point mismatch, fabricated counters or unsealed document. | Actual loader/validator rejects with correct corrupt/incompatible boundary; no solve/activation. Mutation of a source reference breaks graph even when visible totals match. Reopen after failure, C. |
| T17 — legacy dataset/replay | Open baseline-generated schema-1 Target/Dataset/Camera/Rig and RawCapture 1/2 fixtures; run old dataset path, decode/re-encode canonical documents, replay twice. | Historical bytes/hashes, typed observations/selection and replay pixels/headers equal independent retained goldens. V2 observation source cannot be opened as image replay; unsupported old reader fails explicitly. C. |
| T18 — M8 independent-source analyzer | Three datasets/IDs from one source; cloned source with new nonce/manifest but repeated image-content hashes; same daemon incarnation split into three sessions; missing boundary evidence. | Independent analyzer reloads Store evidence and rejects false independence, not just unequal artifact IDs/root hashes. Three genuinely separate scripted daemon sessions with distinct payload witnesses pass software structure only, remain fixture/not real M8. C. |
| T19 — active-binding snapshot/history | Start under rev1, activate rev2 while sampling, finish; clear/restart and replay an actual historical rev1 RawCapture. | Source parent/child/original and active refs stay captured rev1, future source uses rev2; historical pixels/headers/hashes remain rev1. Assert actual Store/replay values, never current-query values as expected history. C. |
| T20 — public SDK + Studio | Real daemon/Protobuf/C++/Python path; disconnect/reconnect, duplicate Finish, stale selections, oversized status and unsupported daemon capability; block helper/Store with barriers. | No UI-thread waits/unbounded refresh, heartbeat/navigation/cancel remain observable, stale results never replace selected lineage, source selectable only after finalization, absence/presence preserved. Full populations/pixels absent from control DTOs. Destroy UI while work runs, then C through daemon controls. |
| T21 — helper/resource fault | Kill helper at request entry, exceed response/point budget, allocation failure, malformed generation/sequence result and deadline expiry. | Session fails explicitly, producer never waits on helper IPC, bounded process/mappings/slot retired with primary error preserved. No implicit restart/RawCapture fallback and no leftover camera ownership. C. |

Negative sensitivity is part of implementation acceptance: in a disposable build,
remove the generation guard (T02), relabel source gaps as candidate drops (T03),
join points by vector position (T10), acknowledge before finalize (T12/T14), or use
artifact IDs alone for independence (T18). Independently selected decisive assertions
must fail nonzero at the named predicate with bounded cleanup; restore and rerun.
At least representative R1 mutations must actually execute, not merely be listed.
Reuse existing negative controls in `calibration-observation`, `calibration-dataset`,
`calibration-solve`, artifact/revision tests and `capture-calibration-binding` as applicable;
inspect their source/oracles before extending. Test counts/green badges alone prove
neither mutation sensitivity nor hardware validity.

## 11. M8 qualification and equivalent live evidence

**Until independently accepted amendments exist, ADR-037's three independent,
disjoint authoritative RawCapture hash sets remain binding.** A live-only run cannot
currently satisfy that requirement. Three Dataset IDs, three solves, three windows
or new nonces from one stream do not establish independence.

Proposed equivalent evidence must include at least three genuinely new acquisitions,
each in a separately restarted validation daemon/incarnation, with unchanged rig
and exact shared measured Target revision, full source/analysis/identity manifests,
operator stationary-pose/physical-role acceptance and independently verified complete
sealed source hashes. Record daemon-stop/start boundaries with no overlapping source
lease, fresh source generation, collection environment/build/profile/target-vector
hashes, physical measurements and operator rig/mounting notes. A nonce is supporting
identity, not proof of a restart or new optical observations.

Independence analyzer must reject shared live source identities/references, shared
admitted frame evidence identities and copied source populations across projects.
Compare per-image original-content hashes in addition to source roots; root hashes
change with fresh nonces and cannot alone reject a cloned population. Repeated frame
hashes make independence **unestablished / review required**, not proof of malicious
reuse: truly static/blank images may repeat. A reviewed rule for ambiguous/partially
overlapping image content remains OPEN; absent original pixels make forensic proof
strictly weaker than full RawCapture. Do not silently waive that difference or enable
forensic image retention to make the run pass. If equivalent confidence is rejected,
keep the existing RawCapture M8 route and do not claim live M8 acceptance.

Preserve ADR-037 repeatability statistics and all six ordered fixed-calibration
cross-dataset evaluations: only mono target pose is estimated; geometry/intrinsics/
stereo remain fixed. M4 held-out samples within one Dataset are not independent M8
datasets. Retain selected/unselected population, missing/rejected reasons, source
continuity and candidate-drop counters; decide explicitly whether measured sampling
drop envelopes are acceptable for characterization. No optical PASS threshold is
created by this document. Source/native errors, inconsistent lineage and failed
finalization remain structural failures.

Keep the physical target generator/layout hash, post-print/mounting active-width
and height measurements, optional truthful uncertainty and stationary-pose guidance.
Actual X1 physical LEFT/RIGHT identity and exact `org.mantis.x1.rig` / `Mantis X1 rig`
frame checks remain mandatory. Real activation/historical RawCapture binding/replay
evidence is still required separately; observation-only solving cannot substitute
for replay of pixels it never retained. A future report version must distinguish
`observation_resolve_reproducibility` from `deterministic_image_replay`; the latter
is unavailable for normal live sources, not PASS. Real M8 acceptance and M8b
threshold review remain independent, with `final_m8_acceptance` NOT ESTABLISHED
throughout characterization.

## 12. Required decision amendments before conflicting implementation

This document changes none of the following accepted files. A subsequent reviewed
decision must explicitly approve the version/persistence/independence choices, then
amend the listed clauses and implementation contracts together.

| Accepted authority | Exact necessary amendment scope |
| --- | --- |
| ADR-028, Decision source identity, RawCapture-only ingestion, canonical keys/config and selection v1 paragraphs | Add typed live-source identity, strict admitted-population/error/counter rules, source-independent observations, new key ordering and proved selection v2. Keep physical-ID semantics, diversity arithmetic and legacy v1 intact. |
| ADR-030, Decision 1–4, 6–9, 12 / Consequences | Add new source type and schema-2 Dataset/Camera/Rig/Document codecs and typed graph references; retain schema-1 bytes. Approve version-dispatched activation and live-source snapshot/provenance, reserved revisions and interrupted-source recovery. No historical rebinding or schema1 reinterpretation. |
| ADR-035, Decision 1–6, 8, 11–12 / Consequences | Add explicit live start/status/finish/cancel, capability/compact bounds and v2 builds, source-ready versus final ACK, bounded analysis helper and ordinary Job integration. Define cancellation linearization and non-preemptive persistence. Remove no-frame-processing implication only for explicitly selected observation mode, not recording. |
| ADR-036, Decision 5–6 / Consequences and M7 Captures flow | Add immutable live-source resume and explicit mode selection/state/counters through public clients, without UI acquisition/CV/storage or autoactivation. Preserve existing recorded workflow, preview lease and async controls. |
| ADR-037, Decision 1–4, 6–8, 11, 13 / Consequences | Independently decide equivalent live evidence, restart/content-reuse proof and limits without originals, sampling-loss envelope and report/tool changes. Keep three genuinely independent datasets, physical measurements, cross-validation, real activation/history/replay and separate M8b acceptance. |

Also update [calibration-detection](calibration-detection.md),
[calibration-dataset](calibration-dataset.md), [calibration-solve](calibration-solve.md),
[calibration-artifacts](calibration-artifacts.md), [calibration-api](calibration-api.md),
[calibration-workspace](calibration-workspace.md), v0.3 M3–M8 descriptions, M8 operator
procedure/report schema and their validation specifications. ADR-029's numerical,
correspondence and held-out policy is preserved; document the typed-key adaptation
and exact equivalence tests, obtain a decision if that proof fails. ADR-021/023/025/026
and frozen v1 stay unchanged: no ABI/layout/pairing/preview/RawCapture rewrite.
ADR-033 and the [binding reliability standard](reliability-performance-and-validation.md)
remain gates, not items to weaken. Approve a new decision record number through
the normal architecture process; this proposal does not allocate an accepted ADR.

## 13. Open decisions and acceptance limits

- Independently approve A versus B, source/document/schema numbering, schema-2
  activation and cross-schema revision rules. Confirm Store uniqueness/import
  strategy and whether any transactional metadata migration is needed.
- Approve selection v2 only after legacy embedding/tie/cross-architecture proof;
  define admission-loss characterization without inventing quality thresholds.
- Qualify analysis-helper transport/copy count, address-space/deadline budgets and
  teardown on x86_64/ARM64; prove actual producer independence and total memory,
  including OpenCV/preview costs. Process supervision is proposed, not delivered.
- Independently decide whether no-pixel live evidence is sufficient for M8 source
  independence and ambiguous duplicate content. Until then live-only M8 is BLOCKED
  by the existing RawCapture evidence contract, not excused by lower storage load.
- Q6A microSD freeze cause, sustained source throughput, detector throughput/thermal
  behavior, UI latency, OS storage-stall retirement, actual print/mounting geometry,
  physical timing/exposure skew and real calibration repeatability remain OPEN.
  No camera, firmware, privileged instrumentation, laser or hardware operation was
  performed for this proposal. Simulation cannot establish any of those guarantees.

Design review must inspect source facts, alternatives, version boundaries and all
negative-test oracles. This documentation package requires lightweight reference,
claim, scope and architecture/instruction checks plus the unchanged five exact-head
CI jobs: x86_64/ARM64 Studio ON/OFF and sanitizers. Full local production builds and
hardware campaigns are outside this design-only assignment. Proposed tests above
are future implementation gates, not execution evidence. Worker completion and
green CI are not independent architecture acceptance, merge authorization, M8
qualification or permission to start implementation.
