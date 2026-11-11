# ALG-01: host CPU single-line candidates

Issue: [#61](https://github.com/martinkoenig/mantis-studio/issues/61).
Status: implementation source; native qualification and independent acceptance
pending. This document specifies algorithm version 1.0.0. It makes no scanner
accuracy, physical illumination, sensor timing, Q6A throughput or production
readiness claim.

## Integration and placement

`pipeline::l1_candidate_extractor(L1CandidateConfig)` constructs an internal
`SemanticNode` with one AcquisitionBundle-1 input and one LaserObservation-1
output, deterministic CPU backend and immutable configuration. The existing typed
DAG compiler/executor, borrowed BufferViews, validators, codecs, RawCapture replay
and observation store remain authoritative. There is no second schema, storage
engine, exported C ABI, environment-selected configuration or QML processing.
The frozen ProcessorV2 descriptor has no arbitrary parameter dispatch; this
implementation uses the existing internal semantic factory extension instead of
inventing one. Daemon-side callers assemble the configured node explicitly; it is
not silently inserted into existing recipes or acquisition recording.

This reference is declared local CPU so the current planner can run software
fixtures. [ADR-031](../adr/031-x1-remote-observation-boundary.md) still places normal
X1 extraction on Q6A, before transport. Host execution does not qualify device
placement or require normal scanning to transport RAW to a host. Subpixel
localization, multi-line identity, geometry, triangulation and hardware control
remain outside this stage.

## Input and evidence

Configuration explicitly declares run, camera, role, stream/generation, the
single-line emitter component, pattern/revision/local line identity, producer
stream/generation, origin, optical frame and an immutable RawCapture-3 ID/hash.
Labels such as `L1` never select a component. The caller must resolve the raw
reference from its actual immutable input artifact. The extractor has no database
lookup; the observation Store independently verifies references to known artifacts
before persistence. Imported/unpersisted bytes cannot be advertised as an exact
persisted input through this entry point.

The input must validate canonically, be a captured bundle in the configured run,
and contain exactly one matching source camera/stream/generation. Selection uses
typed evidence and the existing evidence-to-image association order, never the
first image. Role, original dimensions, native sequence, available timestamps,
sync and calibration must agree with the image. Present component/stream metadata
must agree; image `role` is required. The optical frame must match explicitly.
Unsupported, absent or contradictory identity fails without output.

Supported images are canonical rank-two u8 RAW8 (GREY/RAW8, or no fourcc), or
explicit MIPI RAW10/Y10P packed bytes using `image_layout` and `Y10PView`. Y10P
width must be divisible by four. Checked layout/extent validation rejects corrupt
stride, truncated backing, empty dimensions and overflow. Nonfinite semantic
evidence is rejected by the canonical validator. Thresholds use bounded integer
DN units; they cannot represent NaN. RAW8 thresholds must fit 8 bits and Y10P
thresholds 10 bits. ROI is a scan window within the full original source image;
coordinates are never rebased. Pre-cropped/transformed inputs require an explicit
future preprocessing integration rather than an implicit coordinate transform.

Exposure-effective optical ON for the configured emitter and optical OFF for all
other participants permits valid emitter/line attribution. Optical OFF for the
target, another optically ON emitter, or a missing declared emitter fails. Unknown,
unavailable or non-optical effective evidence remains unresolved: extracted
candidates have zero attribution masks/indices and canonical unknown flags.
Commands, acknowledgements, electrical state and pixel brightness do not become
optical proof. Supplied software fixture evidence remains synthetic evidence.

## Profiles and outcomes

For each ROI column, scan y in increasing order. Subtract the configured constant
background and optional dark sample with unsigned saturating subtraction. Each
maximal contiguous run at or above `minimum_signal` is a profile. Rank profiles
by peak corrected DN, with earlier y winning exact ties; within a plateau select
the first maximum pixel. Retain the highest and second-highest peaks in constant
scratch space. Emit at most one integer-coordinate candidate per nonempty column.
The chosen run is checked after ranking; a narrow strong outlier is rejected
rather than silently replaced with a weaker plausible line.

| Condition | Published meaning |
| --- | --- |
| Valid profile | Source pixel, native peak DN and width; no confidence claim |
| No qualifying profile | No sample for this column; packet missing-profile flag/count |
| Two profiles within inclusive `ambiguity_margin` DN | Deterministic representative with canonical ambiguous and producer-rejected flags, competing-peaks reason |
| Chosen run contains a value at/above saturation threshold | Producer-rejected candidate, saturation reason |
| Chosen run outside inclusive width bounds | Producer-rejected candidate, width reason |
| Any source/dark saturation in an ROI column | Packet saturation-observed flag/count, including N=0 |
| Empty result | Successful N=0, no attributes, distinct from failed/unavailable extraction |
| Invalid input, cancellation, allocation/mapping failure | Failed invocation, structured error and no published result |

Reason bits (`org.mantis.l1.reason`, u32) are saturation=1, competing-peaks=2,
width-rejected=4, combinable. `org.mantis.l1.peak_dn` (u32, DN) and
`org.mantis.l1.profile_width` (u32, pixel) describe the selected original profile.
Canonical flags and dictionary/mask columns retain schema-1 meanings. Packet bits
are missing-profiles=1, rejected-profiles=2, attribution-unresolved=4,
saturation-observed=8. The bounded diagnostic reports profile/missing/rejected/
saturated-column counts. Missing signal may be compatible with occlusion; this
stage does not claim to diagnose physical occlusion. Rejected representatives
are candidates, not accepted measurements. No universal probability is fabricated.

Optional dark subtraction requires a distinct, exactly referenced captured image
from the same run/camera/stream/generation, matching dimensions/packing, emitter
participants, complete image metadata, known integration duration and exact
camera/rig/original calibration references. Every participating emitter needs
exposure-effective optical OFF for that dark exposure. Requested DARK or similar
brightness is insufficient. Unknown exposure or mismatched settings fails closed.
The exact immutable dark artifact and bundle key enter parameter provenance.
These conditions preserve the evidence supplied; they do not independently
qualify real camera gain stability, darkness or background models.

## Lineage, ownership and bounds

Output retains the exact selected CameraFrameEvidence (including calibration,
exposure, clock generations and sync), FrameSet/bundle/evidence keys, program/step,
triggers, clock mappings, requested emitters and source-scoped emitter evidence.
Observation sequence equals bundle sequence within the explicitly configured run
and producer generation; replay does not introduce counters or timestamps.
Completion time and preprocessing are explicitly unavailable. The producer names
algorithm 1.0.0 and the CMake-computed SHA-256 fingerprint of the extraction and
layout source/header inputs. This identifies those source inputs, not every build
dependency/toolchain. Effective configuration has a length-prefixed little-endian
FNV-1a-64 content identity (integrity, not authentication), with explicit input and
dark references. `org.mantis.L1CandidateParameters` is its opaque configuration
reference type, not an additional semantic packet or public codec.

No input image expansion or copy is performed. Mapping uses the supplied
cancellation token, including packed-layout validation and backing-fence waits.
Profiles check cancellation every column and every 64 rows, then before/after
publication. A backing fence must honor its token; this trusted native path does
not kill a noncompliant fence/callback. Calls retain the input through completion
and retain no input publication afterward. Output columns own one shared immutable
buffer independently of node/input lifetime. Instances have immutable configuration;
separate calls use separate local scratch/output. A configured dark bundle stays
owned by the factory/instance.

Limits are at most 8192 per dimension, 32 Mi pixels, 128 attributes per image,
256 metadata entries per image, 16 images per bundle and 128 MiB conservatively
charged complete input backing per bundle (aliases can be overcharged). Smaller
configured dimension/pixel bounds are allowed. Output is at most 8192 records of
32 bytes, plus bounded metadata; columns are strided slices of that allocation.
All record bytes, including reserved bytes, are initialized. Work is O(ROI area)
and scratch is O(1), with one bounded output allocation rather than per-pixel or
per-candidate heap allocation. Dark backing has its own same finite bound and is
included in the node resource estimate. It is private configured input, outside
the executor's per-publication retained-payload metric.

## Verification source and remaining qualification

`l1-candidates` registers the real node, typed DAG/consumer, independently specified
RAW8/Y10P intensities (including low bits/padding), tie/ambiguity/outlier/saturation/
missing-signal cases, invalid layouts/identities/evidence/bounds, deterministic
repeat, negative/recovery behavior, dark-reference restrictions, and fence-observed
cancellation/owner lifetime. Its RawCapture-3 integration finalizes/replays actual
synthetic bytes twice, extracts, persists/maps observations, compares exact bytes,
refuses a mismatched persisted-input hash and retains output after Store teardown.
Fixture-only references in isolated profile tests are synthetic placeholders;
the recorded-path case uses the actual finalized artifact hash.

Source formatting/static checks do not qualify this implementation. No compile,
CTest or benchmark result is claimed until the sole shared-heavy authority admits
the sealed inputs. The following are remaining commands, not execution receipts.
Use the normal sandbox; diagnose genuine failures and preserve their original
evidence. Native commands require the coordinator's finite sole-heavy admission.

Focused iteration, then a representative disposable mutation of the actual
production coordinate assignment (`row.y` shifted by one pixel): the existing
coordinate assertion must fail nonzero. Restore exact sealed source, rebuild and
prove the positive test again. Retain the original failure/restoration hashes.

```sh
cmake --preset headless -DPython3_EXECUTABLE=/usr/bin/python3 -DBUILD_TESTING=ON
cmake --build --preset headless --parallel 4 --target mantis-l1-candidate-tests mantis-packed-image-tests mantis-l1-candidate-benchmark mantis-observation-codec-tests mantis-semantic-processing-tests mantis-synthetic-observation mantis-processor-v2-contract mantis-plugin-host
ctest --test-dir build/headless --parallel 1 -R '^(l1-candidates|packed-image|laser-observation-codec|semantic-pipeline-replay)$' --output-on-failure
```

The focused build includes the existing regression executables and their semantic
contract plugins. Focused results are not final qualification.

Complete applicable local gates after restoration, with no input-equivalence reuse
assumed:

```sh
cmake --preset linux-debug -DMANTIS_BUILD_STUDIO=ON -DBUILD_TESTING=ON -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset linux-debug --parallel 4
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --preset linux-debug --parallel 1
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build/debug --parallel 1 -R '^(studio-home-qml|studio-projects-qml|studio-projects-resize|studio-projects-scroll|studio-devices-qml|studio-devices-stress|studio-scan-qml|studio-scan-stress|studio-scan-surfaces|studio-ui-m0-qml)$' --repeat until-fail:3 --output-on-failure
cmake --preset headless -DBUILD_TESTING=ON -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset headless --parallel 4
ctest --preset headless --parallel 1
cmake --preset sanitizers -DBUILD_TESTING=ON -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset sanitizers --parallel 4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitizers --parallel 1 --output-on-failure
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python3 -B tests/qualification/headless-lsan/test_verifier.py build/sanitizers/bin/mantis-headless-lsan-canary /usr/bin/c++ build/sanitizers/headless-lsan-evidence
python3 -B tests/ci/compiler_cache_test.py
python3 -B -m unittest discover -s tests/unit -p agent_efficiency_test.py -v
cmake -S . -B build/l1-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DMANTIS_BUILD_STUDIO=OFF -DBUILD_TESTING=ON -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/l1-release --parallel 4 --target mantis-l1-candidate-benchmark
build/l1-release/bin/mantis-l1-candidate-benchmark 64
```

The repeatable 1280x720 CPU benchmark reports p50/p95/p99/max wall latency,
allocation calls/bytes/largest allocation, input/output backing and typed DAG
retained-payload high water. GNU/Clang Linux linker wrappers also report observed
memcpy/memmove bytes and overlap with input pixels; inlined/manual/DSO-internal
copies are outside that probe. Source inspection and the largest allocation
corroborate the absence of an eager image copy; interposition alone cannot prove
that absence. Fixture construction and oracle/encoding costs are outside timed
regions. Record compiler/build mode, machine, source seal and all diagnostics;
do not convert local synthetic timings into measured sensor FPS. Material CPU,
allocation or copy regressions require investigation before publication.

Commit/push/PR remain gated on actual local results and coordinator current-base
disposition. Exact-head five-job PR CI, independent source/test acceptance,
separate merge authority and five-job protected-push acceptance remain mandatory.
ARM64, non-Linux, Q6A, GPU, physical timing, optical qualification and metrology
cannot be inferred from host synthetic tests.
