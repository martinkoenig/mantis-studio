# Studio calibration workspace (M7)

M7 composes the [public M6 API](calibration-api.md). It adds presentation and
workflow, preserving M1–M6 target, detection, selection, solver, artifact, binding
and protocol contracts. [ADR-036](../adr/036-studio-calibration-workspace-uses-public-calibration-api.md)
records the decision. M8 real-hardware acceptance remains pending.

## Location and navigation

The incremental shell exposes **Scan / Acquisition** and **Devices / System**.
Devices with composite FrameSet-stream capability and image-stream children
expose **Calibrate**. Single-image sources currently record schema 1 and do not
expose this schema-2 recording workflow. Plugin branding does not determine
availability. The action preselects the logical device and opens the dedicated
workspace. An offline entry permits solving from existing captures without a
connected device. Activation requires a selected logical device.

The existing acquisition, preview, point cloud, processing recipes, artifact
replay/export, plugin, job and diagnostic views remain available. This milestone
implements no placeholder future Home/Process/Inspect/Reverse/Automate workspaces.
The stale presentation-only v0.2 subtitle is removed; application/package/plugin
versions are unchanged.

## Seven stages

| Stage | User action and evidence |
| --- | --- |
| Device | Inspect name, logical ID, discovered/runtime state, capabilities and exact current active binding; select scanner or work offline |
| Target | Select a finalized revision or create ChArUco/Checkerboard with editable square counts, nominal size, ChArUco marker size/dictionary/exact layout, optional measured active extents and explicit provenance presence |
| Captures | Record ordinary RawCapture through CaptureService, reuse dual grayscale preview, stop and observe finalization, or select multiple finalized schema-2 captures |
| Dataset | Select a finalized Dataset or submit a build with exact `left`/`right` roles and editable `max_selected_per_camera`; review per-camera identity/geometry, analyzed/detected/no-target/selected counts, record/source counts and selection policy |
| Cameras | Select exact-Dataset CameraCalibration revisions or independently solve/retry LEFT/RIGHT; review training/held-out/final RMS, median, p95, maximum in px, coverage, optional solver RMS and implementation provenance |
| Rig | Select an existing Rig or solve with exact Dataset/LEFT/RIGHT slots, explicit rig frame and held-out pair count; no activation occurs |
| Review & Activate | Review baseline mm, relative rotation radians, three residual summaries/pair counts and identity lineage; expand named transforms, then explicitly confirm activation or clear the current binding |

The stage rail permits arbitrary return/navigation. Changing upstream selections
can deselect incompatible downstream inputs, while all immutable artifacts remain
available. Selecting a Dataset restores its exact Target. Selecting a Rig restores
its exact Target, Dataset and LEFT/RIGHT CameraCalibration references. A job whose
inputs were superseded still publishes its immutable artifact, but does not replace
current selections.

ChArUco is the initial form choice, with both exact layout values available:
`black_square_at_origin` and `white_square_at_origin_even_rows`. The dictionary
field is editable: convenience choices are not a declaration of daemon support.
Checkerboard remains visible and supports Dataset/mono. Stereo/rig requires
physical correspondence IDs and therefore ChArUco in v0.3; no orientation inference
is introduced. Defaults are editable suggestions, not a Mantis board standard or
quality threshold. The daemon validates all physical/backend/solve constraints.

Measured extents describe the **active grid**, excluding substrate and margins.
Opening Advanced has no semantic effect. The explicit Include provenance checkbox
preserves absence versus present empty provenance; nonempty fields are forwarded
with their optional presence. The presentation converter can also preserve supplied
empty optional strings. Target identity/revision is allocated and returned by M6.

## Controller and QML boundary

`apps/studio/calibration_controller.{hpp,cpp}` defines `CalibrationController`,
an internal QObject presentation controller. `calibration_client.hpp` defines the
small injectable control seam and production `PublicCalibrationClient`, which
forwards exclusively to public C++ Client methods. The seam exists for focused
presentation tests, never for an alternative engine or persistence path.

