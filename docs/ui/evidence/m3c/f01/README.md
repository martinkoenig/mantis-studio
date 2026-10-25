# M3C-F01 offline geometry correction evidence

Base: `f5fefe759f1cfb892604e6e3147656058f7ef2ec` on `feature/ui-m3-scan`.

These unaltered actual Qt 6.4 screenshots use the authenticated public-wire fixture,
real StudioBridge and canonical PointCloudView with a mapped 512-point Published
cloud and grayscale pair. They show the disconnected state after actual orbit,
wheel and keyboard Reset. Reset is usable; stale labels and disabled hardware
controls remain. This is software evidence, not physical scanner validation.

- [Offline Classic Acquisition](offline-classic.png)
- [Offline Hybrid-Live Scan](offline-scan.png)

[Checksums, exact local commands/results and wire receipts](local-verification.json)
identify the images and verified source inputs. Full generated captures remain in
`build/<config>/ui-m3c/{surfaces,wire}/`; existing CI uploads these paths. Accepted
M3c images and receipts in the parent directory remain unchanged.

See [correction scope and regression coverage](../../../validation-m3c.md#m3c-f01-disconnected-local-geometry-inspection).
Remote exact-SHA CI is a separate independent-review gate.
