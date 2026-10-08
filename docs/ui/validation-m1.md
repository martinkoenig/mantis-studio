# UI-M1 Home validation — 2026-10-08

Scope: [Home contract](home.md), based on accepted UI-M0
`3ba819ab96686689c69888e1a1a223a376de7375`, isolated branch
`feature/ui-m1-home`. No integration merge, other-worktree change, daemon, public
client/protocol or plugin ABI change. Capture and calibration retain daemon authority.
The [binding reliability standard](../architecture/reliability-performance-and-validation.md)
and [M0 validation](validation.md) remain authoritative.

## Visual fidelity completion (supersedes earlier presentation)

The approved [1536×1024 concept](reference/01-home.webp) was inspected before
coding, alongside an actual application baseline at reviewed `090fa25`. The
[numbered delta inventory and viewport sketch](evidence/m1-fidelity-delta.md)
preceded implementation. The retained [full-size comparison](evidence/fidelity/reference-before-after.jpg)
contains reference / old / new, with [old](evidence/fidelity/before-mock-1536.png)
and [new](evidence/fidelity/after-mock-1536.png) separate PNGs.

The standalone Home artifacts card and `homeArtifacts` CTA are absent in all
three modes. Recent Activity now directly follows four distinct Quick Actions.
Mock renders Scan/Mesh/Texture/Export rows as a compact five-column table; live
renders descending-sequence events and separately grouped deterministic-ID
artifacts, with row provenance, state, chunks/identity and unknown Date/Size.
There is no synthesized joint chronological order or Home artifact loading.
Bridge/model data and Acquisition workflows remain unchanged.

Final Qt 6.4 primary mock measurements: hero **972×280**, project cards **234×198**,
Quick Actions **150px**, action grid bottom **y803**, Activity heading **y815**,
first actual row bottom **y905**, content edge **y982**. Three complete Activity
rows and the beginning of a fourth fit before the footer. The scanner, system,
compact jobs and illustrated tips rail is visible in this viewport. Original
scanner/casting and four distinct shaded mechanical renders replace tiny art;
[sources, licence and reproduction](../../ui/home/assets/README.md) are included.
Six JPEGs total **221,020 bytes**; no image plugin/build dependency is introduced.

Iteration corrected the first render's cropped bracket, sample grain, Qt 6.4's
unavailable WebP decoder (using its existing JPEG decoder), local-guide tab focus,
and 1920px hero cropping. Screenshot review covered mock/live/hybrid at all three
sizes, primary no-runtime/stale states, 1080 long/empty live and hybrid showcase
scroll, plus real public-wire rejection/access failure/loss/reconnect. Details and
remaining visual differences are checklisted in the inventory. There are **38 M1
PNGs**, plus all existing M0 evidence, under the unchanged CI upload paths.

A normal native host Qt 6.9.2 **Wayland/OpenGL QRhi** window was also rendered and
[inspected](evidence/fidelity/native-wayland-1536-logical.png): 1536×1024 logical,
1920×1280 device pixels at desktop scale 1.25. The scenegraph log confirms OpenGL
QRhi creation and no QML warning. This single-window presentation check does not
certify GPU driver stability, native accessibility or physical scanner acceptance.
Primary size assertions and the full state matrix remain Qt 6.4 offscreen/software.
[Reproduction commands and evidence scope](evidence/fidelity/README.md).

`studio-home-qml` preserves existing bounded-model, no-equal-update, malformed
input, bridge lifetime, state, routing, source/identity and Qt 6.4 removal/resize
regressions. New coverage checks artifacts-card/CTA absence in mock/live/hybrid;
substantive primary geometry and visible first row; controls inside action cards;
truthful Activity Date/Size/provenance/group order, huge literal strings and
missing state/chunks, removal/retention/source changes; current-project vs sample
history; illustrative vs unavailable telemetry; working guide and example focus.
Disabled import/examples/jobs reject mouse, Space, Return and accessible press.
Enabled controls preserve tab/backtab focus and accessible names. No route starts
capture, activates calibration or imports a project. `studio-home-wire` still
uses the production public client/async bridge; its command log contains only
the deliberate fixture faults and restoration, with no Home mutations.

