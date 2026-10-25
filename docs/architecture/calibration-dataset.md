# M3 calibration dataset implementation contract

M3 implements streaming RawCapture analysis, an in-memory dataset and deterministic
per-camera diversity selection. It produces no calibrated camera, pose, stereo
solution, training/held-out split, artifact encoding or project migration.

## Dependency and source boundary

`mantis-calibration` remains header-only and independent of OpenCV, artifact-store,
SQLite, Qt, Protobuf, filesystem and JSON. `calibration_dataset.hpp` contains the
pure model, validation, descriptor, selector and derived summaries; `calibration.hpp`
exposes it. Include and transitive CMake boundary checks enforce this separation.

The separate compiled `mantis-calibration-dataset` library exposes
`build_calibration_dataset(store, capture_ids, target, config, token)` and depends
on the foundation, `mantis-artifact-store`, `mantis-data` and the M2 OpenCV adapter.
Studio ON and OFF both build this layer. The result is `Result<CalibrationDataset>`.
The returned dataset owns no Store, packets, buffers or image pixels.

Every input must be `org.mantis.RawCapture`, schema **2**, state **FINALIZED**.
Other types, schemas and states return `Status::incompatible`. Schema 1 is not a
FrameSet calibration source; it is not migrated. The builder checks descriptors,
then iterates **artifact::CaptureReader::next()**. Calibration code does not parse
segments, packet encodings or SQLite chunks, and does not call random `Store::packet()`
per frame. The existing reader owns its bounded segment mapping and parsing.
No recording, replay, acquisition copy count or storage/project schema changes occur.

## Identity, configuration and canonical order

`FrameSetKey = (raw_capture_id, parent_frameset_sequence)` identifies a sample.
`ObservationKey = (FrameSetKey, camera_role, camera_id)` identifies an image analysis.
Both keys are explicitly comparable, in that field order. Parent sequences are
preserved without renumbering or a zero-origin assumption; child sequences are
not source FrameSet identities. Duplicate parent sequences within one capture fail.
Sequences may overlap across captures: `(A,17)` and `(B,17)` are distinct.

`DatasetAnalysisConfig` records schema version **1**, explicit requested roles,
selection policy version **1**, and a positive caller-chosen
`max_selected_per_camera`. There is no recommended budget default; zero is invalid.
Role strings are exact, nonempty and unique. The builder sorts them lexicographically
and rejects duplicates. X1's recorded roles are `left` / `right`; there are no
case conversions or aliases. Input capture IDs are likewise sorted and duplicates
are rejected. Cameras are sorted by role; records are strictly sorted by
ObservationKey. Caller capture/role order cannot influence the result.

Each dataset retains the exact supplied `CalibrationTarget`, including identity,
revision, type, nominal geometry, dictionary, physical pattern layout, measured
extents and provenance. Geometry is never reconstructed from detections.

## Requested images and camera consistency

Each source record must be a FrameSet with image children. Every requested role
must occur **exactly once** in every analyzed FrameSet. Missing/duplicate requested
roles are `Status::corrupt`. Other roles are ignored; no two-child assumption is
made. Every child needs nonempty role metadata so its applicability can be known;
unrequested children need not supply calibration camera identity/layout metadata.
Requested children need nonempty `metadata["identity"]`.

`DatasetCamera` preserves role, physical camera ID, image width/height and the
child's `spatial::CoordinateFrame`. Its exact frame ID and name/convention
representation must be nonempty and unchanged. For a given role, all frames and
captures must agree on identity, dimensions and both frame fields. Changes return
`Status::incompatible`; frame conventions are not guessed or rewritten. Different
supported byte packing is allowed with identical camera geometry.

An all-blank capture can produce a structurally valid zero-detection dataset.
If all sources contain no images, requested camera descriptors cannot be obtained
and the builder returns a structural source error. This is not a solve-quality rule.

## Image memory and error semantics

`data::image_layout()` owns layout interpretation and validation:

- RAW8: map the validated immutable byte buffer and borrow it directly as the
  M2 `GrayImageView`, preserving row stride. No full-frame conversion or copy.
- Y10P / `mipi-raw10-v1`: map the byte buffer for conversion, use the existing
  `Y10PView::display_row()` contract (10-bit sample **>> 2**), and fill one reusable
  tight u8 scratch frame. No uint16 image, JPEG/PNG intermediate or row-by-row
  remapping during conversion. Layout validation separately checks the packed view.

The builder retains the current FrameSet, borrowed image mapping, one scratch
buffer sized to the largest converted frame, source-sequence keys and compact
observation metadata. Pixel memory is bounded by the existing reader's segment
mapping/current FrameSet plus scratch, independent of recording duration. Detection
is synchronous; no image view escapes the call.

M2 success with an observation creates a detected record. Success with `nullopt`
creates a no-target record. Any detector, metadata, layout, storage or backend error
fails the build with a structured Error; it is never converted to no-target evidence.
In particular OpenCV 4.6's valid-but-unsupported white-origin even-row ChArUco
layout propagates `Status::incompatible`. Storage/data `Failure`s are converted at
the compiled boundary with their status preserved. Cancellation is supported.

