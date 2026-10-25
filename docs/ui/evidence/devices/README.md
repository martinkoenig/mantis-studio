# UI-M2b retained evidence

See [validation-m2b.md](../../validation-m2b.md) for commands, results, source
authority and platform limits. PNGs are complete, unchanged Qt window captures,
except the explicitly derived [nine-viewport contact sheet](viewport-overview.png).
None are product assets. The full state/DPR matrix is reproducible under
`build/qt64/ui-m2b/` and in each exact-SHA Studio ON CI artifact; the CI receipt
`ui-m2b/revision.json` records its literal SHA and Actions run URL.

| Evidence | Files |
| --- | --- |
| Approved equivalent view | [Mock 1536×1024](mock-1536x1024.png), [live](live-1536x1024.png), [actual application default](app-default-1536x1024.png) |
| All nine logical viewports | `mock-WxH.png`; [contact sheet](viewport-overview.png) |
| Compact selection and inspector | [Navigator](mock-navigator-1080.png), [inspector](mock-inspector-1080.png) |
| Truthful lifecycle and source states | [Waiting](waiting-1536x1024.png), [empty](empty-1536x1024.png), [stale](stale-1440x900.png), [hybrid](hybrid-1536x1024.png), [hybrid-stale](hybrid-stale-1536x1024.png), [unknown third party](third-party-1536x1024.png) |
| Persistent local tabs | [Settings](inspector-Settings.png), [Calibration](inspector-Calibration.png), [Diagnostics](inspector-Diagnostics.png), [Info](inspector-Info.png) |
| Fractional/high DPI | [DPR 1.5 hybrid](dpr-1.5-hybrid-1366x768.png), [DPR 2 mock](dpr-2-mock-1536x1024.png) |
| Public authenticated Client/Bridge | `wire-*.png`, [hostile literal descriptors](wire-hostile.png), [request receipt](wire-requests.json), [fixture output](wire-fixture.log) |
| Keyboard virtualization/focus | [Last of 95 traversed rows](navigator-keyboard-last.png) |
| Native Wayland/default renderer, DPR 1.25 | [Normal mock](native-normal-mock.png), [maximized live](native-maximized-live.png), [nine-transition receipt](native-final.log) |
| Complete local verification | `qt64-full.log`, `qt69-debug-full.log`, `qt69-sanitizers-full.log`, `headless-full.log`, `headless-sanitizers-full.log`; complete successful Qt output also retained as `qt64-test-output.log`, `qt69-debug-test-output.log`, `qt69-sanitizers-test-output.log` |
| Repeat regressions | `qt64-transition-repeats.log`, [three anchored-pane stress repeats](qt64-anchored-panes-stress.log) |
| Actual failures retained separately | [Hidden Projects binding loop](qt69-before-layout-guard-failed.log), [Qt 6.4 pane allocation](qt64-before-anchored-panes-failed.log), [rendered-frame reproduction](qt64-frame-barrier-stress.log) |
| Home/Projects pixel regression | [Home unchanged](home-pixel-comparison.txt), [Projects guard unchanged](projects-pixel-comparison.txt) |

`manifest.json` records artifact dimensions, sizes and SHA-256, plus implementation
source hashes. It avoids a self-referential commit hash. Native frames use fixture
runtime data; offline illustrations are labelled. These files certify no real
device, laser, camera pixels, scanner quality or physical timing. Earlier failures
are never counted as passes. No layout/runtime warning or test failure is filtered or skipped. The standard
container `QStandardPaths` environment notice is retained in full output and is
the only notice excluded from the Devices warning assertion.
