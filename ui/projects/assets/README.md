# Original Projects studies

Apache-2.0, like the repository. These original mechanical illustrations are
fictional studies, not vendor CAD, scanned geometry, QC results or hardware evidence.
No reference pixels, third-party images, runtime generation or external image URL
is used. Four accepted Home PNGs and `HomePreview.qml` are reused without changes.

`render_projects.py` reuses the accepted M1 primitive/material/lighting definitions
by executing only the authoring prefix before its foreground render loop. It never
renders or writes a Home asset. The original new geometry consists of a four-bore
block, toroidal intake/flanges, pocketed electronics enclosure, splined annular
housing, three-port manifold and long bearing flange. Two additional variants use
an explicitly illustrative position-derived palette, with no measurement data.

Reproduce offline with Blender 4.3.2 CPU Cycles, 256 samples, fixed seed 0:

```bash
blender --background --threads 8 --python ui/projects/assets/render_projects.py
python3 ui/projects/assets/compress_projects.py
```

Packaging executes the accepted Home RGBA validation/crop routine **in the Projects
script's file scope**, so only these eight foregrounds are processed. It rejects
missing/trivial alpha and edge-clipped silhouettes, crops unused transparent space,
adds an eight-pixel transparent inset and compresses losslessly. It is idempotent.
The initial bracket framing was rejected by this check and corrected in the camera
before packaging. `film_transparent` and hidden floor ensure no background/floor
pixels. Materials and lighting remain part of the original authoring setup.

Every subject is aspect-fit inside Home's QML-owned continuous graphite preview
surface, padding and static shadow. The shared decode bound is 520×355; native PNG
bounds and byte totals are measured in `docs/ui/validation-m2a.md`. Qt can also hold
bounded renderer/cache copies. Gallery slots remain stable; no per-refresh image
encoding, raw-data loading, animation timer or worker is introduced. Blender and
Pillow are absent from build/runtime/CI requirements. Automated Qt decoding tests
check genuine alpha, antialiasing, export bounds and the eight-pixel safe border.
