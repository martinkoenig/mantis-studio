# X1 calibration characterization report schema v1

All JSON reports use UTF-8, explicit field names, finite double-precision numerical
values and `schema_version: 1`. Serialization uses sorted object keys and stable
session ordering. Full calibration artifact documents preserve the accepted M5
schema unchanged. No report changes a public protocol or artifact schema.

`summary.json` and `summary.txt` record:

| Field | Meaning |
| --- | --- |
| evidence_mode | `real` or `fixture`; never inferred from numerical results |
| software_harness | `PASS`, `FAIL`, or `NOT ESTABLISHED`; software execution only |
| acquisition_integrity | Live reference acquisition `PASS` / contract `FAIL` / `NOT ESTABLISHED` |
| calibration_pipeline_execution | Public API dataset/solve execution and valid immutable lineage |
| activation_binding | Exact device/query/parent/children/provenance revision agreement |
| deterministic_replay | Capture integrity and matching replay verification results |
| characterization | `COMPLETE` after at least three independent ChArUco datasets and all pair analyses; otherwise `FAIL` / `NOT ESTABLISHED` |
| final_m8_acceptance | **Always `NOT ESTABLISHED` in M8a**, in real and fixture mode |
| failures | Array of `{stage, reason}`; nonempty forces software_harness FAIL |
| cleanup_errors | Owned-resource/state restoration failures; force harness FAIL |
| started_utc, output | Timestamp and absolute report directory |

Fixture mode cannot report real acquisition PASS. Even all-green synthetic tests
or completed real characterization cannot set final acceptance PASS. No optical
PASS/WARN/FAIL thresholds exist in this schema version.

`environment.json`: evidence_mode, uname_m, kernel, verbatim OS release, git_head,
git_status (including dirty state), profile_path/profile_sha256, fake_state
(absent is null), build_dir, validation_tool_sha256, linked opencv_version and retained project path. Fixture-only environments
without Git may record UNAVAILABLE. Native real collection requires an observable
Git HEAD. OpenCV and Mantis application/build metadata live in each immutable
camera/rig document's `payload.implementation`; generator provider version also
lives in target metadata. Discovery is independently recorded in `devices.json`.

`target.json` / `target.sha256`: generator (`mantis-m8-svg`), generator_version,
exact `specification` (target_type, squares_x/y, nominal_square_size_mm,
nominal_marker_size_mm, dictionary, layout, page_width/height_mm, margin_mm), SVG
SHA-256/path, generation_command, expected active_grid_width/height_mm, actual
margins_mm, marker ID ordering and OpenCV provider version. The SHA-256 covers
exact SVG bytes, excluding the companion metadata. `measurement.json` records
physical active_width/height_mm, optional uncertainties/instrument/note, and
required mounting_note. The authoritative measured geometry/provenance also
lives in the exact M1 Target revision, with the mounting note appended to note.

`session-NAME.json`: schema_version, name, evidence_mode, project, target/dataset/
left/right/rig artifact IDs (rig null for Checkerboard), collection environment,
target_metadata, measurement, acquisition reports and boundary description.
`evidence` contains canonical target/dataset/left/right/rig documents, validated
artifact descriptors and raw_captures descriptors (ID, hash, type/schema/state,
provenance parameters). Documents include exact revision, upstream artifact
references/hashes, measurement, camera parameters, all stage residual/coverage
summaries, partitions and per-observation evidence. Dataset records permit exact
analyzed/detected/selected count derivation; no pixels are embedded. Session
boundary descriptions distinguish the automatic daemon restart from the
operator's mechanically unchanged rig assertion. Real X1 RigCalibration content
requires `payload.solution.config.rig_frame` and the target frames of
`payload.solution.rig.T_rig_from_left` / `T_rig_from_right` to equal
`{"id": {"value": "org.mantis.x1.rig"}, "name": "Mantis X1 rig"}`.
Session, retained-analysis and activation input validation check Store-loaded
immutable content; filenames and solve arguments are not evidence of compliance.
The existing right-handed +X right, +Y forward, +Z up derivation is unchanged.