The controller owns selection IDs, capture choices, stage index, request slots,
normal daemon Job IDs/results, exact-reference compatibility, typed QVariant
conversion and structured errors. QML primarily renders state and forwards intent;
its JavaScript assembles optional form fields and formats values. There is no
calibration computation or domain-rule duplication in QML.

`ui/workspaces/CalibrationWorkspace.qml` composes a seven-stage rail, scrollable
stage panels and a compact revision inspector. `DevicesWorkspace.qml` supplies the
system entry. Small `ui/calibration` components render selectors, errors, jobs,
evidence and stage content. Forms remain instantiated while navigating, avoiding
a destructive wizard. At the minimum desktop width, the optional side inspector
becomes an expandable browser below the stage; selectors remain usable. Dark panels and restrained
Mantis green match the existing shell. No new generic UI framework is introduced.

## Public calls and jobs

- `create_calibration_target`: typed specification with exact physical layout and
  optional measurement/provenance; returned artifact/logical ID/revision shown.
- `calibrations`, `calibration_info`: revision browser and typed compact evidence.
  Open/incomplete artifacts are listed without decoding and cannot be inputs.
- `build_calibration_dataset`: exact Target, deduplicated/bounded finalized schema-2
  RawCapture IDs (maximum 1024), explicit roles, editable selection budget.
- `solve_camera_calibration`: one exact Dataset and one exact role per request.
  The convenience action submits two distinct calls. Each side retains its own job,
  artifact and diagnostics. One side failing never erases the other result.
- `solve_rig_calibration`: exact Dataset plus explicit selected LEFT and RIGHT IDs.
  X1's known frame suggestion is device-specific; others use explicit fields.
- `active_calibration`, `activate_calibration`, `clear_calibration`: metadata-only
  binding controls with explicit user activation and refreshed current binding.
- Existing `start_capture`, `stop_capture`, snapshot and Job cancellation commands
  retain normal capture/job semantics. Recording creates no special calibration
  format, and new captures are not ready until FINALIZED.

All network work runs through QtConcurrent/QFutureWatcher. One refresh request and
at most one request per fixed operation slot are outstanding, with coalesced refresh
intent and no frontend request queue. Ordinary Studio snapshots feed device/capture/
job state; a bounded 2-second workspace refresh obtains list/info/active state and
snapshots. Immutable compact info is cached while its artifact remains listed.
Preview polling remains the existing 66 ms, latest-only, one-watcher path; M7 adds
no frame RPC, frame queue, pixel copies into QML or full-resolution poll increase.

Jobs map Queued/Running/Completed/Failed/Cancelled independently, with progress,
status and useful diagnostics. Completion resolves `result_artifact` using M6 info
and binds it only when its submitted inputs still match the current selections.
Cancellation sends the existing job_cancel command and awaits daemon state; it
never clears artifacts or other jobs. OpenCV cancellation remains non-preemptive
as documented by M6. There is no `Client::wait()` in Studio.

Running jobs do not make Studio globally busy. Navigation, inspection, cancellation
and unrelated views stay usable; only conflicting submission controls are disabled.

## Resume, errors and activation

No frontend workflow table, mutable project schema or second lineage store exists.
After restart, manually select existing Target/Dataset/Camera/Rig revisions through
public list/info. Camera choices require exact Dataset artifact references and
explicit roles; filenames/timestamps/discovery positions never determine lineage.
Open revisions remain visible for diagnostics and are excluded from input selectors.

Synchronous failures retain daemon component/status/message and appear in the
relevant stage. Normal Job failures display their state/status/diagnostics as
provided by the existing Job contract, which has no separate structured error DTO.
Invalid target, non-finalized capture, failed Dataset or mono solve, unsupported
Checkerboard stereo, cancelled job, identity/image geometry incompatibility and
daemon unavailability remain distinguishable. Upstream artifacts survive failures.

Activation confirms the selected logical device, exact Rig artifact and revision.
Stale confirmation IDs are rejected by the controller. Daemon identity/image geometry
errors are shown prominently and never bypassed. Success queries current binding;
Clear is a separate explicit confirmation and never deletes artifacts. Active
calibration applies to future captures only. Historical captures retain their
recorded exact revision; M7 offers no rebinding of old captures.

