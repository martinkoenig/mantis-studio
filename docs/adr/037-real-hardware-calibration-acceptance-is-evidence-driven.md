# ADR-037: Real hardware calibration acceptance is evidence driven

Status: Accepted for v0.3 M8a tooling; real M8 acceptance pending

## Context

M0–M7 implement geometric calibration contracts and public client workflows.
Synthetic fixtures validate mathematics and software boundaries but cannot prove
repeatability on the real Q6A/OV9281 optical system. Numerical quality thresholds
remain unfrozen until real physical observations justify an envelope.

## Decision

1. M8 requires user-executed real Radxa Q6A / dual OV9281 X1 measurements and
   retained artifacts. Reports identify real versus fixture evidence explicitly.
2. CI/software PASS establishes harness validity only, never hardware acceptance.
3. Initial M8a runs characterize finite optical metrics without invented limits.
   Structural and accepted acquisition contracts retain hard failure semantics.
4. Use at least three independent ChArUco datasets, separately captured and
   solved with an unchanged rig. Restart the validation daemon between sessions;
   do not create independence by re-solving one dataset.
5. Compare session intrinsics, distortion and rig geometry. Use relative rotation
   angle, translation difference norm and scalar sample statistics with units.
6. Evaluate each fixed calibration on each other independent dataset, estimating
   only mono target pose and using fixed stereo geometry and physical point IDs.
   Validation helpers reuse M4 conventions; no production solver contract changes.
7. Stationary-pose bursts reduce avoidable moving-board stereo error under the
   accepted 5 ms free-running correspondence policy. They do not establish
   hardware synchronization or optical exposure skew.
8. Generate a reproducible vector target with exact physical layout and hash.
   Measure active extents after printing/mounting, excluding margins/substrate,
   and persist measurements/provenance through M1/M6. Optional uncertainty is
   never fabricated; two extents do not characterize local warping.
9. ChArUco physical IDs are the mandatory v0.3 stereo path. The initial A3 8×6,
   40/25 mm DICT_6X6_250 fixture is not a frozen product recommendation.
10. Checkerboard remains mono-only descriptive characterization. No orientation
    matching or Checkerboard Rig solve is introduced; it is initially optional.
11. Activation and historical capture binding retain exact M5 ID/schema/revision
    semantics. Real future capture parent/children, source provenance and replay
    must preserve the bound revision after active state changes.
12. Geometric camera/rig calibration implies no scanner-mm accuracy or metrology
    claim. Laser calibration/extraction, triangulation, tracking/fusion/meshing,
    texture and hardware trigger implementation are outside M8.
13. Freeze final thresholds only after real evidence review in a separate M8b
    patch. M8 remains pending and final_m8_acceptance NOT ESTABLISHED throughout
    M8a, regardless of CI or characterization completion.

## Consequences

Repository-owned tooling lives in scripts/tools and an internal validation-only
executable, with no Store/OpenCV dependencies in production clients and no public
protocol, schema, hot-path, version or M1–M7 semantic changes. Retained project
artifacts are authoritative; reports and repeatable offline analysis provide
review evidence. Owned daemon/capture cleanup is bounded and preserves data.

See the [operator procedure](../hardware/x1-geometric-calibration-validation.md)
and [report schema](../validation/x1-calibration-report-schema.md).