`session-NAME-captures.json` is an incremental per-burst journal, including normal
SDK capture status/diagnostics and two-pass integrity/replay result. Failed runs
retain the journal, per-burst logs and project even if the session cannot solve.
`capture-NAME-NN.log` retains acquisition validator output/error. Noninteractive
sources have verification evidence and acquisition_integrity NOT ESTABLISHED.
Session manifests allow retained-data analysis without recapture. source_capture_count
and dataset_counts (analyzed/detected/selected per role) provide direct totals.
Unreached session-a/b/c files are explicit NOT ESTABLISHED placeholders;
target-artifact.json records the target ID and measurements before the first burst.

`repeatability.json`: classification CHARACTERIZATION, units, session_order,
`pairs` and `statistics`. Unordered pairs are lexicographically ordered; signed
deltas are B minus A. Focal values include signed delta, absolute_delta_px,
relative_delta `(B−A)/A`, absolute_relative_delta; principal-point deltas use px,
Brown5 deltas are dimensionless. Rig includes baseline_delta_mm,
translation_difference_norm_mm and relative_rotation_difference angle_rad/
angle_deg derived from `R_B * R_A^T` (validated rotations, clamped trace and
robust atan2 extraction). Statistics include min/max/mean/sample_stddev for camera
coefficients, baseline_mm, relative_rotation_angle_rad and translation axes in mm.
Independent sessions require pairwise disjoint source RawCapture content hashes,
in addition to existing project/artifact identity checks. Descriptor `hash` is
the existing `algorithm:digest` representation of Store Hash; both components
form content identity. Different projects/IDs cannot make identical source
content independent. Analysis reloads descriptors from retained Store; the C++
fixed evaluator queries Store descriptors directly, ignoring session JSON hashes.
Missing/empty finalized source hashes are structural failures.
Sample standard deviation uses n−1. No matrix-entry delta is presented as the
primary rotation metric.

`cross-validation.json`: classification CHARACTERIZATION and ordered_pairs,
canonically ordered by calibration session then independent dataset session.
Each entry has schema_version, calibration_session, dataset_session,
parameters_fixed=true, left/right `{usable_observations, skipped_ineligible,
residuals}` and rig `{usable_pairs, skipped_ineligible, residuals}`. Residuals
have point_count, rms_px, mean_px, median_px, p95_px, max_px using existing M4
aggregation. Only selected eligible mono records and M4 stereo candidates are
evaluated. Same-dataset/shared-source evaluation is rejected. No intrinsics,
distortion, stereo R/T/F or physical target geometry is refitted.

`activation.json`: status PASS requires finalized schema-2 capture, positive
framesets, exact `binding` `{id: {value}, schema_version, revision}`, raw descriptor,
real evidence mode, active_query, replay_before/replay_after and
historical_active_change. The query's public reference ID is a plain string;
M5 canonical document IDs retain `{value}`. `active-query.json` and
`activation-acquisition.json` retain independent public API/discovery evidence.

`determinism.json`: status, original/repeat artifact IDs and checks per dataset,
left/right/rig. Compare meaningful dataset content (including selection) and full
solutions; allocation IDs can differ. Scope is the recorded same OpenCV/build,
not cross-version bitwise determinism. `checkerboard-comparison.json` optionally
contains named ChArUco/Checkerboard pairs, camera role and all coefficient deltas,
classified CHARACTERIZATION with no stereo attempt.

Unreached analysis/activation/determinism files contain schema_version and status
NOT ESTABLISHED. Logs include commands.log and daemon-NAME.log (a daemon.log
placeholder makes absence of daemon work explicit). A report references retained
projects rather than duplicating large RawCapture payloads. Consumers must treat
missing/unreached evidence as unavailable, never infer PASS from an empty report.
