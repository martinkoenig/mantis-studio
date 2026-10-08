# L4 implementation and acceptance evidence

Branch: `feature/v0.4-laser-acquisition`. Starting commit:
`68cec0fffaeb98a5065565854125a2e2d6ebda96`.
L0/L1/L2/L3 are FINAL ACCEPTED. L4 is implemented, acceptance pending.
L5–L8 remain planned. The final pushed SHA is supplied in the delivery report.

## Delivered storage boundary

1. The exact [MANTIS03 specification](rawcapture-v3.md) defines little-endian
   typed encoding version 1: magic, version, DataTypeId, BundleKey,
   RuntimeTimestamp, count and kind/length-delimited ordered members. Semantic
   fields are explicit; no struct, pointer, variant, chrono or ABI bytes are stored.
2. Presence tags are 0 Unknown, 1 Unavailable, 2 Established; optional tags are
   0 absent and 1 present. Numeric zero stays established. Explicit one-byte enum
   tables and uint64 member kinds 1 evidence, 2 FrameSet and 3 TriggerEvent are
   documented independently of C++ enum representation. Signed numbers are
   two's complement and floating values IEEE-754 binary64, including signed zero.
3. MEVID001/MTRIG001 semantic payloads contain their own encoding version 1 and
   exact schema-1 type plus every accepted evidence/trigger field, in the
   documented order. Existing MANTIS01 cannot losslessly express the full model;
   dedicated payloads implement ADR-043's conditional reuse rule. ADR-043 is unchanged.
4. MRUNHDR3 version-1 run.header stores the accepted AcquisitionProgram itself,
   RunId, execution GenerationId and all six finite L3 configuration fields. It
   contains body length/checksum and MRUNEND3/complement footer. Flush, file fsync,
   rename and directory fsync finish before begin_projected_capture returns.
   The exact header is readable after reopening before any bundle exists.
5. MRAWREC3 framing is magic, uint64 length, FNV-1a-64 checksum, complemented
   length, MANTIS03 payload and uint64 length XOR 0x4d414e5449533033 footer.
   Payload remains bounded to 128 MiB; segments normally target 64 MiB. OS flush
   preserves complete active records for process-kill recovery. Segment sync,
   rename, directory sync and one existing SQLite chunk transaction publish each
   segment. FNV is accidental-corruption detection, not authentication.
6. Additive Store APIs are begin_projected_capture, append_bundle, capture_header,
   bundle, replay_bundles, bundle_summary, record_run_outcome and run_outcome,
   with BundleCaptureReader. Reader and BundleReplay expose final_outcome separately.
   Packet    append/packet/replay, CaptureReader and recorded_source reject schema 3.
   Bundle append rejects schemas 1/2. Unknown schemas are incompatible.
7. Shared bounded continuity validation enforces run/program/participant identity,
   exact sequence continuity, strictly increasing evidence ordinal, actual earlier
   causal predecessor membership, declared sparse step indices/repetitions and L1
   validate/validate_successor. Late TriggerEvents remain representable. It never
   renumbers evidence or enforces a fabricated event-before-reference requirement.
8. Final daemon outcome is separate from executor bundle disposition and artifact
   state. MRUNOUT3/MOUTEND3 run.outcome stores the resolved L3 snapshot: run and
   execution generation, final state/reason, structured errors and full AbortOutcome.
   Normal finalization requires this durable sidecar; an early attempt leaves OPEN
   writable. A header plus outcome with zero bundles is valid. Executor terminal
   bundles stay unchanged and do not close recording. Without the daemon outcome,
   recovery reports unknown/incomplete even after executor-completed evidence.
   Failed/cancelled daemon outcomes can be FINALIZED; storage errors stay separate.
9. Recovery validates the immutable header first, checks indexed hashes, discovers
   closed unindexed segments and scans the active tail. It checks cross-segment
   semantics and rejects gaps/conflicts. Only incomplete trailing header/payload/
   footer bytes are truncated; corrupt complete records retain their original
   bytes and verified diagnostic prefix. Cancellation remains retryable. No
   recovery or replay path resumes execution or opens a hardware executor.
10. BundleCaptureReader owns Store and at most one segment mapping/index. Returned
    bundles own semantic values; pixel BufferViews independently retain their
    mapping through reader advance, segment change and destruction. Prior
    continuity context retains no mapped FrameSet. There is no capture-sized
    reader index or second recorder queue.
