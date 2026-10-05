# M2 calibration detection implementation contract

M2 implements detection and correspondences. It produces no camera calibration,
pose, dataset selection, artifact, project migration or calibration workflow.
RawCapture/Y10P ingestion remains M3 work; live acquisition is unchanged.

## Dependency and API boundary

`mantis-calibration` remains a pure C++23 header-only foundation. Its self-contained
`calibration_observation.hpp` contains `ObservationSource`, `ImagePoint`,
`DetectionEvidence`, `TargetObservation`, structural validation and the common-ID
intersection helper. `calibration.hpp` exposes this model alongside existing APIs.
There are no OpenCV, Qt, SQLite, Protobuf, filesystem or JSON requirements in this
foundation. The CMake boundary check also rejects transitive OpenCV/JSON/Protobuf
dependencies from `mantis-calibration`.

The separate compiled `mantis-calibration-opencv` library links the foundation and
OpenCV >=4.6 components **core, imgproc, calib3d, aruco**. ArUco requires contrib on
the Ubuntu 24.04 x86_64/ARM64 baseline. All CI jobs install `libopencv-dev` and
`libopencv-contrib-dev`, print their package versions in the job summary and compile
and execute the contracts. CMake reports the discovered OpenCV version. Locally the
contracts passed against OpenCV **4.6.0** and **4.10.0**. For >=4.7, small board/dictionary
accessor compatibility branches preserve the same 4.6-generation detection flow;
there is no `CharucoDetector` usage or dependency on its detection API.

`detect_target(target, image, source)` returns
`Result<std::optional<TargetObservation>>`: an observation on detection, successful
`nullopt` when no target corners are visible, and a structured calibration error
for invalid input/configuration. OpenCV exceptions are caught at this boundary and
reported as `Status::incompatible`; malformed detector output is `Status::corrupt`.
Structural input errors are `Status::invalid_argument`.

`GrayImageView` borrows a `span<const uint8_t>` with width, height and byte row
stride. Only RAW8 grayscale is accepted. The last row needs width bytes; inter-row
padding is allowed. Dimensions, stride, buffer extent, `size_t` overflow and
OpenCV integer/pointer representation limits are checked before access. A
non-owning, input-only `cv::Mat` header introduces no image copy or retained lifetime.

## Observation and correspondence contracts

An observation retains RawCapture ID, FrameSet sequence, camera ID/role, target
ID/revision/type, image width/height, point IDs, double-precision pixel points,
double-precision millimeter object points and detected point/marker counts plus
a partial indication. Identities/role must be supplied; M2 allocates no identities.
Validation requires equal nonempty correspondence lengths, unique IDs, finite
pixel/object coordinates, nonzero image dimensions and detected-point count equal
to correspondence count. There are no coverage, corner-count or solve-quality
thresholds.

`intersect_observations(left, right)` validates observations and target
identity/revision/type, rejects unequal shared object coordinates, and returns
increasing IDs with the correctly associated left pixel, right pixel and shared
object point. Empty intersection is valid. This helper neither pairs FrameSets nor
establishes a Checkerboard's physical orientation; its IDs retain their declared
`PointIdSemantics`.

## Checkerboard

`findChessboardCornersSB` receives **inner corners**:
`Size(squares_x - 1, squares_y - 1)`. Fixed offline flags are
`CALIB_CB_NORMALIZE_IMAGE | CALIB_CB_ACCURACY | CALIB_CB_EXHAUSTIVE`.
There is no FAST_CHECK, origin-marker flag or extra `cornerSubPix` call. Only a
complete expected grid produces an observation; `partial` is false and marker
count is zero. SB itself requires at least three inner corners per axis. Smaller
M1-valid grids are unsupported adapter inputs, not a new M1 validation threshold.
OpenCV signed arithmetic limits are checked before its doubled grid-area search.

IDs enumerate returned detector rows/columns:
`id = row * (squares_x - 1) + column`. Nominal points are
`((column + 1) * nominal_square_size_mm, (row + 1) * nominal_square_size_mm, 0)`.
The adapter calls M1 `scale_target_point()` for effective object geometry.

**These are deterministic detector-grid IDs, not absolute physical board IDs.**
The synthetic even-by-even 8x6 board has identical bytes before/after 180-degree
rotation. With OpenCV 4.6.0, detector ID 0 refers to physical fixture corner 34 in
normal and moderate perspective views, 0 after 180 degrees, 34 after 90 degrees
and 0 after 270 degrees. Thus the same physical corner does not keep the same ID.
Tests permit either row-major direction across backends while proving the physical
reversal for the identical 180-degree image and the 90/270 pair. No image convention
can recover the missing origin. Physical correspondences across Checkerboard views
require independent orientation evidence; grid-ID intersection cannot supply it.

## ChArUco

Canonical names are matched exactly and case-sensitively to predefined enums:

```text
DICT_4X4_50    DICT_4X4_100    DICT_4X4_250    DICT_4X4_1000
DICT_5X5_50    DICT_5X5_100    DICT_5X5_250    DICT_5X5_1000
DICT_6X6_50    DICT_6X6_100    DICT_6X6_250    DICT_6X6_1000
DICT_7X7_50    DICT_7X7_100    DICT_7X7_250    DICT_7X7_1000
DICT_ARUCO_ORIGINAL
DICT_APRILTAG_16h5    DICT_APRILTAG_25h9
DICT_APRILTAG_36h10   DICT_APRILTAG_36h11
```

