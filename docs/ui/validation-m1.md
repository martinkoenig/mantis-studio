# UI-M1 Home validation — 2026-10-08

Scope: [Home contract](home.md), based on accepted UI-M0
`3ba819ab96686689c69888e1a1a223a376de7375`, isolated branch
`feature/ui-m1-home`. No integration merge, other-worktree change, daemon, public
client/protocol or plugin ABI change. Capture and calibration retain daemon authority.
The [binding reliability standard](../architecture/reliability-performance-and-validation.md)
and [M0 validation](validation.md) remain authoritative.

## Executed local checks

Ubuntu 24.04 x86_64 container `mantis-ui-m0-qt64`: GCC 13.3.0,
CMake 3.28.3, Python 3.12.3, Qt 6.4.2 (base package
`6.4.2+dfsg-21.1build5`, declarative `6.4.2+dfsg-4build3`), OpenCV 4.6.0.
The image digest is
`sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55`.
Host Ubuntu 25.10 x86_64: GCC 15.2.0, Python 3.13.7, Qt 6.9.2,
OpenCV 4.10.0, additional Studio ON ASan/UBSan/LeakSanitizer validation.
Qt tests use `QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software` as registered
in CTest. No physical scanner, laser, GPU, metrology or native desktop validation
is established by these checks.

Equivalent supported preset commands (see [BUILDING.md](../../BUILDING.md)):

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug --output-on-failure
cmake --preset headless -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset headless --parallel 4
ctest --preset headless --output-on-failure
cmake --preset sanitizers -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset sanitizers --parallel 4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/sanitizers --output-on-failure
```

Actual isolated local build directories used the same options with separate paths:

```bash
docker exec mantis-ui-m0-qt64 cmake -S /work -B /work/build/qt64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DMANTIS_BUILD_STUDIO=ON -DPython3_EXECUTABLE=/usr/bin/python3
docker exec mantis-ui-m0-qt64 cmake --build /work/build/qt64 --parallel 4
docker exec mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64 --output-on-failure
# Same configuration/build/test with build/qt64-headless and Studio=OFF;
# build/qt64-headless-sanitizers additionally uses -DMANTIS_SANITIZE=ON.
docker exec -e ASAN_OPTIONS=detect_leaks=1 -e UBSAN_OPTIONS=halt_on_error=1 \
  mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64-headless-sanitizers --output-on-failure
docker exec mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64 \
  -R '^studio-home-wire$' --repeat until-fail:10 --output-on-failure
docker exec mantis-ui-m0-qt64 ctest --test-dir /work/build/qt64 \
  -R '^studio-home-(qml|wire)$' --repeat until-fail:3 --verbose
```

Host Studio ON sanitizer configuration uses `-DMANTIS_SANITIZE=ON` and
`-DOpenCV_DIR=$PWD/build/deps/root/usr/lib/x86_64-linux-gnu/cmake/opencv4`
for extracted OpenCV dependencies. Exact execution:

```bash
LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  cmake --build build/qt69-sanitizers --parallel 4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 OPENCV_OPENCL_RUNTIME=disabled \
  LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  ctest --test-dir build/qt69-sanitizers --output-on-failure
