# Calibration service and public control API (M6)

M6 exposes the accepted [M1–M5 contracts](v0.3-geometric-calibration.md) through
`mantisd`. It introduces orchestration, not calibration mathematics. Ordinary
calibration is Free/open-platform functionality, without registration, licensing
or entitlements. [ADR-035](../adr/035-calibration-control-api-and-job-orchestration.md)
records this boundary. The [M7 Studio workspace](calibration-workspace.md) composes
this API; M8 physical calibration acceptance remains pending.

## Service contract and ownership

`src/services/include/mantis/calibration_service.hpp` defines pure service DTOs,
independent of Protobuf, OpenCV, JSON, SQLite and filesystem implementation details.
`Runtime` implements `CalibrationService` alongside its existing services.
`Runtime::project_store()` remains internal and is absent from clients/protocol.

| Operation | Input | Output / boundary |
| --- | --- | --- |
| `create_calibration_target` | `TargetCreate` | Synchronous `CalibrationInfo` with allocated identity |
| `build_calibration_dataset` | `DatasetBuild` | Job ID; result is one CalibrationDataset |
| `solve_camera_calibration` | `CameraSolve` | Job ID; result is one CameraCalibration |
| `solve_rig_calibration` | `RigSolve` | Job ID; result is one RigCalibration |
| `calibrations` | none | Synchronous entries, including incomplete revisions |
| `calibration_info` | artifact ID | Synchronous typed compact finalized-artifact inspection |
| `active_calibration` | logical device ID | Synchronous optional exact current binding |
| `activate_calibration` | device / rig artifact IDs | Synchronous explicit metadata-only activation |
| `clear_calibration` | logical device ID | Synchronous binding removal |

M1 target validation/scaling, M3 canonicalization/selection/ingestion, M4 mono/stereo
solves and M5 graph validation/codecs/persistence/revisioning/activation stay in their
existing modules. The protocol adapter translates DTOs and dispatches only.
C++/Python/CLI use protocol messages; they do not link or connect to runtime/storage
implementations. All jobs use the existing executor (queue 64, retained history 256).

## Requests, revisions and presence

Target specification contains `squares_x`, `squares_y` (square counts),
`nominal_square_size_mm` and an explicit Checkerboard/ChArUco pattern variant.
ChArUco adds exact dictionary, `nominal_marker_size_mm` and physical layout:
`black_square_at_origin` or `white_square_at_origin_even_rows`. Unknown layouts
fail explicitly; an empty/missing pattern is rejected on the wire. M1 validates
physical geometry; M2 validates detector/backend capabilities when building a dataset.

`PhysicalMeasurement` has optional `active_width_mm`, `active_height_mm` and
optional `MeasurementProvenance`. Extents must be present together. Provenance
has optional width/height uncertainty, instrument and note. Measured extents
without provenance are valid. Present empty provenance still requires extents.
The wire uses proto3 optional scalars/strings and message presence. It preserves
`None` versus `MeasurementProvenance()` and absent versus present empty strings.
The returned target entry's logical reference is the allocated target identity
used by M3/M4; clients never supply that identity or a revision number.

All four creation requests may contain `series_id`: empty creates a new logical
series at revision 1; an existing compatible series requests the next revision.
Wrong-kind series fail synchronously. M5 owns atomic reservation/allocation and
immutable graph publication; revisions are never overwritten or reused.
Activation changes only a binding and does not allocate a revision.

Dataset requests carry an exact target artifact ID, repeated finalized RawCapture
schema-2 IDs, unique exact roles, positive `max_selected_per_camera` and optional
series. The server loads the exact target, constructs analysis schema/policy v1,
passes the Job token through M3 and persists with its exact target reference.
RawCapture pixels remain in the data plane. Input source/role order is canonicalized
by M3 and never changes selection policy.

Camera requests carry an exact dataset artifact ID, one exact role, positive
`heldout_per_camera` and optional series. `MonoSolveConfig` schema/solver/split
versions are 1. The server calls the one-camera `solve_camera_intrinsics` API and
persists exact dataset/target dependencies. `SolverImplementation` records actual
`solver_opencv_version()`, `application_version` and `build_version`.

Rig requests carry exact dataset, LEFT CameraCalibration and RIGHT CameraCalibration
IDs, positive `heldout_pairs`, explicit rig frame ID/name and optional series.
M5 loads verify hashes and the dependency graph. Both cameras must refer to the
exact supplied dataset/target. LEFT/RIGHT slots define handedness; role names,
artifact IDs and discovery order never reorder them. Stereo config versions are 1,
roles come from the supplied camera solutions, and intrinsics remain M4's fixed
final mono models. One RigCalibration references that exact dataset/target/LEFT/RIGHT.
Solving never activates it.

