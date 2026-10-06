# Real X1 / Q6A geometric calibration validation (M8a)

**Status: M8 pending. M8a supplies acceptance tooling; real characterization is
pending user execution and review. Final M8 acceptance is NOT ESTABLISHED.**
Software/CI PASS validates this harness only. No numerical optical acceptance
thresholds have been frozen. Three completed real sessions can establish
CHARACTERIZATION COMPLETE, while final acceptance remains NOT ESTABLISHED.
An evidence-based envelope belongs in a separately reviewed M8b patch.

## Prerequisites and provenance

Use the native ARM64 Radxa Q6A and real dual OV9281 Mantis X1. Record the exact
Git HEAD, branch, clean/dirty state, build, OpenCV version, OS and kernel; the
harness records these along with physical discovery identities and profile hashes.
M8a starts from `36392f9a786f5e00150fa01f5476bb0fcb12656a` (M0–M7).
Use the final M8a commit from `feature/v0.3-geometric-calibration`, with matching
built daemon, plugins, SDK and validation executable. See [BUILDING](../../BUILDING.md).
Do not substitute a fixture build/report for hardware evidence.

The [accepted Q6A acquisition baseline](x1-q6a-acquisition-validation.md) remains:
LEFT `platform:acb3000.isp/ov9281 18-0060`, RIGHT
`platform:acb3000.isp/ov9281 20-0060`, 1280×720 Y10P, Y10_1X10 upstream,
VBLANK 196, requested 120 FPS, free-running timestamp-nearest correspondence
bounded by 5 ms. Device node numbers are observations, never identities.
1280×800 and hardware exposure triggering remain unvalidated. The final full
5 ms acquisition harness passed at `fad4df6439e88c7ba4f265c532343c3317e93da4`;
M8 does not reopen that policy or certify sustained storage.

Provision the existing kernel/camera permissions and build dependencies first.
Run as the ordinary acquisition user. Stop other users of the selected camera
routes and the external camera-setup service yourself before collecting data;
the harness does not stop unrelated services, reset media, change boot/DTBO
configuration, or reboot. Its private daemon has a fresh port/token. Existing
project locks and camera conflicts fail explicitly. Close Studio/other daemons
using the retained validation project before any offline analysis.

**Camera-only procedure: keep L1/L7 projectors and all lasers OFF.** No laser
hardware is enabled or required. Keep the camera rig mechanically unchanged
through all sessions. Record any suspected change; start a new characterization
series if the rig moves.

Select persistent storage with enough free space and burst write capacity.
Y10P payload is approximately 276.48 MB/s at requested 120 FPS. A 0.25-second
burst is approximately 69 MB before startup/container overhead; 72 such bursts
can consume several GB. Captures include startup as ordinary RawCapture data.
Every burst checks a conservative capacity reserve, but cached short writes do
not prove sustained throughput. Recorder saturation/loss fails the run. Do not
put the sole evidence copy in `/dev/shm`: it disappears on reboot. Pixel data
stays in the retained `.mantis` project, not duplicated in reports.

## Generate the physical reference

From the checked-out M8a source on Q6A:

```bash
unset MANTIS_X1_FAKE
./scripts/validate-x1-calibration.sh --prepare
/usr/bin/python3 tools/generate_calibration_target.py \
  --tool build/release/bin/mantis-x1-calibration-validation \
  --output validation-output/m8-board.svg
```

`--prepare` configures/builds Release Studio OFF and runs software tests. It
creates a timestamped report and an example target, without collecting hardware
or claiming acquisition/calibration acceptance. The second command creates a
stable print filename plus `m8-board.json` and `m8-board.sha256`.

The first characterization fixture is ChArUco 8×6 squares, nominal square 40 mm,
marker 25 mm, DICT_6X6_250, `black_square_at_origin`. The active grid is 320×240 mm.
The A3 landscape page is 420×297 mm; centered actual margins are 50 mm left/right
and 28.5 mm top/bottom (minimum requested margin 20 mm). This is an **M8 validation
fixture**, not the final recommended Mantis product board.

