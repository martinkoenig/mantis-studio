# ADR-028: Target observations and deterministic calibration datasets

Status: Accepted; observations and detector contracts implemented in v0.3 M2;
datasets and selection deferred to M3

## Context

Checkerboard and partial ChArUco detections must feed the same calibration flow.
Independent camera visibility and many nearly identical 120 FPS frames require
explicit point correspondence and reproducible, geometrically diverse selection.

## Decision

A common TargetObservation associates source FrameSet sequence, camera
role/identity, stable target point IDs, 2D image points in pixels, corresponding
3D/object target points in millimeters and detection-quality metadata. Source
RawCapture identity must disambiguate FrameSet sequences across recordings.
Object points use the target's effective physical geometry from [ADR-027](027-calibration-target-geometry-and-physical-scale.md).

M2 defines deterministic **detector-grid** Checkerboard IDs and retains
detector-provided **physical-board** ChArUco corner IDs. Executable synthetic
contracts verify their indexing and coordinates against OpenCV 4.6. An unmarked
symmetric Checkerboard does not encode an absolute physical origin: rotating an
even-by-even board by 180 degrees produces identical pixels but exchanges physical
corners. Its grid IDs therefore cannot guarantee physical identity across arbitrary
board orientations. No origin marker is assumed or enabled. Plain Checkerboard
correspondence across views requires independently established consistent physical
orientation; joining its grid IDs alone cannot establish that orientation.

Partial ChArUco visibility is valid. LEFT and RIGHT can see different ID sets. Stereo uses the
intersection of visible IDs for that FrameSet, joined by ID rather than vector
position. Mono intrinsic calibration may use all accepted points visible to that
camera, including points absent from the opposite camera.

M2's pure `intersect_observations()` validates both observations, checks target
identity/revision/type and shared object geometry, and joins by ID in increasing
order while preserving both image points. It does not pair frames, certify a
Checkerboard origin or solve stereo geometry. Successful observations have equal,
nonempty correspondence arrays, unique IDs, finite points, nonzero image dimensions
and a matching detected-point count. No-target detection is a successful absence,
not an empty observation or a runtime failure.

A CalibrationDataset may reference multiple RawCapture artifacts. The same
RawCapture inputs + target + analysis configuration must produce the same
accepted/selected FrameSet identities. Selection uses deterministic ordering and
tie-breaking and favors geometric diversity over large numbers of near-identical
frames. Accepted observations, selection and rejected-observation evidence must
remain distinguishable and reproducible. Numerical sample-selection thresholds
are not frozen; they must be based on real data.

## Consequences

M2 implements the pure observation model and a separate compiled OpenCV detector
adapter; the pure calibration foundation remains independent of OpenCV. Dataset
selection, codecs, project migrations, services and jobs remain later work.
The [M2 implementation contract](../architecture/calibration-detection.md) records
dictionary support, homography interpolation, physical scale and orientation tests.
Training/held-out/final-fit evidence and independent-dataset repeatability will be
reported separately; reprojection
error is not scanner accuracy. See the [v0.3 baseline](../architecture/v0.3-geometric-calibration.md).

## Alternatives considered

Treating partial visibility as failure excludes valid ChArUco observations.
Pairing array positions can mismatch physical points. Random sampling or taking
every adjacent frame hides determinism and geometric diversity. A dataset limited
to one recording would prevent independent collection and validation workflows.
