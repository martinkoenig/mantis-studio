# ALG-02: bounded local L1 subpixel localization

Issue: [#62](https://github.com/martinkoenig/mantis-studio/issues/62).
Status: SOURCE implementation; native execution and independent qualification
pending. This specification defines `org.mantis.l1-subpixel`, algorithm 1.0.0.
It estimates a local image-intensity vertex, in original source pixels. It does
not estimate laser-plane geometry, physical accuracy, calibrated uncertainty or
probability of correct attribution. Triangulation remains separate in #63.

## Integration and compatibility

The existing internal `l1_candidate_extractor(L1CandidateConfig)` semantic factory
accepts an optional `L1SubpixelConfig`. Absence retains the [ALG-01](l1-candidates.md)
1.0.0 integer selection, parameter serialization, columns and rejection semantics.
Presence explicitly selects the separate ALG-02 algorithm identity. Callers must
use a distinct producer stream/generation when publishing a different processing
configuration of the same input; replay does not invent a new generation.

The node still consumes one typed AcquisitionBundle and produces one schema-1
LaserObservation through the existing DAG, borrowed image layout, canonical
validation, observation consumer, codecs and Store. No public ABI, schema, service
DTO, laser state or recipe is added. The existing fingerprint includes the changed
extractor/header and unchanged image-layout source/header. Consequently the build
fingerprint changes for both modes; unchanged ALG-01 algorithm/parameter meaning
does not imply identical build provenance or encoded output across source builds.

The [ALG-01 input/evidence requirements](l1-candidates.md#input-and-evidence),
including explicit camera/role/generation selection, exact raw references,
dark-reference requirements, optical evidence and finite resource limits, remain
binding. Output copies the same exact selected camera evidence, FrameSet/bundle,
program/step, trigger, clock, calibration and source-scoped effective-state
availability. Fractional localization does not resolve unknown illumination.
Confidence remains unavailable; no confidence column or inferred optical state is
published. Source-proximal Q6A placement remains ADR-031 policy; this implementation
is an explicitly constructed host CPU reference, with no device throughput claim.

## Estimator and domain

Each ROI column retains ALG-01's strongest contiguous above-threshold run, peak
pixel `k`, topmost tie-breaking and second-run ambiguity comparison. Let
`s(y) = max(raw(x,y) - background_level - dark(x,y), 0)`, with absent dark zero.
All three samples use the same original column and validated row stride/packing.
Use `L=s(k-1)`, `C=s(k)`, `R=s(k+1)`, and `D=2*C-L-R`.

For the unique quadratic through these three samples at coordinates -1, 0, +1,
the vertex is:

```text
delta = (R - L) / (2 * D)
source_pixel = (original_x, k + delta)
```

This is the smallest local fit that estimates a vertex without an additional
noise/background/width model or iterative solver. It exactly recovers a sampled
quadratic vertex within its valid domain before f32 coordinate rounding. Real
laser profiles need not be quadratic. In particular Gaussian samples have
phase/width-dependent interpolation bias; the tests characterize that bias rather
than assert universal unbiasedness. A logarithmic Gaussian fit would require
stronger positive-signal/background/model assumptions and transcendental evaluation;
a centroid would require a broader window and truncation/background policy.

Refinement requires all of the following:

- The selected run passes ALG-01 width, saturation and competing-run gates.
- The run touches neither end of the ROI. Both neighbors of `k` are inside the
  ROI, even when more source pixels exist outside it. No scan-window extrapolation.
- The selected run has only one local maximum, including plateau maxima. A
  positive-to-negative slope transition counts one maximum; zero slopes retain
  the previous direction, and a run still rising at its end counts its last peak.
  Separate comparable runs retain ALG-01's inclusive ambiguity-margin rule.
- `C` is strictly greater than both immediate neighbors. Equal adjacent maxima,
  including exactly half-pixel symmetric sampled ties, are explicitly rejected.
- Both corrected neighbors meet `minimum_neighbor_signal` (default 4 DN), and
  `D` meets `minimum_curvature` (default 2 DN/pixel²). Neighbor floor must be in
  `[1, minimum_signal]`; curvature floor must be in `[1,2046]`. These are numerical
  conditioning/contrast gates, not a measured SNR or camera noise model.
- Source or dark clipping in the selected run or interpolation triplet rejects
  refinement. Clipping elsewhere retains the existing packet diagnostic only.
- The computed offset and coordinate are finite, the offset is within [-0.5,+0.5]
  pixel, and the coordinate remains inside the ROI.

RAW8 and Y10P samples are integer DN, respectively bounded by 255 and 1023;
configuration uses bounded integer thresholds and cannot encode NaN/Inf. Canonical
validation refuses nonfinite semantic evidence; floating point image attributes
are unsupported. Corrected samples fit signed 32-bit; `D` is in [-2046,2046] and
`R-L` in [-1023,1023]. Background plus dark is at most 2046. A positive conditioned
denominator is checked before binary64 division. Final coordinates are binary32;
the existing 8192-dimension limit bounds rounding. All neighbor index subtraction
follows the ROI checks, and existing checked image extents precede access.

## Quality and failure semantics

Every selected nonempty column still produces at most one candidate. A rejected
refinement keeps the original integer peak, sets `producer_rejected`, emits
`refinement_valid=0`, and records combinable reasons. No fractional fallback is
silently advertised as a successful localization. Low-signal/occluded columns
with no run remain successful missing samples, with no fabricated coordinate.

Existing `org.mantis.l1.reason` bits 1/2/4 retain saturation/competing-runs/width
meanings. ALG-02 adds:

| Bit | Reason |
| --- | --- |
| 8 | Insufficient interpolation neighbors inside ROI |
| 16 | Equal adjacent maximum / flat peak |
| 32 | Curvature below the configured positive floor |
| 64 | Corrected neighbor below its configured floor |
| 128 | Selected profile touches an ROI boundary |
| 256 | Numeric/domain guard refused the fit |
| 512 | Multiple local maxima inside the selected contiguous run |

Multiple maxima also set the canonical ambiguous flag. Rejection bits describe
observed algorithm conditions, not the physical cause. Weak curvature and neighbor
floors cannot identify all high-noise images; asymmetric occlusion, background
errors and profiles outside the model can still bias accepted vertices.

For N>0, ALG-02 adds two namespaced extension columns:

| Column | Type and interpretation |
| --- | --- |
| `org.mantis.l1.refinement_valid` | u8 `[N]`, unitless; 1 means the numerical refinement gates passed, 0 means rejected integer representative |
| `org.mantis.l1.curvature_dn_per_pixel2` | u32 `[N]`, `DN/pixel^2`; positive `D` when a complete triplet was read, otherwise finite zero; diagnostic only, even when another gate rejects the candidate |

These carry no universal confidence/accuracy meaning. Attribution masks remain
independent: a valid numerical refinement may have unresolved emitter/line IDs.
N=0 has no columns. Failure/cancellation publishes nothing; later valid calls may
recover deterministically. Saturation/missing/rejected packet flags retain ALG-01
meanings. The opaque configuration hash changes with mode and both new thresholds;
all previous identity, ROI, source and dark input bindings remain included.

## Ownership and work bounds

Input publications own their original backing through process completion. Mapping
and every-column/every-64-row cancellation checks remain in the production path;
the extra triplet reads are constant work. Outputs retain no image publication or
borrowed span. No image expansion/copy, history queue or per-sample heap allocation
is introduced. Work remains O(ROI area), scratch O(1). The existing initialized
32-byte row carries validity in one previously reserved byte. A second bounded
u32 bulk allocation holds curvature; at most `8192*(32+4)=294912` output backing
bytes, plus bounded metadata. Both allocations have independent immutable owners;
the node resource estimate charges their full widths and configured dark backing.

## Test source and admission plan

Risk is R1 for observation integrity/provenance and input lifetime. Decisive source
oracles use the actual production node, not a separate estimator helper:

- `l1-subpixel` checks an independently specified quadratic with vertex 4.25 and
  curvature 32 in RAW8/Y10P, padding, nonzero ROI and near-limit original y=8188.25.
  Typed DAG/consumer bytes equal direct output, all nested context fields equal
  the integer producer except declared algorithm/parameters, and unknown versus
  unavailable effective state remains distinct.
- Fractional sampled Gaussians use sigma 0.75/1/1.5/2 pixels, amplitudes 96/192 DN
  (times four for Y10P), phases -0.375..+0.375 in 0.125-pixel steps, and deterministic
  zero/±1-DN perturbations. Each group reports signed bias, RMS and maximum error;
  scoped assertions require max error <=0.15 pixel, absolute mean bias <=0.04 pixel
  and lower squared error than integer peaks. These bounds are test acceptance
  criteria, not measured results until executed. The mirrored perturbations make
  phase symmetry explicit, not a claim about arbitrary camera noise distributions.
- Failure and recovery cover absent/weak signal, one-row and image/ROI edges,
  flat tops, clipping, weak curvature/neighbor conditioning, contiguous multiple
  maxima, separate competing peaks, malformed stride/extent/overflow, unsupported
  floating images, NaN/±Inf evidence, invalid configuration and cancellation.
  Known dark subtraction and clipped dark-neighbor rejection use exact references.
- `l1-candidates` retains its ALG-01 tests and exercises both modes through the
  fence-observed cancellation/backing-retirement test and actual finalized
  RawCapture replay → node → observation Store/map. Exact repeated bytes and
  post-Store ownership, unchanged original bytes and mismatched raw hash refusal
  remain mandatory.

Current authorization permits source editing, formatting and cheap static checks
only. No compiler, configure, build, CTest, process fixture, benchmark or hardware
execution is claimed here. After Studio1 admission of fresh source/capacity pins,
run the following focused checks (at most four build jobs, serial CTest):

```sh
cmake --preset headless -DPython3_EXECUTABLE=/usr/bin/python3 -DBUILD_TESTING=ON
cmake --build --preset headless --parallel 4 --target mantis-l1-subpixel-tests mantis-l1-candidate-tests mantis-packed-image-tests mantis-l1-candidate-benchmark mantis-observation-codec-tests mantis-semantic-processing-tests mantis-synthetic-observation mantis-processor-v2-contract mantis-plugin-host
ctest --test-dir build/headless --parallel 1 -R '^(l1-subpixel|l1-candidates|packed-image|laser-observation-codec|semantic-pipeline-replay)$' --output-on-failure
```

For numerical mutation sensitivity, retain the sealed source hash, temporarily
reverse `right-left` in the production offset numerator, rebuild the subpixel
target and require `l1-subpixel` to fail nonzero at the independent 4.25-coordinate
oracle. Preserve the initiating diagnostic/exit, restore exact source bytes and
rebuild/re-run focused checks. This wrong-direction refinement is a realistic
numeric error. No test changes/weakening, repeated retries or permanent mutation.

All applicable final commands remain required after restoration:

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

Inspect actual registrations/modes/exits; do not assume historical suite counts.
Strict sanitizer means leak detection enabled and UBSan halt-on-error; the canary
verifier's positive detector case and all negative controls must actually pass.
No cached PASS or predecessor runtime equivalence is claimed for changed inputs.
Use only this worktree's build/cache paths and a separately admitted heavy campaign;
no shared physical writer or unapproved global environment/settings changes.

The paired sensor-sized Release benchmark now runs the identical quadratic input
through ALG-01/ALG-02 direct nodes and typed DAGs, both packings. It reports
p50/p95/p99/max wall time, allocations/bytes/largest allocation, linker-visible
copy/source-overlap counts and backing/high water. Exact integer versus 4.25
oracles, curvature/validity, source immutability, a largest-allocation bound of
40960 bytes and zero observed source-copy overlap guard this scoped fixture.
Interposition misses compiler-inlined/manual/DSO-internal copies; inspect source
as well. Keep raw diagnostics/source/compiler/machine identity; investigate any
material unexplained regression. Timing thresholds require actual admitted
measurement and review, not a fabricated portable FPS budget.

Source readiness is not qualification. Commit/push/Refs #62 PR require complete
actual local results and current-base disposition. All five exact-head PR jobs,
independent production/test source review, separate Martin merge authority and
all five original protected-push jobs remain mandatory. Make only one nonblocking
exact-SHA CI query after pushing, then hand off pending status without polling.
ARM64/non-Linux/Q6A/GPU, physical optical timing, metrology and sustained hardware
performance remain outside these host synthetic results.
