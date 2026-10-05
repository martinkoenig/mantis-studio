# Calibration artifacts and capture binding (M5)

M5 adds `mantis-calibration-artifacts`, a compiled typed persistence module above
`mantis-calibration` and `mantis-artifact-store`. JSON is private to its implementation.
The header-only foundation stays independent of OpenCV, storage, JSON and filesystem.
The codec module itself has no OpenCV/solver dependency; it consumes M4 pure results.
There is no change to numerical solvers, target geometry, M3 selection, RawCapture
schema 2, plugin ABI or acquisition pixel-copy policy.

## Typed APIs

`create_calibration_target`, `create_calibration_dataset`, `create_camera_calibration`
and `create_rig_calibration` return `Result<Stored<T>>`, containing a descriptor and
one typed artifact document. Corresponding `load_*` functions return typed documents.
`encode_document` returns canonical bytes, never a public JSON object.

An omitted `series` creates a random new logical ID/revision 1. An explicit existing
logical ID reserves its next revision. Callers cannot specify a revision number.
Target creation requires an unassigned target identity and returns the allocated,
persisted target. Use that returned target before M3/M4; identity is never rewritten
on an existing dataset or solution. To revise a physical target, supply its physical
fields with an unassigned identity and explicitly supply the existing series ID.

## Four artifact/document schemas

| Descriptor type | Artifact schema | Payload type |
| --- | --- | --- |
| org.mantis.CalibrationTarget | 1 | TargetArtifact |
| org.mantis.CalibrationDataset | 1 | DatasetArtifact |
| org.mantis.CameraCalibration | 1 | CameraArtifact |
| org.mantis.RigCalibration | 1 | RigArtifact |

Every artifact has exactly one generic packet chunk. Packet type is
`org.mantis.CalibrationDocument`, version 1, containing exactly one attribute:
`org.mantis.calibration.document`, scalar `u8`, rank 1, shape `[byte_count]`, byte
stride `[1]`, unit `utf8-json`. Immutable bytes are the canonical document.

The packet header is deterministic: sequence 0; device timestamp 0 with empty clock
ID/name; received monotonic timestamp 0; empty sync ID/trigger 0 with unknown quality;
empty calibration ID/revision 0/schema 1; `spatial::world` frame ID/name; empty metadata.
There are no sensor timestamps, random packet IDs, wall-clock values, self-reference
or self-hash. Artifact IDs remain outside documents.

Every JSON root contains exactly `schema_version` (1), `kind` (exact descriptor type),
`conventions`, and `payload`. Conventions fix `target_pose` to `T_camera_from_target`,
`transform` to `row-major T_target_from_source`, and `stereo` to
`X_right = R_right_from_left * X_left + T_right_from_left`.

Objects have lexicographically sorted keys; UTF-8 is strict; JSON is compact with no
whitespace. Numbers use nlohmann's shortest round-trip double representation, preserving
IEEE double values including negative zero. Nonfinite numbers are forbidden. Integers
must have the correct signedness/range; dimensions/counts never truncate or wrap.
Arrays preserve typed canonical order. Absent optionals encode as `null`; present
objects encode all their fields, including null optional members. All v1 fields are
required and unknown fields are rejected. Duplicate JSON keys/noncanonical documents
are rejected. Enums use strings, never integer ordinals. No C++ memory layouts are
serialized. The same semantic document yields the same bytes and deterministic packet
content hash on current x86_64/ARM64 builds.

## Payload field contract

IDs encode as `{ "value": string }`, hashes as `{ "algorithm": string, "hex": string }`,
artifact references as `{ "id": Id, "hash": Hash }`, logical references as
`{ "id": Id, "schema_version": uint32, "revision": uint64 }`. CoordinateFrames always
encode both `id` and `name`. Transform objects contain `source`, `target`, and the
16-double row-major `matrix`. M4 has no covariance: supplied covariance is rejected,
and no covariance field is serialized.

All model fields below use their exact C++ field names. Optional fields are always
present as a value or null; no mutable counters or image buffers are added.

### TargetArtifact

`revision`, `target`. Target contains `identity` (id/revision), `grid` (squares_x,
squares_y, nominal_square_size_mm), `pattern` (type/definition), and `measurement`.
Pattern type strings are `checkerboard` / `charuco`; checkerboard definition is `{}`;
ChArUco definition retains dictionary, nominal_marker_size_mm and pattern_layout.
Layouts are `black_square_at_origin` and `white_square_at_origin_even_rows`.
Measurement retains active_width_mm, active_height_mm and provenance. Provenance
retains width_uncertainty_mm, height_uncertainty_mm, instrument and note. A present
empty provenance object still requires measured extents. Physical validity does
not imply backend layout support. Payload identity must equal the reserved revision.