```

The host-only OpenCL override avoids the pre-existing installed CUDA/OpenCL
loader issue recorded in M0; no sanitizer suppression or leak detection disabling
is used. Required CI uses its unchanged headless sanitizer preset/environment.

| Executed suite | Result | Retained local log under `build/ui-m1-review/` |
| --- | --- | --- |
| Qt 6.4 full desktop | 49/49 passed, 140.92 s | `qt64-complete-ctest.log` |
| Qt-free headless | 41/41 passed, 107.60 s | `headless-ctest.log` |
| Required-equivalent headless ASan/UBSan/LSan | 41/41 passed, 242.32 s | `headless-sanitizers-ctest.log` |
| Additional Qt 6.9 Studio ON ASan/UBSan/LSan, complete suite | 49/49 passed, 282.41 s | `qt69-sanitizers-complete-ctest.log` |
| Final Qt 6.4 Home/M0/bridge/calibration-QML/acceptance regressions | 7/7 passed, 31.13 s | `verification-tests.log` |
| Final Qt 6.9 Studio ON sanitizer Home/M0/bridge/calibration-QML/acceptance regressions | 7/7 passed, 41.81 s | `last-sanitizers-tests.log` |
| Deterministic result-delivery fix, public-wire repetition | 10 consecutive passes, 13.57 s | `race-fix-repeat.log` |
| Home component and public-wire repetition | Each passed three consecutive times, 25.51 s | `final-repeat.log` |

Exact-SHA CI results are recorded below.
Local logs/build products are ignored, not repository assets;
CI uploads reproducible captures, fixture commands and complete CTest diagnostics.

## Contract and regression evidence

`studio-home-qml` validates actual rendered Home and its presentation model:

- Confirmed project, advertised logical devices/capabilities, jobs, artifacts and
  event sequence; zero/one/many devices, unknown/empty lists and malformed fields.
  Missing values are unavailable; proto3 zero progress/count/sequence cannot prove
  scalar presence. No guessed history, timestamps, ETA or utilization.
- Running/queued/completed/failed/cancelled states, valid and invalid/NaN/infinite
  progress, exact uint64 counts, oversized/control/bidi text, literal markup,
  stable bounded ordering, deletion/replacement of the bridge.
- Last-known snapshot retention, operation failure with confirmed connection,
  failed confirmation and restored current data, including mock/live reattachment.
- All nine enabled Home CTAs via mouse and keyboard, focus/hover, accessible
  Button names and disabled-control description. Planned project tools cannot
  dispatch; calibration routing selects no device and activates no revision.
- Demo fixtures expose no command API or actionable IDs. Mock detaches the model;
  hybrid live counts remain independent of the separate demo showcase.
- 90 immediate model removals/repopulations, mode/route/viewport transitions,
  alternate DejaVu Serif metrics, no horizontal Home overflow or QML warnings.
  M0's original Foundation/Devices removal-and-resize reproducer is retained,
  alongside dedicated Home coverage. All application/QML test resources match.

`studio-home-wire` is public TCP/protobuf → existing C++ client → production
asynchronous StudioBridge → rendered Home, not a provider-only fake. It checks
project/device/job/artifact/event text against a bounded deterministic fixture;
rejected operation retains connected state and typed cause, failed local artifact
access retains connection after confirmation, real peer closure retains cached
data as unconfirmed, and explicit fixture restoration refreshes the project.
The recorded command list asserts exactly two deliberate pipeline attempts,
one data-plane request and one fixture restoration, with no capture, project,
calibration or cancellation mutation. Home CTAs issue navigation only.
The existing M0 CLI socket trap now loads the real Home, including a usable
endpoint/token environment, and still proves no mock network access.

Repeated wire execution exposed a real pending-result replacement race in the
bridge. [Retained red-test/root-cause evidence](evidence/m1-result-delivery-race.txt)
shows the unfixed busy lease failing deterministically. Worker completion is not
GUI result delivery: `request_pending_` now guards both through result application.
This is the sole narrow acquisition-bridge correction; no queue, extra watcher,
transport, polling or mutation retry was introduced.

## Visual review and reproducible captures

The actual [approved reference](reference/01-home.webp) was inspected directly,
then the generated Qt 6.4 contact sheet and full-size captures reviewed at all
three sizes. Corrections included compact spacing, removal of duplicate Home
heading, source labels and per-card last-known context. The dark/mint shell,
geometric hero, four demo projects, Quick Actions, artifact/activity main column
and devices/system/jobs/help rail follow its composition. Original local Canvas
art replaces illustrative scanner photography; the approved image is never an
application asset.

There are 34 M1 PNGs under `build/qt64/ui-m1/` (CI: `build/ci/ui-m1/`), plus
the retained 38 M0 PNGs. Representative paths, each with `1080`, `1536`, `1920`
variants unless noted:

| Capture prefix | Meaning |
| --- | --- |
| `screenshots/mock-populated-` | Deterministic illustrative projects/device/jobs/artifacts/activity |
| `screenshots/live-awaiting-runtime-` | Never-confirmed live runtime |
| `screenshots/live-empty-` / `live-unconfirmed-empty-` | Confirmed empty versus unconfirmed empty |
| `screenshots/live-populated-` / `live-zero-devices-` | Long paths/names, multiple states, missing discovery |
| `screenshots/live-stale-` | Cached data and failed confirmation |
| `screenshots/hybrid-populated-` / `hybrid-demo-separation-` | Live top and separately labelled bottom demo area |
| `screenshots/alternate-font-live-1080.png` | Different font metrics and compact wrapping |
| `wire/wire-connected-live-` | Real public-client/bridge fixture, all three sizes |
| `wire/wire-rejected-operation-1536.png` | Connected snapshot plus operation error |
| `wire/wire-data-failure-1536.png` | Confirmed control plane plus artifact access error |
| `wire/wire-transport-loss-1536.png` | Genuine loss of confirmation and retained data |

Manual inspection covered full-size mock 1536/1080, populated live 1080,
empty live 1536, hybrid 1536, stale 1920, hybrid demo separation 1080 and public-wire
1536 captures, plus the component contact sheet. No clipped important actions or
global horizontal scrolling were observed. At 1080×720 the right rail stacks
below main content; populated demo at 1536×1024 uses modest vertical scrolling
for lower activity rows. This is responsive composition evidence, not a
pixel-perfect comparison or native desktop/GPU accessibility certification.

## Resource discipline and limits

Per notification the Home model inspects at most 256 rows per section, sorts that
bounded sample and exposes at most 3 devices, 4 jobs, 6 artifacts and 4 events.
Strings/capabilities/issues are bounded as specified in [Home](home.md).
Input QVariant lists/maps are implicitly shared; the 10,000-row regression with
oversized strings checks output bounds. The model emits no change for 100 equal
normalized notifications. No per-component timer or network access exists.
The repeatable 90-transition stress reported 3018 / 3049 / 3077 ms on three
consecutive runs in this Qt 6.4 software-rendered environment, including event
processing and final settling. This is
an observed test workload, not a per-update latency measurement, throughput
promise or hardware performance budget.

An earlier additional host sanitizer full run failed the unchanged
`acquisition-y10p-integration` drift assertion (`steady_state_unmatched_left > 0`)
at `tests/integration/acquisition.py:171`; no sanitizer memory diagnostic occurred.
The finite 0.2 s capture checks a drift boundary that needs roughly 24 pairs,
so load-sensitive sampling is a supported inference, not a proven account of
the failed capture: its temporary frame-count data were not retained. An ignored
instrumentation copy kept the same assertion and observed 41 committed pairs,
one steady-state left exclusion and raw integrity/replay PASS
(`acquisition-sanitizer-trace.log`). A fresh **unchanged** complete sanitizer suite
then passed 49/49. No acquisition test/backend change, skipped check or weakened
assertion was used. The original failing log is `qt69-sanitizers-final-ctest.log`.

The extra Studio ON Qt 6.4 LeakSanitizer run retains the independently documented
[baseline Qt software-renderer texture leak](evidence/qt64-baseline-software-renderer-leak.txt).
It is not a passing LSan environment and is not covered up by suppressions. The
required headless sanitizer matrix and additional Qt 6.9 desktop sanitizer suite
pass; the dependency limitation remains scoped to extra Qt 6.4 software rendering.

Known pre-UI-M3 unreadable-PointCloud automatic retry remains tracked in
[Home follow-ups](home.md#known-follow-ups). Home adds no artifact loading or retry.
UI-M2 project browser/history/actions, telemetry and tutorial browser remain
planned. Discovery does not prove physical readiness; no calibration PASS,
firmware, accuracy, laser safety or hardware synchronization claim is made.
Native OS screen-reader, real hardware, macOS/Windows and GPU rendering are
unproven here. Independent technical/visual review remains the acceptance authority.

## Exact-commit CI gate

Implementation commit **`2be93de0c85d85aaa6e0fa07910623dfbe513c0d`**, following
bridge correction `41e6740bbe923d9b4833f71e6cc2b3670ff235b1`, passed the complete
[architecture run 37820671478](https://github.com/martinkoenig/mantis-studio/actions/runs/37820671478).
All five required jobs completed successfully:

| Required job | Result / exact-run link |
| --- | --- |
| Ubuntu 24.04 x86_64 Studio ON, Qt 6.4 | [Success — 49 tests](https://github.com/martinkoenig/mantis-studio/actions/runs/37820671478/job/113460581949) |
| Ubuntu 24.04 native ARM64 Studio ON, Qt 6.4 | [Success — 49 tests](https://github.com/martinkoenig/mantis-studio/actions/runs/37820671478/job/113460582193) |
| Ubuntu 24.04 x86_64 Studio OFF | [Success — 41 tests](https://github.com/martinkoenig/mantis-studio/actions/runs/37820671478/job/113460582225) |
| Ubuntu 24.04 native ARM64 Studio OFF | [Success — 41 tests](https://github.com/martinkoenig/mantis-studio/actions/runs/37820671478/job/113460581575) |
| Ubuntu 24.04 headless ASan/UBSan/LSan | [Success — 41 tests](https://github.com/martinkoenig/mantis-studio/actions/runs/37820671478/job/113460582131) |

Downloaded and verified both exact-run desktop artifacts:
[x86_64 evidence](https://github.com/martinkoenig/mantis-studio/actions/runs/37820671478/artifacts/11568614935)
and [native ARM64 evidence](https://github.com/martinkoenig/mantis-studio/actions/runs/37820671478/artifacts/11568684825).
Each contains 49 passing CTest entries, 34 M1 PNGs, all 38 M0 PNGs and the expected
public-wire command log. The native ARM64 mock 1536 capture was additionally
inspected against the reviewed composition; source labels and layout agree.
Artifacts expire after 30 days; the registered tests regenerate them. Upload
names remain `ui-m0-ubuntu-24.04` / `ui-m0-ubuntu-24.04-arm` and retain both UI
directories, fixture evidence, `LastTest.log` and diagnostic files.

This evidence-recording documentation successor changes no implementation or
tests. It requires its own full five-job matrix after pushing; the final handoff
must identify that exact successor SHA/run and report completion only after all
five succeed. The implementation run above is traceable source evidence, not a
substitute for that final-commit gate. A document cannot embed its own Git commit
hash; retrieve the final exact-SHA run with:

```bash
gh run list --branch feature/ui-m1-home --commit "$(git rev-parse HEAD)" \
  --json databaseId,headSha,status,conclusion,url
gh run view RUN_ID --json headSha,status,conclusion,url,jobs
```
