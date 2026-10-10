# Scan foundation screenshot inventory

These are unaltered actual Qt 6.4.2 offscreen/software PNG readbacks, not mockups
or reference-image crops. [Checksums, source paths and source semantics](screenshots.json)
identify each selected frame. Live data in the layout matrix comes from injected
production StudioBridge DTOs, not a physical scanner; the CLI unreachable capture
uses the actual application/runtime failure path.

| Capture | Purpose |
| --- | --- |
| [Before, Mock 1536×1024](before-mock-1536x1024.png) | Verified accepted starting HEAD 8a4a150; planned-panel Scan route |
| [After, Mock 1536×1024](mock-1536x1024.png) | Reference-aligned camera/viewport/setup/notes/status/dock hierarchy, labelled illustrations |
| [Unreachable Live 1080×720](unreachable-live-1080x720.png) | Actual CLI startup, unknown runtime/capture, usable compact viewport and Classic entry |
| [Stale Live 1920×1080](stale-1920x1080.png) | Explicit last-known capture, no live frames or fabricated readiness |
| [Job authority correction, stale 1920×1080](job-authority-stale-1920x1080.png) | M3B-F01: actual disconnected Main.qml, Processing Not reported, Timeline explicitly unlinked from runtime-wide jobs |
| [Hybrid Live 3440×1440](hybrid-live-3440x1440.png) | Elastic center, bounded rails, separate Live / Demo source controls |
| [Compact Setup 1080×720](compact-setup-1080x720.png) | Visible pane controls, independently scrolling settings and stationary Notes |

Full generated evidence lives in `build/{qt64,debug,qt69-sanitizers}/ui-m3b/`,
covering all nine viewports and DPR 1/1.5/2, source/freshness states and compact
panes. Each required remote Studio ON job uploads its complete `ui-m3b/` with
exact-SHA/run-URL revision receipt. Native Qt 6.9 Wayland DPR 1.25 normal,
maximized and restored captures live in `build/ui-m3b-verification/native/`.
The matrix's waiting-provider case detaches its injected bridge; the versioned
unreachable CLI frame separately verifies the actual missing-runtime path.

Compared with [the approved reference](../../reference/02-scan.webp), the new page
reproduces its main hierarchy and restrained teal palette. Live camera/geometry,
tracking/RGB/markers, timeline waveform, adjustable hardware settings and notes
storage remain explicitly unavailable pending supported M3c/d/e contracts.
Illustrations are labelled and never imply measured coverage. Compact pages use
pane selection; the shared accepted shell/nav typography remains unchanged.
See [complete validation and limitations](../../validation-m3b.md) and
[local verification receipt](local-verification.json).

M3B-F01 replaces the stale matrix capture and adds the production-QML regression
frame above. Other versioned images retain the original M3b implementation evidence.
The correction's complete local checks are recorded separately in
[the correction receipt](correction-verification.json).
