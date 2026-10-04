# Validation record — Skeleton v0.1.0

Validation date: 2026-10-03 (UTC; 2026-10-04 locally in Europe/Berlin for the resumed final run). The evidence belongs to this source delivery; it is not a promise of production scanner performance or unexecuted platform support.

## Executed environment

Ubuntu 24.04 x86_64, GCC 13.3.0, CMake 3.28, Qt 6.4.2 (offscreen software rendering for automation), Protobuf 3.21.12, SQLite 3.45.1, Python 3.12.3. All application/plugin targets compiled with warnings enabled. The final native builds produced no project-source warnings.

| Configuration | Build | CTest result |
| --- | --- | --- |
| Debug desktop, Qt enabled | Passed | 4/4 suites passed |
| Debug headless, Qt disabled | Passed | 4/4 suites passed |
| Headless ASan + UBSan | Passed | 4/4 suites passed; no ASan/UBSan diagnostics; final run uses `detect_leaks=0` |

The sanitizer build enables address/undefined behavior instrumentation and frame pointers and halts on undefined behavior. The earlier pre-interruption run passed with leak detection enabled. In the restarted execution environment, LeakSanitizer cannot inspect `/proc/<pid>/task` and terminates with an environment/ptrace error. The final ASan/UBSan repeat therefore used `ASAN_OPTIONS=detect_leaks=0`; no final leak-detection pass is claimed. The failed environment probe is retained in `docs/validation/leaksanitizer-environment-limitation.log`. CI keeps leak detection enabled for normal native runner environments. The deliberately crashing plugin terminates only its child host; a successfully detected child crash is expected in that fixture.

## Coverage

- **Core:** buffer publication/lifetime/alignment; non-host mapping rejection; host ABI allocation/release and immutable maps; attribute extent validation; double transform composition/versioning; Virtual Scanner DSO loading and lifecycle; deterministic frames and geometry; recipe graph typing/schema versions/cycles/backend rejection; fan-out; retained stateful streaming instances; every queue policy, blocking/cancellation and ordered stress; mapped packet round-trip; project lock; immutable final artifacts; interrupted capture with an invalid trailing partial; successful isolated processing and export; job cancellation.
- **C ABI:** the SDK header compiles as C and validates size/version prefix placement.
- **Boundaries:** forbidden foundation includes, frontend/runtime separation and first-party public SDK usage. CMake also checks actual transitive target dependencies at configure time.
- **Integration:** real daemon/CLI/Python/Qt/host processes; local token rejection; unknown-field tolerance and protocol-version rejection; identical Python/CLI/Studio/replay PLY payloads; capture continues during unrelated plugin crash; plugin failure/state/diagnostics; actual Studio viewport has nonzero dimensions and populated geometry; actual Studio restart; hard daemon kill/restart; explicit capture recovery and reprocessing.

## Reference workflow acceptance

| Steps | Evidence |
| --- | --- |
| 1–3: daemon, Studio, discovery | Real process launch, plugin device descriptor, asynchronous Studio snapshot |
| 4–8: capture, public contracts, processing, artifact, visualization | Studio automation uses the same bridge slots as controls; viewport-ready condition verifies populated, visible geometry |
| 9: plugin export | Studio-created export matches deterministic reference bytes |
| 10–11: CLI and Python | Independent control clients create/export through the daemon |
| 12–13: isolated crash and survival | Host abort is a failed plugin/job; daemon and Studio remain alive; raw frame count increases |
| 14–15: UI restart and state | Qt client exits/restarts; runtime capture remains active; finalized artifacts remain valid |

The actual Qt screenshot in `docs/images/studio.png` was inspected after a layout correction. It shows the 3,072-point reference cloud, capture status, artifact state, completed jobs and runtime diagnostics. It is not a mockup.

## Performance observation

A local Debug run of `mantis-benchmark` transferred 100,000 integer queue items in approximately 1,357 ns/item, reached the configured high-water mark of 64 and dropped zero items. This is a workstation observation, not a throughput guarantee, an ARM measurement, or scanner benchmarking. Pipeline jobs emit individual node timings into diagnostics. Capture snapshots expose frame count, dropped count and queue high-water mark.

## Not executed / not claimed

