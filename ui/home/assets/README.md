# Original Home illustrations

These six assets were modelled locally for UI-M1, from procedural meshes in
`render_home.py`, using Blender 4.3.2 CPU Cycles (256 fixed samples). They depict
an unbranded scanner study, a deep flanged housing, swept impeller, gusseted
mounting bracket and ribbed closed gearbox cover. No vendor CAD, stock image,
reference screenshot pixels, generative API or separate worktree is used.
Apache-2.0, like this repository. They are decorative illustrations, never
measured geometry, actual device photos, readiness or accuracy evidence.

Reproduction, from the repository root:

```bash
blender --background --threads 8 --python ui/home/assets/render_home.py
python3 ui/home/assets/compress_home.py
```

The second step uses Pillow **offline only** to package RGB JPEGs at quality 92.
Qt's existing JPEG decoder works in the Qt 6.4 reference/CI environment; no extra
image plugin, compiled dependency or network asset is needed. Blender/Pillow
are not application or build dependencies. Source scripts are not QML resources.

Hero: 1600×480; scanner: 480×400; four objects: 640×380 each. Combined source
pixels decode to 7,731,200 bytes (7.37 MiB at RGBA8); optional small uses have
explicit source-size bounds. Qt may add renderer/cache overhead; this is a pixel
footprint, not a measured process RSS promise. Total packaged JPEG footprint is
approximately 216 KiB. Shared local URLs and fixed source sizes reuse Qt image
caching. Hidden galleries/optional illustrations have empty sources. No timer,
per-frame geometry generation, runtime rendering worker or polling is added.
