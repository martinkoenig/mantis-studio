# UI-M3c implementation and verification

Branch: `feature/ui-m3-scan`. Accepted starting revision:
`d01c00b47fd5bf6d2b0e73e9121d296fb2f07383`. This record describes implementation
self-verification; independent review and exact-SHA remote CI remain acceptance gates.

## Ownership and compatibility

Before: Classic's QML declarations created the live renderer and two live previews;
Calibration declared another pair and reassigned the preview attachment. After:

```text
Main.qml / QQmlApplicationEngine
└── LiveSurfaces / LiveViews (QObject lifetime owner)
    ├── one CppOwnership PointCloudView ─── visual host: Scan / Classic / parking
    ├── one CppOwnership left MeasurementView ─── Scan / Classic / Calibration / parking
    └── one CppOwnership right MeasurementView ── Scan / Classic / Calibration / parking
        ▲ one attachView + one attachPreview to the existing StudioBridge
```

QObject parenting and CppOwnership control deletion. QQuickItem `parentItem`
controls presentation only; explicit GUI-thread size connections replace anchors
when hosting in a different slot. Cross-window/thread hosts are rejected. Hidden
views park at finite size; the owner is never globally hidden. Teardown does no
host reattachment. Bridge QPointers become null on engine destruction; its existing
watchers finish on bridge shutdown. Calibration's capture stage previously declared
another live pair and reassigned
StudioBridge's preview targets. It now receives slots for the same canonical pair,
without changing its controller or capture workflow. The whole shell therefore has
exactly **one PointCloudView and two MeasurementViews**, including Calibration.

The existing 500 ms snapshot and 66 ms preview paths, asynchronous client,
generation-checked M3a cloud retry, Published ownership and renderer are reused.
The preview completion lease now includes queued GUI application. A complete pair
is required for current evidence; partial/error pairs retain explicitly last-known
buffers. Old project generations and stopped-source results are rejected. No image
conversion, packet handling or hardware worker was added to QML.

Classic remains the live startup/default acceptance route with the same canonical
objects and replay/verify/artifact/recipe/export/plugin/job controls. The accepted
direct RunPipeline rejection/diagnostic contract remains unchanged;
Classic controls require a known capture identity. Home, Projects, Devices and
Calibration remain covered by their full existing regressions.

## Authority and failure semantics