- Native Linux ARM64 CI is configured but was not run on GitHub during this task.
- Windows x86_64 and macOS ARM64 were not built or tested.
- No physical Q6A, camera, laser, GPU interoperability or real scanner accuracy tests were performed.
- Process-kill recovery is demonstrated; hardware power-loss durability across storage devices/filesystems is not certified.
- No packaging installer, long-running soak, large-cloud renderer benchmark or adversarial sandbox/security audit is claimed.

Reproduce with the commands in `BUILDING.md`. CTest evidence logs from the final successful validation and the separate LeakSanitizer environment limitation are included in `docs/validation/`.

## Historical v0.2 acquisition validation before Q6A findings — 2026-10-04

The earlier sections are historical v0.1 evidence. The v0.2 continuation preserves
those workflows and adds the following executed checks; physical hardware is absent.
Local Ubuntu 25.10 x86_64 toolchain: GCC 15.2.0, CMake 3.31.6, Qt 6.9.2, Protobuf 3.21.12,
SQLite 3.46.1 and Python 3.13.7. CI uses Ubuntu 24.04/GCC 13 and native ARM64.

| Local configuration | Build | CTest |
| --- | --- | --- |
| Debug Studio ON | PASS, no project-source warnings observed | 9/9 PASS, 9.54 seconds |
| Debug Studio OFF | PASS | 9/9 PASS, 3.67 seconds |
| Headless ASan/UBSan | PASS | 9/9 PASS, 4.74 seconds; no sanitizer diagnostics |

The final run used `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`. Leak detection passed. The historical v0.1
LeakSanitizer environment failure is not a current
v0.2 limitation. The deliberately crashing isolated plugin remains an expected
child-process failure; daemon survival is tested.

| Suite | Evidence |
| --- | --- |
| legacy-abi | C DSO built against frozen v0.1 header loads in current host; host buffers survive callbacks; existing interface behavior retained |
| acquisition | Stable roles across renumbered/swapped nodes; missing/ambiguous/RAW8 unavailable; deterministic pairing and metadata; EAGAIN, gaps, repeats, lag, stall, disconnect, mode/timestamp failures; retained buffers after stop |
| acquisition-integration | Real daemon/CLI/Python/C++ processes; CLI capture start/status/stop, reconnect, capture-level provenance/clocks/calibration, metrics, leased preview immutability/release; actual Studio dual views; hard kill/restart, recovery jobs, finalized hardware-free replay and verification |
| acquisition-qos | Promise-gated writer failures/saturation, bounded raw queue, preview isolation/drops, cooperative shutdown and final in-flight FrameSet drain |
| raw-capture | Many records per segment, two byte/metadata-identical replays, deterministic indexing, provenance, incomplete header/payload recovery, unindexed closed segment, corrupt complete record/index mismatch refusal, cancellation retry and old schema-1 path |
| core / c-abi / boundaries / acceptance | Existing Virtual Scanner/CLI/Python/C++/processing/export/recovery/crash-isolation baseline; FrameSet codec and invalid table version/size validation; C header and Qt/Linux/runtime boundaries |