Complete local suites passed using the supported build options and environments
recorded below. Complete runs were followed by final resource/test-fixture checks
(including the corrected wide hero and settled long-activity scroll). No test is
skipped, removed or weakened; the obsolete artifact-button test is replaced by
all-mode absence and real artifact-data preservation coverage.

| Fidelity suite | Result | Log under `build/ui-m1-fidelity/` |
| --- | --- | --- |
| Qt 6.4 complete Studio | 49/49, 178.43s | `qt64-full.log` |
| Qt-free complete headless | 41/41, 127.29s | `headless-full.log` |
| Complete headless ASan/UBSan/LSan | 41/41, 286.37s | `headless-sanitizers-full.log` |
| Additional Qt 6.9 complete Studio ASan/UBSan/LSan | 49/49, 372.94s | `qt69-sanitizers-full.log` |
| Final Qt 6.4 bridge/Home/M0/CLI/calibration-QML/acceptance | 7/7, 38.15s | `qt64-final-regressions.log` |
| Final Qt 6.9 sanitizer same regressions | 7/7, 52.07s | `qt69-final-regressions.log` |
| Home/public-wire repetition | Each passed 3 consecutive times, 40.54s | `qt64-repeat.log` |

The three repeated 90-transition Home workloads observed 3186 / 3088 / 3208ms,
including event handling/settling, not an invented performance budget. The
accepted 500ms bridge update cadence and GUI-result-delivery busy lease remain
unchanged. The separate pre-UI-M3 unreadable PointCloud retry/backoff issue remains
tracked in [Home](home.md#known-follow-ups). The earlier Qt 6.4 software-renderer
LSan dependency leak remains documented below; it is not declared fixed or hidden
with suppressions. Final Qt 6.9 and required headless sanitizers keep leak detection.

**Final commit gate:** these changes require all five mandatory architecture jobs
on the pushed exact final SHA (x86_64/ARM64 Studio ON/OFF and headless sanitizers).
The final handoff must supply that SHA, successful run and artifact URLs after
reading complete results. Earlier CI links below belong to earlier accepted
commits and cannot satisfy this change's gate. No branch merge is authorized.

The first fidelity CI run ([37841122157](https://github.com/martinkoenig/mantis-studio/actions/runs/37841122157),
`b95972a2b92e35b5079cb7d91bdc4d194197c7d6`) passed four jobs but ARM64 Studio ON
reported a Home QML segmentation fault; it is **not** acceptance evidence.
Review found that mode-dependent labels inside the Quick Actions Repeater model
recreated all four controls and their nested layouts on mode changes. Labels now
bind within persistent delegates. A new QPointer regression fails on the previous
QML at live → mock and passes with the correction, alongside existing focus and
input checks. No test is skipped or relaxed. Both Studio CI jobs additionally run
the complete Home transition test three consecutive times; the required full
CTest suites remain mandatory. The failure's exact native stack was unavailable,
so the corrected code still requires the final native ARM64 gate.
Local correction checks passed: Qt 6.4 bridge/Home/M0/CLI/calibration-QML/acceptance
**7/7 in 36.84s**, the same Qt 6.9 ASan/UBSan/LSan checks **7/7 in 50.15s**, and
three consecutive Qt 6.4 Home runs **33.07s**. The final mock 1536 screenshot is
byte-identical to the retained after image; labels/layout and assets are unchanged.
[Before-fix identity failure](evidence/fidelity/persistent-actions-before.txt),
[Qt 6.4 checks](evidence/fidelity/qt64-persistent-actions.txt),
[Qt 6.9 sanitizer checks](evidence/fidelity/qt69-persistent-actions.txt) and
[repetition](evidence/fidelity/qt64-persistent-actions-repeat.txt) retain complete
results separately from the earlier full suites.

## Earlier accepted implementation checks (retained history)

All remaining sections preserve prior validation history, including superseded
Canvas artwork and the separate artifacts/events presentation. The fidelity
completion above and current [Home contract](home.md) describe the final layout.



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
- All nine enabled live Home CTAs via mouse and keyboard, focus/hover, accessible
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

## Independent-review corrections — UI-M1-01 / UI-M1-02

Reviewed baseline: `da440f2e54559ed6f8d35cb6a37327422a22ce88`, whose complete
[final five-job run](https://github.com/martinkoenig/mantis-studio/actions/runs/37822471911)
passed. This correction preserves the independently accepted model, data authority,
bridge lifecycle and acquisition/calibration workflows. Only Home art/action
presentation, resource registration, tests and documentation change.

UI-M1-01: original deterministic local Canvas geometry now depicts four different
mechanical forms: open flanged/ribbed housing, swept-blade impeller/rotor, upright
L mounting bracket with gusset, and shallow closed ribbed cover. Shared projection
and palette keep the style coherent; shape geometry and silhouettes differ.
All four retain their Demo / Mock labels. No external asset/dependency, reference
image rendering, network loading, random input or animation timer is introduced.
The hero remains unchanged. The drawing uses fixed bounded polygons/curves (at
most 11 blades, 40 disc segments and 32 hub segments), rendering on resize/shape
changes, with no polling or animation timer.

UI-M1-02: mock detail buttons are disabled, visibly explain that illustrative
jobs/artifacts have no available details, and expose that reason through accessible
descriptions. Handlers also guard the illustrative source. Live/hybrid buttons
still open existing Acquisition; no browser or backend operation is added.
`studio-home-qml` covers live → mock → hybrid → mock → live transitions, enabled
mouse/keyboard routing, disabled Space/Return/mouse suppression, accessible
role/description and accessible press safety. Real Tab/Backtab traversal verifies
focus even when a persistent button retained mouse focus. Existing public-wire
navigation/authority checks remain in place.

The rendered-art regression compares all six pairs in the fitted artwork viewport
at 1080×720, 1536×1024 and 1920×1080. It requires a broad visual difference,
without depending on specific reference pixels. Semantic mechanical shapes are
established by manual review, not inferred by that numeric check.
[Retained red-test evidence](evidence/m1-home-review-corrections.txt) proves the
original gear variants and unguarded mock actions fail the new regressions;
temporary substitutions were restored before final testing.

Updated full-size mock captures at all three required sizes were inspected directly
against the approved Home reference. Review corrected a cropped upper bracket
edge and a rectangular shadow; final silhouettes fit the thumbnail viewports,
shadows fade transparently, explicit labels remain, and disabled detail actions
have visible reasons. Compact layouts retain vertical scrolling for lower cards.
Generated capture paths and CI upload structure remain unchanged (34 M1 PNGs,
plus all 38 M0 PNGs per desktop artifact).

Qt 6.4 accessibility scope: its Quick item state does not derive the disabled flag
from `Item.enabled`, as confirmed in the
[Qt 6.4 implementation](https://github.com/qt/qtdeclarative/blob/v6.4.2/src/quick/accessible/qaccessiblequickitem.cpp)
and [attached properties](https://github.com/qt/qtdeclarative/blob/v6.4.2/src/quick/items/qquickaccessibleattached_p.h).
The tests prove effective enabled state, actual input/accessible press behavior
and accessible explanations. No private Qt API, dependency patch or fabricated
disabled state is introduced. Native OS screen-reader validation remains unproven.

All complete local correction suites passed, using the environment/commands
above and the restored final implementation:

| Correction suite | Result | Local log under `build/ui-m1-review/` |
| --- | --- | --- |
| Qt 6.4 complete desktop | 49/49 passed, 165.47 s | `corrections-qt64-full.log` |
| Complete Qt-free headless | 41/41 passed, 118.92 s | `corrections-headless-full.log` |
| Headless ASan/UBSan/LSan | 41/41 passed, 272.36 s | `corrections-headless-sanitizers-full.log` |
| Additional Qt 6.9 desktop ASan/UBSan/LSan | 49/49 passed, 353.45 s | `corrections-qt69-sanitizers-full.log` |
| Final Qt 6.4 Home component/public-wire checks | 2/2 passed, 10.77 s | `corrections-final-focused.log` |
| Final Qt 6.9 sanitizer Home component/public-wire checks | 2/2 passed, 16.14 s | `corrections-final-sanitizers-focused.log` |

The final handoff identifies the exact correction SHA, all five completed job results and
its uploaded evidence; the baseline run above cannot satisfy the correction gate.
