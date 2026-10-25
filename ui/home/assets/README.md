# Original Home illustrations

The existing procedural models in `render_home.py` depict an unbranded scanner,
deep flanged housing, swept impeller, gusseted bracket and ribbed cover. They were
modelled locally for UI-M1; no vendor CAD, stock image, reference pixels or external
asset service is used. Apache-2.0, like the repository. All are decorative studies,
never physical hardware, measured geometry, readiness or accuracy evidence.

Reproduce foregrounds with Blender 4.3.2 CPU Cycles, 256 samples, fixed seed 0:

```bash
blender --background --threads 8 --python ui/home/assets/render_home.py -- --subjects-only
python3 ui/home/assets/compress_home.py
```

Without `--subjects-only`, the original scenic hero is also reproduced. The
accepted `hero.jpg` is unchanged by this correction. Foreground passes use
`film_transparent`, RGBA PNG and hide the studio floor; the world still supplies
lighting, not opaque pixels. Packaging rejects trivial alpha or a silhouette
reaching an export boundary. It losslessly crops unused transparent camera space,
adds an eight-pixel transparent border and optimizes PNG compression. This step is
idempotent and never converts foregrounds to RGB. Only the scenic Hero uses JPEG.
Blender/Pillow remain offline authoring tools, absent from build/runtime and CI.

| Packaged asset | Dimensions | Bytes |
| --- | --- | ---: |
| Hero JPEG | 1600×480 | 61,884 |
| Housing RGBA PNG | 426×345 | 180,342 |
| Rotor RGBA PNG | 388×328 | 171,662 |
| Bracket RGBA PNG | 412×355 | 139,718 |
| Cover RGBA PNG | 520×279 | 147,763 |
| Scanner RGBA PNG | 196×351 | 64,509 |

See the measured manifest in corrective validation for authoritative byte totals
if encoder versions differ. The packaged images total approximately 748 KiB;
native pixels occupy 5,609,480 bytes / 5.35 MiB at RGBA8. Qt may additionally hold
bounded resized/cache variants and renderer textures; this is not a process RSS
promise. Project images share a 520×355 decode ceiling, sufficient for their
120–180px preview regions at DPR 2; small scanner/action/tip uses retain smaller
source-size bounds. Identical URL/size combinations reuse Qt's image cache.

`HomePreview.qml` owns a continuous neutral graphite gradient, 12–22 logical-pixel
safe padding, centered aspect-fit foreground, eight static low-opacity shadow
ellipses and subtle footer divider. No animation, per-frame geometry renderer,
worker, timer, external URL or network image is added. Scanner, Actions and Tips
use transparent PNGs directly against their card-owned backgrounds; Tips uses
aspect-fit, preserving complete geometry. Hybrid uses the same project component.

`studio-home-qml` and DPI tests decode all five packaged PNGs through Qt, verify
nontrivial transparent/opaque/antialiased pixels and complete inset silhouettes,
and check actual preview safe areas and continuous rendered background bands.
The application and tests use the same CMake resource inventory. Studio OFF stays
Qt-free and needs none of these assets or authoring tools.