Remote [run 37191026776](https://github.com/martinkoenig/mantis-studio/actions/runs/37191026776)
for `d3cfc7c` was inspected: x86_64 Studio ON/OFF, native ARM64 Studio ON/OFF and
ASan/UBSan all **PASS**. The same complete matrix also passed for the recovery and
shutdown code checkpoint `2927889` in inspected
[run 37192144976](https://github.com/martinkoenig/mantis-studio/actions/runs/37192144976).
Newer checkpoints require their own CI conclusion; no earlier run is evidence
for an unexecuted HEAD. CI trigger/matrix policy is unchanged.

### Informational Release benchmark

Executed `build/release/bin/mantis-acquisition-benchmark /tmp 64` on the final
review tree. The exact JSON is in
[`docs/validation/v0.2-acquisition-release-benchmark.json`](../validation/v0.2-acquisition-release-benchmark.json).
Fixture: two reusable owned 1280×800 RAW8 buffers, 64 FrameSets,
131,072,000 payload bytes, 131,146,944 container bytes, two segments and two segment
index transactions. Construction/queue phases use 10,000 observations. This short
cache-backed fixture is neither sustained disk speed nor physical acquisition.

| Measurement | Observed |
| --- | --- |
| FrameSet construction | 86.43 ns/observation |
| Raw queue throughput | 1,054,923 items/s; capacity/high water 32/32; zero drops |
| Sequential append payload | 498.52 MB/s |
| Finalization | 0.5678 s |
| Append plus finalization payload | 157.79 MB/s |
| Verified replay payload | 428.60 MB/s |
| Validated index scan | 0.2397 s |
| Process peak RSS | 76,115,968 bytes |

Append includes segment-boundary sync/indexing where reached; finalization seals
the active segment and verifies data separately. Combined throughput is reported
so final integrity costs are visible. The fixture's initial two image copies took
0.000612 s; these buffers are then reused, so this does not measure repeated
V4L2 copying. Index timing includes checksum validation, not just SQLite lookup.
Peak RSS includes transient mapped pages and the benchmark process; bounded queue
and per-segment mapping sizes are additionally verified structurally/by tests.
There is no hardware throughput CI threshold.

Theoretical dual RAW8 payload at 1280×800×2×120 is **245.76 MB/s / 234.375 MiB/s**,
before overhead. The measured fixture does not prove sustained recording at that
rate; validation must include actual storage on Q6A. SD throughput may be
insufficient; evaluate NVMe rather than assuming its performance.

### Hardware acceptance and known limits

At this earlier checkpoint, Q6A build, real discovery, acquisition, throughput
and physical synchronization were unexecuted. The hardware-gap record below
supersedes this earlier status.
Use [the exact procedure](../hardware/x1-q6a-acquisition-validation.md), including
hardware-free double replay and abrupt daemon termination/recovery.

Production uses native V4L2 MMAP and **one acquisition copy**, followed by shared
immutable fan-out. DMABUF zero-copy is deferred. Preview file writes and grayscale
display conversion add separate preview costs. Real ioctl behavior is not proven
by backend fixtures; that checkpoint required preconfigured enabled media links/pads.
At that checkpoint only RAW8 recording was implemented. Exposure/gain snapshots are queried when
supported, not fabricated per-exposure telemetry. V4L2 timestamp source varies
by driver; software timestamp/arrival deltas and sequence agreement do not measure
true optical exposure skew. Hardware-sync configuration is an operator assertion.

Recovery refuses corrupt complete records and retains their earlier verified
prefix on disk for diagnosis; it does not silently salvage/relabel corruption.
Power-loss guarantees are bounded by synchronized segments and filesystem/device
behavior, not certified by process-kill tests. Local preview requires shared
filesystem access. Windows/macOS and physical ARM64 hardware were not tested.
Project manifest and SQLite schema remain 1; schema-1 artifacts retain their
original path. New RawCapture schema 2 / MANTIS02 do not require migration or rewrite
of v0.1 data. No later scanner algorithm or release tag is included.

Final CTest logs are preserved in `docs/validation/v0.2-{debug,headless,sanitizers}-ctest.log`.

## Q6A hardware-gap continuation — 2026-10-04 (historical checkpoint)

This section supersedes the earlier v0.2 mode/setup limitations. The architecture,
project format, recorder, protocol/data plane and public ABI remain in place.
The reference mode is now 1280×720 packed Y10P, Y10_1X10 upstream and VBLANK=196,
requested target 120 FPS. RAW8 remains supported and version-1 profiles preserve
externally configured semantics. ADRs 024–025 document scoped plugin setup and
the additive packed image byte contract; project/SQLite schema remains 1 and
RawCapture schema/container remains 2. No v0.1 data rewrite or tag change occurs.

### Automated implementation evidence

The local toolchain remains Ubuntu 25.10 x86_64 / GCC 15.2 / Qt 6.9.2.

| Configuration | Build | Test evidence |
| --- | --- | --- |
| Debug Studio ON | PASS | 13/13, 21.44 s |
| Debug Studio OFF | PASS | 13/13, 12.33 s |
| Release Studio OFF | PASS | 13/13, 11.31 s |
| ASan + UBSan Studio OFF | PASS | 13/13, 13.61 s, detect_leaks=1 and halt_on_error=1 |

These final runs include the route-check/rollback review fixes.
Stored logs: `docs/validation/v0.2-q6a-gap-{debug,headless,release,sanitizers}-ctest.log`.
No sanitizer errors occurred. The intentional isolated crash fixture remains a
successful daemon-survival test, not an unreported parent crash.

Added coverage includes disabled-link route selection; explicit entity/pad
validation; missing/ambiguous entities; unsupported media-bus/RAW10 format;
scoped incoming conflicts and EBUSY-gated source conflicts; unrelated RGB/fan-out
preservation; VBLANK set/read-back; format/link/control read-back mismatch;
LEFT/RIGHT setup and STREAMON failures with context; failure rollback, committed
setup and incomplete rollback reporting. Required packing metadata rejection, known Y10P bit vectors and padded rows
prove sample access and display reduction while preserving raw bytes. RAW8 and
Y10P both traverse daemon → protocol → CLI/Python/C++ → actual offscreen Studio
dual preview, segmented recording, repeated deterministic replay/digest,
hardware-free restart and interrupted-capture recovery. A STREAMOFF failure
remains explicit and leaves the artifact RECOVERABLE. ABI-v1 C DSO and old
RawCapture compatibility tests still pass. Listener idle regression exceeds six
seconds, then verifies control requests and shutdown without accept error spam.
Genuine listener failures remain errors; a failed listener is not retried in a
busy loop. POSIX and Windows branches keep bounded connection read/write timeouts;
Windows/macOS native execution remains untested.

CI was inspected for these continuation checkpoints:

| Commit | Architecture run | Result |
| --- | --- | --- |
| 0b7177c | [37197914514](https://github.com/martinkoenig/mantis-studio/actions/runs/37197914514) | all five jobs PASS |
| dfd8628 | [37198468003](https://github.com/martinkoenig/mantis-studio/actions/runs/37198468003) | all five jobs PASS |
| 54ffb5e | [37198878341](https://github.com/martinkoenig/mantis-studio/actions/runs/37198878341) | all five jobs PASS |
| f096603 | [37199422239](https://github.com/martinkoenig/mantis-studio/actions/runs/37199422239) | all five jobs PASS |
| c08452e | [37201194604](https://github.com/martinkoenig/mantis-studio/actions/runs/37201194604) | all five jobs PASS |
| 95975df | [37202101702](https://github.com/martinkoenig/mantis-studio/actions/runs/37202101702) | all five jobs PASS |

The matrix remains Ubuntu x86_64 Studio ON/OFF, native Ubuntu ARM64 Studio ON/OFF
and ASan/UBSan. No workflow or tag-trigger policy change was made. Final HEAD
requires its own inspected conclusion.

### Informational Y10P Release fixture

Executed `build/release/bin/mantis-acquisition-benchmark /tmp 64 Y10P`.
Exact [JSON](../validation/v0.2-q6a-gap-y10p-release-benchmark.json) is retained.
Two reusable 1280×720 byte-packed buffers, stride 1600, 64 FrameSets, 147,456,000
payload bytes, 147,559,616 container bytes and three segments/SQLite segment
commits. Construction/queue phases use 10,000 observations. No physical V4L2
frame is acquired; only the initial owned-buffer copy is measured.

| Measurement | Observed |
| --- | --- |
| FrameSet construction | 108.76 ns/observation |
| Raw queue | 1,292,460 items/s, capacity/high water 32/32, zero drops |
| Append payload | 365.45 MB/s |
| Finalization | 0.5670 s |
| Append plus finalization | 151.94 MB/s |
| Verified replay payload | 480.62 MB/s |
| Validated index scan | 0.2642 s |
| Initial fixture copy | 0.001044 s |
| Process peak RSS | 78,303,232 bytes |

This short cache-backed workstation result is not sustained disk speed, Q6A
speed or real 120 FPS evidence. The theoretical dual 1280×720 Y10P payload at
120 FPS is **276.48 MB/s / 263.671875 MiB/s** before overhead. 1280×800 Y10P would
be 307.20 MB/s, but that mode is unvalidated. The user reported approximately
32.04 MB/s on the current `/dev/mmcblk1p3` ext4 microSD. That is a storage-device
observation, not evidence of a RawCapture implementation ceiling. No NVMe is
installed yet; suitable storage and full-rate recording acceptance remain pending.

### Historical hardware evidence at the setup checkpoint

**USER-REPORTED PASS:** Q6A ARM64 build, automated tests on Q6A, real plugin load,
CAMSS traversal, both OV9281 sensors, stable LEFT/RIGHT assignment and dynamic
capture-node resolution. **OBSERVED FAILURE:** initial GREY 1280×800 STREAMON
returned EPIPE against upstream Y10_1X10 1280×720. **KNOWN-GOOD EXTERNAL SETUP:**
1280×720 Y10_1X10/Y10P, stride 1600, sizeimage 1,152,000, VBLANK=196, as documented
in the [hardware procedure](../hardware/x1-q6a-acquisition-validation.md).

**PENDING USER EXECUTION:** plugin-owned setup and Y10P STREAMON, actual dual
FrameSets/receive FPS, ten-second lossless capture, NVMe performance, real Y10P
replay and crash recovery, physical synchronization and optical exposure skew.
No pending item is a PASS. VBLANK/read-back, requested FPS, driver intervals,
V4L2 timestamp delta and host arrival delta are distinct from measured optical
exposure timing. Exposure/gain are start-time queried snapshots or unavailable.

Memory path: MMAP plus one copy before QBUF; exact packed bytes share immutable
fan-out. Preview converts on the client worker with LATEST_ONLY semantics.
DMABUF/external-buffer zero-copy and physical trigger programming are deferred.
Selected upstream state rollback is attempted and verified, but cannot be atomic
against another camera setup process. Capture-node S_FMT is not restored on
failure; selected media configuration remains after successful stop. Provisioning,
permissions and exclusive use of selected resources remain operator duties.
The raw recorder/durability design is unchanged. Process crash tests do not certify
power-loss behavior of an untested storage device.


## Timestamp-pairing continuation — 2026-10-04 (historical 4 ms policy)

This section supersedes historical pending setup/streaming results above.
**USER-VALIDATED PASS:** Q6A ARM64 build/tests, discovery with four mutable routes
disabled, plugin-owned CAMSS setup, Y10_1X10 pads, both VBLANK=196 read-backs,
Y10P 1280×720 dual STREAMON, real packed RAW10 and ~119.223228 receive FPS on each
camera. The reported capture produced/committed 141 FrameSets, 141 LEFT/RIGHT
frames, zero native gaps/errors, zero raw recorder drops/saturation and queue
high water 12/32. Artifact FINALIZED: 325,625,318 bytes, five chunks,
`fnv1a64:26a3ea204d51863c`. Two-pass real replay verified 141 FrameSets/LEFT/RIGHT,
continuous sequences, raw integrity PASS and replay PASS.

That capture used a temporary diagnostic 100 ms tolerance. Equal native counters
showed ~51.5–51.9 ms V4L2 offset (final 51,786,000 ns), host delta 5–8 ms,
`linux.monotonic` clocks and flags 8193. Independent counter origins are not
exposure correspondence. The profile stays at 4 ms; [ADR-026](../adr/026-bounded-software-observation-pairing.md)
replaces software counter equality with bounded timestamp-nearest pairing.
**PENDING USER EXECUTION:** sustained ten-second/full-rate NVMe recording,
real process-crash recovery after correction, hardware trigger
synchronization, optical exposure skew and 1280×800 mode. The short successful
recording does not certify sustained microSD throughput. No new performance
benchmark or physical timing measurement is claimed for this pairing change.

### Added deterministic coverage

- Equal origins/exact or close timestamps; LEFT/RIGHT startup six frames apart;
  stable unequal-counter pairing and independent origins with offset −100.
- Nearest lookahead, deterministic ties, tolerance failures, bounded two-slot
  queues, total startup limit, clock mismatch/unknown/change and native gaps,
  repeats, reversals and timestamp discontinuity after alignment.
- Hardware-configured mode retains counter equality plus timestamp bounds;
  SyncQuality stays software and optical exposure skew is unavailable.
- Thirty-eight format/scenario combinations exercise the loaded plugin through
  the public ImageStream adapter. RAW8 and Y10P pixel bytes, native counters,
  clocks/timing, calibration, sync and pairing metadata survive two exact canonical
  replays through the generic recorded source. Native setup remains unchanged. A delayed-ready first pair is rejected after
  the startup deadline even when the caller requests a longer blocking read.
- Real daemon/CLI/Python/C++ and offscreen Studio tests use ±6 counter offsets;
  no raw drops/saturation, explicit startup/tail accounting, exact replay digest,
  restart/recovery and the public-client Q6A validation command remain covered.
- Python Capture.stop returns the capture error immediately; successful asynchronous
  finalization waits and job-error propagation are independently covered.

All thirteen baseline suites remain, plus pairing policy, loaded pairing/replay
and Python stop tests (sixteen total). ABI-v1 C/Virtual Scanner, old RawCapture/
project readability, native route setup, bounded QoS and control-listener tests
are retained. New tests use deterministic sequences/timestamps and readiness
waits rather than sleeps to force outcomes.


### Final local matrix and checkpoint CI

The final runs include the startup-deadline guard and executable Q6A validation
handoff. No project-source compiler warnings or sanitizer reports were observed.

| Configuration | Build | CTest |
| --- | --- | --- |
| Debug Studio ON | PASS | 16/16 PASS, 27.06 s |
| Debug Studio OFF | PASS | 16/16 PASS, 16.57 s |
| Release Studio OFF | PASS | 16/16 PASS, 16.42 s |
| ASan + UBSan Studio OFF | PASS | 16/16 PASS, 20.74 s; leak detection and halt-on-error enabled |

Logs: `docs/validation/v0.2-software-pairing-{debug,headless,release,sanitizers}-ctest.log`.
The theoretical 720-line dual packed payload remains 276.48 MB/s at 120 FPS;
no new throughput benchmark is claimed. Pending buffer capacity is two frames
per camera (at the validated size, at most 4,608,000 bytes of pending payload,
separate from the existing driver, recorder and preview bounds).

| Checkpoint | Inspected architecture run | Conclusion |
| --- | --- | --- |
| bc870cb | [37206748881](https://github.com/martinkoenig/mantis-studio/actions/runs/37206748881) | all five jobs PASS |
| cf752a2 | [37207910251](https://github.com/martinkoenig/mantis-studio/actions/runs/37207910251) | all five jobs PASS |
| 3a287a2 | [37211493832](https://github.com/martinkoenig/mantis-studio/actions/runs/37211493832) | all five jobs PASS |
| a64c2d1 | [37213924718](https://github.com/martinkoenig/mantis-studio/actions/runs/37213924718) | all five jobs PASS |
| bc3e93f9886d741a946293da7d69f96e25f93bff | [37221849934](https://github.com/martinkoenig/mantis-studio/actions/runs/37221849934) | all five jobs PASS, inspected |

The unchanged matrix covers native Ubuntu x86_64/ARM64 Studio ON/OFF and
sanitizers. The final `bc3e93f` documentation/handoff checkpoint has now been
inspected: run 37221849934 passed sanitizers and all four Ubuntu 24.04
x86_64/ARM64 Studio ON/OFF jobs. Later checkpoints require their own inspection.

Review against origin/main and the preceding hardware-gap HEAD found no new
native media/backend or profile change, kernel/Qt leakage, raw-path unpacking,
unbounded queue, frame payload in Protobuf, per-frame storage transaction,
ABI/layout/schema modification or synchronization overclaim. Pairing metadata
is additive; old recordings replay without re-pairing or fabricated metadata.
The reference profile remains 1280×720 Y10P, Y10_1X10, VBLANK=196 and 4 ms.


## Official Q6A validation harness continuation — 2026-10-04 (historical favorable start)

**USER-VALIDATED successful 4 ms start; later cold-start failure limits its scope.** The user
supplied a Linux V4L2 result for the v2 reference profile, 1280×720 Y10P,
Y10_1X10, VBLANK=196, `hardware_sync_configured=false`,
`max_v4l2_delta_ns=4000000`. Produced/committed FrameSets: **140/140**;
queue high water **11/32**, raw drops/saturation **0/0**;
raw bytes **323,488,370**, writer **240.798 MB/s**.
Timestamp-nearest pairing selected **-2,861,000 ns** (absolute **2.861 ms < 4 ms**),
native offset **+8**, startup unmatched LEFT/RIGHT **8/0**, shutdown unmatched
**0/1**. Pairing failures, pending saturation, timestamp discontinuities, both
native sequence gaps and both capture errors were all **zero**. Plugin-owned
selected routes were verified; both VBLANK read-backs were **196**, strides
**1600**, buffer sizes **1,152,000**. Replay verified **140 FrameSets, 140 LEFT,
140 RIGHT, two passes**, continuous sequences, **raw integrity PASS, replay PASS,
short_pairing_check PASS**. These are user-provided hardware results, separate
from the developer's automated tests. They supersede the earlier pending
corrected-pairing status; the diagnostic 100 ms result remains historical.

The official entry point is `scripts/validate-x1-q6a.sh --smoke`; `--full` adds
Debug/Release software tests and discovery with the four mutable measurement
links disabled. It retains reports/captures, dynamically resolves controller and
sensor/video nodes, protects unrelated media links with an explicit runtime
profile snapshot, restores the prior active service and temporary links, and
stops only its own daemon. Generic acceptance removes both caller X1 variables;
CTest deliberately injects a discoverable X1 fixture to exercise that isolation.
The previous sixteen suites plus harness regressions now total seventeen on Linux.
See [the hardware guide](../hardware/x1-q6a-acquisition-validation.md) for CLI,
resource behavior, artifacts, and lower-level manual procedures.

**PENDING at this checkpoint (later smoke/full evidence below):** a real Q6A
execution of the new harness, sustained NVMe/equivalent
storage acceptance, real process-crash recovery after corrected pairing,
physical trigger synchronization, optical exposure skew and 1280×800 acquisition.
No duration or reported short-run writer speed upgrades sustained storage to PASS.


### Harness software verification and checkpoints

| Configuration | Result | Retained log |
| --- | --- | --- |
| Debug, Studio ON | 17/17 PASS, 27.79 s | `v0.2-q6a-harness-debug-ctest.log` |
| Debug, Studio OFF | 17/17 PASS, 18.15 s | `v0.2-q6a-harness-headless-ctest.log` |
| Release, Studio OFF | 17/17 PASS, 17.12 s | `v0.2-q6a-harness-release-ctest.log` |
| ASan + UBSan, Studio OFF | 17/17 PASS, 22.36 s | `v0.2-q6a-harness-sanitizers-ctest.log` |

All four runs used `TMPDIR=/dev/shm` and exported `MANTIS_X1_PROFILE` and
`MANTIS_X1_FAKE=normal` in the invoking shell. The sanitizer run enabled
`ASAN_OPTIONS=detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1`.
Removing the acceptance isolation in a temporary copy reproduced the exact
one-Virtual-Scanner assertion failure with the discoverable fixture profile.
The harness suite contains twelve hardware-free tests, including real shell
EXIT/INT/TERM supervision, owned daemon termination, unrelated-process survival,
service restoration, partial measurement-link restoration, storage/fake/profile
rejection, runtime-profile preservation, dynamic sensor/video/pad read-back,
CTest-failure reporting and explicit absence of sustained-storage certification.
The final media header parser accepts optional route counts; its focused suite
passed after that adjustment. The actual `--full` wrapper also built and passed
17/17 Debug and Release tests on the workstation, then correctly returned FAIL
at `media-controller` because `platform:acb3000.isp` was absent. Reports were
retained, no daemon started, and no service or media links changed. This is a
software orchestration/failure check, not a Q6A hardware result. `bash -n` and Python compilation passed.
`shellcheck` was unavailable in the development environment; no ShellCheck pass
is claimed. The unchanged architecture workflow runs the seventeen-suite matrix
on native Ubuntu 24.04 x86_64/ARM64 Studio ON/OFF plus sanitizers; its final pushed
HEAD must be checked separately from the historical bc3e93f run above.

Focused implementation checkpoints: `ac64ec4` isolates generic acceptance,
`002a019` adds the official harness and twelve-case regression suite,
`9af1aab` handles media-ctl entity headers that include route counts.


## Free-running phase/drift correction after real harness evidence

The user has now run the official harness on real Q6A. **SMOKE PASS** under the
former 4 ms criterion: native offset +7, startup unmatched 7/0, selected V4L2 delta
+1.686 ms, 123/123 FrameSets, zero pairing/raw failures, FINALIZED and deterministic
replay/integrity PASS. **FULL:** Debug 17/17 PASS, Release 17/17 PASS, disabled-link
discovery PASS and plugin-owned cold setup reached acquisition. Capture then
**FAILED** with `Camera timestamp delta exceeds profile pairing limit`.

The earlier 140-FrameSet -2.861 ms success and this smoke success establish
favorable starts only. **4 ms is not robustly validated for arbitrary free-running
startup phase.** At measured ~119.27 FPS, T ≈ 8.384 ms, T/2 ≈ 4.192 ms. A healthy
pair of independent periodic streams can exceed 4 ms without native gaps/errors.
No automatic retries or favorable-phase waits address this limitation.

[ADR-026](../adr/026-bounded-software-observation-pairing.md) now separates
software timestamp correspondence from hardware synchronization. The candidate
software reference is **5 ms**, nominal half-period plus 20% headroom at requested
120 FPS, approximately 0.808 ms over measured half-period. Software defaults are
period-derived; explicit bounds below nominal half-period are rejected and
observed intervals must fit the configured half-period bound. This margin is an
engineering policy, not a measured optical exposure skew or future cadence proof.

Steady-state re-alignment permits at most two explicitly identified exclusions
between published pairs, two pending observations per camera and two reads per
call, bounded by the stall timeout since the last publication. Counts and budgets
survive across calls. Every native received counter is checked before pairing;
loss/repeat/reversal, clock/time discontinuity and exceeded bounds still fail.
`steady_state_unmatched_left/right` remain separate from raw recorder drops.
Per-pair exclusion identities/timestamps and exact published associations persist
in RawCapture metadata; exact replay never re-pairs. A terminal un-published
exclusion remains explicitly in final diagnostics. Hardware mode keeps equal
counters, its configured bound, a 4 ms default and no re-alignment. SyncQuality
remains software and optical exposure skew remains unavailable.

**PENDING real evidence:** the new 5 ms policy and counted steady-state
re-alignment on Q6A, sustained NVMe/equivalent storage, process-crash recovery,
physical trigger synchronization, optical timing and 1280×800 mode. The harness's
operational smoke/software/discovery success is now user-validated; its cold
capture failure is retained rather than upgraded to PASS.


### Automated correspondence validation

| Local configuration | Result |
| --- | --- |
| Debug Studio ON | 17/17 PASS, 32.41 s |
| Debug Studio OFF | 17/17 PASS, 21.33 s |
| Release Studio OFF | 17/17 PASS, 19.43 s |
| ASan + UBSan Studio OFF | 17/17 PASS, 33.96 s; leak detection enabled |

Logs: `docs/validation/v0.2-free-running-{debug,headless,release,sanitizers}-ctest.log`.
Debug retains console output; the other three retain CTest's detailed `LastTest.log`.
All used `TMPDIR=/dev/shm` and an exported X1 profile; the final full Debug run
also exported `MANTIS_X1_FAKE=normal`. The focused Debug pairing suites passed
again after ordering clock-change validation ahead of observed-period validation.
Bash syntax/Python compilation passed. ShellCheck is still unavailable.

Coverage includes 16,769 phase values across both signs at 1 µs spacing, exact
half-period ties, nearest distances 4.0/4.1/4.192/4.2/4.3 ms, repeatable 90,000-pair
traces at 25 ppm mismatch in either direction, multiple neighbor-boundary
crossings, strict hardware counter/timestamp rejection and independent hardware
4 ms/default behavior. Fourteen additional loaded RAW8/Y10P scenarios use the
actual bounded recorder to check zero loss/saturation, produced=committed,
explicit native identity/timestamp accounting, an injected gap after re-alignment,
the two-exclusion bound across calls and exact canonical replay twice with X1
configuration removed. CLI/Python daemon integration also verifies the public
validator accepts counted drift exclusions without relaxing raw loss/integrity.
RawCapture schema/ABI, physical media setup, VBLANK and pixel layouts are unchanged.

Implementation checkpoint: `b079f5e` (free-running correspondence and its tests).
The preceding harness checkpoint `325690cec86658917f0f220a7c500beb6e296648` was
inspected in [run 37231208917](https://github.com/martinkoenig/mantis-studio/actions/runs/37231208917):
all five native x86_64/ARM64 Studio ON/OFF and sanitizer jobs passed 17/17.
That earlier CI is not evidence for this later policy; its pushed final HEAD
requires separate CI inspection.