## Dataset evidence and validation

Each `DatasetObservationRecord` retains its key and `DetectionOutcome`:

| State | Observation | Diversity | Selection rank |
| --- | --- | --- | --- |
| No target | Absent | Absent | Absent |
| Detected, unselected | Present | Present | Absent |
| Detected, selected | Present | Present | Present |

The pure validator checks target/identity/config, canonical unique sources, cameras
and record keys, known source/camera references, one record per requested role in
each represented FrameSet, and state consistency. Detected observations must be
structurally valid and match their source key, target identity/revision/type and
camera dimensions. IDs must be inside the target grid, object points must match
M1 physical scaling, and the descriptor must match its observation. Selection
ranks appear only on detections, are unique and contiguous from zero per camera,
and cannot exceed the budget. Validation introduces no quality thresholds.

`summarize_dataset()` derives FrameSets per capture and per-camera analyzed,
detected, no-target and selected counts, partial/full ChArUco counts and point-count
min/max. These are diagnostics, not mutable acceptance scores.

## Diversity descriptor and selection policy v1

Normalize each visible image point as `nx = x_px / image_width`,
`ny = y_px / image_height`. Accumulate in increasing point-ID order. The descriptor
components, in order, are:

1. Mean nx: centroid_x.
2. Mean ny: centroid_y.
3. max(nx) - min(nx): extent_x.
4. max(ny) - min(ny): extent_y.
5. Population mean `(nx - centroid_x)^2`: variance_x.
6. Population mean `(ny - centroid_y)^2`: variance_y.
7. Population mean `(nx - centroid_x)*(ny - centroid_y)`: covariance_xy.
8. Visible corner count / `((squares_x - 1)*(squares_y - 1))`: visible_fraction.

All visible points contribute. These are image-space diversity features; no pose,
intrinsics, accuracy/confidence score or reprojection computation is involved.
Normal M2 Checkerboard observations have visible_fraction 1; partial ChArUco
observations preserve their smaller fraction and original IDs.

The selector derives `array<int64_t,8>` using checked
`llround(component * 1,000,000)`, with halfway values rounded away from zero.
Scale is a policy-v1 constant, not a caller tuning knob. Nonfinite/unsafe conversions
fail. Descriptor values remain doubles in the record; fixed values are derived,
avoiding a second mutable stored representation. Quantization reduces sensitivity
to small backend floating differences; it is not a universal guarantee for values
lying on quantization boundaries.

Selection runs **independently for each camera** over all its detections across
all captures. If candidates <= budget, select all in canonical ObservationKey
order. Otherwise choose the lexicographically smallest fixed descriptor as seed,
then smallest ObservationKey on a tie. Repeatedly choose the remaining candidate
whose minimum squared Euclidean distance to selected descriptors is greatest.
Every distance uses all eight equally weighted fixed components. Ties use the
smallest ObservationKey. Store ranks in this greedy order. Selection count is
exactly `min(candidate_count, budget)`, including zero when nothing was detected.

Signed differences are converted to exact unsigned magnitudes; each square and
uint64 accumulation is checked for overflow. No compiler-specific wide integer,
RNG, wall clock, pointer value or unordered iteration influences selection.
Overflow fails rather than saturating or changing ordering. Dataset selection is
transactional: failure preserves existing ranks.

## Executable evidence and M4 boundary

`calibration-dataset` is linked only to the pure foundation. Synthetic observations
exercise structural errors, all evidence states, summaries, descriptor values,
partial fraction, quantization/overflow, exact maximin keys, budget behavior,
candidate-order independence and repeated ranks.

`calibration-dataset-ingestion` creates temporary real Store artifacts at runtime.
It exercises padded RAW8, Y10P with nonzero low bits, mixed packing, perspective/
scale/position variation, blank images, full/partial ChArUco, camera/source/layout
errors, unchanged packed bytes, and dataset lifetime after Store destruction.
Both capture and role input orders are reversed. Runtime Store IDs are canonicalized
into fixture labels A/B; the expected checkerboard selections are explicitly:
left **A/17 B/17 B/18**, right **B/17 A/18 B/18**. Both use overlapping source
sequence 17 and samples from both recordings. These assertions run on x86_64,
ARM64 and sanitizers. The Store itself prevents constructing duplicate sequences
and non-FrameSet schema-2 records; tests exercise those writer rejections, and the
builder independently checks the reader outputs. No malformed storage is hand-parsed
or synthesized in calibration code.

M3 prepares diverse mono observations. Checkerboard IDs remain `detector_grid`;
selection does not establish absolute physical identity or stereo correspondence.
ChArUco IDs remain `physical_board`. M4 owns mono training/held-out policy, shared-
FrameSet stereo candidates, ChArUco common IDs, explicit Checkerboard orientation
handling, intrinsic/stereo solving and validation. Persistence, activation,
services/protocol/CLI/Python/Studio workflows remain later work. No minimum point
count, coverage, distance, perspective, blur or other quality threshold is added.
