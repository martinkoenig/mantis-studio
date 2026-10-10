# UI-M3b verification — Scan workspace foundation

Implementation starts at accepted UI-M3a `8a4a150edddfa5a7271ff3c119814a23bbcd128f`
on `feature/ui-m3-scan`. [Scan contracts](scan.md) describe the read-only scope;
M3c/d/e integrations remain deferred. No bridge, Classic Acquisition, runtime,
protocol, capture/replay/preview or retry-policy code is changed.

## Reproduction and automated evidence

The production shared QML bundle registers ScanModel in the application and every
shell test. Seven new normal CTest entries run on both Studio-enabled CI runners:

| Test | Evidence |
| --- | --- |
| studio-scan-model | Strict typed authority; missing/invalid metadata; confirmed/stale/reconnect and active/idle; A→B→A; retained phase errors; replacement/destruction; 12000-entry input bounds; 1000 unchanged projections with zero model notifications |
| studio-scan-qml | Actual Main.qml routing; disabled controls and accessible explanations/hover; local tabs; settings wheel isolation; visible notes; compact Escape focus; selection removal/project invalidation; hybrid source detach/rebind; mouse/keyboard/programmatic Classic navigation; 100 route switches with identical renderer/preview objects and zero command intents |
| studio-scan-matrix | Nine logical sizes × Mock/waiting/confirmed-active/stale/hybrid-live/hybrid-demo; panel bounds, right anchoring, elastic viewport growth, badges/imagery isolation, compact camera/setup/sequence captures |
| studio-scan-dpi-1.5 / studio-scan-dpi-2 | All nine sizes × Mock/live/hybrid-demo; exact physical PNG dimensions; no QML warnings |
| studio-scan-stress | 500 unchanged production DTO projections without delegate churn; 120 resize/route transitions; 30 source changes; bounded virtual sequence keyboard traversal and local selection with no artifact request |
| studio-scan-wire | Authenticated public Client/StudioBridge transport, unchanged polling/busy transitions without artifact delegate churn, active daemon-owned capture, 80 Classic/Scan switches, disconnect retaining last-known capture, project A→B→A and idle/recovery; records only existing snapshot/preview polling plus three explicit read-only EventsSince fixture controls |

The CLI regression also launches unreachable live/hybrid Scan and separate Classic
Acquisition, while its existing twelve-route mock socket trap remains intact.
Existing acceptance-export, M0–M3a, Home, Projects, Devices, Calibration, ABI,
streaming/performance and boundary tests remain in the full graphs. The command
spy intercepts every bridge command intent in QML even with runtime disabled;
the independent authenticated wire fixture rejects every unexpected request.
No scanner, real camera frame, physical laser or external service is needed.

```bash
# Ubuntu 24.04 / GCC 13.3 / Qt 6.4.2 / OpenCV 4.6 container
for dir in qt64 qt64-headless qt64-headless-sanitizers; do
  docker exec mantis-ui-m0-qt64 cmake --build /work/build/$dir --parallel 3
  docker exec -e ASAN_OPTIONS=detect_leaks=1 -e UBSAN_OPTIONS=halt_on_error=1 \
    mantis-ui-m0-qt64 ctest --test-dir /work/build/$dir --output-on-failure
done

# Existing native GCC 15.2 / Qt 6.9.2 / OpenCV 4.10 configurations
export OPENCV_OPENCL_RUNTIME=disabled
export LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
for dir in debug qt69-sanitizers; do
  cmake --build build/$dir --parallel 3
  ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
    ctest --test-dir build/$dir --output-on-failure
done

# Focused reproduction / GUI transition repeat gate
ctest --test-dir build/debug -R '^studio-scan-' --output-on-failure
ctest --test-dir build/debug -R '^studio-scan-(qml|stress)$' \
  --repeat until-fail:3 --output-on-failure

# Real desktop surface, normal/maximized/restored in each source mode
QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software \
  build/debug/bin/mantis-studio-scan-tests build/ui-m3b-verification/native native
```

