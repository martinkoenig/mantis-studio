# Fluid Home and integrated foregrounds — corrective evidence

Reviewed rejected baseline: `754e0da95dc824dfdb6f6b428b236ddd2f62526c`.
Earlier working visual baseline: `1eaae357783f9a348c012ac12f11d9bf8f1199ee`.
Read the complete corrective work package before editing, alongside Architecture
v1, ADR-002/033, the binding reliability standard and approved Home reference.
The previous centered cap passed CI but was **not visually accepted**.

## Review and design decision

Actual baseline CLI renders at 1536/3440/3840 established two defects: the rail
moved 900px inward at 3440 while the dashboard remained a 1440px island; opaque
project JPEGs had baked teal floors, aspect-fit color bars and no subject inset.
The upfront plan retained laptop composition, moved the rail to the workspace
edge, introduced two primary lanes at wide widths, and separated object art from
QML-owned surfaces. No global scaling or new resolution/minimum constraint.

[Reference / rejected / after, 1536](reference-rejected-after-1536.png),
[rejected / after, 3440](rejected-after-3440.png),
[rejected / after, 3840](rejected-after-3840.png) preserve full original pixels,
with labels outside the frames. These comparisons and all nine separate
`after-mock-WIDTH.png` frames were inspected, not merely tiny contact sheets.
Baseline images are actual CLI application windows; after frames use the actual
registered shell/Home QML and accepted presentation model in the test fixture.
The reference remains documentation only, never an application asset.

## Measured layout contract

All values below are Qt logical pixels. The unchanged shell uses 152px navigation
below 1250 window pixels, otherwise 176px; Home has 12px shell insets. `availableWidth`
is the usable ScrollView workspace, not the complete window or device-pixel image.
The Basic vertical scrollbar overlays its existing edge; rail controls retain
in-card padding. Rail right gap is measured against ScrollView available width,
including its padding/scrollbar policy, with a 1px rounding tolerance.

| Window | Workspace | Primary | Hero / Activity reading width | Project / action width | Rail width / right gap |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1080×720 | 904 | 904 | 904 | 217 | 904 / 0, stacked |
| 1280×720 | 1080 | 1080 | 1080 | 261 | 1080 / 0, stacked |
| 1366×768 | 1166 | 802 | 802 | 191.5 | 348 / 0 |
| 1440×900 | 1240 | 876 | 876 | 210 | 348 / 0 |
| 1536×1024 | 1336 | 972 | 972 | 234 | 348 / 0 |
| 1920×1080 | 1720 | 1356 | 1356 | 330 | 348 / 0 |
| 2560×1440 | 2360 | 1996 | 986 | 237.5 | 348 / 0 |
| 3440×1440 | 3240 | 2876 | 1426 | 347.5 | 348 / 0 |
| 3840×2160 | 3640 | 3276 | 1626 | 397.5 | 348 / 0 |

