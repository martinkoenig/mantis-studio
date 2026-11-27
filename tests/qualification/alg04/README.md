# Recorded ALG-04 software qualification (#64)

`alg04-recorded-qualification` is a bounded Linux CTest, not a scanner benchmark or a service. It uses the existing Store writer/finalizer, RawCapture-3 reader and typed pipeline. The Architecture workflow and branch gates are unchanged. Run it with `ctest --test-dir build/<profile> -j1 -V -R '^alg04-recorded-qualification$'` after building `mantis-alg04-recorded-tests`. For native comparison use the existing Ubuntu 24.04 Studio **ON**, Debug, unsanitized x86_64 and ARM64 jobs, not the sanitizer job or an emulator.

## Inputs and outcomes

- One finalized capture: control-only startup followed by six distinct one-camera records. Report exact RawCapture ID/hash, segment and sidecar sizes, source ordinals and keys. Within run `alg04-run`, capture generation `alg04-capture`, ordinals 1–6 have bundle sequences 30–35, evidence ordinals 4–9, FrameSets 17–22, native images 7–12 and triggers 2–7. The `packed-alg02` / `alg04-extractor` observation keys keep the corresponding bundle sequences. Same immutable camera, rig, plane and normal policy on all six; no active-calibration lookup.
- Padded MIPI Y10P, 1280×960, stride 1603 bytes (1600 packed + three `e3` padding bytes). **ROI is the full 1280×960**: 1280 vertical profiles, 1,228,800 primary sample positions per invocation, plus subpixel-neighbor reads for detected profiles. No dark image. Stripe columns 512–767 reuse the #151 quadratic, `723 + ((x+3) mod 4) + 17d − 34d²` for `d=y−400` from −2 to +2; 48 DN otherwise. The independently expected peak is at `y=400.25`, curvature 68 and width 5. The existing long-double forward-projection oracle checks every accepted XYZ row within 2e-8 mm L2; it does not use the product inverse or triangulator.
- Stripes at ordinals 1 and 4 give `256/256/0` (sample/accepted/rejected), with 1024 missing profiles. Literal zero at ordinals 2 and 5 and flat 48 DN at ordinal 3 both give 1280 missing profiles, no rejected or saturated profiles, successful observations with no rows/attributes and manifest-only `empty_input` with `0/0/0` and no XYZ. Their optical evidence is **ON**. `max(DN−48,0) < 64` for both inputs: 0 clamps and 48 exactly cancels. This is the packed equivalent of the integrated #150 flat 12 DN RAW8 with background 12 and threshold 16, not a new empty classification. Padding is nonzero and is never a pixel. FNV-1a64 over the complete packed image with padding: stripe `290e6de172c53e25`, zero `e17ec31aa0b2b0e5`, flat `4ed51f2aa952b0e5`.
- Ordinal 6 has the same stripe bytes with exposure-effective optical **Unknown**. It preserves 256 subpixel rows with unknown-attribution masks, `256/0/256`, reason `unresolved_attribution`, no valid geometry and masked positive-zero placeholders. It is not the empty outcome. Tightening only `max_samples` to 255 produces explicit `busy` with a retained finalized observation receipt, no metric reservation in any state and an unchanged capture. Normal policy recovery is explicit and same-source; no automatic retry or fallback.
- `process_recorded` is synchronous. It runs two real `execute` calls, commits one observation and materializes the Tensor1 using that observation and its pinned calibration. Validation and typed readback inside `persist_result` are part of the ordinary operation. The test's extra typed consumer and oracle are outside that measurement window. `data::point_cloud` refuses each actual Tensor as `incompatible`. The independent virtual-scanner / example-points control is a genuine legacy PointCloud, not a conversion from ALG03. Positive public PointCloud delivery remains open; compact projection, empty output and linked/atomic publication semantics need a compatible contract. No PLY, Studio, mantisd, remote service or hardware qualification is added.

