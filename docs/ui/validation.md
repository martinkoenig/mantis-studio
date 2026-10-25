# UI-M0 validation — 2026-10-08

Implementation and checks ran only in `/home/martin/src/mantis-studio-ui` on
`feature/ui-concept-mock`, based on
`ea40d12777db391b00d10e8f378bc4c447f18fa0` (the supplied v0.4 base). No backend,
protocol, schema, SDK or plugin ABI changes are part of M0.

## Environment and build

Local Linux x86_64: Ubuntu 25.10, GCC 15.2, Qt 6.9.2, OpenCV 4.10, Python 3.13.7.
The first normal configure failed because this machine lacked OpenCV development
files. Existing project dependencies were downloaded as Ubuntu packages and
extracted under ignored `build/deps/root/`; nothing was installed system-wide or
in another worktree. Runtime library lookup used that local directory.

Both configurations and complete builds passed:

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DOpenCV_DIR="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu/cmake/opencv4"
cmake --preset headless -DPython3_EXECUTABLE=/usr/bin/python3 \
  -DOpenCV_DIR="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu/cmake/opencv4"

# This environment-specific prefix was used for builds and tests below:
export LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake --build --preset linux-debug --parallel 4
cmake --build --preset headless --parallel 4
```

On a normally provisioned machine, use the standard commands in
[BUILDING.md](../../BUILDING.md) and [UI README](README.md); this local prefix is
not a new repository dependency. Qt Test is used only by the new UI test target,
inside the Studio/BUILD_TESTING CMake boundary. Headless configures/builds without
Qt discovery.

## Executed checks

| Check | Result | Local evidence |
| --- | --- | --- |
| Complete desktop CTest: `ctest --preset linux-debug --output-on-failure` | 46/46 PASS, 104.67 s | `build/debug/ctest.log` |
| Complete headless CTest: `ctest --preset headless --output-on-failure` | 41/41 PASS, 77.45 s | `build/headless/ctest.log` |
| After explicit scrollbars, mock endpoint isolation and compact acceptance override: `ctest --test-dir build/debug -R 'studio-\|^acceptance$\|acquisition-y10p-integration' --output-on-failure` | 7/7 PASS, 28.47 s | `build/debug/ui-m0-final-tests.log` |
| After revision-rail width correction and C++ formatting: `ctest --test-dir build/debug -R 'studio-ui-m0\|studio-calibration-qml\|^acceptance$' --output-on-failure` | 4/4 PASS, 18.42 s | `build/debug/ui-m0-layout-tests.log` |
| Final visible source-label assertions: `ctest --test-dir build/debug -R ^studio-ui-m0-qml$ --output-on-failure` | 1/1 PASS, 3.34 s | `build/debug/ui-m0-label-tests.log` |
| Exact requested mock launch with `/tmp/mantis-ui-m0-home.png`, 5000 ms | PASS, exit 0, no QML warnings | PNG at 1536x1024 |
| Additional local daemon / Virtual Scanner visual run | PASS: connected live/hybrid summaries, calibration, real acceptance export and viewport with 3072 synthetic points | `build/debug/ui-m0/*-review.log`, `screenshots/connected-*.png` |
| Final disconnected live calibration screenshot | PASS, exit 0, no QML warnings | `/tmp/mantis-ui-m0-calibration.png` |
| `git diff --check` | PASS | No whitespace errors |

The full desktop suite includes `studio-calibration-controller`,
`studio-calibration-daemon`, `studio-calibration-qml`, acquisition integration,
calibration/activation contracts, ABI/boundary checks, crash/recovery and the
original asynchronous acceptance workflow. Existing calibration tests were not
weakened. Acceptance now additionally starts with `--workspace=home` at 1080x720;
`--acceptance-export` must override that route and make the real viewport ready.
The daemon-owned capture survives Studio restart as before.

The new QML test checks mouse/keyboard navigation, ten routes and titles, source
and capability transitions, disconnected live/hybrid data, mock authority,
calibration accessibility, preserved render hooks and three resolutions.
The new CLI test checks both argument forms, invalid arguments, a shorter
screenshot timeout, startup without a token, independence from invalid runtime
endpoint variables and twelve mock routes without any TCP connection. A listening
socket with a usable test token traps accidental runtime access.

## Visual review

Generated PNGs are local review artifacts, not concept comparisons or hardware
acceptance evidence. The reference WebPs and UI documentation are versioned;
generated screenshots remain outside tracked source files.

Reviewed all ten mock route captures at 1536x1024, representative Home,
acquisition and calibration captures at 1080x720 and 1920x1080, and connected
live/hybrid Home plus the synthetic acquisition viewport. Also reviewed final
calibration wrapping and the disconnected mode treatments.

- Navigation, page titles, cards and source labels remain readable at all three
  sizes. At 1080x720, foundation content scrolls vertically and the preserved
  three-column acquisition workspace scrolls horizontally/vertically.
- Explicit scrollbars were added after review so overflow is visible.
- The calibration revision rail received a narrow width constraint after a
  long description was found clipped; controller and stage logic are unchanged.
- Live runtime connection is labelled separately from device readiness. Hybrid
  shows actual Virtual Scanner metadata and a separate amber Demo / Mock device.
- Mock has no connected hardware claim, capabilities, measurements or active
  jobs. The legacy example pipeline continues to label its geometry synthetic.
- Supported smoke paths produced no QML binding, type or runtime warnings.

Artifact locations:

```text
/tmp/mantis-ui-m0-home.png
/tmp/mantis-ui-m0-calibration.png
build/debug/ui-m0/cli/cli-mock-home.png
build/debug/ui-m0/screenshots/mock-{home,scan,process,inspect,reverse,automate,projects,devices,plugins,settings}.png
build/debug/ui-m0/screenshots/mock-{home,acquisition,calibration}-{1080,1536,1920}.png
build/debug/ui-m0/screenshots/{live,hybrid}-home.png
build/debug/ui-m0/screenshots/connected-{live,hybrid}-home.png
build/debug/ui-m0/screenshots/connected-live-{acquisition,calibration}.png
```

## Changed files

- `apps/studio/{main.cpp,bridge.cpp,bridge.hpp}`: validated launch options,
  mock transport/command boundary and additive capability metadata.
- `ui/shell/Main.qml`, `ui/design/{Theme.qml,qmldir}`,
  `ui/components/{DeviceSummary,NavigationItem,Panel,SourceBadge,StatusIndicator,StudioButton,StudioIcon}.qml`,
  `ui/state/{AppUiState,MockFixtures}.qml`: shell, design system and provider.
- `ui/workspaces/{AcquisitionWorkspace,FoundationWorkspace}.qml`: preserved
  acquisition and shared M0 panels; `CalibrationWorkspace.qml`: revision width.
- `CMakeLists.txt`, `tests/unit/studio_ui_m0.cpp`,
  `tests/integration/{studio_ui_m0,acceptance}.py`: resource/test wiring and
  stronger acceptance startup coverage.
- `docs/ui/{README,validation}.md`, `docs/ui/reference/README.md` and all ten
  reference WebPs: intent, migration, evidence and approved reference pack.
- The supplied temporary `.ui-m0-task.md` is removed before committing.

## Remaining scope and limits

At this checkpoint, no failing criterion was known from the local Qt 6.9 run.
That local-only conclusion did not establish Qt 6.4 acceptance: independent CI
subsequently found the Studio QML SIGSEGV described below.
UI-M1…M7 full workspaces remain planned as described in the
[coverage/migration matrix](README.md#coverage-and-migration). The new Home is
only the M0 foundation. Existing acquisition retains its earlier visual style
until UI-M3 and requires scrolling at compact sizes.

This is local Qt 6.9/Linux x86_64/offscreen evidence. Qt 6.4 baseline compatibility,
ARM64 builds, native window-system accessibility and physical Q6A scanner/laser
behavior were not separately executed here. No physical scanner or laser was
activated. Existing v0.2/v0.3/v0.4 hardware/evidence limitations and the master
roadmap remain unchanged. Independent review remains the next step.


## Additional correction record — 2026-10-08 (review gate)

Starting SHA: `618e03eef584a729b0036c9ec73eabf567d76183`, branch
`feature/ui-concept-mock`; no newer commits or unexpected tracked changes.
[Original CI run](https://github.com/martinkoenig/mantis-studio/actions/runs/37760698948)
confirmed Studio ON x86_64 and native ARM64 each failed `studio-ui-m0-qml`
with SIGSEGV (45/46 tests passed); both Studio OFF jobs and ASan/UBSan passed.
These outcomes supersede the earlier local-only acceptance statement, without
changing its historical Qt 6.9 evidence.

### Crash investigation and correction

Unchanged source reproduced under a fresh Ubuntu 24.04 x86_64 container,
GCC 13 and distribution Qt 6.4.2, with the CI dependency list. Verbose CTest
failed in 1.98 seconds; three subsequent direct runs exited 139. GDB located
the failure in `settle()` at original test line 156: Home resize to 1080×720
after changing source mode. Navigation, grabs and the first fixture assertions
had already executed; the later provider assertions had not.

The cause is a Qt 6.4 layout cache retaining a removed Repeater delegate until
layout polish. Repeater removal detaches the child (removing the layout's item
change listener), then destroys it. A resize before polish queries nested layout
size hints using the stale `QQuickGridLayoutItem::m_item`. The symbolic stack is:

```text
QQmlData::get / qmlAttachedPropertiesObject
QQuickGridLayoutItem::sizePolicy (qquickgridlayoutengine_p.h:68)
QGridLayoutItem::stretchFactor / QGridLayoutEngine::fillRowData
QQuickGridLayoutBase::sizeHint (qquicklinearlayout.cpp:237)
QQuickLayoutAttached::maximumHeight / effectiveSizeHints_helper
QQuickGridLayoutBase::rearrange / QQuickItem::setWidth
QGuiApplicationPrivate::processGeometryChangeEvent
settle() (studio_ui_m0.cpp:50)
main() (studio_ui_m0.cpp:156; Home, QSize(1080,720))
```

A standalone Window → ColumnLayout → ColumnLayout → Repeater of ColumnLayouts,
with `rows: [1]`, reproduces the same stack when a single-shot changes rows to
`[]` then resizes from 1536×1024 to 1080×720. Qt 6.4 source inspection confirms
`sizeHint()` queries the engine without refreshing removed items; child removal
invalidates for later polish. No Qt private API is used in the correction.

`FoundationWorkspace` now places dynamic device rows in a Qt Quick `Column`
behind a stable `Item` with explicit implicit height. Surrounding Layouts never
cache the removable delegates. All source modes and responsive behavior remain.
The layout-only change passed the unchanged test five consecutive times.
The repaired test adds immediate model-change/resize sequences and type-aware
QVariantList/QJSValue decoding with malformed, missing, non-array, empty and
non-map negative inputs. Every first-element read and UI lookup is checked.
The original layout still fails the repaired regression, proving the test repair
did not hide the crash. The fixed regression passed three consecutive runs.

Reproduce the reviewed baseline in an isolated directory inside this worktree:

```bash
mkdir -p build/ui-m0-review/baseline
git archive 618e03eef584a729b0036c9ec73eabf567d76183 | tar -x -C build/ui-m0-review/baseline
# In Ubuntu 24.04 with BUILDING.md/CI packages plus gdb:
cmake -S build/ui-m0-review/baseline -B build/ui-m0-review/baseline-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/ui-m0-review/baseline-build --target mantis-studio-ui-m0-tests --parallel 4
ctest --test-dir build/ui-m0-review/baseline-build -R '^studio-ui-m0-qml$' -V
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software gdb -batch -ex run -ex bt \
  --args build/ui-m0-review/baseline-build/bin/mantis-studio-ui-m0-tests
```

Local investigation logs: `build/ui-m0-review/{original-ci.log,original-ci.json,
original-ctest.log,original-gdb.log,symbolic-gdb.log,minimal-gdb.log,
red-regression.log,layout-fix-tests.log,root-regression-tests.log}`.
Instrumentation affects scheduling: the unchanged full test passed under
Valgrind and breakpoint-heavy tracing; neither is used as acceptance evidence.

The [retained symbolic backtrace](evidence/qt64-baseline-crash.txt) includes the
original test and matching standalone crash. The [standalone reproducer](evidence/qt64-layout-reproducer.cpp)
can be compiled in Ubuntu 24.04 with:

```bash
g++ -g docs/ui/evidence/qt64-layout-reproducer.cpp -o /tmp/mantis-layout-reproducer \
  $(pkg-config --cflags --libs Qt6Quick Qt6Qml Qt6Gui Qt6Core)
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  gdb -batch -ex run -ex bt --args /tmp/mantis-layout-reproducer
```

### Other review corrections and regression evidence

| Finding | Correction | Meaningful coverage |
| --- | --- | --- |
| Unsafe fixture decoding | Check actual QVariant/QJSValue types, array shape, nonempty map rows and a 65,536-row limit before conversion/access; name the failed property in diagnostics | Native and JS lists; missing/invalid, scalar, non-array, empty, non-map and oversized array inputs |
| Every discovered device actionable | Consume `CalibrationController.devices`, which validates the complete parent/child graph; separate discovery, unknown availability/readiness and calibration permission | Eligible FrameSet parent with image child, childless parent, emitter-only child, single image, disconnected runtime, live/hybrid/mock, button enablement and eligible ID intent |
| Cached capture presented as current | `capturing` requires connection; retain last-known capture and authoritative project/artifacts/devices; explicitly label stale capture, descriptors, preview and diagnostics; disable offline intents | Active → transport failure → active reconnect → idle; data retention, UI labels/action state and idle telemetry reset; no stop/cancel on disconnect |
| Render attachment lifetime | Use `QPointer` for non-owning render attachments; discard preview completion while disconnected | Destroy attachment, apply late cloud result, verify retained artifact identity and no dangling access; desktop ASan/UBSan |
| Primary hover/pressed contrast | Add two Theme accent states, retain dark foreground on light primary fills, explicit disabled text/fill and keyboard focus border | Actual mouse hover/press/release, Space activation, keyboard focus, disabled no-click; enabled-state text contrast at least 4.5:1 for both button variants |
| Screenshot before rendering | One bounded request waits for queued `QQuickWindow::afterFrameEnd`; GUI-thread readback, explicit errors, minimum screenshot/quit combination 100 ms | Three 100-ms CLI launches with PNG dimensions/content, normal 600-ms path, 7680×4320 maximum viewport, save failure, unrendered-window deadline, missing window, rejected 1/99-ms combinations and ordinary 1-ms quit |

All ten routes, three source modes, seven calibration stages, immutable artifact
semantics, dual preview and point-cloud hooks remain covered by existing tests.
The CLI socket trap still exercises all twelve initial routes with usable
credentials and rejects any mock connection. Existing calibration, acquisition,
replay, acceptance, client/SDK boundary and headless tests remain enabled.
No protocol, runtime, SDK, storage, calibration algorithm or hardware interlock
changes were made. The existing bridge refresh intervals are unchanged.

### Local verification

The reproducible baseline container uses Ubuntu 24.04 image digest
`sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55`,
GCC 13 and Qt 6.4.2 (`6.4.2+dfsg-4build3`). The host uses Ubuntu 25.10,
GCC 15 and Qt 6.9.2. All runs use offscreen/software Qt rendering and Debug builds.

| Configuration | Outcome | Local log in `build/ui-m0-review/` |
| --- | --- | --- |
| Ubuntu 24.04 / Qt 6.4 desktop | PASS, 46/46 | `qt64-final-ctest.log` |
| Ubuntu 25.10 / Qt 6.9 desktop | PASS, 46/46 | `qt69-final-ctest.log` |
| Ubuntu 24.04 / Qt disabled | PASS, 41/41 | `headless-final-ctest.log` |
| Ubuntu 25.10 / Qt 6.9 desktop ASan/UBSan, leak detection enabled | PASS, 46/46 | `qt69-sanitizers-ctest.log` |
| Qt 6.4 focused QML after bounded conversion addition | PASS, three consecutive runs | `qt64-bounded-ctest.log` |
| Qt 6.9 desktop ASan/UBSan affected QML/CLI after final additions | PASS, 2/2 | `qt69-sanitizers-bounded-ctest.log` |
| Ubuntu 24.04 / Qt disabled ASan/UBSan, leak detection enabled | PASS, 41/41 | `headless-sanitizers-ctest.log` |
| Additional Ubuntu 24.04 / Qt 6.4 desktop ASan/UBSan | FAIL, pre-existing Qt software texture leaks; see below | `sanitizers-ctest.log` |

The host OpenCV packages were extracted locally because installed development
packages were unavailable; `OpenCV_DIR` and `LD_LIBRARY_PATH` point into ignored
`build/deps/root`. Host sanitizer tests use `OPENCV_OPENCL_RUNTIME=disabled` to
avoid the previously documented installed CUDA/OpenCL loader leak while testing
the unchanged CPU calibration path. No sanitizer suppression or disabled leak
detection is used. The Ubuntu 24.04 CI-equivalent run uses normal distribution
dependencies and no OpenCL override.

Two initial environment/timing failures were investigated and retained: the
container lacked `git` and its read-only worktree administrative directory for
the validation harness; supplying both restored that test. An initial Qt 6.9
maximum-size screenshot exhausted the old 100-ms margin under concurrent build
load. Requesting short-deadline screenshots immediately while waiting for a
rendered frame fixed it; the original timing assertions remain enabled.

Edited C/C++ files were formatted with the repository `.clang-format` and checked
with `clang-format --dry-run --Werror`; `git diff --check` passes. The supplied
`.ui-m0-review-task.md` was removed before the first correction commit.

### Explicit Qt 6.4 software-renderer sanitizer limitation

The additional Studio ON Qt 6.4 sanitizer suite is **not green**. Qt Quick's
software render context retains cached control-image textures at shutdown.
The unchanged reviewed SHA reproduces the same failure in the unchanged
`studio-calibration-qml`: **12,096 bytes in 18 allocations**, with allocation
stacks in `QQuickDefaultTextureFactory`, `QSGSoftwarePixmapTexture` and
`QSGRenderContext::textureForFactory`. The [complete baseline failure log](evidence/qt64-baseline-software-renderer-leak.txt)
is retained for independent review. Reproduce with:

```bash
cmake -S build/ui-m0-review/baseline -B build/ui-m0-review/baseline-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DMANTIS_SANITIZE=ON -DMANTIS_BUILD_STUDIO=ON \
  -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build/ui-m0-review/baseline-asan --target mantis-studio-calibration-qml --parallel 4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/ui-m0-review/baseline-asan -R '^studio-calibration-qml$' --output-on-failure
```

Source inspection provides a specific dependency explanation:
[Qt 6.4.2 `QSGSoftwareRenderContext::invalidate`](https://github.com/qt/qtdeclarative/blob/v6.4.2/src/quick/scenegraph/adaptations/software/qsgsoftwarecontext.cpp)
only emits `invalidated`; the
[Qt 6.9.2 implementation](https://github.com/qt/qtdeclarative/blob/v6.9.2/src/quick/scenegraph/adaptations/software/qsgsoftwarecontext.cpp)
explicitly deletes both cached texture collections and glyph caches. This is a
pre-existing third-party shutdown leak, justified as a remaining platform limit,
not suppressed or recorded as PASS. No Qt private cleanup, architecture change
or newer-Qt pin was added. Ownership/lifetime changes additionally pass the full
Qt 6.9 desktop sanitizer suite. The required, unchanged CI sanitizer job remains
Qt disabled; regular Studio ON CI continues to test Qt 6.4 on both architectures.

### Visual review and independently accessible captures

Reviewed the approved ten concepts and generated Qt 6.4 captures: mock Devices,
compact Home/calibration, disconnected acquisition, and both shared-button
variants in default, hover, pressed, focus and disabled states. Primary text
remains readable during press; hover has a distinct light accent, focus has a
visible border, and disabled controls have muted text/fill. Stale acquisition
uses a wrapping global capture-status label rather than implying every discovered
device is streaming. Compact legacy acquisition still scrolls as documented.

Reproduce all captures with the normal Studio build:

```bash
ctest --test-dir build/debug -R '^studio-ui-m0-(qml|cli)$' --output-on-failure
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/debug/bin/mantis-studio --ui-mode=mock --workspace=devices \
  --window-size=1536x1024 --quit-after=5000 --screenshot=/tmp/mantis-ui-m0-devices.png
```

CTest writes `ui-m0/screenshots/` and `ui-m0/cli/` beneath the selected build
directory, including `button-{primary,secondary}-{default,hover,pressed,focus,disabled}.png`,
`disconnected-last-known-acquisition.png`, all ten routes and three viewport sizes.
Studio ON CI uploads these PNGs, `LastTest.log` and the retained crash/leak
diagnostics as `ui-m0-ubuntu-24.04` and `ui-m0-ubuntu-24.04-arm` artifacts for
30 days. Captures are software presentation evidence, not scanner measurements.

### Exact-SHA CI release gate

Correction implementation SHA: `3446ace818506938f245285b9eab53c9e83222a7`.
[Correction CI run](https://github.com/martinkoenig/mantis-studio/actions/runs/37778795139).
Every job's API `head_sha` matches this SHA. No test, architecture gate or
matrix entry was removed or weakened. The workflow change only adds artifact upload.

| Required job | Outcome | Exact-SHA job |
| --- | --- | --- |
| Ubuntu 24.04 x86_64, Studio ON / Qt 6.4 | PASS, 46/46 | [Desktop x86_64](https://github.com/martinkoenig/mantis-studio/actions/runs/37778795139/job/113316342788) |
| Ubuntu 24.04 native ARM64, Studio ON / Qt 6.4 | PASS, 46/46 | [Desktop ARM64](https://github.com/martinkoenig/mantis-studio/actions/runs/37778795139/job/113316342874) |
| Ubuntu 24.04 x86_64, Studio OFF | PASS, 41/41 | [Headless x86_64](https://github.com/martinkoenig/mantis-studio/actions/runs/37778795139/job/113316342975) |
| Ubuntu 24.04 native ARM64, Studio OFF | PASS, 41/41 | [Headless ARM64](https://github.com/martinkoenig/mantis-studio/actions/runs/37778795139/job/113316343179) |
| Ubuntu 24.04 x86_64 ASan/UBSan | PASS, 41/41 | [Sanitizers](https://github.com/martinkoenig/mantis-studio/actions/runs/37778795139/job/113316342628) |

The two uploaded [review artifacts](https://github.com/martinkoenig/mantis-studio/actions/runs/37778795139#artifacts)
were downloaded and verified: each contains 38 PNGs and `LastTest.log` with
the QML and CLI PASS markers, plus the committed diagnostic evidence.
Local runs are x86_64; ARM64 evidence is from native GitHub runners.

Correction commits are the layout/root-cause repair `4d87b7df7f2010bf1b5b893c0ca18a0751843b8a`
and the remaining review fixes `3446ace818506938f245285b9eab53c9e83222a7`.
Correction files relative to the reviewed SHA:

- `apps/studio/{bridge.cpp,bridge.hpp,main.cpp,screenshot.hpp}`.
- `ui/workspaces/{FoundationWorkspace,AcquisitionWorkspace}.qml`,
  `ui/state/{AppUiState,MockFixtures}.qml`, `ui/shell/Main.qml`,
  `ui/components/StudioButton.qml`, `ui/design/Theme.qml`.
- `tests/unit/studio_ui_m0.cpp`, `tests/integration/studio_ui_m0.py`,
  `CMakeLists.txt`, `.github/workflows/build.yml`.
- `docs/ui/{README,validation}.md`,
  `docs/ui/evidence/{qt64-baseline-crash.txt,qt64-layout-reproducer.cpp,qt64-baseline-software-renderer-leak.txt}`.

The documentation follow-up must itself pass a fresh complete exact-SHA CI matrix
before handoff; the final handoff supplies its SHA and run URL.
Physical scanner/laser, metrology precision, remote networking and native
window-system accessibility acceptance remain outside this UI fixture evidence.
Independent reviewer approval remains required after all five jobs pass.


## Final P2 correction — operation errors and confirmed runtime state (2026-10-08)

Baseline: `2c712f2ebf3eae579bf32ef29c99a3616dfe7600`, whose five required CI
jobs passed. This follow-up addresses the independent review's remaining error
classification finding; earlier evidence and dependency limitations above remain
historical records.

Read ADR-002, ADR-033, CONTRIBUTING.md and the binding reliability/performance
standard before editing. Investigation of `mantis-client` showed that
`Client.call` preserves a server error's code/component/message in `mantis::Failure`,
but socket failures use the same type. `Client.data` also performs a control call,
checks the data reference and reads a local mapped packet. A mapped-file failure
and a socket failure can both be `Status::io` with component `platform`; server
operations may also reject with `io`. Authentication rejection currently uses
`invalid_argument`, without a distinct authentication status. Neither code nor
message parsing can safely establish connectivity. Previously, a single catch
reduced all exceptions to `what()`, skipped subsequent snapshot confirmation and
marked the runtime disconnected, even after receiving a valid snapshot before
artifact loading failed.

The bridge now separates operation, snapshot and artifact phases. The result
contains an optional successfully obtained snapshot, plus typed root causes.
`connected` means a usable authenticated snapshot was confirmed; a failed
confirmation means runtime state is unconfirmed, not a claim that the peer or
physical scanner ceased operating. UI labels follow this distinction.

Operations execute once, followed by the existing read-only snapshot request even
when rejected. Artifact failures discard the preceding confirmation and make one
bounded read-only snapshot request because a data error alone cannot distinguish
control transport from local mapping. Failure of that confirmation preserves both
causes and all authoritative last-known data. Successful confirmation updates
capture/device/project state while retaining the operation/artifact diagnostic.
No mutating operation is retried, no timer/polling loop is added, and no capture or
calibration authority moves out of mantisd. No protocol, SDK, plugin ABI or daemon
behavior changes were required.

`StudioBridge.errorDetails` retains phase, numeric code, original component and
message. Untyped exceptions retain their message with an absent code, without
inventing an `io` classification. Diagnostics are bounded to the latest issue per
phase: unrelated successful refreshes retain operation/artifact errors; the next
attempt in that phase resolves/replaces them. Snapshot errors clear on successful
confirmation. A failed cloud load preserves displayed cloud identity and does not
mark the failed artifact as already displayed.

The new `studio-bridge-errors` test uses the real public C++ client and a bounded
Python wire fixture, without a daemon or hardware. It covers:

- Successful connection and rejected operations with `busy`, `io` and
  `invalid_argument`, preserving structured root cause and refreshing state.
- Actual peer closure during operation and artifact requests; failed confirmation
  retains both root causes and authoritative last-known state.
- Missing/corrupt mapped packets, successful recovery, preserved cloud identity,
  and repeated automatic acquisition of an artifact that has not loaded.
- Authentication rejection and missing credentials, with no assumed connection.
- Reconnection with refreshed active/idle capture state and diagnostic persistence.
- Untyped local exceptions and exactly-once mutating operation requests.

The QML regression checks that a rejected operation remains visibly labelled
“Operation failed” while the runtime stays confirmed, and that failed snapshot
confirmation shows “Runtime state unconfirmed”. All original M0 and calibration
assertions remain; disconnected-label assertions now reflect that more precise
meaning. Restoring the old early-return failure behavior makes the wire regression
fail at the first rejected-operation snapshot assertion. Restoring the corrected
collector passes; five consecutive Qt 6.4 runs also pass.

Local logs are retained beneath ignored `build/ui-m0-errors/`. Reproduce focused
coverage after the normal Studio build with:

```bash
ctest --test-dir build/debug -R '^studio-(bridge-errors|ui-m0-qml|calibration-qml)$' --output-on-failure
```

| Local configuration | Result | Log in `build/ui-m0-errors/` |
| --- | --- | --- |
| Ubuntu 24.04 / Qt 6.4 desktop | PASS, 47/47 | `qt64-final-ctest.log` |
| Ubuntu 24.04 / Qt disabled | PASS, 41/41 | `headless-ctest.log` |
| Ubuntu 24.04 / Qt disabled ASan/UBSan | PASS, 41/41 | `headless-sanitizers-ctest.log` |
| Ubuntu 25.10 / Qt 6.9 desktop ASan/UBSan | PASS, 47/47 | `qt69-sanitizers-final-ctest.log` |
| Qt 6.4 real-client error regression under ASan/UBSan | PASS, 1/1 | `qt64-sanitizers-focused.log` |
| Qt 6.4 error regression repeated | PASS, five consecutive runs | `qt64-repeat.log` |

Sanitizer runs retain leak detection; host OpenCV setup and the previously
documented host OpenCL override are unchanged. The full Qt 6.4 software-QML
sanitizer dependency limitation above is not reclassified as passing; the new
QCoreApplication-based wire test separately passes Qt 6.4 ASan/UBSan without a
suppression. Edited C/C++ formatting and `git diff --check` pass.

Final exact-SHA CI results are provided in the handoff for this correction.
Screenshot artifacts and the required five-job matrix remain enabled; no failing
check, warning assertion or matrix entry was suppressed. Physical hardware and
metrology acceptance limits are unchanged.

The implementation commit `997b1abde9315ad8ac225649cfe41dd7a1fcb1e6`
passed all five jobs in [run 37785770600](https://github.com/martinkoenig/mantis-studio/actions/runs/37785770600):
Studio ON x86_64/native ARM64 each 47/47, Studio OFF x86_64/native ARM64 and
ASan/UBSan each 41/41. Both desktop artifacts were downloaded and verified;
each includes 38 PNGs and wire/QML/CLI PASS markers in `LastTest.log`.

The final assertion follow-up checks rejection codes directly against the wire
fixture's known `busy`, `io` and `invalid_argument` values, both before presentation
and in `errorDetails`, rather than comparing only two representations of the
collected result. That final regression passes under Qt 6.4, Qt 6.4 ASan/UBSan and
Qt 6.9 ASan/UBSan. Logs: `wire-status-{qt64,qt64-sanitizers,qt69-sanitizers}-test.log`.
This assertion/documentation follow-up changes no production behavior and requires
a fresh complete exact-SHA matrix before the final handoff.
