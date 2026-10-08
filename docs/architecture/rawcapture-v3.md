# RawCapture schema 3: frozen L4 byte specification

Status: implemented, acceptance pending. This specifies storage encoding version 1.
A change to these bytes requires an explicit new encoding version. Schemas 1/2,
MANTIS01/MANTIS02/MRAWREC2 and their field order/alignment/footer are unchanged.
Project manifest remains 1 and SQLite metadata remains 2.

## Primitives and bounds

All integers are little-endian. Unsigned integers (including model uint32 fields,
versions, counts, lengths and member kinds) occupy eight bytes. uint32 values must
fit uint32. Signed integers and nanosecond durations use eight-byte two's-complement
bit patterns. Doubles use IEEE-754 binary64 bits, little-endian, including signed
zero; L1 rejects nonfinite semantic values. No native layout is serialized.

Strings are uint64 byte length followed by exact bytes without terminator. Strings
are bounded to 1024 bytes; IDs to 256. Arrays are uint64 count followed by ordered
elements, capped at 4096 before allocation and by the tighter L1 field bounds
(participants 64, cameras 16, steps 256). No implicit sorting or normalization.
Optional fields use one byte: 0 absent, 1 present followed by the value.
Evidence uses one byte: 0 Unknown, 1 Unavailable, 2 Established followed by the value.
Zero numeric values remain established. Other tags are corrupt. Enum tags are one
byte, explicitly assigned below. Type IDs contain name and uint32 version.
All payloads are bounded to 128 MiB; checked addition and remaining-span checks
precede size arithmetic. L1 validation is mandatory after decoding.

## Bundle envelope

`MANTIS03` (8 bytes), uint64 encoding version 1, DataTypeId (must be
org.mantis.AcquisitionBundle schema 1), BundleKey, RuntimeTimestamp, uint64 member
count (1..64), then members. A member is uint64 kind, uint64 payload byte length,
exact payload. Canonical order: one evidence (kind 1), optional FrameSet (kind 2),
ordered triggers (kind 3). Unknown kinds, out-of-order/duplicate required members,
multiple FrameSets, missing evidence and trailing bytes are corrupt. Embedded
legacy strings retain their 1-MiB bound; metadata/attributes remain capped at
256/128. Schema-3 embedding checks exact schema/enum widths, ordered unique
metadata and zero padding without changing either legacy codec.

Evidence payload: `MEVID001`, uint64 version 1, AcquisitionEvidence fields below.
Trigger payload: `MTRIG001`, uint64 version 1, TriggerEvent fields below.
The full accepted structures cannot fit the existing MANTIS01 typed attributes
losslessly without new conventions. Dedicated typed semantic payloads therefore
implement ADR-043's 'where sufficient' rule; MANTIS01 is not extended.

FrameSet payload is the unchanged MANTIS02 byte representation (must have that
magic and schema-1 FrameSet with only schema-1 MANTIS01 images). Pixel alignment is
relative to each embedded packet origin exactly as before. Decoded BufferViews are
slices of the segment mapping and retain it independently, without pixel copies.
Maximum nesting is bundle -> FrameSet -> ImageFrame.

## Immutable pre-run header

`objects/ID/run.header` is distinct from numbered segments. Envelope:
`MRUNHDR3`, uint64 version 1, uint64 body length, uint64 FNV-1a-64 of body,
body, `MRUNEND3`, uint64 bitwise-complemented body length. Body is
ProjectedCaptureHeader below, including terminal policy byte 0 after program
bounds (inhibit future triggers and request all OFF). Body is typed, never JSON.
The header is validated, written to run.header.part, flushed, fsynced, renamed,
and its directory fsynced synchronously before begin_projected_capture succeeds.
It is immutable thereafter. Its self-checksum is verified before recovery/scanning.
Queue/correlation capacities are positive, at most 1,000,000; operation/abort/
cleanup/publication timeouts are 1..60,000 ms. These are storage sanity limits;
L3 graph and reservation preflight remains authoritative.

## Record/segment framing and artifact integrity

