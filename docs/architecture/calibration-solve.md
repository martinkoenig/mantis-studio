# Calibration solve — M4 implementation contract

M4 consumes the pure, canonical in-memory CalibrationDataset from M3. It does
not ingest images, change selection, persist artifacts or activate calibrations.
[ADR-029](../adr/029-calibration-solve-validation-and-stereo-policy.md) freezes
this solve/validation policy. M5 and later own those persistence and workflow seams.

## APIs and dependency boundary

The header-only `mantis-calibration` foundation exposes `calibration_solve.hpp`
through `calibration.hpp`. It contains camera/config/result models, eligibility,
partitions, residual/coverage statistics, rig construction and structural result
validation, with no OpenCV, storage, filesystem, JSON, Qt or Protobuf dependency.

The compiled `mantis-calibration-solver-opencv` target directly links **OpenCV
core and calib3d**. It has no detector, ArUco, RawCapture builder or artifact-store
dependency. OpenCV's own calib3d dependencies are internal transitive dependencies.
Its public header `calibration_solver_opencv.hpp` exposes no OpenCV types:

- `solve_camera_intrinsics(dataset, role, MonoSolveConfig)` returns one camera
  solution; the overload without role returns solutions for all dataset cameras
  in their canonical order, each fitted independently.
- `solve_stereo_rig(dataset, left_solution, right_solution, StereoSolveConfig)`
  returns a stereo solution and final rig geometry.
- `evaluate_heldout_target(observation, fixed_model)` and
  `evaluate_epipolar_pair(frame_key, common_points, stereo_model, left_model,
  right_model)` expose the same fixed-model evidence operations used internally.
- `solver_opencv_version()` records the linked build's OpenCV version.

All operations return Result/Error with component `calibration`. Invalid input
is an error; insufficient partition population and incompatible correspondence
are explicit incompatibilities. Numerical failure and OpenCV exceptions become
structured errors. Poor but finite residuals are evidence, not failure criteria.

## Camera model and optimizer

`CameraModel::pinhole_brown5` is explicit. `PinholeBrown5` contains doubles
`fx, fy, cx, cy, k1, k2, p1, p2, k3`. All must be finite; focal lengths must be
positive. Camera descriptors retain exact role, physical identity, nonzero image
width/height and optical frame id/name. Model identity is never inferred from
an OpenCV vector length.

Both solve configs have schema, solver-policy and split-policy versions **1**.
Mono requires caller-specified positive `heldout_per_camera`. Stereo requires
positive `heldout_pairs`, distinct nonempty exact left/right roles resolving to
dataset cameras, and an explicit rig frame. There are no recommended counts in
the core. Stereo input mono solutions must match the dataset target, camera role,
physical ID, resolution and exact optical frame representation.

Mono uses **cv::calibrateCamera**, explicitly **flags = 0**, which selects the
ordinary five-coefficient model and estimates all nine Brown5 parameters. It
never enables rational, thin-prism, tilted-sensor, zero-tangential or focal/principal
point fixing flags. It supplies a five-coefficient double distortion matrix and
checks the returned representation and explicit model. Stereo uses exactly
**CALIB_FIX_INTRINSIC**. Both fits explicitly supply
**TermCriteria(COUNT | EPS, 100, 1e-12)**. These are optimizer termination settings,
not product quality thresholds.

The OpenCV 4.6 `collectCalibrationData` implementation requires Point3f object
and Point2f image arrays. An executable contract test checks rejection of double
calibration arrays against the actual linked API. Fitting converts Mantis double
coordinates to float explicitly, checking finite range, nonzero underflow,
signed OpenCV counts and geometry after conversion. Source observations remain
unchanged and authoritative. Calibration matrices, distortion coefficients,
pose estimation, projection and residual computation use doubles. The tests
execute on both local OpenCV 4.6.0 and 4.10.0; Tier-1 CI uses Ubuntu OpenCV 4.6.

## Selected population, eligibility and split policy v1

Mono candidates are only records with M3 `selection_rank`, ordered by rank then
ObservationKey. Solver eligibility requires at least **four** correspondences
with non-collinear planar object geometry. Current board points have Z=0.
Non-collinearity uses scale-normalized differences and a tolerance derived from
double precision (`256 * epsilon`). It imposes no image-area, pose-angle, blur,
coverage, residual or other calibration-quality threshold.

Partitions preserve every selected ObservationKey, rank, point count and typed
`SolveSampleDisposition`: training, held_out, ineligible_insufficient_points or
ineligible_degenerate_geometry. Stereo additionally uses
ineligible_missing_detection. Ineligible samples do not consume split positions.

For N eligible samples and caller H, require **0 < H < N**; H is never reduced.
The held-out positions in the eligible ordered population are exactly:

```
floor((k + 1) * (N + 1) / (H + 1)) - 1, k = 0 .. H - 1
```