The old wide primary was fixed at 1076px, cards 260px and far-right gaps
460 / 900 / 1100px at 2560 / 3440 / 3840. The new primary grows at every desktop
step, with the rail at the usable right edge and a constant 16px outer gutter.
Below 1720 primary pixels, accepted Hero → Projects → Actions → Activity order
remains. Above it, Hero/Activity occupy the left lane and Projects/current-project/
Actions the right lane, separated by 24px. The threshold keeps pre-transition
four-card widths below 430px (less than twice the reference's 234px), while each
new lane can fit four readable cards. It is a composition threshold, not a window
resolution or a cap on both columns. A stable outer Item isolates the responsive
GridLayout from Qt 6.4 layout caches; all delegates remain alive.

Hero height stays 280px normally and grows only up to 330px in wide lanes, with
aspect-fit original scenic art and complete scanner/casting. Project previews
grow 120–180px, retaining the 78px metadata body. Actions remain 150px high.
At 1536 the Hero is still 972×280, projects 234×198, action bottom y803, Activity
y815, first row bottom y905, viewport bottom y982. Only foreground integration
changes the normal composition. Wide screens use substantial horizontal lanes
rather than a small centered island or 700px cards. Short-height lower actions,
Activity rows and stacked rail are reached through the original vertical scroll.
Hybrid's separate sample gallery alone has a local 1680px reading bound; it never
caps the dashboard or moves its rail. No extra content or fake Live data fills space.

## Foreground / surface review

[Full project panels at 1536](project-panels-1536.png) and
[at 3440](project-panels-3440.png) are crops of actual Qt frames. Each was inspected
for complete silhouettes, nonzero top/bottom clearance, matching side backgrounds,
soft contact shadow, rounded header and connected metadata. Scanner, Actions,
Tips and narrow/wide Hybrid frames were also inspected. They now use transparent
subjects; Tips uses aspect-fit rather than cropping. The Hero JPEG is byte-unchanged.

[Asset manifest](asset-manifest.json) records dimensions, bytes, hashes, alpha
ranges, bounds, corner samples and transparent/opaque/antialiased pixel counts.
All five PNGs have genuine 0–255 alpha, transparent corners, an 8px export inset,
and thousands of antialiased edge pixels. Qt decodes these same qrc resources in
all Studio tests. [Reproducible authoring and pixel budget](../../../../ui/home/assets/README.md):
Blender CPU Cycles, seed 0, no floor/world pixels in foregrounds; lossless crop,
8px alpha border, PNG optimization without RGB conversion. Two complete exports
produced [identical](foreground-hashes-before.txt) [hashes](foreground-hashes-after.txt).
No Blender/Pillow dependency enters application, build or CI.

Packaged Hero + PNGs: **765,878 bytes** versus 221,020 bytes previously (+544,858).
Native RGBA8 pixels: **5,609,480 bytes / 5.35 MiB**, versus 7,731,200 / 7.37 MiB.
Projects share a fixed 520×355 decode ceiling, with smaller optional uses; Qt may
hold additional cache/texture variants. No RSS/GPU-memory or physical-display
performance promise follows from this bounded pixel inventory. Eight fixed,
low-opacity QML ellipses provide the contact shadow, without timers/animations.

## Automated and interactive evidence

Both [rejected-layout](rejected-layoutcheck.log) and
[opaque/no-padding preview](rejected-previewcheck.log) contrast checks fail using
the reviewed baseline's actual QML, then pass after restoring the corrected files.
The obsolete cap assertions were replaced by desired right-edge/fluidity checks;
no accepted functionality, fault, authority or delegate regression was removed.

The complete Studio suite now has **53 tests**, including:

- `studio-home-qml`: all nine sizes in mock, confirmed-live and hybrid; actual
  right edge, primary growth, balanced useful lanes, bounded cards/gutters,
  nonoverlap, control containment, zero horizontal scroll; Qt-decoded genuine
  alpha, full-width rendered backdrop and padded aspect-fit geometry; keyboard/
  mouse guide actions and example focus; persistent project/action QPointers.
- `studio-home-states`: all nine sizes in never-confirmed, confirmed empty,
  unconfirmed empty and retained stale Live. Real counts/history/metrics/date/size
  stay unavailable where appropriate, with original failure labels and literal
  long strings. The standalone Home artifacts card remains absent.
- `studio-home-resize`: 270 progressive resize steps (1080–3808 and back in
  62px steps) across all three modes, plus maximize/restore geometry/focus,
  lower Activity and stacked-rail keyboard reachability. The main test separately
  repeats exact breakpoint crossings in both directions: 1248/1252 shell,
  1298/1302 rail, 1322/1326 card grid, 2282/2286 wide lanes. Original 90 immediate
  source/model/route/resize transitions remain.
- `studio-home-fractional-dpi` and `studio-home-hidpi`: DPR 1.5 and 2 at 1536 and
  2560 in all modes, exact crossings and scrolled 3440 Hybrid; logical geometry
  and physical PNG dimensions checked separately. 2560×1440 captures are
  3840×2160 and 5120×2880, respectively.
- The unchanged public-wire fixture still proves async delivery ownership,
  rejection, artifact failure, real transport loss/reconnection and navigation
  without mutable Home commands. All M0/calibration/acquisition/ABI/acceptance
  tests remain registered, with no suppression or weakened deadline.

CI's existing uploads retain **119 M1 PNGs**, all 38 M0 PNGs, full-suite output
and three additional Home repetitions on both x86_64 and native ARM64. Paths are
`build/ci/ui-m1/{screenshots,states,resize,hidpi,fractional-dpi,wire}/` and
`build/ci/ui-m1/full-ctest.log`. All required jobs must pass on the final pushed SHA;
that SHA/run and both architecture artifact links are supplied in the handoff.

## Local verification results

All complete runs used the final production layout and assets. Subsequent changes
strengthened test assertions only (normal Hero height, aspect-fit mode and stacked
rail clearance), then reran the six focused Home tests in both Qt environments.
No test deadline, accepted fault regression or sanitizer setting was relaxed.

| Configuration | Result | Seconds | Complete output |
| --- | --- | ---: | --- |
| Qt 6.4.2 Studio | 53/53 | 224.96 | [log](qt64-complete.txt) |
| Qt-free headless | 41/41 | 124.38 | [log](headless-complete.txt) |
| Headless ASan/UBSan/LSan | 41/41 | 282.22 | [log](headless-sanitizers-complete.txt) |
| Qt 6.9.2 Studio ASan/UBSan/LSan | 53/53 | 407.02 | [log](qt69-sanitizers-complete.txt) |
| Final Qt 6.4 six Home checks | 6/6 | 56.89 | [log](qt64-strengthened-focused.txt) |
| Final Qt 6.9 sanitizer six Home checks | 6/6 | 72.03 | [log](qt69-strengthened-focused.txt) |
| Qt 6.4 Home + public wire, three each | 6/6 | 74.06 | [log](qt64-final-repeat.txt) |

Additional pre-strengthening focused [Qt 6.4](qt64-final-focused.txt) and
[Qt 6.9](qt69-final-focused.txt) outputs remain retained. Sanitizer runs enabled
leak detection and halt-on-UB, without suppressions. The existing documented host
OpenCV override disables its OpenCL dependency probe; CI uses its packaged OpenCV.
DPR 1.5 and 2 full-size frames were also visually inspected, with physical sizes
checked separately from logical geometry. Both contrast cases retained above fail
for the rejected implementation's specific defects.

## Environment limits and failed native attempt

Required geometry/rendering uses Qt 6.4.2 offscreen/software, plus additional
Qt 6.9.2 Studio ASan/UBSan/LSan. Native Wayland was attempted, but GNOME's
[GetActive result](native-session-lock.txt) confirms the desktop was locked.
The window became unexposed, leaving an unpolished sample gallery and unavailable
capture. The [final guarded attempt](native-final.log) explicitly rejects that
condition; [initial diagnostics](native-diagnostic.log) are retained. Native
maximized/restore and physical-display/GPU certification are **not** claimed.
The registered offscreen maximize/restore and fractional-DPI tests passed.

The extra native software sanitizer attempt also reported GTK/fontconfig/Qt
allocations at shutdown, alongside unavailable rendering; the
[complete compressed failed log](native-software-sanitizer-failure.log.gz) is
retained, not suppressed or reported as a pass. Required headless sanitizers and
additional offscreen Qt 6.9 Studio sanitizers remain separate passing environments.
The existing Qt 6.4 software-renderer LSan limitation and pre-UI-M3 PointCloud
retry/backoff issue retain their earlier scope. No backend/model/client/authority,
minimum-window, other-workspace or calibration/acquisition contract changed.
User visual acceptance and independent review remain authoritative; green CI alone
never establishes UI-M1 design acceptance.