The generator exposes square counts, square/marker sizes, target type, dictionary,
layout, page dimensions and minimum margin explicitly. This version supports
only DICT_6X6_250 and the verified `black_square_at_origin` layout; it rejects
`white_square_at_origin_even_rows`. Marker IDs increase row-major over white
squares, starting at 0. Black origin parity is constructed explicitly, with no
implicit legacy-pattern setting. CI rasterizes actual SVG rectangles and uses
M2 to check physical ChArUco IDs and target-local coordinates. SVG bytes have a
deterministic golden SHA-256 test. Metadata records all parameters, nominal active
extents, actual margins, generator version, OpenCV dictionary provider version,
SHA-256 and the generation command. Analysis verifies bytes against metadata.
No PDF dependency is required: print the vector SVG using a viewer that respects
physical page dimensions.

## Print, mount and measure

Print at **100% / actual size**. Disable fit-to-page and all scaling. Mount the
board as flat and rigidly as reasonably practical. Measure **after printing and
mounting**, from one outer active-grid edge to the opposite outer active-grid
edge. Exclude page margins, substrate and mounting plate. Measure width and height
separately; never substitute the nominal page or grid size.

Create `validation-output/m8-measurement.json` with the actual measured values:

```json
{
  "active_width_mm": 319.8,
  "active_height_mm": 240.1,
  "instrument": "describe the instrument actually used",
  "note": "outer active-grid edges measured after mounting",
  "mounting_note": "describe the actual substrate, mounting and observed flatness"
}
```

The numbers above are format examples: replace both with your measurements.
Optional `width_uncertainty_mm` / `height_uncertainty_mm` may be supplied when
truthfully known; otherwise omit them. An instrument/note may be omitted, but a
mounting note is required. The harness passes the extents and optional provenance
through M1/M6 `PhysicalMeasurement`/`MeasurementProvenance`; the mounting note is
included in the target provenance note and separately retained in the report.
Existing M1 anisotropic X/Y scale is the only global print-scale model used.
The harness never rescales pixels or silently averages extent measurements.
Two global extents do **not** characterize local board warping or curvature.
If the board or measurement changes, use a new target revision and a new series.

## Collect three independent stationary-pose sessions

Choose the real persistent project path; its parent directory must exist. For
example, after creating a validation directory on your actual capture storage:

```bash
./scripts/validate-x1-calibration.sh --characterize \
  --project /mnt/mantis-nvme/M8/Calibration.mantis \
  --profile profiles/x1-q6a.json \
  --target-metadata validation-output/m8-board.json \
  --measurement validation-output/m8-measurement.json \
  --poses 24 --duration 0.25 --max-selected 48 --heldout 4 \
  --determinism
```

This guides sessions a/b/c, solves each using new sources and a new Dataset,
LEFT/RIGHT CameraCalibration and RigCalibration, restarts the owned daemon
between sessions, compares all sessions, evaluates the six ordered independent
pairs, and finally prompts for the activation burst. It reuses the exact physical
Target artifact revision. The initial counts are a practical characterization
protocol, **not** universal quality thresholds. Counts and bursts are configurable;
held-out counts must also satisfy the existing M4 eligibility requirements.
`--determinism` repeats dataset creation and camera/rig solves from exact inputs
in each session. Distinct logical artifact IDs are allowed; meaningful canonical
dataset content/selection and solution values must agree in the same environment.

At each prompt you may reposition. Press Enter **only once the target is stable**,
then hold still until “Burst finalized; you may reposition.” Each burst is an
ordinary finalized RawCapture schema 2; M3 consumes multiple captures. Free-running
correspondence is sufficient for a stationary planar board; moving the board
between paired exposures can introduce additional stereo error. This is not
hardware synchronization, nor an optical exposure-skew measurement.

Aim for roughly 20–30 diverse poses per session: center, left/right edges,
top/bottom, all four corners, near/medium/farther distances, fronto-parallel,
positive/negative yaw and pitch, and combined oblique poses. Keep sufficient
visibility in both cameras. Partial ChArUco visibility is valid. Do not collect
hundreds of near-identical images or try to hit exact physical coordinates.
There is no invented live quality score or coverage PASS threshold.