[Scan's authority matrix](scan.md#capture-and-display-contracts) separates source
permission, confirmed state and descriptor capability. Scan functions reject
Mock/Demo intents even when invoked directly. Bridge Start/Stop independently
validate runtime enablement, confirmed snapshot, capture uniqueness, admitted
capabilities and the existing operation completion lease. Selection is an exact
local ID, never a display label or free-text input; project/source/disconnect and
inventory removal invalidate it. The daemon confirms outcomes. Mutation rejection,
socket closure and timeout produce diagnostics with no optimistic state or retry.
Processing remains Not reported; stale runtime jobs retain M3B-F01 qualifiers.

## Regression and wire evidence

`studio-scan-surfaces` drives production Main.qml: 100 Scan/Classic changes,
120 compact/breakpoint changes, 40 Demo/Live cycles and 500 snapshots. Assertions
cover canonical identity, QObject/CppOwnership, GUI host/window/size, one-time
attachments, Published reference count and unchanged QImage storage. GC and a
foreign-window host exercise lifetime boundaries. All seven Calibration stages
are traversed; its capture stage renders the canonical grayscale pair with no
reattachment, and returning to Scan retains the same images. Real frame regions prove grayscale
values, non-square aspect preservation and visible Published geometry in both
routes. Actual pointer/wheel/reset, missing frame, disconnect, A→B→A, late preview
and cloud, stopped-source completion and recovery are covered. All shell test modes
assert the live objects are destroyed at engine shutdown and reject Qt/QML warnings.

`studio-scan-capture-wire` uses the existing authenticated public-client fixture
and real asynchronous bridge/Main.qml. It publishes a 512-point float32 cloud and
non-square checkerboard FrameSet through Client.data/preview and the existing worker.
One permitted Start and Stop survive route/source changes. Invalid, duplicate,
missing, child-only, non-capable, oversized inventory/ID/capabilities, busy repeats,
ambiguous capture and Mock/Demo intents send no mutation. Rejected Start/Stop,
socket-close Start and timeout Start each send exactly one request. Disconnect
while recording retains stale evidence; recovery restores previews. A→B→A clears
buffers and reloads only once. Request/operation receipts and screenshots are saved
under `build/<config>/ui-m3c/wire/`.

Verified total in each final Studio configuration: four capture_start (one positive, three failed intents), two
capture_stop (one positive, one rejected), two artifact_data (initial A and return
to A), zero navigation-induced loads, preview count equals lease releases. Forbidden
replay/project/plugin/job mutations are rejected by the fixture's allowlist.
A test-only GUI heartbeat proves the five-second socket timeout stays asynchronous.

| Wire scenario | capture_start | capture_stop |
| --- | --- | --- |
| Confirmed idle → active → idle, route/source changes and repeated input | 1 | 1 |
| Invalid/duplicate/missing/non-capable/child/oversized device, busy, unknown/ambiguous capture, Mock/Demo, disconnected | 0 | 0 |
| Daemon rejects Start (fixture mode 17) | 1 | 0 |
| Daemon rejects Stop (mode 31) | 0 | 1 |
| Start socket closes (mode 18) | 1 | 0 |
| Start socket times out (mode 19) | 1 | 0 |

The retained `wire_operations` entries identify each actual mutation by fixture
mode. Snapshot/events and preview request totals are also retained per environment;
timed preview totals vary with scheduling and every request has one lease release.

## Visual and resource evidence

The nine-size matrix is 1080×720, 1280×720, 1366×768, 1440×900, 1536×1024,
1920×1080, 2560×1440, 3440×1440, 3840×2160. DPR 1 covers Mock, waiting, active,
idle, stale, Hybrid-Live and Demo plus compact Cameras/Setup/Sequence. DPR 1.5 and
2 cover Mock, active and Demo at all nine sizes. Native Wayland additionally covers
normal/maximized/restored in those three sources. Tests verify frame dimensions,
panel bounds, central growth, right-edge anchoring, keyboard focus and independent
scrolling. Evidence is actual Qt readback, never the reference image rendered as UI.

The approved `02-scan.webp` hierarchy, palette, bounded rails and elastic center
are retained. Intentional differences: actual fixture geometry replaces illustrative
measured coverage, only Geometry is enabled live, RGB/tracking/rates/power/setup
remain unavailable, and capture readiness is explicitly unknown. No hardware
accuracy, FPS, physical timing or safety is claimed.

One canonical cloud shares its immutable 6144-byte position buffer with the fixture
(two Published references); the two canonical preview images total 9216 bytes and
retain the same storage addresses across routing. QQuickPaintedItem's existing
render backing and temporary worker/packet allocations are not claimed zero-copy.
Route latency is measured around GUI host transition/event processing, not scanner
latency or full frame presentation. Wire preview request windows compare active
capture before/after navigation; this is local software transport evidence. Existing
source inspection confirms one preview timer, watcher and conversion path; it is not
physical camera-rate evidence. No new production timers/polling or cloud decoder.

## Local checks and reproduction

Final execution results and retained receipts are recorded in
[evidence/m3c/README.md](evidence/m3c/README.md). Build caches use these configurations:

| Directory | Environment | Full suite result |
| --- | --- | --- |
| build/qt64 | Ubuntu 24.04 container, Qt 6.4.2, GCC 13.3, Studio ON | PASS 81/81, 437.05 s |
| build/qt64-headless | Same container, Studio OFF | PASS 42/42, 119.41 s |
| build/qt64-headless-sanitizers | Same container, OFF, ASan/UBSan/LSan | PASS 42/42, 247.34 s |
| build/debug | Host Qt 6.9.2, GCC 15.2, Studio ON | PASS 81/81, 424.76 s |
| build/qt69-sanitizers | Same host, ON, ASan/UBSan/LSan | PASS 81/81, 701.98 s |

These five complete suites total 327 passing tests. The final full-run software
performance observations are below; values reflect these local test environments,
not a hardware or frame-presentation latency guarantee.

| Configuration | Route GUI p50 / p95 / max (µs) | Preview requests before / after routing (per 1000 ms) | GUI heartbeats during 5 s socket timeout |
| --- | --- | --- | --- |
| Qt 6.4 | 122 / 3723 / 8072 | 16 / 15 | 496 |
| Qt 6.9 | 214 / 7186 / 10082 | 16 / 15 | 519 |
| Qt 6.9 sanitizers | 11422 / 18252 / 27854 | 17 / 16 | 521 |

All three recorded zero navigation-induced artifact loads and equal preview
request/release counts. The machine-readable receipt binds the final verification
inputs, logs and selected screenshots with SHA-256 digests. Earlier full runs
exposed and led to corrections of a pipeline-guard regression, Calibration view
hosting/registration, and the transport-disabled M0 presentation fixture; the table
reports the complete final runs after those corrections.

For each configuration: `cmake --build build/<config> --parallel 3`, then
`ctest --test-dir build/<config> --output-on-failure`. Container paths are `/work`;
commands run with `docker exec mantis-ui-m0-qt64`. Host runs use
`OPENCV_OPENCL_RUNTIME=disabled` and the existing dependency library path
`build/deps/root/usr/lib/x86_64-linux-gnu`. Sanitizers use
`ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`; no suppressions added.

The existing nine-test route/stress repeat gate adds studio-scan-surfaces and runs
`--repeat until-fail:3` (30 executions per configuration). All 90 executions passed:
Qt 6.4 in 309.70 s, Qt 6.9 in 311.52 s, and Qt 6.9 ASan/UBSan/LSan in
463.16 s. Native Wayland passed all nine normal/maximized/restored readbacks.
Native test command:
`QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software build/debug/bin/mantis-studio-scan-tests build/debug/ui-m3c/native native`.
Changed C++ files are clang-formatted; full suites include architecture boundary,
CMake, lint, CLI, bridge/retry and complete acquisition acceptance checks.

## CI and limits

The unchanged five-job architecture matrix requires Ubuntu 24.04 x86_64/ARM64
Studio ON/OFF and headless sanitizers. Studio jobs retain all existing UI artifacts
and add `ui-m3c`, including exact-SHA revision receipts. Remote checks belong to
independent review; pending checks are never counted as passes. Local evidence is
x86_64 Linux only. Windows/macOS, ARM64 and physical scanner behavior are not locally
verified. The known backend project/replay isolation P1 remains unresolved and
New/Open stay gated. No M3d replay/timeline/marker or M3e setup/profile/notes mutation
was implemented; unsupported RGB/coverage/confidence/texture remain explicit.
