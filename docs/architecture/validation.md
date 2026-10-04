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

## Q6A hardware-gap continuation — 2026-10-04

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
| Debug Studio ON | PASS | 13/13, 21.59 s |
| Debug Studio OFF | PASS | 13/13, 12.34 s |
| Release Studio OFF | PASS | 13/13, 11.33 s |
| ASan + UBSan Studio OFF | PASS | 13/13, 13.49 s, detect_leaks=1 and halt_on_error=1 |

These final runs include the route-check/rollback review fixes.
Stored logs: `docs/validation/v0.2-q6a-gap-{debug,headless,release,sanitizers}-ctest.log`.
No sanitizer errors occurred. The intentional isolated crash fixture remains a
successful daemon-survival test, not an unreported parent crash.

Added coverage includes disabled-link route selection; explicit entity/pad
validation; missing/ambiguous entities; unsupported media-bus/RAW10 format;
scoped incoming conflicts and EBUSY-gated source conflicts; unrelated RGB/fan-out
preservation; VBLANK set/read-back; format/link/control read-back mismatch;
LEFT/RIGHT setup and STREAMON failures with context; failure rollback, committed
setup and incomplete rollback reporting. Known Y10P bit vectors and padded rows
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

### Real hardware evidence and handoff

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