11. BundleReplay is separate from ImageStream/ProjectedExecutor. ASAP and paced
    delivery preserve exact recorded bundle bytes/timestamps. Relative pacing
    uses publication RuntimeTimestamp, checked arithmetic and a 24-hour horizon;
    delivery waits are at most 50 ms within a 0..60,000 ms next timeout. Concurrent
    stop wakes waits, and an injected monotonic clock supports deterministic tests.
12. Structural sizing reads no pixels; checksum probing and direct sequential
    writing map each pixel span exactly twice. A non-seekable ostream adapter
    forwards bytes while maintaining only position, allowing unchanged MANTIS02
    embedding without staging. Tests also cover multi-image order and unknown
    namespaced attributes. No application-level pixel copy/conversion is introduced.
13. Schema-3 aggregate integrity covers the exact header and every finalized
    segment and the final daemon outcome when present. Header hash ASCII hex
    prefixes segment-hash aggregation; outcome hash follows segments.
    Descriptor bytes includes header, segment and outcome bytes; chunks means segments.
    No project manifest bump or SQLite metadata migration occurs.
14. Ten independent immutable binaries freeze MANTIS01/MANTIS02/MRAWREC2,
    four v3 bundles, the header, a complete MRAWREC3 record and the daemon-outcome
    sidecar. The original nine binary fixtures remain unchanged. C++ tests use
    independently declared semantic values and compare both decode and encode.
    reference_v3.py imports no codec/catalog, runs only explicitly and refuses
    overwriting existing expected bytes. Final hardening corrected only the acceptance-
    pending daemon-outcome fixture to explicit optional outer presence; the nine
    accepted legacy/bundle/header/framing fixtures were not regenerated.
15. Negative tests cover magic/version/presence/optional/enum/member/count/length/
    overflow/nesting failures, every byte-boundary truncation, duplicate/missing
    evidence, repeated FrameSets, schema-2/3 record mismatch, >128-MiB framing,
    complement/checksum/footer failure, valid-checksum semantic corruption,
    order/run/program/causal violations, records after the final daemon outcome
    and corrupt headers/outcomes. Executor terminal evidence alone permits later records.
16. Recovery tests cover partial headers/payloads/footers, corrupt complete tails,
    closed unindexed segments, immutable hash mismatch, missing/gapped segments,
    cancellation before and during verification with retry, prefixes with/without
    final daemon outcome, a real SIGKILL after executor completion and sealing/
    outcome-publication storage failures. A 65-MiB record
    crosses the normal segment target. An evidence-only run has no image at all.
17. Two finalized-artifact replays produce digest `4ae5f7ea1f971887` over exact
    header and canonical bundles. ASAP and paced replay bytes match. Replacing and
    clearing the project's active calibration does not change this digest or
    recorded camera/rig references.
18. Legacy data.cpp/data_io.hpp/data.hpp are untouched. Accepted L1 model fields
    and bundle validation behavior remain unchanged; a shared emitter structural
    validator now supports abort-record validation without invented exposure context.
    Independent MANTIS01/MANTIS02/MRAWREC2 fixtures remain identical.
    Existing raw-capture tests retain schemas 1/2 semantics and pass. Their normal
    aggregate hashing and live packet append performance remain unchanged.
19. CalibrationDataset ingestion remains schema-2-only. Its rejection fixture now
    authors a real finalized schema-3 bundle capture through the explicit API;
    the ingestion implementation and incompatible rejection assertion are unchanged.
20. artifact-store remains below device-runtime and depends only on artifact-api,
    data and platform. No Qt/protobuf/services dependency or data-store cycle was
    introduced. Architecture boundaries and diff whitespace checks pass.
21. Frozen L2 SDK headers, ABI snapshots and catalogs are untouched. No L5/service/
    protobuf/CLI/UI, X1 hardware, extraction, laser geometry or triangulation change
    is included. No physical hardware acceptance is claimed.

## Local verification

- x86_64, GCC 15.2, Debug, Studio OFF: full non-hardware CTest **45/45 passed**.
  This includes raw-capture, all four storage/terminal tests, projected-sequencer,
  projected-light-semantics/contract, frozen-l2-abi/catalog, c-abi, legacy-abi,
  core, acquisition/acquisition-qos and all calibration-dataset regressions.
