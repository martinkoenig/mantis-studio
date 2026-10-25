# UI-M2b Devices validation — 2026-10-10

Branch `feature/ui-m2b-devices`, created directly from verified
`201e60d44db960f8d765f4daa665f4a171a8596f`. Existing untracked task/review files
were explicitly authorized to remain untouched and are not committed. No merge,
rebase, neighboring worktree, daemon/device implementation, protocol, ABI or
schema change. [Capability/authority contract](devices.md).

## Reproducible verification

The supported local reference uses the existing Ubuntu 24.04 x86_64 container,
GCC 13.3, Qt 6.4.2 and OpenCV 4.6.0. Existing build caches are reused. All commands
run against this worktree mounted as `/work`; no physical hardware is required.

```bash
docker exec mantis-ui-m0-qt64 cmake -S /work -B /work/build/qt64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DMANTIS_BUILD_STUDIO=ON -DPython3_EXECUTABLE=/usr/bin/python3
docker exec mantis-ui-m0-qt64 cmake --build /work/build/qt64 --parallel 3
docker exec mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64 --output-on-failure
docker exec mantis-ui-m0-qt64 cmake -S /work -B /work/build/qt64-headless -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DMANTIS_BUILD_STUDIO=OFF -DPython3_EXECUTABLE=/usr/bin/python3
docker exec mantis-ui-m0-qt64 cmake --build /work/build/qt64-headless --parallel 3
docker exec mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64-headless --output-on-failure
docker exec mantis-ui-m0-qt64 cmake -S /work -B /work/build/qt64-headless-sanitizers -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DMANTIS_BUILD_STUDIO=OFF -DMANTIS_SANITIZE=ON \
  -DPython3_EXECUTABLE=/usr/bin/python3
docker exec mantis-ui-m0-qt64 cmake --build /work/build/qt64-headless-sanitizers --parallel 3
docker exec mantis-ui-m0-qt64 env ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir /work/build/qt64-headless-sanitizers --output-on-failure
```

Additional Qt 6.9.2 Studio ON ownership/lifetime coverage uses GCC 15.2 and
OpenCV 4.10 from the existing host dependency bundle:

```bash
cmake --build build/debug --parallel 3
OPENCV_OPENCL_RUNTIME=disabled \
LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ctest --preset linux-debug
cmake --build build/qt69-sanitizers --parallel 3
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
OPENCV_OPENCL_RUNTIME=disabled \
LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
ctest --test-dir build/qt69-sanitizers --output-on-failure
```

The existing OpenCL loader workaround is unchanged; it is not a sanitizer
suppression. No leak/error suppression, skipped test or weakened assertion is
added. Required remote sanitizer CI remains Studio OFF, as before. Changed C++
files pass repository `clang-format --dry-run --Werror` and `git diff --check`.

All five configurations build successfully. Complete final suites, after the
layout repairs, pass before commit/push:

| Local configuration / command above | Final result | Elapsed | Retained receipt |
| --- | --- | --- | --- |
| Qt 6.4.2 Studio ON | **71/71 PASS** | 358.45s | [Full CTest](evidence/devices/qt64-full.log) |
| Qt-free Studio OFF | **42/42 PASS** | 117.07s | [Full CTest](evidence/devices/headless-full.log) |
| Qt-free ASan/UBSan/LSan | **42/42 PASS** | 241.23s | [Full CTest](evidence/devices/headless-sanitizers-full.log) |
| Qt 6.9.2 Studio ON (`linux-debug` preset) | **71/71 PASS** | 344.58s | [Full CTest](evidence/devices/qt69-debug-full.log) |
| Qt 6.9.2 Studio ON ASan/UBSan/LSan | **71/71 PASS** | 590.09s | [Full CTest](evidence/devices/qt69-sanitizers-full.log) |
| Seven CI transition suites, three repeats each | **21/21 invocations PASS** | 268.57s | [Repeat receipt](evidence/devices/qt64-transition-repeats.log) |
| Qt 6.4 anchored-pane stress, three repeats before full suites | **3/3 PASS** | 46.80s | [Stress receipt](evidence/devices/qt64-anchored-panes-stress.log) |
| Native Wayland normal / maximized / restored × mock/live/hybrid | **9/9 transitions PASS** | DPR 1.25 | [Native receipt](evidence/devices/native-final.log) |

