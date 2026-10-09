# Projects V2 review evidence

Supersedes [the reviewed baseline](../projects/README.md), starting at
`5b68b580bb8f7d72df60c80eeaf970297dda6019`, on `feature/ui-m2a-projects` only.
The [region audit and input investigation](../../validation-m2a.md) explain the
changes, remaining differences and safety contracts. No Home/backend/model/API,
minimum-window-size or runtime authority changes are included.

## Actual Qt images

Images come from the compiled application QML and public-snapshot test fixtures,
not painted mockups. Baseline frames use the reviewed UI tree. Final software
frames use Qt 6.4.2 on Ubuntu 24.04. Native frames use Qt 6.9.2, Wayland and the
default renderer at DPR 1.25; the actual maximized window is 2048×1210 logical
pixels (2560×1513 screenshot pixels), rather than the user's earlier 2048×1280
capture. No screenshots are stretched or retouched. Composites paste complete
original images beside each other and add a 32px caption band.

- [Approved reference / reviewed baseline / final Versions](reference-baseline-final.png)
  at identical 1536×1024 dimensions. The reference retains its separate approval
  and fictional services; disabled operations remain honest in the implementation.
- [Tab-only switch: Scans / empty Measurements](tab-switch.png): same list offset,
  selection, rails, toolbar, panel placement and footer. Versions' four rows are
  shown in [the final grid](mock-docked-1536x1024.png).
- [List top / last projects](list-scroll.png) and [grid top / final row](grid-scroll.png):
  only the browser offset changes; all docks remain stationary.
- [128-row table top / keyboard-revealed last row](table-scroll.png): the table
  owns scrolling and focus reveal; the gallery and inspector remain stationary.
- [Live](live-1536x1024.png), [Hybrid](hybrid-1536x1024.png),
  [compact filters](compact-filters-1080x720.png) and
  [compact details](compact-inspector-1080x720.png).
- Native maximize [Mock](native-maximized-mock-2048x1210.png),
  [Live](native-maximized-live-2048x1210.png),
  [Hybrid](native-maximized-hybrid-2048x1210.png), with all three normal/maximized/
  restored states verified in [the native log](native-final.log).
- Mock captures cover all nine required sizes. All sources, stale/empty/rejected
  cases, DPR 1/1.5/2 and resize transitions are retained in the CI artifacts.
- [Home pixel comparison](home-pixel-comparison.txt): final Home equals the
  reviewed baseline pixel for pixel at 1536×1024.

### Measured normal grid geometry (logical pixels)

| Window | Columns | Card width | Desktop inspector |
|---|---:|---:|---:|
| 1080×720 | 4 | 214 | compact Details |
| 1280×720 | 5 | 204 | compact Details |
| 1366×768 | 3 | 199.33 | 320 |
| 1440×900 | 3 | 219 | 334.8 |
| 1536×1024 | 4 | 178.75 | 360.72 |
| 1920×1080 | 5 | 205.6 | 420 |
| 2560×1440 | 6 | 276 | 420 |
| 3440×1440 | 6 | 420 | 420 |
| 3840×2160 | 6 | 420 | 420 |

The inspector is pinned within <=1px of the usable right edge. The bottom pane
ends at the workspace bottom within <=1px; the shell's existing 12px footer inset
remains. The lower pane is bounded to 188–300px, independent of row count. Tall
windows retain intentional breathing room in the browser and inspector rather
than stretching a hollow table. Compact Details/Filters stack in the browser;
all content is reachable through its clipped viewport and scoped focus reveal.

## Physical touchpad evidence

[Summary](native-input-summary.json) and retained wheel-only traces:
[baseline](native-before-physical.log),
[first central coast](native-after-physical-central.log),
[speed-sensitive revision](native-speed-final.log),
[immediate-pixel revision](native-immediate-final.log).
Only this diagnostic test window records wheel events; no product input logging
or timer drives scrolling. Raw boundary events include bubbled duplicates.

The laptop's PIXA touchpad supplies Begin/Update/End with **no Momentum phase**.
Qt's native release flick supplements that missing phase, using bounded measured
velocity; platform Momentum bypasses the supplement. Initial default-speed
coasting felt constant to the user; [the failing speed regression](speed-before.log)
measured 500/2500px/s instead of the distinct 500/4000px/s now tested. Immediate
native pixels also remove the drag-acquisition startup delay. The final physical
trace has 919 raw observations, all over the gallery, and first-release speeds
approximately 105–5448px/s. The user confirmed: **“Yes, it starts promptly and
coasts naturally.”**

Later frame-polish and queued-release cancellation guards preserve this input
algorithm. The final native maximize/restore check was rerun after those guards.
Mouse wheel, platform Momentum, direction, pause, cancellation, boundary and
keyboard behavior are separately regression-tested. Injected tests do not certify
physical touchpad inertia on other compositors, Qt 6.4 or ARM64 devices; neither
touchscreen hardware nor native screen-reader operation is claimed.

## Complete suites and exact revision

All complete local suites passed on the final implementation:

| Configuration | Result | Log |
|---|---|---|
| Qt 6.4.2 Studio ON | 64/64 | [full output](qt64-full-ctest.log), [receipt](qt64-verified.log) |
| Studio OFF | 42/42 | [receipt](headless-verified.log) |
| Headless ASan/UBSan/LSan | 42/42 | [receipt](headless-sanitizers-verified.log) |
| Qt 6.9.2 Studio ASan/UBSan/LSan | 64/64 | [full output](qt69-sanitizers-full-ctest.log), [receipt](qt69-sanitizers-verified.log) |
| Five UI suites × three repetitions | 15/15 invocations | [full output](qt64-repeat-ctest.log), [receipt](qt64-repeat-verified.log) |

Studio ON registers 64 tests;
OFF/headless sanitizers register 42. Qt 6.4 additionally repeats Home, Projects,
resize, scroll and M0 route suites three times (15 invocations). Qt 6.9 Studio
uses ASan/UBSan/LSan without suppressions; the accepted loader/OpenCL environment
workaround is documented in the validation record. No tests, warnings, assertions
or deadlines are weakened.

[source-sha256.json](source-sha256.json) identifies the implementation/test sources
used for local final evidence. GitHub Actions generates `ui-m2a/revision.json` in
both Studio artifacts with the exact review SHA, run URL and architecture. The
review handoff supplies the final SHA/run only after all five required jobs pass.
No branch is merged. Visual acceptance remains the user's judgment.