MRAWREC3: eight-byte magic, uint64 payload length (1..128 MiB), uint64 FNV-1a-64
payload checksum, uint64 bitwise complement of length, exact MANTIS03 payload,
uint64 completion footer `length XOR 0x4d414e5449533033`.
A complete footer is required. Partial header/payload/footer is an incomplete tail;
complete integrity or semantic failure is corruption and never truncated.

Segments use N.segment/N.segment.part and the unchanged segment-level SQLite
index. Normal target 64 MiB (tests may select 4 KiB..64 MiB); a last record may
cross the target. Each segment index is capped at 100,000 records and the writer
also seals at that count. The schema-3 mapped segment bound includes the final
record framing (64 MiB + 128 MiB + 40 bytes). Complete active records flush to the OS; sealed segments fsync,
rename and directory-sync before index publication. No per-record transaction.
One checksum read pass precedes direct sequential writes; structural nested
length calculation reads no pixels. Segment verification reads them again as
in schema 2. No entire-bundle staging allocation or pixel conversion is used.

Schema-3 ArtifactDescriptor.bytes counts exact durable header plus indexed segment
bytes, with live active bytes added by get(). chunks remains segment count.
The aggregate FNV-1a-64 starts with the ASCII lowercase hexadecimal file hash of
run.header, then the file hash of each contiguous segment in index order. Each
file hash covers every file byte. Legacy aggregate hashes remain unchanged.
FNV detects accidental corruption; it is not authentication.

## Exact typed field order

Structures concatenate their fields in the following order. SemanticId encodes
its Id; SemanticSequence its uint64 value; Duration its signed nanoseconds.
- `Id`: `value`.
- `Hash`: `algorithm`, `hex`.
- `SemanticVersion`: `major`, `minor`, `patch`.
- `schema::DataTypeId`: `name`, `version`.
- `calibration::Reference`: `id`, `schema_version`, `revision`.
- `time::ClockDomain`: `id`, `name`.
- `time::MonotonicTimestamp`: `nanoseconds`.
- `time::SyncGroup`: `id`, `trigger`.
- `time::ClockMapping`: `source`, `target`, `scale`, `offset_ns`, `uncertainty_ns`.
- `StepInstance`: `run_id`, `repetition_index`, `step_index`.
- `StreamIdentity`: `id`, `generation`.
- `SourceFrameKey`: `camera`, `stream`, `native_sequence`.
- `FrameSetKey`: `run_id`, `stream`, `sequence`.
- `BundleKey`: `run_id`, `sequence`.
- `EvidenceKey`: `run_id`, `ordinal`.
- `TriggerKey`: `run_id`, `source`, `controller_generation`, `sequence`.
- `NativeTriggerIdentity`: `controller`, `generation`, `value`.
- `ContentReference`: `id`, `type`, `hash`, `revision`.
- `ProgramReference`: `id`, `hash`, `content`.
- `ExactCalibrationReference`: `calibration`, `content`.
- `ClockIdentity`: `domain`, `generation`.
- `SemanticTimestamp`: `nanoseconds`, `clock`.
- `RuntimeTimestamp`: `time`, `clock`.
- `TimeInterval`: `start`, `end`.
- `ClockMappingEvidence`: `mapping`, `source_generation`, `target_generation`, `reference`.
- `EvidenceSource`: `source`, `method`, `reference`.
- `Acknowledgement`: `request`, `stage`, `result`, `evidence`, `time`, `scope`.
- `EmitterCommand`: `request`, `target`, `state`, `dispatched`.
- `StateObservation`: `state`, `scope`, `evidence`, `time`, `coverage`.
- `ExposureEvidence`: `requested_duration`, `startup_readback_duration`, `integration_duration`, `interval`, `evidence`, `uncertainty_ns`.
- `ExposureAssociation`: `frame`, `trigger`, `method`, `evidence`, `native_trigger`.
- `SyncEvidence`: `group`, `quality`, `hardware_association`.
- `CameraFrameEvidence`: `frame`, `camera_role`, `width`, `height`, `source_timestamp`, `host_received`, `timestamp_meaning`, `exposure`, `sync`, `camera_calibration`, `rig_calibration`, `original_calibration`.
- `ExposureEffectiveState`: `frame`, `state`, `scope`, `evidence`, `coverage`.
- `CameraEffectiveState`: `frame`, `state`.
- `EmitterEvidence`: `emitter`, `commanded`, `acknowledged`, `observed`, `exposure_effective`.
- `CameraParticipant`: `component`, `stream`, `role`.
- `Participants`: `cameras`, `emitters`, `controllers`.
- `TriggerIntent`: `controller`, `request`, `endpoints`.
- `CaptureIntent`: `mode`, `cameras`, `trigger`.
- `EmitterIntent`: `emitter`, `state`.
- `AcquisitionStep`: `index`, `label`, `emitters`, `capture`, `evidence_requirement`, `required_scope`, `settle`, `max_duration`.
- `RunBounds`: `max_duration`, `max_on_duration`, `max_step_instances`, `max_commands`, `max_events`, `max_bytes`, `max_in_flight_captures`.
- `AcquisitionProgram`: `type`, `identity`, `participants`, `steps`, `repetitions`, `bounds`.
- `TriggerEvent`: `type`, `key`, `step`, `request`, `native_trigger`, `kind`, `acknowledgement`, `evidence`, `device_time`, `host_received`, `host_dispatched`, `uncertainty_ns`, `clock_mapping`, `intended_endpoints`, `actual_endpoints`, `requested_exposure`, `exposure_association`.
- `ImplementationIdentity`: `implementation`, `version`, `build`, `configuration`.
- `LossAccounting`: `kind`, `source`, `count`, `reason`, `frame`, `request`.
- `UnresolvedRequest`: `request`, `target`, `reason`.
- `AcquisitionEvidence`: `type`, `key`, `program`, `step`, `causal_predecessors`, `participants`, `implementations`, `emitters`, `frameset`, `frames`, `triggers`, `clock_mappings`, `rig_calibration`, `disposition`, `reason`, `unresolved_requests`, `losses`, `diagnostic`.
- `RecordedRunConfig`: `queue_capacity`, `operation_timeout_ms`, `abort_timeout_ms`, `cleanup_timeout_ms`, `publication_timeout_ms`, `max_correlation_entries`.
- `ProjectedCaptureHeader`: `program`, `run`, `generation`, `config`.