The normal/debug and instrumented final logs include the entire existing suite,
not selective legacy checks. Complete Qt test output retains geometry/DPR and
bounded-update measurements: 110ms Qt 6.4 and 149ms Qt 6.9 Debug for the described
12,000-descriptor setup plus 100 observations. These runs share local machine
resources; elapsed values are evidence of bounded work, not performance guarantees.
A successful local exit is not independent acceptance.

```bash
docker exec mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64 \
  -R '^(studio-home-qml|studio-projects-qml|studio-projects-resize|studio-projects-scroll|studio-devices-qml|studio-devices-stress|studio-ui-m0-qml)$' \
  --repeat until-fail:3 --output-on-failure
```


## New regression coverage

Seven new CTest registrations use `MANTIS_STUDIO_QML_FILES`, the actual application
resource list. The old 64 Studio ON / 42 OFF tests remain enabled, giving **71 ON /
42 OFF**. Qt 6.4 native ARM64 remains in the unchanged five-job CI matrix.

| Suite | User-visible/failure contract |
| --- | --- |
| `studio-devices-model` | Complete parent/component graph, multiple parents, childless/emitter-only composites, unknown capabilities/names, ambiguous/missing/invalid UTF-8 IDs, self/cycles/orphans/contradictions, depth/cap/edge/metadata bounds, exact vs truncated identity, plugin mismatch/duplicates/state change, stale/empty/recovery/removal/project reset, structured errors, identical notifications and bridge destruction |
| `studio-devices-qml` | Actual mouse/Space/arrows, all four tabs with stable geometry/identity, exact existing seven-stage route, stale/component/demo/colliding-ID refusal, offline route, disabled mouse/Return/Space/accessibility/direct-signal actions, zero controller mutations and preserved workflow instances |
| `studio-devices-matrix` | Nine viewports × eight sources/states; compact pane captures; right-edge anchoring and no horizontal page overflow/clipped toolbar |
| `studio-devices-dpi-1.5`, `studio-devices-dpi-2` | Nine viewports × mock/live/hybrid with actual Qt pixel-size/DPR assertions |
| `studio-devices-stress` | 150 persistent-pane/tab/selection/resize transitions, 95 virtualized keyboard rows with correct focus reveal, 30 source/graph transitions; QPointers detect lost panes/delegates |
| `studio-devices-wire` | Real authenticated public C++ Client/asynchronous StudioBridge, descriptor/plugin/events, hostile literal metadata, disconnect/auth refusal/empty/recovery/plugin changes; only snapshot and six explicitly read-only Events fixture controls |

The dedicated fixture never starts a device. Every unsupported UI press emits
zero runtime/project/capture/replay/preview/laser/configuration calls. The wire
peer rejects any unexpected command; its retained request receipt shows only
`snapshot` and `events`. The existing CLI listening-socket trap still proves all
mock launch routes require no daemon/token and issue no client commands. Existing
calibration/controller/daemon, acquisition, Home, Projects, M0, SDK, recovery and
boundary suites remain intact.

A 12,000-descriptor injectable snapshot inspects/renders 512 and disables bound
calibration; huge maps are omitted explicitly. One hundred identical bounded field
digests emit zero model or graph notifications and bypass DTO/graph conversion.
The measured elapsed value in full CTest output includes the initial bridge
projection, selection and 100 repeated model observations; it is local Debug or
instrumented evidence, not a physical timing or universal throughput promise.

## Qt visual evidence

Individual images are actual `QQuickWindow::grabWindow` or the application's rendered-frame
screenshot mechanism; the labelled contact sheet derives from these unchanged frames. No reconstructed HTML, reference pixels or brittle concept
equality test. Matrix evidence is generated under `build/qt64/ui-m2b/` and uploaded
by both Studio ON CI jobs. Selected full PNGs and complete verification logs are
retained in [the evidence directory](evidence/devices/README.md).

| Logical viewport | Pane composition |
| --- | --- |
| 1080×720 | Compact Overview / All Devices / Inspector |
| 1280×720 | Compact panes |
| 1366×768 | Compact panes |
| 1440×900 | Three desktop panes; center art/provenance stack |
| 1536×1024 | Three desktop panes; side-by-side central illustration/provenance |
| 1920×1080 | Three desktop panes |
| 2560×1440 | Three panes; center overview and preview/capabilities lanes |
| 3440×1440 | Three panes; center lanes |
| 3840×2160 | Three panes; center lanes |