Unknown names, integer strings, case changes and whitespace variations are errors.
There is no fallback or preferred production dictionary. Dictionary marker
capacity and float representability of nominal board geometry are adapter
constraints; M1 retains its double-precision physical model.

### Physical pattern layout and backend support

`CharucoDefinition::pattern_layout` is physical target geometry:
`black_square_at_origin` means a black square at the active-grid origin and is the
source-compatible default. `white_square_at_origin_even_rows` means a white square
there, with the historical pre-4.6-compatible checkerboard/marker parity, and is
valid only with even `squares_y`. Unknown enum values are invalid. Both layouts
retain Mantis's active-grid origin, increasing-column +X, increasing-row +Y and
planar Z=0 corner coordinates. Layout is required to distinguish boards that
otherwise share dimensions, dictionary and square/marker sizes.

The incompatible even-row pattern-generation change occurred in OpenCV 4.6.0;
see the [upstream compatibility issue](https://github.com/opencv/opencv/issues/23152).
The public [4.7.0 header](https://raw.githubusercontent.com/opencv/opencv/4.7.0/modules/objdetect/include/opencv2/objdetect/aruco_board.hpp)
lacks `setLegacyPattern()`, while the
[4.8.0 header](https://raw.githubusercontent.com/opencv/opencv/4.8.0/modules/objdetect/include/opencv2/objdetect/aruco_board.hpp)
first exposes it. Its documented even-row white-origin semantics match the
physical layout above. The adapter uses compile-time API detection rather than
assuming support from a version number, accommodating backports.

| Backend | Black-origin layout | White-origin even-row layout |
| --- | --- | --- |
| Upstream OpenCV 4.6 / 4.7, without the API | Default board representation | `Status::incompatible` |
| Upstream OpenCV 4.8+, with the API | Explicit `setLegacyPattern(false)` | Explicit `setLegacyPattern(true)` |

The layout is configured before detection/interpolation and before reading native
corner indexing. An unsupported backend reports the requested physical layout and
linked OpenCV version, even on a blank image. Target validity is unchanged. There
is no silent fallback, heuristic ID flipping or replacement interpolation.
Local executable contracts exercise the unsupported path on **4.6.0** and the
supported path on **4.10.0**, including full/partial legacy-compatible detection,
ID preservation, physical scaling and opposite-layout rejection despite detectable
markers. The Ubuntu CI baseline uses its packaged **4.6.0** backend. The adapter
continues using `detectMarkers()` / `interpolateCornersCharuco()`; it does not call
`CharucoDetector` directly. A newer OpenCV may implement those free functions using
its internal detector and board-consistency checks.

Preferred dictionary, board dimensions and the preferred physical layout for
future Mantis-owned boards remain separate product/reference-board decisions.

The nominal board uses M1 square counts, square/marker size, dictionary and physical
pattern layout.
`detectMarkers()` uses `CORNER_REFINE_NONE`; `interpolateCornersCharuco()` uses
explicit `minMarkers = 2`, empty camera matrix and empty distortion coefficients.
This is the local homography path. Marker corners are not separately refined;
interpolation supplies the refined ChArUco pixels. This follows the
[OpenCV 4.6 ChArUco recommendation](https://docs.opencv.org/4.6.0/df/d4a/tutorial_charuco_detection.html).

Returned corner IDs are preserved and correspondences are sorted together by ID;
duplicates/out-of-range IDs are errors. The full board has
`(squares_x - 1) * (squares_y - 1)` possible corners. Any nonempty interpolation is
valid; `partial` means fewer than that total. Marker count records markers detected
in the image using the configured dictionary.

Executable contracts check each ID against the native board chessboard-point
index, its known synthetic image location and corresponding planar X/Y positions.
The verified mapping is `id = row * (squares_x - 1) + column`, with native
chessboard-corner positions `((column + 1) * square, (row + 1) * square, 0)` for
the tested layouts/backends. This is a corner-index contract, not a universal
claim about every OpenCV board coordinate-frame convention. Mantis owns the
physical target-local convention. The adapter verifies that mapping, reconstructs
nominal Mantis points in double and calls M1 scaling. OpenCV float board points
are not authoritative physical
output. Measured scale metadata changes object points without changing pixel
detection or IDs; X/Y pitch 41.3/40.6 mm is tested independently.

## Executable evidence

`calibration-observation` covers pure invariants, single-point validity, ID semantics,
unsorted intersection, disjoint IDs, mismatched targets and inconsistent geometry.
`calibration-opencv` generates all images at runtime with white borders. It tests
full boards, perspective, repeated detection, rotation/origin contracts, borrowed
padded buffers, input immutability, blank/tiny images, invalid/overflowing inputs,
all 21 dictionaries and their capacities, native ChArUco indexing, preserved partial
IDs, two 20-corner partial crops with common IDs `3 10 17 24 31`, anisotropic scale
and double-precision physical output despite fractional nominal float geometry.
Pixel-contract comparisons use tolerances; ID sets/order are exact. Repetition on
one backend checks exact floating output. Tests print the actual OpenCV version
and orientation results and run on x86_64, ARM64 and under ASan/UBSan.

Dataset construction, RawCapture ingestion, sample diversity and solver decisions
are deferred. Detection evidence is not a calibrated camera solution.