- ASan + UBSan: full non-hardware CTest **45/45 passed**, with leak detection
  and halt-on-error enabled. The run includes late/blocked abort terminal freezing,
  L3-produced versus recorded-prefix mismatch and retry, finalized sidecar deletion/
  replacement after reopen, incomplete outcome.part recovery, all codec/store/replay
  tests, legacy formats, ABI/catalog and acquisition/calibration regressions.
  No sanitizer, undefined-behavior or leak report occurred.
- Tests used worktree-local TMPDIR. LD_LIBRARY_PATH selected a consistent system
  OpenCV 4.10 library family; this resolves the pre-existing mixed SDK/system
  imgcodecs loader error. OPENCV_OPENCL_RUNTIME=disabled prevents the host NVIDIA
  OpenCL loader's 4,248-byte allocation leak during the calibration sanitizer
  regression. Leak detection remained enabled; no suppression was installed.
- `git diff --check` and `python3 tests/contract/boundaries.py .` passed.

The available [previous L4 CI run](https://github.com/martinkoenig/mantis-studio/actions/runs/37785486189)
on `a8f2f6b1357dad11e37dd7245d52b33ba27a8c5d` is green for x86_64 Studio
ON/OFF, ARM64 Studio ON/OFF and ASan/UBSan. This hardening follow-up's matrix
is pending its normal push; previous results are not claimed for the new SHA.
No ARM64 or Studio-ON result is claimed locally. L4 acceptance remains pending
matrix results and review.

## Final daemon-outcome review correction

The review blocker was executor disposition being mistaken for final daemon state.
The follow-up fixes that authority boundary without changing bundle/header/legacy
fixtures. Actual L3 tests cover prepare failure before any publication, pending-next
cancellation without an executor terminal bundle, normal completion, completed
executor evidence plus stop/close failure, cancellation plus cleanup failure, and
established-negative abort flags. The conversion requires cleanup_resolved, exposing
the existing L3 completion latch. It never infers cleanup from terminal-looking state.

The sidecar is at most 1 MiB, typed and versioned, with a checksum/footer and exact
bundle-prefix count. It is immutable, fsynced and included in bytes/aggregate hash.
Every error and AbortOutcome presence is preserved, including false values, exact
emitter command/ack/observation/effective fields and structured executor errors.
Recorded software OFF requests never become physical OFF evidence.

Outcome tests cover independent golden bytes in both directions, every truncation,
incomplete provisional publication, corrupt complete published/provisional data,
identity/prefix mismatch, malformed tags/overflow, duplicate and later-record
rejection, early finalization retry, zero-bundle failed capture, and changed aggregate
hash with unchanged canonical bundle bytes. Existing replay digest remains
4ae5f7ea1f971887. Summary final_outcome comes only from the sidecar and names the
last_executor_disposition separately.

Follow-up files added:

- `tests/fixtures/storage/run-outcome3.bin`
- `tests/integration/projected_outcome.cpp`

Follow-up files changed:

- `CMakeLists.txt`
- `docs/architecture/projected-light-sequencer.md`
- `docs/architecture/rawcapture-v3-validation.md`
- `docs/architecture/rawcapture-v3.md`
- `docs/architecture/v0.4-laser-acquisition.md`
- `src/artifact-store/artifact-store.cpp`
- `src/artifact-store/bundle_state.hpp`
- `src/artifact-store/include/mantis/artifact_store.hpp`
- `src/data/include/mantis/projected_light_io.hpp`
- `src/data/projected_light_fields.inc`
- `src/data/projected_light_io.cpp`
- `src/device-runtime/include/mantis/projected_run.hpp`
- `src/device-runtime/include/mantis/replay.hpp`
- `src/device-runtime/projected_run.cpp`
- `src/device-runtime/replay.cpp`
- `tests/fixtures/storage/README.md`
- `tests/fixtures/storage/reference_v3.py`
- `tests/integration/calibration_dataset.cpp`
- `tests/unit/projected_raw_capture.cpp`
- `tests/unit/projected_run.cpp`
- `tests/unit/projected_storage_values.hpp`

## Final hardening review

The accepted executor/daemon outcome separation, mapped bundle codec, header and
segment formats remain intact. The follow-up closes these archive boundaries:

- recorded_run_outcome returns ProjectedCaptureOutcome with the exact final
  snapshot queue.produced count. Store verifies expected publications against
  received bundles before sealing or writing. A 2/1 mismatch leaves OPEN and the
  writer usable, with no outcome files; draining bundle 1 permits the same final
  result to be retried as 2/2. The actual L3 integration also verifies the sidecar
  count and valid zero-publication 0/0 failed/cancelled captures.
- Finalized semantic reads reconstruct aggregate identity from bounded current
  header/outcome hashes and ordered expected SQLite chunk hashes. Missing, added
  or internally checksum-valid changed sidecars are corrupt. Tests reopen Store
  after deletion/replacement and exercise header, outcome, bundle, summary,
  reader and replay access. A recovered prefix hashed without an outcome remains
  valid and unknown/incomplete. Segment content checks still run on access;
  aggregate reconstruction does not reread all pixels.
- The finite abort-deadline path freezes FAILED, the deadline/refusal errors and
  absent abort outcome. A gated late callback can retire only internal bookkeeping.
  The test encodes the final snapshot while the callback is blocked, releases it,
  retires the concurrent caller, then proves canonical outcome bytes and the
  authoritative terminal state/reason/bundle references unchanged.
- The four outer error fields and optional abort outcome use explicit one-byte
  optional tags, 0 absent and 1 present. The conversion copies exact L3 absence
  and complete structured errors. AbortOutcome internal Evidence retains all
  three states and established false. Only run-outcome3.bin was corrected during
  acceptance; all nine accepted fixture binaries were compared byte-for-byte
  with the reviewed HEAD and remain unchanged.
- Checked event accounting counts every bundle plus actual TriggerEvents, never
  references alone. Bounded unique established command identities count once
  for identical late evidence and reject contradictions. Append, finalization,
  recovery and replay share this validation. Tests cover exact event/command
  limits, one event/command beyond, duplicate identical/contradictory commands,
  checksum-valid out-of-bound recovery records and final verification limits.
  Recorded executor errors obey the frozen category/code invariant; abort commands
  are OFF-only, with structural L1 emitter validation reused without fabricated
  exposure context. Unknown/Unavailable command states remain representable.
- Recovery removes structurally incomplete run.outcome.part only after the
  outcome-less verified prefix is durably FINALIZED. Complete corrupt sidecars
  refuse recovery and remain available for diagnosis. Tests check every partial
  envelope boundary and require no active-looking outcome.part after recovery.

The full ordinary and ASan/UBSan non-hardware suites each passed 45/45.
The ordinary suite took 105.75 seconds; the sanitizer suite took 250.12 seconds.
Architecture boundaries and whitespace checks passed. Legacy formats, frozen L2 ABI, calibration ingestion and L5 scope
remain unchanged. No known local implementation blocker remains; acceptance is
pending the new CI matrix and review.

## Original L4 exact changed files

Added:

- `docs/architecture/rawcapture-v3-validation.md`
- `docs/architecture/rawcapture-v3.md`
- `src/artifact-store/bundle_state.hpp`
- `src/data/include/mantis/projected_light_io.hpp`
- `src/data/projected_light_fields.inc`
- `src/data/projected_light_io.cpp`
- `tests/fixtures/storage/README.md`
- `tests/fixtures/storage/mantis01.bin`
- `tests/fixtures/storage/mantis02.bin`
- `tests/fixtures/storage/mantis03-captured.bin`
- `tests/fixtures/storage/mantis03-evidence.bin`
- `tests/fixtures/storage/mantis03-terminal.bin`
- `tests/fixtures/storage/mantis03-trigger.bin`
- `tests/fixtures/storage/mrawrec2.bin`
- `tests/fixtures/storage/mrawrec3.bin`
- `tests/fixtures/storage/reference_v3.py`
- `tests/fixtures/storage/run-header3.bin`
- `tests/unit/projected_codec.cpp`
- `tests/unit/projected_raw_capture.cpp`
- `tests/unit/projected_storage_values.hpp`
- `tests/unit/storage_legacy.cpp`

Changed:

- `CMakeLists.txt`
- `ROADMAP.md`
- `docs/architecture/milestone.md`
- `docs/architecture/projected-light-plugin-api.md`
- `docs/architecture/projected-light-sequencer.md`
- `docs/architecture/v0.4-laser-acquisition.md`
- `src/artifact-store/artifact-store.cpp`
- `src/artifact-store/include/mantis/artifact_store.hpp`
- `src/artifact-store/segments.cpp`
- `src/artifact-store/segments.hpp`
- `src/device-runtime/include/mantis/projected_run.hpp`
- `src/device-runtime/include/mantis/replay.hpp`
- `src/device-runtime/replay.cpp`
- `tests/integration/calibration_dataset.cpp`