Checkerboard supports dataset/mono. Rig requests fail `Status::incompatible` via
M4's accepted correspondence policy. ChArUco supports all stages. Poor finite
residuals remain evidence; no PASS/WARN/FAIL quality gate or accuracy-in-mm is added.

## Jobs, validation and cancellation

The three expensive operations immediately return `result_id` containing the normal
daemon Job ID. A completed `Job.result_artifact` identifies exactly one new immutable
artifact. SDKs never wait implicitly for calibration jobs. Numerical M4 solve failures
remain failed Jobs with diagnostics. A client wait timeout does not cancel work.

Before scheduling, the service rejects empty/oversized strings, missing or duplicate
roles/sources, wrong/nonfinalized artifact inputs, unsupported schemas, zero budgets/
held-out counts, wrong-kind/missing series, missing dataset camera roles, equal LEFT/
RIGHT artifacts, cross-dataset camera inputs and malformed frames. M4 stereo
partition validation also catches the Checkerboard limitation before scheduling.
RawCapture packet/geometry validation occurs inside M3 while the dataset job runs.

M3 observes its Job cancellation token during streaming analysis. Camera/rig jobs
check cancellation before solving, immediately after solving and immediately before
persistence. OpenCV calls are **non-preemptive**: cancellation is not instantaneous.
When observed at these boundaries it prevents publication/persistence. Once M5
persistence is entered it completes or fails according to M5; M6 does not claim a
cancellable transaction. Each operation publishes at most one artifact. Previously
created inputs remain unchanged on solve or persistence failure.

Synchronous errors preserve structured statuses: `invalid_argument` for malformed
requests/configuration; `not_found` for missing artifacts/series; `incompatible`
for wrong kinds/schemas/states/dependencies/geometry or Checkerboard stereo;
`corrupt` for invalid immutable documents/hash graphs. Queue exhaustion is `busy`.
No malformed discovered metadata is treated as device absence.

## Protocol v1 and resource bounds

Protocol version remains 1. Existing Request commands 10–31 and Response fields
1–12 are frozen, verified by `protocol-field-compatibility` against generated
Protobuf descriptors. The new Request assignments are:

| Tag | Command |
| --- | --- |
| 32 | calibration_target_create |
| 33 | calibration_dataset_build |
| 34 | calibration_camera_solve |
| 35 | calibration_rig_solve |
| 36 | calibrations_list |
| 37 | calibration_info |
| 38 | calibration_active |
| 39 | calibration_activate |
| 40 | calibration_clear |

Response additions: 13 repeated `calibrations` (`CalibrationEntry`),
14 `calibration_info`, 15 `active_calibration`. Creation of a target also returns
its artifact ID in `result_id` and the full compact target info. Job operations
return only the Job ID; normal snapshots expose job completion/result.

`CalibrationEntry` reuses the existing `Artifact` descriptor and adds a logical
`CalibrationReference` (ID/schema/revision). Exact upstream references use
`CalibrationArtifactReference` (artifact ID/hash algorithm/hash). List resolves
M5 revision metadata without decoding incomplete artifacts and sorts by artifact
type, logical ID, revision, artifact ID.

The common calibration string bound is 4096 UTF-8 bytes (also rejecting NUL),
with at most 1024 source IDs and 64 requested roles. These protect resource use,
not product editions. Repeated duplicates fail before expensive work. The CLI
bounds argument-derived collections identically and total arguments at 1300;
Python also bounds generator inputs before materializing them.

The transport retains its 4 MiB frame bound on requests and responses. The adapter
checks serialized response size and returns a compact structured `busy` error if
it exceeds the bound; oversized lists are not silently truncated. Per-artifact
inspection avoids transmitting full observation populations, pixels, captured frame
metadata, image/object points or per-point residuals. Data-plane artifact documents
remain the authoritative complete evidence.

`ActiveCalibration` response is present for a successful query; its optional
`binding` is absent for none. A binding contains device ID, logical reference and
backing rig artifact ID/hash. Python/C++ return `None`/`std::nullopt` for none.

## Compact inspection

`CalibrationInfo` contains an entry and a typed oneof:

- Target: complete public target specification, including optional measured
  extents/provenance and physical ChArUco layout. Entry identifies allocated target.