The implementation decomposes N+1 into quotient/remainder before multiplication.
H is uint32; the remainder product fits uint64. A nonrepresentable N+1 returns
an error. The remaining positions are training, and their union is the eligible
population. Splitting evaluates M3's bounded diversity population, not every
120 FPS detection. There is no RNG, wall clock or platform iteration dependence.

## Mono's three stages

1. **Training fit:** fit only training observations, retain the preliminary
   model, exact source keys, per-view target poses/residual arrays/statistics,
   aggregate residuals, coverage and separately labeled OpenCV solver RMS.
2. **Held-out validation:** leave the preliminary model unchanged. For each
   held-out view call **solvePnPGeneric(SOLVEPNP_IPPE)** with its planar points.
   Evaluate every returned pose through double projectPoints and select the
   lowest recomputed reprojection RMS; ties use first returned solution. Preserve
   the chosen index and all candidate RMS values in returned order. No RANSAC,
   intrinsic refit or residual-based rejection occurs here.
3. **Final fit:** after held-out evidence is frozen, independently refit using
   every eligible selected training + held-out view. Preserve that final model,
   sample keys, poses, residuals and coverage separately. This final model feeds
   stereo and future M5; it never overwrites preliminary or validation evidence.

`CameraCalibrationSolution` retains the exact target, camera, config, partition,
training/final models and three stages. Validation checks model/camera/frame
identity, policy versions, sample uniqueness and split, nonempty stage evidence,
training/held-out disjointness, exact final union, per-view and aggregate counts/
statistics, finite poses, coverage and the IPPE minimum/tie policy. Dataset-aware
validation also checks the exact target and M3-selected source partition.

## Residual and coverage conventions

Reprojection distance for one observed/projected point is Euclidean pixel
separation: `hypot(observed_x - projected_x, observed_y - projected_y)`.
`ResidualSummary` contains point_count, rms_px, mean_px, median_px, p95_px, max_px.

- RMS = sqrt(mean(distance squared)); mean = arithmetic mean.
- Median sorts distances: odd middle; even mean of the two middle values.
- P95 uses nearest rank: sorted[ceil(0.95*N)-1].
- Max is the largest distance.

Empty required evidence, negative distances, NaN and infinity are rejected.
Scaled moments avoid overflow when squaring large finite residuals. Per-view
residual arrays are retained and aggregate statistics are checked against them.

Coverage normalizes all observed points by that camera's image width/height and
records min_x/min_y/max_x/max_y and `(max_x-min_x)*(max_y-min_y)`. Stage coverage
unites its per-view bounds. Coordinates are **not clamped**. Coverage is descriptive
evidence with no acceptance threshold.

## Stereo correspondence, candidates and split

Plain unmarked Checkerboard is supported for mono and mono held-out validation,
but **not stereo or rig solving**. Such calls return Status::incompatible with:

> Plain Checkerboard detector-grid IDs do not establish physical stereo correspondence; stereo and rig solving require ChArUco physical-board IDs

No ID reversal, image-top-left origin, camera-placement assumption, direct/reverse
lowest-error guessing or combinatorial orientation fitting is implemented.
Checkerboard detector_grid semantics remain unchanged.

ChArUco observations preserve physical_board IDs. Candidate FrameSetKeys are the
**union** of frames whose left or right record has a selection rank, bounded by
the combined per-camera budgets. A side need not itself be selected if the other
side seeded the frame. Missing detections remain explicit ineligible candidates.
For both detections, intersect_observations returns sorted common physical IDs
and consistently paired image/object coordinates. At least four non-collinear
common planar points are needed; insufficient/degenerate pairs remain evidence.

Let L/R be left/right ranks or UINT32_MAX for an unselected side. Order candidates
by **(min(L,R), max(L,R), FrameSetKey)**. At least one rank is present. Apply the
same v1 held-out position formula to the eligible ordered pairs. The selected
union, common IDs, reasons, ordering and split are preserved in StereoPartition.

## Stereo's three stages and epipolar evidence

Training and final **cv::stereoCalibrate** use fixed **final mono** intrinsics and
explicit criteria. Copies of K/distortion are extracted afterward and checked
for exact coefficient equality with the supplied models. Only relative R/T is
estimated. The stored convention is:

```
X_right = R_right_from_left * X_left + T_right_from_left
```

Training stores R/T/E/F and training pair evidence. Held-out validation keeps
this training geometry unchanged. Final fitting then uses all eligible stereo
pairs, retaining final R/T/E/F and its own evidence. This held-out stage validates
extrinsics with fixed final mono intrinsics: **it is not an end-to-end independent
calibration dataset**. Acquisition repeatability needs later real-data validation.

For each physical correspondence, undistort both observed image points into
pixel coordinates with their fixed K/distortion and **P=K**. The explicit iterative
undistortion uses the same termination criteria. With homogeneous xL/xR:

