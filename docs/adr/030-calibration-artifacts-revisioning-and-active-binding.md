# ADR-030: Calibration artifacts, revisioning and active binding

Status: Accepted for M5

## Context

M1–M4 describe physical targets, detected/selected source observations and three-stage
calibration solutions. Historical reconstruction requires their exact immutable
content and dependency graph. An active project calibration is a mutable choice;
it must never reinterpret a past capture.

## Decision

1. Freeze `org.mantis.CalibrationTarget`, `org.mantis.CalibrationDataset`,
   `org.mantis.CameraCalibration` and `org.mantis.RigCalibration` at artifact schema 1.
   Persist each in one `org.mantis.CalibrationDocument` schema-1 packet in the
   generic Artifact Store. No pixels or calibration document blobs go into SQLite.
2. An artifact ID identifies an immutable object. A separate logical calibration
   ID and positive revision identify a calibration series. Persistence allocates
   the identity before the target is used by detection/dataset/solve. Revisions
   never overwrite objects, reuse a reserved number or change series kind.
3. Downstream payloads contain exact upstream artifact IDs and hashes, matching
   descriptor provenance inputs. Readers verify hashes and the typed graph.
4. Documents are canonical UTF-8 JSON: sorted object keys, no whitespace, explicit
   versions/string enums, canonical arrays, finite round-trip double numbers,
   null for absent optionals. Empty supplied measurement provenance stays distinct
   from absence. Packet headers use deterministic neutral values. There is no
   self-hash or random artifact ID in a document.
5. Project SQLite metadata becomes schema 2. Manifest/container format stays 1.
   Sequential transactional v1→v2 migration creates revision and activation tables
   without rewriting old rows, hashes or objects. Failed migration rolls back;
   future schemas are incompatible.
6. Active binding is project-local and keyed by the exact logical acquisition
   device ID, including stable physical component identities for X1. The binding
   identifies one finalized RigCalibration artifact and logical revision.
7. Runtime snapshots active binding once before Session starts. The writer stamps
   that reference into parent and image-child headers, sharing their existing
   immutable pixel BufferViews. No acquisition-thread database lookup or plugin
   ABI change is introduced. Runtime preview/pipeline adaptation uses the snapshot.
8. The original source-device reference is preserved once per capture in provenance.
   Original parent/children must agree and the source reference must stay stable;
   disagreement fails explicitly before an inconsistent FrameSet is recorded.
9. Replay reads recorded headers and never consults active binding. Rev1 captures
   remain rev1 after rev2 activation. Absence of active calibration remains a valid
   raw-acquisition state and preserves plugin-provided headers.
10. Camera target-view poses explicitly represent `T_camera_from_target`:
    `rotation_vector_rad` and `translation_mm` map target-local points into camera
    optical coordinates. Rig transforms retain row-major `T_target_from_source`.
11. M4 residual/coverage evidence is persisted unchanged. It is not a product
    quality classification or scanner accuracy. No covariance is invented.
12. New provenance writes include `calibration_schema_version`; old rows missing
    it decode with the historical default 1, without rewriting those rows.

## Consequences

The full graph and all training, held-out and final-fit evidence can be loaded
without rerunning detection or solving. Activation changes metadata only. A failed
reserved revision can remain incomplete/recoverable and still consumes its number.
Services/protocol, CLI/SDK calibration operations and Studio workflows remain M6/M7.

The exact implementation contract is [calibration-artifacts](../architecture/calibration-artifacts.md).
