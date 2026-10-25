# CUT-0B rendered software evidence

Actual unedited Qt 6.9 offscreen/software window captures from the integrated
UI/Core candidate, produced by existing CTest fixtures. The source histories are
main `ec045dd`, Core `24c4d87`, UI `62428b0`; history-preserving merges are recorded
in the [qualification record](../../../architecture/cut-0b-integration-qualification.md).
The final candidate SHA is supplied in the handoff and exact-revision CI receipts.

- [Home](home-mock.png): `studio-home-qml`, 1536×1024, explicit Demo / Mock; illustrative artwork and sample jobs.
- [Scan](scan-synthetic-wire.png): `studio-scan-capture-wire`, 1920×1080; actual StudioBridge/public-wire fixture, synthetic checkerboard previews and 512-point cloud, active synthetic capture.
- [Classic](classic-offline-wire.png): same wire fixture, 1920×1080; disconnected retained geometry/previews, stale evidence, disabled capture controls and local Reset.

Inspection found the expected layouts, source/stale labels and unavailable controls,
without an obvious integration layout regression. Automated geometry, attachment,
route, capture-guard and warning assertions remain authoritative for tested cases.
These images are not measurements, physical camera captures, laser output or
proof of scanner readiness. Main showcase images remain unchanged.

| File | SHA-256 |
| --- | --- |
| classic-offline-wire.png | `a5c64c2b16aa87e17b5496de0779a09494f4526f3e2b0ce5a476e7406f45f57b` |
| home-mock.png | `231538b208b8c63ee1a3b447d75dd06f86d739ea5570923a3e87e949c8256b90` |
| scan-synthetic-wire.png | `62c35e0990f5643dea2554333d284d5ed618c5a21e057a75342c7148f0d391b3` |