DPR 1 has mock, confirmed live, hybrid, confirmed empty, never-confirmed waiting,
stale last-known, selected unknown third-party and hybrid-stale: **72 primary
captures**, plus compact navigator/inspector states (**48**). DPR 1.5 and 2 each
have nine × three sources (**54** total). Four inspector tab captures, public-wire
faults, keyboard focus, application default capture and native frames supplement
this. Pixel dimensions are checked against actual Qt DPR; geometry assertions
verify visible panels and toolbar controls are contained and the right inspector
meets the usable right workspace edge. No global width island, shell minimum
change or whole-page scroll is introduced. The actual application default mock
capture and the shared-shell 1536×1024 mock frame are pixel-identical.

Full Qt images were inspected at equivalent 1536×1024 against
[08-devices.webp](reference/08-devices.webp), at compact sizes, the desktop
breakpoint, ultrawide/4K, fractional DPI, stale and native states. The reference's
three regions, mechanical subject, dual studies, dark teal/mint palette and
inspector hierarchy are recognizable. Large centers reorganize into lanes.
Compact panes preserve access rather than clipping controls. Scrolling is confined
to the appropriate navigator/detail/inspector. Local tabs keep panel geometry.
Deliberate absent-control/telemetry and original illustrative-art differences are
listed in [devices.md](devices.md); minor visual refinements join the approved
global polish pass.

Native GNOME Wayland/default Qt renderer, Qt 6.9.2, DPR 1.25 was also executed:

```bash
cmake --build build/debug --parallel 3
env QT_QPA_PLATFORM=wayland OPENCV_OPENCL_RUNTIME=disabled \
LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
build/debug/bin/mantis-studio-devices-tests build/ui-m2b-verification/native-final native
```

Mock/live/hybrid each pass normal, maximized and restored geometry/rendering/focus,
with nine native captures. This is real desktop presentation evidence with
injectable runtime descriptors, not real hardware discovery, screen-reader
certification, physical 4K/DPR monitor coverage or metrology validation.

## Investigated failures and focused corrections

The first extra Qt 6.9 Studio sanitizer run and native run exposed an existing
Projects inspector `implicitHeight` binding loop at its tree wrapper during source
transitions, including while that workspace was hidden. These failures are retained
and are not counted as passes. The correction is confined to two layout guards in
`ProjectsInspector.qml`: nonnegative hidden-body width and explicit tree-row
intrinsic height. No Projects model/actions/data/scroll strategy is rewritten.
Focused Qt 6.9 Devices tests and native transitions pass without warnings after
repair. The 1536×1024 Projects frame before/after the two guards is pixel-identical;
the Home frame also matches its retained accepted UI-M2a comparison exactly.
Complete final suites provide functional regression evidence.
The required Qt 6.4 matrix remains distinct from the extra Qt 6.9 coverage.

The final rapid-resize test then detected a real Qt 6.4 pane allocation defect:
changing compact-pane visibility and window width could retain an inspector only
3px wide beyond the workspace. Synchronizing the assertion with an actual rendered
frame reproduced the same failure, ruling out an assumed fixed-delay test issue.
Replacing the dynamic pane `RowLayout` with three persistent anchored panes fixes
allocation directly. The inspector remains pinned to the right edge; compact panes
fill the same host. Containment, edge, focus and delegate assertions remain intact.
Three stress repeats pass before complete final verification. Both original
failures are retained; no timing delay, layout-warning filter or skipped test repairs them.
The warning assertion excludes only Qt's standard `QStandardPaths` environment
setup notice, which remains visible in complete CTest output.

## Limits and remote handoff

Device configuration, presets, identify, firmware, live Devices preview and safety
telemetry remain unavailable public APIs. Stale/ambiguous/truncated evidence cannot
authorize a bound calibration route. No actual scanner/emitter/interlock,
exposure timing, laser safety, Windows/macOS or native ARM64 execution is claimed
locally. Existing project-switching/replay isolation and PointCloud retry defects
remain separate, unchanged backend findings.

The five-job GitHub Actions matrix is an independent acceptance gate for the exact
pushed SHA. Both ON jobs include Devices suites, repeat Devices QML/stress alongside
Home/Projects/M0 three times, and upload `ui-m2b/revision.json` containing literal
SHA/run URL. After push, Codex makes one nonblocking exact-SHA status check and
reports PENDING if incomplete, ending without polling. The independent reviewer
verifies all required jobs before ACCEPT; READY FOR REVIEW describes completed
local verification only. No merge is performed.
