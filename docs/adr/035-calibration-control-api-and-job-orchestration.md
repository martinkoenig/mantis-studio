# ADR-035: Calibration control API and job orchestration

Status: Accepted for v0.3 M6

## Context

M1–M5 implement target geometry, detection, deterministic datasets, numerical solves,
immutable artifacts and project-local capture bindings. Headless clients need these
stages through the daemon architecture without acquiring storage or solver ownership.

## Decision

1. `CalibrationService` exposes M1–M5 through pure typed DTOs. Existing target,
   dataset, solver, artifact, revision and active-binding APIs remain authoritative.
2. Target creation is synchronous, as are listing, compact inspection, current
   activation query, activation and clear. Dataset, Camera and Rig creation use
   the existing bounded daemon Job manager and return its Job ID.
3. One camera solve job solves one exact role and publishes at most one artifact.
   A rig job consumes two exact CameraCalibration artifacts, the exact dataset
   and explicit left/right request slots. Slot order defines handedness.
4. There is no monolithic calibrate-everything primitive. The caller composes the
   stages and independently reusable immutable intermediate artifacts.
5. Protobuf v1 remains a compact, 4 MiB control plane. Add Request tags 32–40 and
   Response tags 13–15; existing numbers remain frozen. Typed calibration info
   reports operational parameters and compact evidence, not observation populations,
   image/object points, pixels or per-point residual vectors. Full documents are
   an artifact/data-plane concern.
6. Target requests contain geometry/pattern/measurement only. Protobuf scalar and
   message presence preserve absent versus present empty measurement provenance.
   Identity and revision allocation belong to M5 persistence; an optional existing
   series requests its next revision, never an arbitrary caller revision number.
7. Rig solving never activates its output. Explicit activation validates current
   discovered components with the accepted strict parsing and M5 compatibility
   path. Offline activation remains project-local; capture start always revalidates.
   Clear removes the mutable binding and deletes no artifacts.
8. Current active query describes configuration for future capture. Historical
   RawCapture/replay calibration is never resolved from the current active binding.
9. Plain Checkerboard supports dataset and mono solving. Stereo/rig requests retain
   M4 `Status::incompatible`; ChArUco supports the full path. No orientation workaround,
   invented covariance, quality gate or scanner-accuracy claim is introduced.
10. Ordinary calibration belongs to the Free/open platform. There are no licensing,
    entitlement, registration or product-tier checks.
11. M3 receives the daemon cancellation token. OpenCV M4 calls remain non-preemptive;
    cancellation is observed before/after the solve and before persistence, preventing
    publication when cancellation is observed at those boundaries. No instantaneous
    interruption is claimed. M5 persistence itself is not preempted once entered.
12. M7 will compose these same public service/protocol/SDK contracts in its UI.
    M6 implements no Studio workspace. M8 real-hardware acceptance remains pending.

## Consequences

SDKs and CLI communicate only through the protocol, with explicit Job waits.
Synchronous preflight rejects already invalid references/configuration before
queueing where practical. Genuine numerical failures remain Job failures. A failed
one-artifact persistence leaves upstream immutable artifacts unchanged. Calibration
operations add no frame-thread processing, database lookup or pixel copy.

See [Calibration API](../architecture/calibration-api.md) for concrete contracts,
wire bounds, error behavior and examples.
