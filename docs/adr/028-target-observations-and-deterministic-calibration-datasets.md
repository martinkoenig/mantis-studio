# ADR-028: Target observations and deterministic calibration datasets

Status: Accepted; observations, RawCapture ingestion, in-memory dataset model and
deterministic per-camera selection implemented through v0.3 M3; M4 solve/split/stereo
policy implemented under [ADR-029](029-calibration-solve-validation-and-stereo-policy.md)

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

M3's pure CalibrationDataset retains the exact target, versioned analysis config,
canonical source IDs, camera descriptors and image-analysis records. A sample is
identified by `(RawCapture ID, parent FrameSet sequence)`; camera observations add
exact role and physical camera identity. Sequences may overlap across recordings,
but duplicates within one recording are invalid. Source IDs and role configuration
are sorted lexicographically, duplicates are rejected, cameras are sorted by role
and records by that complete observation key. Reversing RawCapture argument order
does not affect dataset content or selection.

Only finalized RawCapture schema-2 artifacts are accepted, read sequentially through
CaptureReader. Each FrameSet must provide exactly one image for every explicitly
requested role; other roles are ignored. Role strings are neither case-folded nor
aliased (`left` / `right` are X1's recorded values). Across all frames and captures,
each role must preserve physical camera identity, resolution and the child optical
frame's exact ID/name representation. Mixed RAW8/Y10P packing is allowed. RAW8 is
borrowed with its stride; Y10P uses existing top-eight-bit grayscale semantics and
one reusable u8 scratch frame. Pixels remain in RawCapture, never in the dataset.

Records distinguish no target (no observation/descriptor/rank), detected but
unselected (observation/descriptor without rank), and selected detection (with
rank). Detector/layout/backend errors fail analysis; they are not no-target
evidence. Zero detections is structurally valid. Counts are derived diagnostics,
not calibration acceptance.

Diversity descriptor v1 uses normalized visible point coordinates and contains,
in order: centroid X/Y, extent X/Y, population variance X/Y, covariance XY and
visible_fraction. The latter divides detected points by
`(squares_x - 1)*(squares_y - 1)`. These are image-space features, not pose or quality
scores. Selection quantizes each component using checked
`llround(component * 1,000,000)`, halfway away from zero, into int64 values.
Squared distances and uint64 accumulation are overflow checked; there is no
compiler-specific wide-integer requirement or saturating fallback.

Selection policy v1 operates independently per camera over the combined population
of all captures. Budget is an explicit positive caller choice. If candidates fit
the budget, all are selected in canonical observation-key order. Otherwise the
seed is the lexicographically smallest fixed descriptor, then smallest observation
key. Greedy maximin chooses the largest minimum squared Euclidean distance to the
selected set, over all eight equally weighted components. Every tie uses canonical
observation key; ranks record greedy order. No RNG or chronological sampling is
used. Selection count is `min(candidates, budget)` and no quality threshold is added.

M3 selection supplies diverse per-camera mono observations, not final stereo
training samples. Checkerboard grid IDs are not promoted to absolute physical IDs;
joining them still does not prove stereo correspondence. M4 owns mono training/
held-out sets, shared-FrameSet stereo candidates, ChArUco physical-ID joins,
explicit Checkerboard mono-only policy and intrinsic/stereo solves. The exact
selected-population split, final mono refit, fixed-intrinsic stereo candidate union
and three-stage evidence are frozen by [ADR-029](029-calibration-solve-validation-and-stereo-policy.md).

## Consequences

M2 implements the pure observation model and a separate compiled OpenCV detector
adapter. M3 adds a pure dataset/selector and separate compiled RawCapture ingestion
layer; the foundation remains independent of OpenCV and artifact-store. Dataset
codecs, project migrations, services and jobs remain later work.
The [M2 implementation contract](../architecture/calibration-detection.md) records
dictionary support, homography interpolation, physical scale and orientation tests.
The [M3 implementation contract](../architecture/calibration-dataset.md) records
source/layout boundaries, exact ordering, policy arithmetic and executable evidence.
Training/held-out/final-fit evidence and independent-dataset repeatability will be
reported separately; reprojection
error is not scanner accuracy. See the [v0.3 baseline](../architecture/v0.3-geometric-calibration.md).

## Alternatives considered

Treating partial visibility as failure excludes valid ChArUco observations.
Pairing array positions can mismatch physical points. Random sampling or taking
every adjacent frame hides determinism and geometric diversity. A dataset limited
to one recording would prevent independent collection and validation workflows.
