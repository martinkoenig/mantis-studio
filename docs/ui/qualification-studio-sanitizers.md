# Complete Studio Qt sanitizer qualification (UI-QT-SAN-1B)

Local assessment: **QUALIFIED for the two recorded source snapshots, this VM/toolchain, the existing instrumentation graph and the required offscreen/software workloads. All mandatory local gates passed.**
Independent review and required exact-commit remote CI remain separate acceptance
requirements. This package changes qualification tooling and documentation only.
It does not change or publish the uncommitted UI-M3d-1 implementation.

## Source and dependency identities

- Unchanged baseline: CUT-0C `d2614b96304ff5fd45db8f09076da058afb9230d`.
- Accepted tooling dependency: UI-QT-SAN-1A
  `309d79a3407abf52269ad28d3ebaa901502ba175`;
  [PR #4](https://github.com/martinkoenig/mantis-studio/pull/4).
- Tooling branch: `ao/ui-qt-san-1b/full-studio`, created at the accepted dependency.
  The PR and AO completion receipt identify the final package commit.
- Protected source: `/home/martin/.ao/data/worktrees/mantis-studio/mantis-studio-3`,
  branch `ao/ui-m3d1-recovery/root`, original HEAD exactly CUT-0C, nine modified
  tracked files and four untracked source/test/document files.
- Baseline source manifest: 899 files, SHA-256
  `1c9bf658d61f0301d921f7769f7074a73fcd4d51e427194e2d798af75df9c353`.
- Candidate source manifest: 903 files, SHA-256
  `43432a4cca62fb22630df2bee5f93ecb48a5cfb49b970f32294cb5862ddd9814`.

Private local source snapshots are `/tmp/ui-qt-san-1b/baseline-source` and
`/tmp/ui-qt-san-1b/candidate-source`; compilation uses separate sibling build
directories. All original tracked files and the four relevant untracked files
were copied, with no Git metadata, ignored build products or caches. File bytes
were hashed before/after copying and compared with the immutable copies. Snapshot
files/directories were made read-only, preserving executable bits. There are no
candidate qualification source/test overlays.

Protected branch, HEAD, status, index SHA-256, original permissions and every file
hash were checked without optional Git locks. `protected-before.json`,
`protected-after-copy.json` and `protected-final.json` retain these identities.
The protected worktree, index and all its build directories were untouched.
The portable archive contains manifests and diagnostics, not either source tree
or any unpublished candidate source. Reproducing the candidate requires the owner’s
private source matching its independently retained manifest.

## Environment and actual instrumentation

Ubuntu 24.04.5 x86_64 VM; eight CPUs, approximately 23 GiB RAM. GCC **13.3.0** at
`/usr/bin/g++`; CMake **3.28.3**, Ninja **1.11.1**, `/usr/bin/python3` **3.12.3**,
OpenCV **4.6.0**, Protobuf **3.21.12**. The dynamic sanitizer packages are Ubuntu
`libasan8` and `libubsan1` **14.2.0-4ubuntu2~24.04.1**; this does not mean the
compiler was upgraded. Raw host/package/disk/memory receipts are archived.

Qt is the existing **6.9.2** installation at
`/home/martin/opt/mantis-qt/6.9.2/gcc_64`. Core, Gui, Qml, Quick, QuickControls2,
Concurrent and Test resolve inside this prefix. No additional modules, SDKs,
system packages or compiler were installed/upgraded. The accepted qtbase,
qtdeclarative and ICU73 archive provenance is reused from
[1A](qualification-qt-sanitizers.md). Its evidence archive SHA-256 is
`c8e9bd1fc73b21695d1cbef038fc3ab00052232fff2a4cca667b9fc09ddef5a2`;
its existing raw evidence was independently reverified without repeating its
complete experiment.

Both qualification variants use Debug, Studio ON, sanitizers ON and the complete
default build, including `mantis-python` and every fixture. Actual compilation and
link commands, executable ASan/UBSan symbols, `.debug_info`, runtime linkage and
resolved Qt paths were checked for Studio, Scan, M0 and Bridge executables.
CMake settings alone were not treated as instrumentation proof. The actual
compilation database contains 277/282 instrumented units in baseline and 281/286
in candidate; every authored product compilation under apps/src/plugins is
instrumented. Both existing build graphs leave generated `mantis.pb.cc`, the
`x1_media.cpp` test driver, `c_abi.c` contract driver and the latter targets’ two
AUTOMOC stubs uninstrumented. The media test still links ASan/UBSan through its
instrumented backend; the C-only ABI-size contract has no sanitizer runtime.
`instrumentation-coverage.json` and their linkage logs make this boundary explicit.
No whole-native-translation-unit instrumentation claim is made. Prebuilt Qt and
system dependencies themselves were **not** rebuilt with sanitizers.

```bash
cmake -S <sealed-source> -B <separate-build> -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DMANTIS_BUILD_STUDIO=ON -DMANTIS_SANITIZE=ON \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DCMAKE_PREFIX_PATH=/home/martin/opt/mantis-qt/6.9.2/gcc_64 \
  -DQt6_DIR=/home/martin/opt/mantis-qt/6.9.2/gcc_64/lib/cmake/Qt6 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build <separate-build> --parallel 8
```

Eight jobs fit observed available memory; expensive builds and CTest suites are
not run concurrently. Existing headless sanitizer presets remain unchanged.
Normal repository regression builds use this worker’s tooling source, based on
accepted 1A with no product-source changes. Studio Debug uses isolated Qt6.9.2;
headless Debug and headless sanitizers use Studio OFF. All use fresh separate
build directories and the explicit compiler/Python above.

## Strict execution and rendered workload

Every qualified sanitizer invocation uses:

```bash
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
QT_QPA_PLATFORM=offscreen
QT_QUICK_BACKEND=software
```

The execution environment is an allowlist: system PATH, HOME, C.UTF-8 locale,
private 0700 XDG runtime directory, these strict variables, `QT_DEBUG_PLUGINS=1`,
`QML_IMPORT_TRACE=1` and `QSG_INFO=1`. No LSAN_OPTIONS, suppressions, loader/plugin/
QML-import override, leak disable or forced render-loop setting is supplied.
CTest’s repository-defined environment, timeouts and registered order are retained;
full suites run sequentially without selectors, exclusions or mandatory skips.

Separate production Studio smoke executions use `--ui-mode mock --workspace scan
--quit-after 5000 --screenshot <evidence.png>`, with process-scoped `LD_DEBUG=libs`,
severity-labelled messages and opt-in scenegraph/Quick diagnostics. Their real
loader paths, offscreen plugin/QML modules, software backend initialization,
completed software frames and rendered PNGs are retained. Scan surface tests also
assert real rendered geometry/grayscale pixels and canonical object destruction.
This is actual software rendering evidence; no GPU or physical scanner is involved.

### Qualification-fixture corrections and retained failures

The first five-test focused diagnostic run exited 8: Bridge and M0 passed; Scan
QML, surfaces and capture-wire failed on four SafeArea lifecycle messages. The
optional `qt.quick.*=true` rule enabled **QtInfoMsg** creation/destruction messages.
The existing Scan test collector uses `t >= QtWarningMsg`; Qt6 assigns Info an
ordinal after Warning, so this captured the informational messages as warnings.
An unchanged Studio smoke labelled these exact messages `[info] qt.quick.safearea`,
rendered software frames and exited 0 without a sanitizer finding. Removing only
the optional broad `QT_LOGGING_RULES` restored repository-default logging. All five
focused tests then passed, with required sanitizer/QPA/backend settings identical.
No source, assertion, warning suppression or cleanup behavior was changed.

The first full baseline run exited 8, **183/184**: only
`q6a-validation-harness` failed because exported sources had no Git metadata.
The harness records branch/HEAD/status before testing hardware-preflight rejection.
This was a qualification fixture problem, not a scanner, Qt or product memory
failure. Separate private Git directories were created outside both source
snapshots, fetched only exact CUT-0C and loaded that committed index. Their real
working trees are the sealed source directories: baseline is clean; candidate has
exactly the original nine tracked modifications and four untracked files. Private
branches `qualification-baseline`/`qualification-candidate` are explicitly separate
from the protected original branch. No candidate content was staged or committed.

CTest processes receive variant-specific `GIT_DIR`, `GIT_WORK_TREE` and
`GIT_OPTIONAL_LOCKS=0`. The Q6A fixture captures its subprocess environment before
its tests temporarily clear `os.environ`; its isolated reproduction then passed.
All source/index hashes were rechecked, and the full baseline gate was repeated
without rebuilding unchanged sources. These metadata settings are equivalent
qualification context for both variants and change no sanitizer or UI setting.
A collector environment-key typo initially prevented one reproduction from launching;
`collector-launch-failure.json` records it as **NOT RUN**, never as a passing test.
Original diagnostic/full failures and launch evidence remain in the archive.

## Results, inventories and comparison

| Gate | Actual exit | Result | Elapsed / evidence |
| --- | --- | --- | --- |
| Baseline focused | 0 | PASS 5/5 | 59.15 s |
| Baseline complete Studio sanitizers | 0 | PASS 184/184 | 1562.48 s |
| Candidate focused (including replay) | 0 | PASS 6/6 | 85.43 s |
| Candidate complete Studio sanitizers | 0 | PASS 185/185 | 1599.00 s |
| Normal Studio Debug, sanitizers OFF | 0 | PASS 184/184 | 782.40 s |
| Normal headless Debug, sanitizers OFF | 0 | PASS 144/144 | 278.96 s |
| Headless ASan/UBSan/LSan | 0 | PASS 144/144 | 771.48 s |
| Baseline lifetime repetitions, three initial runs | 0, 0, 0 | PASS 36/36 (12 per run) | 864.90 s |
| Candidate lifetime repetitions, three initial runs | 0, 0, 0 | PASS 39/39 (13 per run) | 952.19 s |
| Fail-closed tooling regressions | 0 | PASS 32 tests | Synthetic policy controls and CLI failure propagation |
| Architecture/contracts | 0 | PASS | Boundary command plus all registered contract cases in full suites |
| C/C++ formatting | N/A | No changed C/C++ | Accepted probe/build graph/presets unchanged; new Python AST/whitespace checks pass |

`baseline-inventory.json` and `candidate-inventory.json` are actual CTest JSON
inventories, not inherited counts from earlier reports. Full logs, individual
JUnit results, original LastTest logs, commands, actual exits and exact environments
are retained separately for each phase. No failed run was overwritten or converted
to a pass. Three initial independent selected-suite executions per source provide
lifetime repetition evidence; there is no retry-until-pass policy.

Baseline and candidate have 184 and 185 registered tests respectively. The only
added case is `studio-bridge-replay`; no baseline test is missing. Every common
command and property is identical after normalizing source/build paths. Required
focused, full and repeated executions report no ASan/UBSan/LSan findings. The
candidate adds replay/cancel/epoch/shutdown coverage without a new observed
sanitizer regression. Separate original failed fixture runs remain classified as
failures and are not included in passing counts.

Historical Qt6.4.2 results (Scan QML/capture-wire 4,434,200 bytes/48 allocations;
M0 QML 12,733,344 bytes/96 allocations) are context from the protected task record,
not newly measured here. Current allocation findings and their classification are
reported in the result table and raw logs. Qt stack frames alone are not ownership
attribution; passing this workload does not certify every Qt application/context.

## Lifetime coverage and limits

Inspection and execution cover QML engine/window initialization and destruction,
canonical QObject/CppOwnership versus visual parenting, GUI-thread hosting,
foreign-window rejection, repeated routes/source/size changes, shared cloud/image
storage, actual rendering, preview leases, queued focus/GC callbacks and bridge
watcher shutdown. Candidate replay tests cover exact receipt/job correlation,
Replay/Verify/Cancel authority, held-worker-pool revocation, GUI heartbeat/thread
affinity, ABA/disconnect/source invalidation, delayed GUI completion and destruction
with outstanding work. These paths use `StudioBridge` and Classic controls; there
are no separate classes named `ReplayVerify` or `JobBridge` in these snapshots.
No artificial cleanup or production lifetime change was introduced.

Coverage is finite and fixture-based. Prebuilt Qt’s internal code lacks compiler
sanitizer instrumentation; allocation interception still operates. No claim covers
all internal texture handles, every shutdown interleaving, long-running soak,
Wayland/X11/native display, GPU/backend resets, ARM64, Windows/macOS, physical timing,
scanner accuracy, safety or production readiness. Test inspection plus successful
finite executions is not a proof of absence of every race.

## Fail-closed evidence tooling and reproduction

`tools/verify_studio_sanitizers.py` verifies independent source/evidence trust
anchors, all raw file checksums, fixed CUT-0C source identity, protected/candidate
manifest consistency, configurations, actual compile/link instrumentation, symbols,
debug/runtime evidence, Git provenance, equal runtime settings, complete CTest
inventories/log names/counts/JUnit results, mandatory gates and distinct repetitions.
It rejects missing rendering/logs/results, skipped or missing tests, nonzero exits,
sanitizer-policy errors, manipulated manifests and misleading PASS labels.
`tests/qualification/studio-sanitizers/test_verifier.py` exercises deterministic
synthetic positive/negative packages, including rehashed semantic corruption and
CLI nonzero propagation. It does not execute unpublished candidate source.

```bash
/usr/bin/python3 tests/qualification/studio-sanitizers/test_verifier.py
/usr/bin/python3 tools/verify_studio_sanitizers.py <extracted-evidence-directory> \
  --expected-manifest-sha256 13b72d23d3e8cc3e202a8b16b6eab36f12e23cf16427907bd51b621a36812109 \
  --expected-candidate-sha256 43432a4cca62fb22630df2bee5f93ecb48a5cfb49b970f32294cb5862ddd9814
```

Semantic verification of resolved Qt symlinks additionally needs the recorded Qt
prefix available; portable SHA-256 verification and raw-result inspection do not
require an SDK. The caller must use independently retained expected hashes, not
trust replacement hashes supplied alongside modified evidence.

Raw receipts/logs/manifests and generated UI evidence remain under
`/tmp/ui-qt-san-1b/evidence/`; private build outputs remain in the separately named
`*-build` directories. Generated JSON/log/text/PNG files are copied per phase;
source files, executable binaries, SDKs, reusable build caches and generated packet
inputs are excluded from the portable package. Original transient fixture failures
are retained through their complete logs/results and explicit classifications.
The archive also contains the final semantic-verification command/exit receipt and
both inner and outer file checksum manifests.

Portable archive: `/home/martin/.ao/data/artifacts/mantis-studio-5/ui-qt-san-1b-evidence.tar.gz` (1,092,237,756 bytes).
Archive SHA-256: `574e1399cda7fcecfd00b3d498f482e03ff0c208002d83b057a340da29970959`.
Evidence-manifest SHA-256: `13b72d23d3e8cc3e202a8b16b6eab36f12e23cf16427907bd51b621a36812109`.
The adjacent `.sha256` file provides the archive checksum. After extraction, run
`sha256sum -c SHA256SUMS` from the archive's `ui-qt-san-1b` root; semantic verification
uses its `evidence` child and the independently retained hashes above.

The exact final commit/PR and its one nonblocking exact-SHA CI status query are
reported in the AO completion receipt and PR. Pending remote checks are not green
checks and do not establish independent acceptance.

## Follow-up recommendation

Use the preserved isolated Qt6.9.2 prefix and strict process-scoped settings when
independently reviewing the exact UI-M3d-1 snapshot. This removes the observed
software sanitizer environment blocker for that snapshot; it does not accept the
feature or resolve its separate known backend project-switch replay/preview P1.
Keep that ownership/authority issue in its own authorized correction workflow.
For UI-QT-SAN-2, decide separately whether to extend instrumentation to generated
code/test-driver exceptions and source-built Qt, and whether native display/GPU,
other platforms and longer shutdown/soak coverage are required. Historical full
Qt6.4.2 allocation ownership remains an environment-sensitive comparison, not a
blanket Qt ownership conclusion or a waiver of future findings.
No UI-M3d-1 correction, Qt-wide cleanup refactor, hardware work, UI-M3d-2 or
UI-QT-SAN-2 implementation was performed. This is implementation self-verification;
only independent review may accept the resulting package.