### DatasetArtifact

`revision`, `target_reference`, `raw_capture_references`, `dataset`. Dataset retains
exact target content; analysis config (schema_version, camera_roles,
selection_policy_version, max_selected_per_camera); raw_capture_ids; cameras
(role, camera_id, image_width/height, optical_frame); and every canonical record.
Each record contains key (frame raw_capture_id/frameset_sequence, camera_role,
camera_id), outcome (`detected` / `no_target`), observation, diversity and selection_rank.
Observations retain source, target identity/type, image dimensions, point_ids,
image_points_px, object_points_mm, and detection evidence (detected_points,
detected_markers, partial). Diversity retains centroid_x/y, extent_x/y, variance_x/y,
covariance_xy, visible_fraction. Fixed-point descriptors remain derived by the
unchanged M3 policy from exact stored doubles; they are not a separate mutable field.
No-target, detected/unselected and selected evidence remain distinct.

Sources must be finalized `org.mantis.RawCapture` schema 2 with nonempty final hashes.
References follow raw_capture_ids order, including distinct captures with overlapping
FrameSet sequences. No pixels or packet copies are persisted in the dataset.

### CameraArtifact

`revision`, `dataset_reference`, `target_reference`, `implementation`, `solution`.
Solution retains target, DatasetCamera, MonoSolveConfig, every MonoSample key/rank/
point_count/disposition, training_model, training_fit, heldout_validation, final_model,
and final_fit. Camera model string is `pinhole-brown5`, with fx/fy/cx/cy/k1/k2/p1/p2/k3.

Every mono stage retains views, aggregate residuals, coverage, and optional
opencv_solver_rms_px. Every view retains key, pose, residuals_px, residual summary
and coverage. Pose has rotation_vector_rad, translation_mm, optional
ippe_solution_index and every ippe_solution_rms_px candidate. It represents
**T_camera_from_target**, with radians/mm. Residual summaries retain point_count,
rms_px, mean_px, median_px, p95_px and max_px; coverage retains min_x/y, max_x/y and
bounding_box_area. All original doubles round-trip exactly.

### RigArtifact

`revision`, `dataset_reference`, `target_reference`, `left_camera_reference`,
`right_camera_reference`, `implementation`, `solution`. Solution retains target,
left_camera/right_camera, StereoSolveConfig, both final mono intrinsics, all StereoSample
keys/optional ranks/common IDs/dispositions, training_model/training_fit,
heldout_validation, final_model/final_fit, and rig. StereoModel contains the exact
R_right_from_left, T_right_from_left, E and F arrays. Every stereo stage retains
pairs, aggregate residuals and optional opencv_solver_rms_px. Each pair retains key,
common_point_ids, symmetric_epipolar_residuals_px and residual summary. Rig retains
T_right_from_left, T_rig_from_left, T_rig_from_right, baseline_mm and
relative_rotation_angle_rad, with exact transform frame IDs/names.

Disposition strings are `training`, `held_out`, `ineligible_insufficient_points`,
`ineligible_degenerate_geometry`, `ineligible_missing_detection`.
Implementation records actual opencv_version supplied by the solve caller,
mantis_version (major/minor/patch from application_version), and mantis_build
(actual build_version). Solver/split/model policies remain in the solution. No Git
SHA is invented. Producer identifiers are `org.mantis.calibration.target`, `.dataset`,
`.camera`, `.rig`; producer SemanticVersion is the Mantis application version.

## Immutable graph validation

Dataset depends on target plus every RawCapture; camera depends on dataset plus
target; rig depends on dataset, target, LEFT camera and RIGHT camera. Payload artifact
references carry exact ID/hash and descriptor Provenance.inputs must match their
ordered IDs exactly. Every resolution compares Store::get(id).hash to the reference;
a mismatch is corrupt. Targets must match identity/revision **and complete physical
content**. Camera/rig validators run against the decoded dataset and exact camera
solutions; camera roles, identities, optical frames, image dimensions, final models
and all shared target/dataset references must agree.

Readers require exact descriptor type/schema, FINALIZED, one packet, document packet
version/layout, strict canonical UTF-8 JSON and matching registry/provenance identity.
They run M1/M3/M4 pure validators and recursively validate the dependency graph.
Unknown descriptor/document schemas are incompatible; malformed fields, references,
logical identities or graph content are corrupt. Typed APIs return structured Results.
A valid white-origin target can be persisted independently of OpenCV support.