Sessions must use separately captured sources. Re-solving one Dataset three
times is not repeatability. Source RawCapture content hashes must be disjoint
across sessions, using the complete Store hash identity (algorithm and digest).
Copied/imported identical captures remain shared content even with different
artifact IDs or projects and are rejected by repeatability and fixed-calibration
cross-validation. Missing/empty finalized source hashes are structural failures.
Finalize all captures and finish calibration before the next daemon start.
`--cold-boundary` additionally pauses after daemon stop
for an operator-selected stronger boundary; it never reboots/power-cycles the Q6A.
Keep the rig unchanged and the target measurements valid across that boundary.

## Collect separately or rerun analysis

`--session NAME` records/solves one session. Supply the same project, target
metadata and measurement options as above. For b/c additionally use
`--target-id ID` from session a's JSON; this preserves the exact Target revision.
Each invocation stops its own daemon. Reports are timestamped under
`validation-output/x1-calibration-YYYYMMDD-HHMMSS-microseconds/`.

After collecting, no camera access is needed to repeat analysis:

```bash
./scripts/validate-x1-calibration.sh --analyze \
  --session-input validation-output/REPORT_A/session-a.json \
  --session-input validation-output/REPORT_B/session-b.json \
  --session-input validation-output/REPORT_C/session-c.json
```

Replace REPORT_A/B/C with the actual directory names (the orchestrated run stores
all three in one report). Keep the retained projects at the recorded absolute
paths, or update only the session `project` paths when moving projects to another
machine. Analysis reloads and validates immutable artifacts/hashes/lineage. It
requires an exact shared physical Target revision, matched camera identities and
geometry, and independent sources. Canonical session names determine ordering.
A report can be analyzed on a software build without Q6A hardware; original
collection environment/provenance stays in each session. Close the project daemon
first: Store enforces exclusive project access.

To build and solve a dataset noninteractively from existing sources use
`--session NAME --raw RAW_ID --raw RAW_ID ...` with `--project`, `--target-id`,
`--target-metadata` and `--measurement`. No new capture is made. This mode verifies
finalization, integrity/replay and physical provenance but **cannot establish
acquisition integrity** from an absent live recording report. Fixture CI uses
`--evidence-mode fixture`; fixture reports can never assert real acquisition or
final M8 acceptance. Fake backends are rejected independently by environment,
discovery and retained source provenance checks in real collection/analysis.

## Activation and historical binding

The orchestrated run includes this check. To repeat it independently:

```bash
./scripts/validate-x1-calibration.sh --activation \
  --project /mnt/mantis-nvme/M8/Calibration.mantis \
  --profile profiles/x1-q6a.json \
  --session-input validation-output/REPORT/session-c.json
```

The real discovered device must match physical identities and 1280×720 geometry.
The SDK activates the exact Rig artifact, queries its exact logical ID/schema/
revision, prompts for a NEW stationary real capture, finalizes and verifies it.
It then clears active calibration and verifies the historical capture again.
Clearing is an actual active-state change: replay must preserve the old exact
revision rather than reinterpret the capture. The prior active state is restored
on exit. Offline inspection checks the parent and both children on every record,
RawCapture provenance binding, original source-device/plugin calibration
provenance, physical identity and image geometry. Exact query/recorded revision
agreement and replay/binding integrity are hard contracts, not optical thresholds.
`activation.json`, `active-query.json` and `activation-acquisition.json` retain
these results. The original profile calibration ID/revision is preserved, including
an empty/unavailable source ID; the harness never edits the profile.

Ctrl+C or TERM stops only the owned child/daemon with bounded escalation and
keeps all projects/logs, finalized captures and partial diagnostic artifacts.
A stalled daemon may leave recoverable data rather than finalize it; inspect it
using the existing explicit recovery procedure. No real data is deleted. Failed
active-state restoration is reported as a cleanup failure. No unrelated process
is killed or service state changed.

## Optional Checkerboard mono cross-check

