# UI-M2a review evidence

These are actual Qt window captures, never rendered reference pixels or scanner
results. Selected compact, desktop, ultrawide, runtime, hybrid and native captures
accompany the [validation record](../../validation-m2a.md). Complete nine-size
state/DPR matrices and full CTest logs are retained in both Studio ON Actions
artifacts; `ui-m2a/revision.json` ties them to the exact pushed commit and run URL.

`public-capability-audit.log` records the real daemon/public-client experiment that
blocks GUI New/Open. It proves a backend defect, not permission to enable switching.
Isolated temporary projects and virtual scans were removed after the experiment.

The earlier failed `qt69-focus-trace.log` shows focus reveal at content height 876
before the layout grew to 1384. The old fixed 30ms test delay observed the layout
between frames. `qt69-qml-scroll-final.log` retains a later failed attempt with that
delay. The correction observes final layout/focus geometry, schedules reveal after
geometry delivery, and waits for the actual visible rectangle within a one-second
test deadline. No failure is suppressed. Final repeated/full-suite results are
recorded separately. The native `native-final.log` retains an earlier transient
desktop focus failure; three subsequent native runs assert exposure, focus and
maximize/restore across all sources, without weakening the focus assertion.

Illustrations are the repository's original offline Blender foregrounds. See
[asset provenance](../../../../ui/projects/assets/README.md). No evidence image is
loaded by the application.

Selected full captures: [default desktop](mock-default-1536x1024.png),
[compact](mock-1080x720.png), [ultrawide](mock-3440x1440.png),
[live](live-1536x1024.png), [hybrid](hybrid-1536x1024.png),
[compact inspector](compact-inspector-1080x720.png),
[native maximized](native-maximized-mock-2048x1210.png) and
[preserved Home](home-unchanged-1536x1024.png).

Complete local suites: [Qt 6.4](qt64-full-ctest.log),
[Qt 6.9 Studio sanitizers](qt69-full-ctest.log),
[Studio OFF](headless-verified.log) and
[headless sanitizers](headless-sanitizers-verified.log).

ARM64 CI investigation retains the [first](ci-first-arm-job.log) and
[second](ci-second-arm-job.log) failures and the
[native GDB/QML stack](ci-arm-qml-stack.log) identifying the shared icon paint
handler. These are failed runs, not final verification. See the validation record
for the Qt 6.4 captured-context compatibility correction and exact-final CI gate.

Selected matrix images, the default CLI view and full Studio CTest logs were
refreshed after the final Qt 6.4 captured-context and combo-padding corrections.
[Home pixel comparison](home-icon-pixel-comparison.log) reports no changed pixels.
The earlier locked native run remains in [native-locked-final.log](native-locked-final.log).
The [later session query](native-session-final.txt) reports unlocked;
[final default-renderer native checks](native-final-rhi.log) passed all three sources.
All nine native normal/maximized/restored images were refreshed and inspected on
the final application code. The separate [software DPR failure](native-software-dpr-failure.log)
remains documented and no screenshot assertion was weakened.