- Dataset: exact target reference, source IDs/count, requested roles, max samples,
  analysis schema/selection-policy version, camera role/physical identity/image
  geometry/optical frame, analyzed/detected/no-target/selected counts and total
  records. Counts are derived from the immutable dataset using M3 summaries.
- Camera: exact dataset/target, camera descriptor, explicit pinhole-brown5
  training/final models, solve config versions/count, training/held-out/final
  residual and coverage summaries, sample counts, optional OpenCV solver RMS
  and implementation provenance.
- Rig: exact dataset/target/LEFT/RIGHT, roles/physical identities, final mono
  models, final stereo R/T/E/F, three named transforms, baseline mm, relative
  rotation radians, rig frame, config and training/held-out/final symmetric
  epipolar residual summaries/pair counts and implementation provenance.

Transforms carry source and target ID/name plus exactly 16 row-major doubles for
`T_target_from_source`. R/E/F carry nine row-major doubles each; stereo T carries
three XYZ millimeter doubles. There is no serialized `cv::Mat` or covariance.
Pixel residuals are not scanner accuracy.

## Activation and historical capture

For a currently discovered exact logical device, activation resolves its exact
component descriptors, applies the same strict M5 capture-calibration metadata
parser and validates role/physical identity/width/height through
`activate_rig_calibration`. Present invalid/missing metadata is an explicit error.
If the logical device is not discovered, project-local offline binding uses M5
semantics, without inventing current geometry. Capture start always revalidates
live compatibility and snapshots the exact active reference once.

Active query reports only current project-local configuration for **future**
capture. It never determines calibration for historical data. RawCapture/replay
packet references remain authoritative after activation changes. Activation starts
no capture and changes no existing capture or immutable artifact. Clear deletes
only the binding. M5's frame path remains unchanged: no per-frame DB lookup or
new pixel copies.

Successful transitions emit bounded events `calibration.target.created`,
`.dataset.created`, `.camera.created`, `.rig.created`, `.activated` and `.cleared`,
with stable artifact/device IDs, never per-observation events.

## C++ Client SDK example

Link only `mantis-client`; generated wire DTOs are value types, not runtime ownership.

```cpp
mantis::client::Client client;
mantis::wire::v1::CalibrationTargetSpecification spec;
spec.set_squares_x(8); spec.set_squares_y(6); spec.set_nominal_square_size_mm(40);
auto *pattern = spec.mutable_charuco();
pattern->set_dictionary("DICT_6X6_250"); pattern->set_nominal_marker_size_mm(25);
pattern->set_pattern_layout("black_square_at_origin");
auto target = client.create_calibration_target(spec);
auto dataset_job = client.build_calibration_dataset(target.artifact().id(),
    {raw1, raw2}, {"left", "right"}, 40);
auto dataset = client.wait(dataset_job, std::chrono::hours(1)).result_artifact();
auto left = client.wait(client.solve_camera_calibration(dataset, "left", 5),
    std::chrono::hours(1)).result_artifact();
auto right = client.wait(client.solve_camera_calibration(dataset, "right", 5),
    std::chrono::hours(1)).result_artifact();
auto rig = client.wait(client.solve_rig_calibration(dataset, left, right, 5,
    "org.mantis.x1.rig", "Mantis X1 rig"), std::chrono::hours(1)).result_artifact();
client.activate_calibration(device_id, rig);
auto current = client.active_calibration(device_id); // optional exact binding
```

Other methods are `calibrations`, `calibration_info` and `clear_calibration`.
Each create/build/solve optionally accepts an existing series ID as its final
argument. No SDK method accepts an arbitrary revision.

## Python Client SDK example

```python
import mantis
client = mantis.connect()
target = client.calibration.create_charuco_target(
    squares_x=8, squares_y=6, square_mm=40, marker_mm=25,
    dictionary="DICT_6X6_250", layout="black_square_at_origin",
    measurement=mantis.PhysicalMeasurement(
        active_width_mm=330.4, active_height_mm=243.6,
        provenance=mantis.MeasurementProvenance()))
# provenance=None instead preserves absent measurement provenance.
dataset = client.calibration.build_dataset(
    target, [raw1, raw2], roles=["left", "right"],
    max_selected_per_camera=40).wait(timeout=3600)
left = client.calibration.solve_camera(dataset, "left", heldout_per_camera=5).wait(timeout=3600)
right = client.calibration.solve_camera(dataset, "right", heldout_per_camera=5).wait(timeout=3600)
rig = client.calibration.solve_rig(
    dataset, left, right, heldout_pairs=5,
    rig_frame_id="org.mantis.x1.rig", rig_frame_name="Mantis X1 rig").wait(timeout=3600)
client.calibration.activate(x1.id, rig)
print(client.calibration.active(x1.id))
print(client.calibration.info(rig))
client.calibration.clear(x1.id)
```

