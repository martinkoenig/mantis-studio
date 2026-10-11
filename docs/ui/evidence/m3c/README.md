# UI-M3c camera, geometry and capture evidence

These are unchanged actual Qt PNG readbacks. [Checksums and original paths](screenshots.json)
identify the selected captures. The authenticated wire fixture feeds valid mapped
Published cloud and FrameSet packets through the existing public client, bridge,
preview worker and renderer; no physical scanner is involved.

| Capture | Evidence |
| --- | --- |
| [Active wire capture, 1920×1080](active-real-wire-1920x1080.png) | Actual checkerboard grayscale pair and 512 rendered points; daemon-confirmed capture |
| [Recording disconnected, 1920×1080](stale-real-wire-1920x1080.png) | Retained real pixels/cloud labelled last-known; Unknown capture, guarded controls and structured diagnostics |
| [Classic with same objects, 1920×1080](classic-real-1920x1080.png) | Canonical grayscale/geometry restored to compatibility hosts, preserving orbit and pixel buffers |
| [Calibration capture-stage pair, 1920×1080](calibration-real-1920x1080.png) | Same canonical images in existing capture stage; injected Bridge evidence with offline/no-token controller, not calibration or hardware validation |
| [Compact stale Cameras, 1080×720](compact-stale-cameras-1080x720.png) | Persistent camera hosts, aspect preservation, explicit stale labels and independent scroll |
| [Compact idle Setup, 1280×720](compact-idle-setup-1280x720.png) | Device selection and capture authority visible above deferred setup; Notes stays outside settings scroll |
| [Hybrid Demo, 3440×1440](hybrid-demo-3440x1440.png) | Illustrative composition isolated from live buffers; elastic center and right-edge rail |
| [Native Wayland restored Live](native-restored-live.png) | Qt 6.9 compositor/DPR 1.25 readback with actual shared surfaces |

Nine viewport sizes, seven DPR-1 source/state cases, compact panes and DPR 1.5/2
captures remain under `build/{qt64,debug,qt69-sanitizers}/ui-m3b/{matrix,dpi-*}/`.
New identity/lifetime/pixel evidence is under `ui-m3c/surfaces/`, authenticated
counts/failures under `ui-m3c/wire/`, and host Wayland captures under
`build/debug/ui-m3c/native/`. CI retains the old paths and additionally uploads
`ui-m3c/` with an exact-SHA/run receipt in each Studio ON job.

Manual inspection covered all nine viewport sizes through representative active,
waiting, idle, stale and Demo frames, DPR 1/1.5/2, native restored and Classic.
Comparison with [02-scan.webp](../../reference/02-scan.webp) preserves the accepted
hierarchy and palette. Actual recorded fixture geometry replaces illustrative
coverage; RGB, tracking, texture/confidence, hardware rates/settings, sequence
mutations and notes persistence remain explicitly unsupported/deferred.

See [implementation, commands, scope and limitations](../../validation-m3c.md).
The machine-readable final local results are in [local-verification.json](local-verification.json).
Remote CI is an independent acceptance gate; this evidence does not claim ACCEPT.