## Explicit enum tags

- `EvidenceMethod`: 0=software_dispatch, 1=controller_report, 2=register_readback, 3=electrical_readback, 4=optical_sensor, 5=validated_executor, 6=camera_metadata, 7=software_association, 8=imported.
- `EvidenceScope`: 0=controller_register, 1=electrical_enable, 2=optical_emission.
- `EmitterState`: 0=off, 1=on.
- `AcknowledgementStage`: 0=acceptance, 1=completion.
- `AcknowledgementResult`: 0=success, 1=rejected, 2=failed.
- `TimestampMeaning`: 0=exposure_start, 1=exposure_end, 2=driver_delivery, 3=device_event.
- `AssociationMethod`: 0=native_trigger, 1=validated_executor, 2=software_correspondence, 3=imported.
- `time::SyncQuality`: 0=unknown, 1=software, 2=hardware.
- `CaptureMode`: 0=none, 1=free_running, 2=hardware_trigger.
- `EvidenceRequirement`: 0=commanded_only, 1=controller_acknowledged, 2=exposure_effective.
- `TriggerEvent::Kind`: 0=requested, 1=acknowledged_accepted, 2=acknowledged_completed, 3=rejected, 4=observed, 5=timed_out, 6=cancelled.
- `AcquisitionDisposition`: 0=startup, 1=captured, 2=control_only, 3=completed, 4=failed, 5=stopped, 6=cancelled.
- `AcquisitionReason`: 0=none, 1=timeout, 2=rejected, 3=device_failure, 4=transport_failure, 5=evidence_missing, 6=contradictory_evidence, 7=resource_limit, 8=user_stop, 9=user_cancel, 10=cleanup_failure.
- `LossKind`: 0=command, 1=trigger, 2=frame, 3=exposure, 4=bundle, 5=record, 6=excluded_frame.

## Store, continuity and recovery