Artifact parameters accept string IDs, SDK `Artifact` objects or wire entry/reference
objects. `create_checkerboard_target` and typed `create_target(TargetSpecification)`
are also available. `list`, `info`, `active`, `activate` and `clear` are synchronous.
The three expensive operations return the existing `Job` class.

## CLI examples

```sh
mantis-cli calibration target create checkerboard --squares-x 8 --squares-y 6 --square-mm 40
mantis-cli calibration target create charuco --squares-x 8 --squares-y 6 --square-mm 40 \
  --marker-mm 25 --dictionary DICT_6X6_250 --layout black_square_at_origin \
  --measured-width-mm 330.4 --measured-height-mm 243.6 --measurement-provenance
mantis-cli calibration dataset build TARGET RAW1 RAW2 --role left --role right --max-samples 40
mantis-cli job wait JOB
mantis-cli calibration camera solve DATASET left --heldout 5
mantis-cli calibration camera solve DATASET right --heldout 5
mantis-cli calibration rig solve DATASET LEFT RIGHT --heldout 5 \
  --rig-frame-id org.mantis.x1.rig --rig-frame-name 'Mantis X1 rig'
mantis-cli calibration list
mantis-cli calibration info ARTIFACT
mantis-cli calibration activate DEVICE RIG_ARTIFACT
mantis-cli calibration active DEVICE
mantis-cli calibration clear DEVICE
```

Creation commands support `--series LOGICAL_ID`; no `--revision` option exists.
Measurement accepts `--width-uncertainty-mm`, `--height-uncertainty-mm`,
`--instrument`, `--note`. Any provenance field implies provenance presence;
`--measurement-provenance` alone represents present empty provenance. Partial
measured extents, provenance without extents, unknown/duplicate singular options,
malformed integers/doubles, missing values, duplicate roles/sources and zero
sample/held-out counts are rejected. Job commands print `result_id` without waiting.

## Executable evidence

`calibration-service-protocol` exercises service orchestration, revisions, old
immutability, physical measurement, cancellation, typed inspection, exact dependencies,
slot reversal, Checkerboard mono/stereo policy and every new generated command.
`protocol-field-compatibility` verifies all old/new public tags through descriptors
and absence of large evidence fields. `python-calibration` tests typed encoding,
provenance presence, bounds, artifact arguments, Job/current-binding/error semantics.
`calibration-client-integration` starts real mantisd, crosses Protobuf with Python
and C++ SDKs, runs CLI subprocesses and checks local strict parser failures.
Fixtures are generated, with small RawCapture images and M4 synthetic datasets;
no binary capture is committed. Prior M1–M5/capture suites remain release gates.

### Local validation, 2026-10-06

All 35 tests passed in each configuration, including every prior v0.3, acquisition,
raw-capture, ABI/boundary, Python, acceptance and Q6A validation-harness suite:

| Build | Studio | Tests | Wall time |
| --- | --- | --- | --- |
| Debug | ON | 35/35 | 102.35 s |
| Debug | OFF | 35/35 | 91.42 s |
| Release | ON | 35/35 | 67.76 s |
| Release | OFF | 35/35 | 47.94 s |
| ASan + UBSan, Debug | OFF | 35/35 | 145.96 s |

Local environment: Ubuntu 25.10 x86_64, GCC 15.2, Protobuf 3.21.12, OpenCV 4.10.0
and Qt 6.9.2. Missing OpenCV development dependencies were extracted into the
worktree's ignored `build/deps` directory, without changing other worktrees.
ASan used `detect_leaks=1`, UBSan used `halt_on_error=1`. The local sanitizer run
also used `OPENCV_OPENCL_RUNTIME=disabled` because this machine's installed CUDA
OpenCL loader leaked during its initialization; no leak suppression or disabled
leak detection was used. Calibration/detection execute on their ordinary CPU path.

The final branch HEAD must also pass the existing five Ubuntu 24.04 CI jobs
(x86_64 and ARM64 with Studio OFF/ON, plus sanitizers) before M6 completion is
reported. Synthetic/software tests do not establish real Q6A calibration acceptance.
