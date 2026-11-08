# Shared calibrated scene (SIM-01C)

This opt-in **synthetic** model extends the existing virtual-rig plugin. Select
`profiles/virtual-rig-scene.json` via `MANTIS_VIRTUAL_RIG_PROFILE`. Profile version
2 requires scene version 1 and an explicit calibration namespace
`org.mantis.virtual-rig.scene-v1`, positive revision and complete scene object.
Version 1 profiles, discovery, image bytes and their analytic reference remain
unchanged. No public ABI, RawCapture schema, Session queue or Recorder policy is
changed. This is not a calibration solver, active binding service or calibration
database. Optional projected-line illumination is deferred; no LaserObservation,
emitter acknowledgement, safety state or physical synchronization is fabricated.

## Coordinates and geometry

Scene `rig-body` names the packet parent frame `<rig-id>/body` and uses canonical right-handed millimeters: X right, Y forward,
Z up. Optical coordinates use X right, Y down, Z forward. Every camera explicitly
supplies row-major homogeneous `T_camera_from_scene`: multiply a scene point to
obtain its optical coordinates. Width/height, `fx/fy/cx/cy` (pixels), camera ID,
lens and five Brown coefficients are required. Each image identifies its camera
index and component ID; the calibration camera ID must match that component.
The scene body is an explicit synthetic reference, not an M4 solved rig origin.
The example left center is (0,0,0), right center (40,0,0). Both optical axes face
scene +Y. There is no implicit right-camera translation.

Pixels sample integer image coordinates (x,y). Rays intersect two finite opaque
rectangles parallel to scene XZ at their explicit Y depths. The closest positive
optical-Z intersection in `[near_mm,far_mm)` wins, regardless of declaration
order. Rectangles and feature patches include their lower edges and exclude upper
edges. Rays parallel to a surface, behind-camera hits, clipped depths and misses
produce zero signal. Features are finite patches on their containing surfaces;
their centers are known 3D points. Both views observe the same surfaces and
motion. There is no image-space shift or camera-dependent texture substitute.

The example has a far surface at Y=640 mm, near occluder at Y=320 mm and a
40-mm baseline, with fx=64 px. Far feature (40,640,0) projects to (36,24) left
and (32,24) right: disparity `fx*baseline/depth=4` px. Near feature (-100,320,0)
projects to (12,24) and (4,24), disparity 8 px. Near geometry occludes far geometry.
General proper rotations/translations are supported; a nonrectified view is tested
using calibrated rays, without assuming constant disparity or equal image rows.

## Bounded optics and time

`pinhole` requires zero Brown coefficients. `pinhole_brown5` uses accepted order
k1,k2,p1,p2,k3 and the usual normalized-coordinate radial/tangential model. To
bound inversion, focal lengths are at least width/height and at most 16 times
those dimensions, principal points lie within the central half sensor, radial
coefficients have magnitude <=0.005 and tangential coefficients <=0.001. The
inverse uses 16 bounded correction steps and checks residual <=1e-12 normalized
units. Wider/noncontractive lens models fail instead of silently substituting
pinhole. All transforms must be finite, proper rigid matrices (orthogonality and
determinant residual <=1e-12); stereo centers must be >=0.1 mm apart. Dimensions,
finite extents, feature containment, distinct surface depths and complete fields
are validated before acquisition. Unknown fields/versions/lenses fail explicitly.

Event exposure time is **row-zero integration start**, in the existing simulation
reference clock. Row y samples scene time
`event.exposure_ns + y*row_time_ns + floor(integration_ns/2)`.
Zero row time is global shutter; positive row time is a rolling-shutter-style
approximation. Integration is 1 ns..100 ms, row time 0..100 us; the complete scan
plus integration must fit the shortest possible camera period (nominal floor minus configured jitter).
Delivery delay, measured from row-zero start, must be at least that complete
scan/integration span; an image cannot be delivered before it is complete. Moving geometry translates in
scene X with a bounded triangle wave, starting at zero, reaching +amplitude at
quarter period and -amplitude at three quarters. Amplitude is 0..1000 mm, period
1 ms..10 s. Integration uses **one midpoint sample**, without blur or photometric
integration claims. Independent camera phase/native clock mapping and host delivery
remain the existing separate controls. Rendering later does not change exposure
or synthesize catch-up events.