```
lR = F*xL; lL = transpose(F)*xR
n = abs(transpose(xR)*F*xL)
dR = n / hypot(lR.a, lR.b)
dL = n / hypot(lL.a, lL.b)
symmetric_epipolar_px = sqrt((dL*dL + dR*dR)/2)
```

F is scale-normalized for numerical line checks; distances are scale invariant.
Numerically zero line lengths are structured failures, using double epsilon and
the homogeneous input norm. Residuals use the same RMS/mean/median/p95/max
conventions, per pair keyed by FrameSetKey plus its exact common IDs. OpenCV
stereo RMS is separately labeled. Pair count and common-point counts are derived
from the evidence. Nothing converts these pixel metrics into scanner accuracy.

## Final calibrated rig

Given final R/T, the right camera center in left coordinates is `C=-transpose(R)*T`.
The left center is zero. Rig origin is `C/2`; baseline_mm is `norm(C)`.

```
x = normalize(C)
f_left = (0,0,1); f_right = transpose(R)*(0,0,1)
b = normalize(f_left + f_right)
y = normalize(b - dot(b,x)*x)
z = normalize(cross(x,y))
y = normalize(cross(z,x))
```

This is +X left toward right, +Y calibrated forward orthogonalized to +X, +Z
cross product; right-handed millimeters. Relative rotation angle is
`acos(clamp((trace(R)-1)/2,-1,+1))` in radians, not a claimed toe-in angle.

RigGeometry stores baseline, relative_rotation_angle_rad and existing spatial
row-major **T_target_from_source** transforms:

- T_right_from_left uses the exact optical frames and final R/T.
- T_rig_from_left uses x/y/z as rows and translation `-basis*(C/2)`.
- T_rig_from_right has rotation `basis*transpose(R)` and translation `basis*(C/2)`.

Result validation checks frame IDs **and names**, finite proper rotation/basis,
positive baseline, derived matrices and
`compose(T_rig_from_right,T_right_from_left) ≈ T_rig_from_left`.
Normalization rejects norms at or below `256*double_epsilon*max(1,vector_scale)`;
translation uses millimeters, with a 1 mm numerical reference. Rotation and
composition checks use `16*256*double_epsilon` scaled by matrix/translation
magnitude. These detect numerical degeneracy, not CAD deviations or product
quality. Zero baseline, opposing optical forwards, parallel projected forward,
nonfinite geometry and improper rotation fail explicitly.

## Executable synthetic evidence

Tests generate observations mathematically with projectPoints; they do not run
detection or ingestion. Source sequences overlap in captures A and B and retain
full FrameSetKey identity. Point geometry uses nominal 40 mm squares and measured
**41.3 mm X / 40.6 mm Y** pitch on an 8×6-square target.

Known 1280×960 models (fx,fy,cx,cy,k1,k2,p1,p2,k3):

- LEFT: `(1000,1015,638,482,-0.06,0.008,0.0007,-0.0004,-0.0006)`.
- RIGHT: `(1020,1005,643,477,-0.05,0.006,-0.0005,0.0008,-0.0004)`.
- Right-from-left Rodrigues vector `(0.008,-0.025,0.005)` rad, translation
  `(-120,1.5,0.8)` mm; baseline `120.0120410626` mm.

Checkerboard uses 24 selected views with H=3: held-out pose indices 5,11,17
(keys A/105, A/111, B/105). ChArUco includes partial and differing ID subsets,
selected-only-left/right/both, missing detections, insufficient common points
and collinear selected points. With H=3, mono LEFT holds A/103,A/109,B/101;
RIGHT holds A/109,B/102,B/107; stereo holds A/110,B/101,B/106. Full expected
candidate order is asserted in the fixture on x86_64 and ARM64.

Synthetic tolerances are 0.05 px for fx/fy/cx/cy; k1 0.001, k2 0.003, k3 0.005,
p1/p2 0.0001; stereo R entries 1e-4 and T/baseline 0.05 mm; rotation angle
1e-4 rad; transform composition 1e-9. They are **test-fixture tolerances**, not
production acceptance rules. Exact ground-truth epipolar distances are below
1e-8 px; an analytic distortion-free parallel case yields exactly 2 px for a
2 px vertical perturbation. Deterministic 3 px noise remains a valid finite fit
with worse metrics. Changing only mono held-out pixels leaves the preliminary
model exactly unchanged. Repeated solves preserve source keys, dispositions,
partitions and pose candidate choice; cross-architecture doubles are compared
with tolerances, not bitwise.

M5+ remains deliberately unimplemented: artifact codecs/persistence, project
schema migration, activation/binding, capture references, services/jobs/protocol,
CLI/SDK APIs, Studio workspace and independent real-data product acceptance.