### Frozen calibration

`calibration.json.gz` freezes the existing `alg03_fixture::graph` mathematical M4 input: its dataset, two mono solutions and stereo/rig solution, as encoded by the calibration codecs, generated with OpenCV 4.6.0. The **only** substitutions in the frozen values are the generated target and two RawCapture IDs (`@target`, `@raw-a`, `@raw-b`); the bounded loader rebinds them to the fresh Store's real fixture objects and creates the ordinary typed artifacts. No recalibration or OpenCV solve occurs on the test runners. Uncompressed SHA-256 is `11a71005c053ea4f1e8b287e6a42fcb844ca66d62a17c72513f1f2a803efe677` (366,992 bytes). The fixture retains its off-line synthetic provenance. UUIDs and artifact hashes are per project; comparison uses the frozen fixture digest, numerical model/transform, policy, image hashes and exact binding checks, not cross-ISA equality of random IDs or binary64 result bytes.

## Bounds and evidence windows

| Scope | Bound and reason |
| --- | --- |
| Workload/storage | Seven records, one segment; capture ≤10 MiB for six 1,538,880-byte planes, small typed evidence, framing and sidecars. Image caps are exactly 1280 max dimension and 1280×960 max pixels. Normal policy admits 256 rows. Result ≤64 KiB for 256×121 ordered row bytes plus manifest, framing and reserve. Observation file ≤128 KiB; ALG03 source cap 2 MiB covers conservative alias-wrapper charging during repeated typed validation. Geometry is limited to 32 MiB/32 dependency nodes for this M4 graph and its decoded metadata. These are test workload limits inside the existing product maxima, not tighter product defaults. |
| Backing admission | Each semantic invocation keeps the 256 MiB default. Extraction charges one whole segment plus 1280×36 bytes of row/curvature backing for a nonempty output. Triangulation charges that observation backing, result payload, and the pinned persisted observation mapping when nonempty. Exact readback views share one backing per artifact (no observation buffer for N=0). A separate semantic run lowers its budget to one byte below the stripe input-plus-output charge and requires `busy` before downstream publication. |
| Lossless stream | Separate `execute_stream`: actual `BundleCaptureReader` records, configured ALG02 and existing observation consumer. Source capacity 2 / sink capacity 1. Real condition waits prove both blocked publications before drain; counters at release are source 4 pushed/2 popped and sink 1/0. Six exact FIFO identities and canonical observations match the six ordinary recorded results. No loss: both queues reach capacity, then 6/6/0 pushed/popped/dropped and retire closed. Separately report full occupancy durations and observed wait-to-release lower bounds, not admission deadlines or throughput. All input/in-flight/producer/reader-held views share one segment. Total bulk backing for the stream is bounded by one segment plus three 1280×36 row backings (sink slot, executor, consumer); this excludes metadata/test comparison strings and file cache. Cancellation, closure on failure and unwind have inherited `semantic-pipeline-replay` coverage. |
| Location refusal | Compiler uses real `compute::select`; only the consumer's required location changes. `remote` is `unsupported` before factory invocation, publication or reservation, with no local CPU fallback; matched local CPU graph runs. `mantis-worker` is not a remote service. |
| Application probe | Separate ordinary `process_recorded` call per measured input. Executable-only linker wraps file-backed `mmap`/`mmap64` (map calls/lengths, not live RSS), `munmap` and `memcpy`/`memmove` calls of at least 4 KiB. Exact device/inode and live map extents identify RawCapture source overlap; deliberately copying 4 KiB from a real mapped record proves the classifier is live. Require two raw segment maps (Store chunk integrity and reader scan), zero wrapped bulk bytes from them and no single observed full-plane copy. Bound cumulative wrapped output at 128 file maps/32 MiB and 512 bulk copies/16 MiB, allowing repeated bounded geometry, observation and result validation with headroom. This excludes inlining, smaller copies, shared-library internals, syscalls/page-cache/kernel copying and other unobservable transfers. It is not a claim of zero overall copying. |