Full final local results and exact revision are recorded in the completion report
and `docs/ui/evidence/m3b/local-verification.json`. Build/full-test logs remain in
`build/ui-m3b-verification/`; full CTest transcripts also reside in each build's
`Testing/Temporary/LastTest.log`. Sanitizers run with leak detection and UBSan
halt-on-error, without new suppressions or skipped tests. The final Studio graphs
contain 79 tests; headless graphs contain 42.

## Screenshot inspection and reference comparison

The full-resolution [approved Scan reference](reference/02-scan.webp) is a visual
concept, with [explicit capability caveats](reference/README.md). Before changing
code, the accepted mock Scan route was captured with the actual Qt 6.4 application
at 1536×1024 (`build/ui-m3b-verification/before/mock-scan.png`). It showed the M0
planned-panel foundation. The new route provides actual Qt components for camera
cards, elastic grid/viewport, scanner/setup/notes rail, status strip and dock.

The automated matrix writes unaltered Qt PNGs beneath each Studio build's
`ui-m3b/`. See [selected versioned captures and inventory](evidence/m3b/README.md).
`matrix/` contains all nine logical sizes (1080×720, 1280×720, 1366×768,
1440×900, 1536×1024, 1920×1080, 2560×1440, 3440×1440, 3840×2160), including compact
Cameras/Setup/Sequence views in each source state. `dpi-1.5/` and `dpi-2/` contain
27 captures each; DPR 2 4K logical windows yield 7680×4320 physical PNGs.
`qml/` captures setup scrolling with stationary notes and a Classic round trip;
`stress/` captures keyboard selection at the final advertised row; `wire/`
contains active/stale/recovered screenshots, a fixture log and `requests.json`.

Native Qt 6.9 Wayland desktop captures cover normal/maximized/restored Mock, Live
and Hybrid at actual desktop DPR 1.25. Normal PNGs are 1920×1280; maximized PNGs
2560×1512. Wayland fractional surface allocation can differ by one physical pixel
from rounded logical-size × DPR; only this native path admits that bounded
rounding tolerance. All offscreen matrix DPR assertions remain exact. Native
captures do not claim a physical 4K display or scanner validation.

Inspection checks hierarchy, spacing, fonts, right-edge anchoring, compact pane
access, scrollbars/notes visibility, source labels, grid/axes, stale warnings and
absence of live-looking invented data. Qt 6.4 scrollbar positioning was corrected
using its existing managed ScrollView bars; only vertical scrolling is enabled.
The full regression exposed a repeated nested status-layout polish loop while
Scan was hidden on Home startup. Replacing those cells with bounded Qt items and
guarding dormant zero-size layout also eliminated reproducible fontconfig leaks
in the Qt 6.9 sanitizer Home/Projects wire tests. A new production-shell scenario
starts on Home and exercises source/size changes before exposing Scan. No warning
filter, test skip or sanitizer suppression was added.
The final graph rejects binding/layout warnings and tests independent wheel/focus
behavior. Source-order artifacts are deliberately not labelled scan sessions.

Differences from the concept are intentional where its capability is deferred:
neutral live camera/geometry placeholders and a Classic link; separate labelled
housing studies in Mock; unavailable RGB/tracking/markers/rates/settings/notes;
no waveform or point/GPU/distance figures. Global navigation and shared shell
spacing follow accepted M0–M2b; center chrome uses the existing icon/typography
system. At compact sizes panes are selected rather than squeezed into three
unreadable columns; setup fields scroll while Notes stays visible. The foundation
never pastes the concept into the UI or claims measured coverage.

## Independent handoff

The existing five-job remote matrix is unchanged. Studio ON jobs upload `ui-m3b/`
and exact-SHA/run-URL `revision.json`, with full CTest and prior UI evidence. The
existing repeat gate adds Scan QML/stress, without multiplying full suites.
Only the feature branch is pushed. One nonblocking exact-SHA Actions check is
reported; unfinished remote jobs remain PENDING and are not polled. Independent
review must verify all five jobs and code before ACCEPT.

Software/native UI evidence does not validate real sensor timing, laser safety,
calibration accuracy, ARM64 locally, or Windows/macOS. The known backend
project/replay isolation gate remains pending. No scope deviation introduces
capture/replay commands, new views/transport, notes persistence or M3c/d/e work.
