# ADR-029: Calibration solve, validation and stereo policy

Status: Accepted for v0.3 M4

## Context

M3 supplies a bounded, diverse selected population and preserves all detection
and selection evidence. M4 must evaluate a preliminary solve independently of
its held-out observations, then produce final geometry without changing M1–M3
or adding persistence. Plain Checkerboard grid IDs cannot establish physical
stereo correspondence across arbitrary target orientation.

## Decision

1. The explicit baseline camera model is **pinhole-brown5**: fx, fy, cx, cy,
   k1, k2, p1, p2, k3. Each configured camera is solved independently.
2. Mono candidates are M3-selected observations, ordered by selection rank and
   ObservationKey. Eligibility requires at least four correspondences and
   non-collinear planar object geometry. Ineligible samples retain typed reasons.
3. Split policy v1 requires caller-selected `0 < H < N` eligible samples and
   chooses held-out positions `floor((k+1)*(N+1)/(H+1))-1`, for `k=0..H-1`.
   This evaluation split follows M3 diversity selection, never all detections.
4. Preliminary mono fitting uses training samples only. Held-out validation
   keeps that model fixed and estimates only a target pose. Final mono refitting
   uses all eligible selected samples after preliminary/held-out evidence has
   been recorded. All three stages remain separately inspectable.
5. Stereo uses the **final** mono models with `CALIB_FIX_INTRINSIC`; intrinsics
   are verified unchanged. Candidates are the union of FrameSets selected by
   either configured left or right role. Ordering is
   `(min(left_rank,right_rank), max(left_rank,right_rank), FrameSetKey)`, using
   UINT32_MAX for an unselected side. Ineligible pairs retain typed reasons.
6. ChArUco physical-board IDs and their intersection are authoritative stereo
   correspondence. Plain unmarked Checkerboard is **mono-only in M4**. Stereo
   and rig requests return `Status::incompatible`; no reversal, image-origin,
   camera-placement or lowest-error orientation inference is permitted.
7. Stereo uses the same split formula and keeps training, held-out epipolar
   validation and final refit separate. Held-out extrinsic validation uses fixed
   final mono intrinsics; it is not an end-to-end independent calibration dataset.
8. The final rig derives from calibrated R/T, not nominal CAD. Its origin is the
   camera-center midpoint; +X points from the configured left camera to right;
   +Y is the calibrated optical forward bisector orthogonalized to +X; +Z is
   +X cross +Y. Units are millimeters and the frame is right-handed.
9. Reprojection and symmetric epipolar residuals are pixel-space evidence,
   **not scanner accuracy**. M4 freezes no product quality thresholds. Finite
   poor fits remain reportable; numerical validity and geometric degeneracy are
   distinct from quality acceptance.
10. Solver policy v1 explicitly uses COUNT+EPS, 100 iterations and epsilon 1e-12.
    Cross-architecture source keys, dispositions and partitions must agree
    exactly; floating solver results use documented synthetic fixture tolerances.

## Consequences

The foundation stays pure and header-only. The compiled OpenCV solver consumes
only CalibrationDataset and pure solve models, with direct core/calib3d linkage.
It does not ingest RawCapture or require ArUco. OpenCV 4.6 calibration requires
explicitly range-checked float point arrays; Mantis double geometry stays
unchanged and authoritative. Held-out poses use IPPE and the lowest recomputed
RMS across all returned solutions, with returned-order tie breaking.

M5 and later own immutable artifact encoding/persistence, activation and capture
binding, services and frontends. Real acquisition repeatability and product
acceptance require later real-data evidence. See the
[M4 implementation contract](../architecture/calibration-solve.md) for numerical,
metric, transform and API details.