The runner invokes three fresh processes: load/entry RSS baseline; fixture-only (after finalization and after verification); and the full campaign. Each fixture process uses a new project on the build filesystem, records its filesystem type and uses ordinary warm page-cache conditions after setup/readback—no cache dropping or fabricated disk state. Per pattern (stripe, literal zero), report one checked warmup then three checked samples in fixed order. Process CPU uses `getrusage` user and system microseconds around the real recorded call, with steady wall time for context. The internal opt-in two-slot receipt measures CPU/wall at the actual semantic executors after planning, plus existing node wall and retained-byte counters. It has no persistent fields; ordinary and diagnostic calls must produce the same observation/result rows and manifest up to the fresh observation ID. The unchanged strict ALG03 build-identity policy still applies, including the changed source fingerprint.

`ru_maxrss` is lifetime peak in KiB, not a resettable per-window measure. Entry, fixture phases, pre/post recorded calls, external readback and final campaign peaks are deliberately kept distinct; do not subtract them to claim an exact stage RSS. Compare CPU distributions (min/median/max/range, n=3), RSS controls and structural bounds against matching profiles. Shared-runner wall time is contextual only: no wall-time SLA, real-time rate, thermal or physical-scanner claim. A failed predicate or unmatched build, workload or runner invalidates that comparison; do not substitute sanitizer, emulated, historical or generic CTest evidence.

## Review receipts

Read the **successful test text** in each exact-candidate Architecture Studio ON job's `ui-m0-*` artifact (`ui-m1/full-ctest.log`). Correlate PR head, tested checkout/tree, job runner, arch/ELF, Debug/Studio/sanitizer profile, executable/config/fixture identity, storage conditions and all sample/queue/outcome lines. Algorithm build hashes identify the actual compiled processing inputs. Ordinary CI green without the matching receipts does not establish native characterization. Required independent source/test review, published head acceptance and protected integration stay separate.

## Public PLY follow-up

`alg03-public-ply` reuses the same frozen calibration and sensor recording. It
exports the 256-point stripe and a mixed result from that stripe through stock
`mantisd` and the loaded first-party PLY exporter. An independently determined
incidence threshold gives 128 accepted and 128 near-parallel rows. The checker
reads every vertex, compares ordered binary32 bits/spacing, and reports narrowing
error separately from the forward-geometry oracle. The same public path covers a
repeat via CLI, coincident valid points in a typed synthetic control, the two
zero-accepted outcomes, bad inputs, obstruction and legacy PointCloud. Project
inventory and stored bytes are compared around the exports.

The separate link-probe daemon runs the same main, runtime, adapter and exporter
on the same project. It measures the typed reader's retained mapped extent and
aligned replay allocations separately from the projection/native export. It
counts linker-visible `memcpy`/`memmove` of at least one sensor packed XYZ extent;
this excludes inlined/smaller copies, DSO internals, I/O and kernel/page cache.
The probe verifies a single 3,072-byte position allocation reaches the Registry
at the original address, with the original frame/calibration, and matches stock
output. A gate after the real part-file sync proves entered-state cancellation
and late destination obstruction before rename. No timing or RSS inference is
made from these counts. `alg03-ply-rounding` independently checks exact midpoint,
subnormal, carry, range/refusal and ambient floating-point controls.

Build `mantis-alg03-ply-probe-daemon` and `mantis-alg03-ply-rounding-tests`, then
run `ctest --test-dir build/<profile> -j1 -V -R '^alg03-(public-ply|ply-rounding)$'`.
Read `ALG03_PLY_CONTEXT`, `ALG03_PLY` and `ALG03_PLY_BACKINGS` in the same native
Architecture log artifacts described above; the two receipt populations are
separate. This is read-side software interoperability, not physical qualification.
