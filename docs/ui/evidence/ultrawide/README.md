# Historical Home maximized validation — rejected composition

**Superseded:** the centered whole-dashboard cap delivered at `754e0da` was
rejected in independent visual review. CI success is not design acceptance. See
[the corrective package](../corrective/README.md) for the right-edge rail, fluid
primary composition and transparent previews. The measurements below retain only
the earlier implementation’s historical scope.

Baseline `1eaae357783f9a348c012ac12f11d9bf8f1199ee`, Qt 6.4.2 offscreen/software.
[Before 3440×1440](before-mock-3440.png), [after 3440×1440](after-mock-3440.png),
[nine-size overview](mock-matrix.jpg). The overview scales screenshots for review
only; the application has no scaling/zoom change. Before is an actual CLI app
capture; after and the matrix are actual rendered Home test-fixture screenshots.

The original workspace filled its available width; the bounded rail left all
extra space to the main column. At 3440px, the original main column measured
approximately 2876px and its four cards 710px each. The corrected main column is
1076px, with 260px project/action cards, a 348px rail and a 16px gutter. Hero,
project and action heights remain 280 / 198 / 150 logical pixels. The accepted
1536px screenshot remains byte-identical to the preceding fidelity evidence.

Measured in every mode (margins exclude the unchanged navigation and shell inset):

| Window, logical pixels | Content width | Left / right margins | Main / rail width | Composition |
| --- | ---: | ---: | ---: | --- |
| 1080×720 | 904 | 0 / 0 | 904 / 904 | Stacked, vertical scroll |
| 1280×720 | 1080 | 0 / 0 | 1080 / 1080 | Stacked, vertical scroll |
| 1366×768 | 1166 | 0 / 0 | 802 / 348 | Two columns, vertical scroll |
| 1440×900 | 1240 | 0 / 0 | 876 / 348 | Two columns |
| 1536×1024 | 1336 | 0 / 0 | 972 / 348 | Approved primary proportions retained |
| 1920×1080 | 1440 | 140 / 140 | 1076 / 348 | Capped, centered |
| 2560×1440 | 1440 | 460 / 460 | 1076 / 348 | Capped, centered |
| 3440×1440 | 1440 | 900 / 900 | 1076 / 348 | Capped, centered |
| 3840×2160 | 1440 | 1100 / 1100 | 1076 / 348 | Capped, centered |

All nine sizes were rendered in mock, confirmed-live fixture and hybrid modes.
The existing real-public-client fixture still covers rejection, data-access
failure, loss and reconnection; no Home action issues runtime work. At short
heights, lower controls remain reachable through vertical scroll. Hybrid examples
stay separate and their focus/scroll still works inside the centered container.
No horizontal overflow, card overlap or inaccessible guide controls was observed.

Resize regression crosses 1298/1302 (rail stack), 1322/1326 (two/four cards) and
1638/1642 (content ceiling), then 3840 and back, in all three modes. QPointers
verify that all eight project cards and four action cards survive mode/resize
changes; the Learn control retains keyboard focus. Guide open/close works with
mouse and tab/backtab/Space at every size. The existing 90 immediate model/mode/
route/resize stress remains. A stable outer Item carries scroll height while its
ColumnLayout centers by anchors, avoiding movement of the layout inside Flickable.

HiDPI: `studio-home-hidpi` uses Qt's test-only `QT_SCALE_FACTOR=2` at 1536×1024 and
2560×1440 in every mode, repeats all breakpoint crossings, and checks the 3440×1440
hybrid showcase. Bounds remain logical; captured PNGs are 3072×2048, 5120×2880 and
6880×2880 device pixels. This is Qt software-rendered HiDPI evidence, not a new
physical-display or GPU-driver certification claim.

Reproduce with the same supported Studio build:

```bash
ctest --test-dir build/qt64 -R '^studio-home-(qml|wire|hidpi)$' --output-on-failure
```

CTest supplies the offscreen/software environment; the HiDPI test adds its scale
factor. Outputs: `build/qt64/ui-m1/screenshots/responsive-{mock,live,hybrid}-WIDTH.png`
and `build/qt64/ui-m1/hidpi/`, plus the existing 38 images. CI's existing artifact
upload uses `build/ci/ui-m1/` and retains all 73 M1 PNGs on x86_64 and native ARM64.

Baseline CLI reproduction (on the reviewed baseline):

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/qt64/bin/mantis-studio --ui-mode=mock --workspace=home \
  --window-size=3440x1440 --quit-after=1000 --screenshot=before-mock-3440.png
```

Complete [Qt 6.4 full suite](qt64-full.txt) and
[final Home/public-wire/HiDPI checks](qt64-final-regressions.txt) and
[Qt 6.9 Studio sanitizer checks](qt69-final-regressions.txt) are retained.
No backend/model, resource/asset, source authority or artifact-workflow change.