Recommended additional characterization, not a blocker for initial full-rig
acceptance. Generate a separate board using the generator's
`--target-type checkerboard` option, measure its physical extents after mounting,
and collect a separate `--session checker` with its own metadata/measurement and
a new Target artifact. Both mono cameras are solved; no Checkerboard Rig is
requested. Preserve its report/project. Add
`--checkerboard-session /absolute/path/session-checker.json` to the three-session
`--analyze` command to describe coefficient deltas against every ChArUco session.
The camera identities and image geometry must match. No Checkerboard physical
orientation matching or stereo correspondence is invented. ChArUco is the
mandatory v0.3 full stereo path.

## Evidence and interpretation

Session evidence retains target geometry/measurement provenance, exact revision,
source captures, camera identities/dimensions, analyzed/detected/selected records
and counts, fx/fy/cx/cy and Brown5 k1/k2/p1/p2/k3, training/held-out/final residual
summaries and image coverage/sample counts. Rig evidence includes R/T right from
left, both rig transforms, baseline/relative angle, epipolar residual summaries
and pair counts. Real X1 rig solves supply exactly `org.mantis.x1.rig` /
`Mantis X1 rig`, with the existing right-handed +X right, +Y forward, +Z up
M4 derivation. Session validation, retained-session analysis and activation input
validation inspect the actual immutable solution: its configured rig frame and
both `T_rig_from_left` / `T_rig_from_right` targets must have that exact ID and
name. Other frame identities are incompatible with real X1 M8 evidence; old
immutable artifacts are never retargeted. Full canonical immutable documents
retain finer evidence.

`repeatability.json` reports every unordered pair's B−A coefficient/baseline
deltas, absolute/relative focal changes, translation difference norm in mm and
the angle of `R_B * R_A^T` in radians/degrees. Statistics report min/max/mean/sample
standard deviation for scalar parameters. `cross-validation.json` reports every
ordered A→B pair using fixed A intrinsics/distortion and fixed A stereo R/T/F on
B's independent observations. Only mono target pose is estimated using M4's
existing helper; intrinsics and stereo geometry are never refitted. ChArUco
correspondences intersect physical IDs using the existing M4 convention.
The evaluated observations are selected, solver-eligible mono records and the
M4 stereo candidate union. Usable and skipped counts are explicit.

High finite optical residuals/variation are **CHARACTERIZATION**, requiring review;
they do not fail M8a or establish scanner performance. Structural failures include
fake/non-ARM real collection, wrong identity/geometry/profile, pairing/native
capture errors, loss/saturation, nonfinalized/corrupt captures, absent usable
detections, solve failure, invalid lineage/nonfinite parameters, incompatible
activation, exact binding/replay changes, invalid physical target metadata and
failed cross-validation computation. No arbitrary reprojection/epipolar,
intrinsics/distortion/baseline/angle envelope has been invented.

Troubleshoot the first failing stage in `summary.json`, daemon logs, per-burst
logs and `commands.log`. Check storage, permissions, competing camera users,
profile/read-backs, target hash/print size, stationary handling and pose diversity.
An insufficient eligible/held-out split is a solver execution failure: acquire
additional diverse poses or explicitly select a suitable analysis budget; do not
hide failure with synthetic artifacts. Preserve failed projects for diagnosis.
Do not edit M1–M7 semantics to make this harness pass.

Send back **every report directory** used for preparation, sessions, analysis and
activation, the exact printed SVG/JSON/SHA-256, the physical measurement JSON,
and any operator notes (rig stability/cold boundary/mounting). Retain the complete
`.mantis` project directory, including artifact objects, database and diagnostics.
Initially share the small reports and project paths/storage inventory; supply the
retained projects for reproducibility/offline review via suitable large-data
transfer when requested. Do not send only screenshots or omit failed bursts.
See the [machine-readable report schema](../validation/x1-calibration-report-schema.md).

M8 proves no laser-plane calibration, laser extraction, triangulation, scanner
point accuracy in mm, metrology performance, tracking, fusion, meshing, RGB
texture or hardware trigger implementation. Camera/rig geometric calibration
alone makes none of these claims. No release/version bump follows from M8a.