Surface and feature signals are 10-bit synthetic DN at 1 ms, gain 1. The model
multiplies by `integration_ns/1e6 * gain`, clips to [0,1023], rounds half upward,
adds bounded read and signal-dependent shot approximations, then clips again.
Gain is 0..16. Read noise is discrete uniform +/-`read_noise_dn` (0..32).
Shot noise is discrete uniform +/-floor(sqrt(clipped signal)*shot_noise), scale
0..4. These are deterministic bounded approximations, **not physical radiometry
or a calibrated Poisson/Gaussian sensor model**. Integer seed/event/camera/pixel
mixing makes repeated scene+seed+event pixels identical independently of generation,
rendering schedule or host delivery. Changed seeds and subsequent noise events
are tested to differ. GREY stores DN>>2; Y10P packs all ten bits per ADR-025,
including exact zero row padding. Version 2 has no diagnostic pixels outside the
scene; changing noise/motion provides event-sensitive lifetime witnesses.

## Immutable provenance and resource bounds

Every image carries the complete canonical scene JSON in
`org.mantis.virtual-rig.scene`, projection version `calibrated-ray-scene-v1`,
synthetic intensity version, camera index, seed, integration and row interval.
The snapshot includes camera identities/dimensions/intrinsics/Brown/extrinsics,
surfaces and optical controls. Calibration headers contain the positive supplied
revision and namespace suffixed with a canonical-snapshot FNV-1a-64 content tag.
The **full snapshot is authoritative**; the tag detects accidental conflicts,
not adversarial collisions or tampering. No reader resolves a tag using current
configuration. Discovery also exposes the snapshot. Store/replay preserves the
original typed reference and snapshot exactly; changing current calibration
cannot reinterpret recorded pixels. Project active-binding overrides remain the
accepted runtime mechanism, with original source provenance preserved by runtime.

The existing 16-KiB profile and two-camera limits remain. Surfaces are a fixed
array of two; no per-pixel allocation, scene sampling list, expanded image,
frame-sized scratch buffer or mutable raster cache is introduced. Rendering is
O(width*height*2); Brown inversion is at most 16 steps per pixel. Each emitted
camera allocates one fresh host image buffer and writes directly to it. Published
buffers are immutable and never reused while retained. Existing bounded source,
pending/in-flight ownership, callbacks, cancellation and error retirement remain
unchanged. Generation restart changes epoch identities, preserving historical
buffers. Metadata serialization and runtime/storage encoding still have costs.

## Independent software qualification

`virtual-rig-scene` loads the actual compiled DSO through normal Registry and
MantisAcquisitionV1. OpenCV supplies independent forward projection and Brown
undistortion; the test uses general matrix inversion and separately specified
ray/rectangle expectations. It does not include renderer headers or call its
projection functions. Exact integer pixels check feature location/disparity sign,
visibility, depth ordering, finite boundaries, misses, exposure/gain/clipping,
nonrectified/distorted rays, rolling rows, packing and padding. Wrong baseline,
rotation, translation, focal length and principal-point controls must disagree
with actual emitted pixels. Forward Rodrigues projection uses 1e-12-pixel
roundoff tolerance; image goldens remain exact. Noise has exact reproducibility
checks and a model-derived +/-39-DN support bound for the selected scale/read
settings. Invalid geometry/calibration/controls fail public open explicitly.

The existing test-only ClockV1 hook controls source events through the unchanged
Registry adapter. The real Session/32-entry LOSSLESS queue/Store path records
three stereo FrameSets, finalizes, reopens and replays twice, comparing canonical
headers and every image byte. Original calibration remains after revision/seed
change. Retained buffers, restart epochs and shutdown accounting are checked.
One bounded 1280x720 stereo Y10P recording/replay case checks selected calibrated rays/features,
padding, exact two pixel allocations and materialized byte extent; it reports
CPU/wall materialization cost separately from Recorder throughput. It imposes no
wall-clock FPS oracle. Existing contract/runtime/daemon/storage, schema/recovery,
ABI and legacy tests remain mandatory alongside full Studio ON/OFF/sanitizers.

This software model qualifies no physical optics, timing, metrology, GPU, HIL,
Windows/macOS, SSD or full-rate Recorder performance. SIM-01B's opt-in 120
FrameSets/s / 276.48 MB/s result remains **NOT QUALIFIED**; its saturation failure
and historical #12 cause are not changed or explained by this work.