## Evidence and acceptance limits

The normal view emphasizes device/active revision, stage intent, descriptive counts,
pixel residuals, baseline and relative rotation. Full identity references and
`T_right_from_left`, `T_rig_from_left`, `T_rig_from_right` row-major matrices appear
in expanded Engineering details. Residuals are not scanner accuracy. There are no
quality percentages, green PASS badges, metrology gates or accuracy-in-mm claims.
M7 is Free/open, without registration, licenses, Pro badges or reduced quality.

M8 must establish real X1 capture/board repeatability and hardware calibration
acceptance. M7 freezes no physical target dimensions, dictionary, manufacturing
tolerance or calibration-quality thresholds, and does not claim full v0.3 acceptance.

## Testing and validation

Studio ON adds:

- `studio-calibration-controller`: typed target requests, both layout/presence
  semantics, bounded/deduplicated capture choices, independent jobs/results,
  partial failure/retry/cancel, exact Rig slots, no implicit activation, active
  mapping, exact-reference choices, structured errors and resume.
- `studio-calibration-daemon`: controller → mantis-client → Protobuf → real mantisd;
  public list/create/build/job observation/info, independent camera/rig solving,
  new recording/finalization/selection, real activation incompatibility, activation
  query/activate/clear and fresh-controller resume. It reuses deterministic
  M6 fixtures and requires no hardware. A UI heartbeat runs during daemon work.
- `studio-calibration-qml`: offscreen full shell/workspace load, controller bindings,
  seven stages, minimum/normal desktop widths and no QML binding/type warnings.

The existing 35 suites remain intact. CMake walks actual transitive dependencies
for `mantis-studio` and `mantis-studio-calibration`, rejecting services, adapter,
Runtime, Store, compiled calibration modules, SQLite and OpenCV. Include checks
also reject these private libraries. Studio OFF introduces no Qt dependency.

M7 adds zero per-frame database operations, zero calibration processing in QML,
zero RawCapture pixel transport through Protobuf, no unbounded frontend queue and
no long-running solve on the UI thread.

### Local validation, 2026-10-06

M0–M7 implemented; M8 pending. The complete existing suite remains intact, with
three new Studio-only suites:

| Configuration | Studio | Complete suite | Wall time |
| --- | --- | --- | --- |
| Debug | ON | 38/38 PASS | 81.49 s |
| Debug | OFF | 35/35 PASS | 67.52 s |
| Release | ON | 38/38 PASS | 63.50 s |
| Release | OFF | 35/35 PASS | 52.83 s |
| Debug ASan + UBSan | ON | 38/38 PASS | 174.18 s |
| Debug ASan + UBSan | OFF | 35/35 PASS | 151.27 s |

Local environment: Ubuntu 25.10 x86_64, GCC 15.2, Qt 6.9.2, Protobuf 3.21.12,
OpenCV 4.10.0. Missing development packages were extracted into this worktree's
ignored `build/deps`; other worktrees and system packages were not modified.
Studio/QML tests use offscreen software presentation. Sanitizers use leak detection
and halt-on-UB, without suppressions. As in M6 local validation,
`OPENCV_OPENCL_RUNTIME=disabled` avoids the installed CUDA OpenCL loader's
initialization leak; calibration uses its ordinary CPU path.

Actual transitive CMake dependency checks and the include boundary suite pass.
The linked Studio executable has no OpenCV/SQLite dynamic library and no private
service, Store, Runtime, OpenCV or SQLite implementation symbols. The real-daemon
controller test preserves a UI heartbeat during recording and calibration jobs,
checks capture finalization and surfaces real device/calibration incompatibility.

The final branch HEAD must also pass all five existing Ubuntu 24.04 GitHub CI jobs:
x86_64 Studio ON/OFF, ARM64 Studio ON/OFF and ASan/UBSan. This software validation
is not full v0.3 or M8 physical-hardware acceptance.
