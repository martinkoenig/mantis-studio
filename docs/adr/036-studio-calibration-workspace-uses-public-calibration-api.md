# ADR-036: Studio calibration workspace uses the public calibration API

Status: Accepted for v0.3 M7

## Context

M0–M6 supply geometric calibration, immutable artifacts, exact lineage, daemon
jobs and public controls. Studio needs a usable device workflow without owning
calibration computation or introducing a second persistent source of truth.
The older acquisition shell remains functional while the full product shell
is deferred.

## Decision

1. Studio calibration is a client of M6, through `mantis-client`, Protocol v1,
   `mantisd` and `CalibrationService`. It never accesses Store, Runtime, SQLite,
   OpenCV or private calibration implementations.
2. `CalibrationController` owns transient presentation state and converts typed
   compact wire evidence. No calibration mathematics lives in Studio or QML.
3. Calibration lives in **Devices / System**, opening a dedicated Calibration
   workspace. It is not a Process, Inspect, Reverse or Automate operation.
4. Seven freely navigable stages form a persistent guided workspace, rather than
   a modal destructive wizard. Moving between stages deletes no artifacts.
5. Immutable Target, Dataset, CameraCalibration and RigCalibration revisions,
   listed/inspected through M6, are the restart/resume source of truth. Studio
   adds no workflow tables, project schema or hidden lineage persistence.
6. Dataset, independent LEFT/RIGHT camera solves and Rig solve remain normal
   daemon Jobs. Studio retains their IDs and observes snapshots; it never waits
   for a solve on the UI thread. Control requests run through QtConcurrent.
7. Activation is explicit, confirmed against the selected device and exact Rig
   artifact/revision, and separate from solving. Clear removes only the binding.
   The daemon remains authoritative for identity and geometry incompatibilities.
8. Current active calibration affects **future captures only**. Historical
   captures retain the exact calibration revision recorded with them.
9. Training, held-out and final-fit evidence is displayed descriptively. There
   is no invented quality score, PASS/WARN/FAIL threshold, scanner-accuracy or
   metrology claim. Pixel residuals are not scanner accuracy. Matrices and full
   identity references are available in expanded engineering details.
10. M7 has no Free/Pro distinction, registration, license or entitlement check.
11. M8 remains physical-hardware calibration acceptance. Synthetic, protocol and
    frontend tests do not establish real X1 accuracy, repeatability, target
    manufacturing tolerance or recommended board/dictionary parameters.

## Consequences

Capability-based discovery permits compatible third-party devices. X1 may supply
its known `org.mantis.x1.rig` presentation suggestion; other devices require
explicit frame ID/name. ChArUco supports the complete workflow; Checkerboard
remains visible and supports mono only, with its stereo limitation explained.
The normal capture service and existing bounded dual grayscale preview are reused.
Frontend transient selections may be lost on restart; selecting an existing Rig
restores exact upstream references without recomputation.

Focused controller tests, a real-daemon integration and an offscreen QML smoke
exercise these boundaries. Studio OFF retains Qt-free daemon/client/calibration
builds. See [Calibration workspace](../architecture/calibration-workspace.md).