`begin_projected_capture(header, provenance)` is the durable initialization API.
`append_bundle`, `capture_header`, `bundle`, `replay_bundles`, `bundle_summary`
and `BundleCaptureReader` are explicit schema-3 APIs. Packet append/packet/replay,
CaptureReader and camera-only recorded_source reject schema 3; schema-3 append
rejects schemas 1/2. Unknown schemas are incompatible. CalibrationDataset ingestion
remains schema-2-only. No project migration, hardware action or recorder queue is
introduced. Higher-layer `recorded_run_config` explicitly converts the finite L3
configuration; Store never depends on device-runtime.

Each bundle passes L1 validation and exact persisted run/program-reference/
participant binding. First sequence is preserved as supplied; each successor must
be exactly previous+1, with checked overflow. Evidence ordinal strictly increases
and every causal predecessor must exist in the recorded earlier ordinal set.
L1 validate_successor checks publication clock/time and adjacent source generations/
event ordering. Established step instances must name a persisted declared step
index and valid repetition. Trigger/frame references may resolve late; storage
does not reinterpret them as hardware policy or require earlier TriggerEvents.
The ordinal set is bounded by recorded correlation-entry reservation and program
max_events; canonical bundle payload bytes are cumulatively bounded by max_bytes.
Header/framing overhead is accounted in artifact bytes separately. One previous
bundle's semantic context is retained without its mapped FrameSet. No pixel buffer
or capture-sized record index is retained for continuity.

Normal finalization requires recorded completed/failed/stopped/cancelled evidence.
Any such outcome may be FINALIZED. Only one terminal record is allowed, last.
Recovery can finalize a verified prefix without terminal evidence, including just
a durable header with zero records. Summary terminal absence means unknown/incomplete;
its reason is meaningful only when terminal is present. No run reason is invented
for a storage error, and no completed/OFF/safe outcome is inferred. Storage failures
mark the artifact RECOVERABLE and report their own error independently.

Recovery validates header integrity first, checks indexed segment hashes, rejects
ambiguous/conflicting/gapped filenames, discovers closed unindexed segments and
scans the active tail with the schema-3 framing. Cross-segment continuity is checked
before publishing each recovered segment. Only structurally incomplete trailing
bytes are truncated. Complete corruption preserves the bytes and verified prefix
for diagnosis. Cancellation checks occur between segments/records and remain
retryable. FINALIZING verification rechecks immutable header, segment hashes,
record checksums, semantics, order and terminal constraints before FINALIZED.
Recovery never resumes a run, sends a trigger or issues an emitter command.

## Readers and delivery pacing

BundleCaptureReader retains Store ownership and at most one bounded mapped segment
and record index. A returned bundle owns its semantic values and each pixel view
retains the segment mapping independently after advance/destruction. Header access
is independent of records and available after reopening before the first bundle.
Summary scans recorded dispositions only, without deriving hardware evidence.

`device::BundleReplay` is independent of ImageStream/ProjectedExecutor. ASAP and
receive-paced delivery return identical original bundle values and timestamps.
Relative delivery pacing uses the recorded publication RuntimeTimestamp in its
unchanged clock generation. It checks monotonicity, checked timestamp/deadline
arithmetic and a 24-hour relative pacing horizon. Each next() accepts a 0..60,000 ms
bounded delivery timeout; waits are at most 50 ms and stop wakes/cancels them.
One next call is admitted at a time (concurrent next reports busy); stop may run
concurrently. Destruction follows completion of concurrent calls.
An injectable nonblocking monotonic clock and notify_clock_advanced seam support
deterministic tests. finished() distinguishes EOF/stop from a pending poll.
Pacing is delivery timing, never optical/exposure evidence. Replay does not look
up project calibration, re-pair images, replace evidence or open an executor.

## Compatibility evidence

Checked-in independent binary fixtures under tests/fixtures/storage freeze
MANTIS01, MANTIS02 and MRAWREC2 before storage refactoring, then four MANTIS03
values (evidence-only, captured, trigger-bearing, failed terminal), one capture
header and one MRAWREC3 record. Expected bytes are never regenerated by tests.
reference_v3.py documents independent explicit construction, imports no C++ codec
or field catalog, is never a test step and refuses overwriting existing fixtures.