## Revision registry and metadata migration

Metadata schema 2 adds:

```sql
CREATE TABLE calibration_revisions(
 calibration_id TEXT NOT NULL,
 revision INTEGER NOT NULL CHECK(revision>0),
 artifact_id TEXT NOT NULL UNIQUE REFERENCES artifacts(id),
 kind TEXT NOT NULL,
 PRIMARY KEY(calibration_id,revision),
 UNIQUE(calibration_id,revision,artifact_id));
CREATE TABLE active_calibrations(
 logical_device_id TEXT PRIMARY KEY NOT NULL,
 calibration_id TEXT NOT NULL, revision INTEGER NOT NULL, artifact_id TEXT NOT NULL,
 FOREIGN KEY(calibration_id,revision,artifact_id)
 REFERENCES calibration_revisions(calibration_id,revision,artifact_id));
```

Revision UPDATE/DELETE triggers reject changes. A Store-mutex-protected BEGIN IMMEDIATE
transaction creates an OPEN artifact and its reservation together. A new series uses
a random logical ID/revision 1; existing series allocate max(revision)+1, checked
against SQLite's signed integer limit. Kind remains fixed for the series. Once reserved,
a failed encoding/write consumes the revision and leaves its artifact recoverable;
it is never recycled. Finalization does not change its mapping.

New projects create metadata directly at user_version 2. Opening v1 uses BEGIN IMMEDIATE,
creates both tables/triggers, sets user_version=2 and COMMITs. Exception unwinding rolls
back the entire migration. Opening >2 is incompatible. Manifest format stays 1.
Migration changes no existing artifact/chunk rows, IDs, provenance, hashes, object files
or RawCapture segments. Normal pre-existing startup recovery still applies to provisional
artifacts. Tests construct the actual v1 SQL schema with packet/segmented RawCapture
objects and verify old rows/file/manifest bytes before/after migration and reopen.

New Provenance JSON writes calibration_schema_version. Old rows without it decode
as 1; no migration rewrite is required.

## Activation and capture snapshot

`activate_rig_calibration` performs typed graph/device validation, then Store's
transactional metadata-only activation. Store also exposes generic revision resolution,
active_calibration, activate_calibration and clear_active_calibration seams. Activation
requires a finalized RigCalibration schema 1. Multiple logical devices may reference
one artifact. X1's exact key is `org.mantis.x1:<left physical identity>:<right physical identity>`;
component role/identity metadata is validated where available. X1 identity ordering uses
recorded `left`/`right` component roles; it is independent of configured stereo-role
order and rig handedness. Video node paths are
never calibration identity. Clearing a binding deletes no artifacts.

Runtime samples active binding exactly once before Session begins. Its internal
project_store seam permits persistence tests/internal callers without a public service,
protocol, CLI or SDK operation. Before capture it loads/verifies the rig and validates
current discovered component identities. A mismatch is incompatible before acquisition.

The capture writer stamps the snapshot into every FrameSet and image child using new
packet/header containers and the same BufferView storage. No pixels are copied/modified,
no database lookup happens in acquisition, and no per-frame active lookup happens.
Runtime preview and first-frame pipeline adaptation use that same snapshot.

RawCapture provenance contains existing logical_device_id, active_calibration_id,
active_calibration_schema_version, active_calibration_revision, active_rig_artifact_id,
active_rig_artifact_hash_algorithm and active_rig_artifact_hash; inputs includes the rig
artifact. Once, before first append, it preserves source_device_calibration_id,
source_device_calibration_schema_version and source_device_calibration_revision.
Original parent/children must agree on all three fields and remain stable during the
capture; mismatch fails explicitly. The storage append path stays generic and naturally
records stamped references in initial_observations. Header references and capture-level
provenance agree.

No active binding returns the original Published packet unchanged. Calibration remains
optional for acquisition. Replay reads historical packet headers; it never queries active
metadata. Tests switch activation while capture A runs, verify every A parent/child stays
rev1, future B uses rev2, and replay A/B under rev2 retains rev1/rev2. Pixel hashes match
unbound fake-source capture and direct BufferView/storage addresses remain identical.

## Deferred

M6 owns service/protocol/CLI/SDK calibration operations. M7 owns Studio workflows.
Persistence adds no quality classifications, scanner-accuracy estimates, covariance,
solver changes, project-container version bump or calibration requirement for raw capture.
