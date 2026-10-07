# ADR-041: Laser observations and acquisition evidence are typed semantic boundaries

Status: Accepted v0.4 L0 architecture baseline; implementation planned

## Context

Architecture v1 already names LaserObservation and TriggerEvent. ADR-031 places
normal X1 source-proximal extraction on Q6A and triangulation downstream, while
the roadmap assigns real extraction/subpixel localization to v0.5. Current
FrameSet schema 1 carries only image children and cannot stand for every control,
trigger or failure event in a projected-light run.

## Decision

1. v0.4 implements acquisition semantics and LaserObservation schema-1
   infrastructure, including synthetic/test producers. Production L1/L7 extraction,
   subpixel localization, seven-line disambiguation, laser geometry and triangulation
   remain v0.5. ADR-031 is a final deployment policy, not an extractor delivery date.
2. Use `org.mantis.AcquisitionBundle` schema 1 as the immutable projected-light
   publication unit: one typed AcquisitionEvidence packet, zero or one unchanged
   FrameSet and bounded TriggerEvents. Evidence-only bundles support startup,
   control-only steps, failed exposure requests and terminal outcomes. FrameSet
   schema 1 remains the image association unit, without new non-image children.
3. `org.mantis.AcquisitionEvidence` schema 1 preserves run/program/step identity,
   per-emitter commanded/acknowledged/observed/exposure-effective state, exact source
   frame associations, timing/evidence method, disposition, calibration and loss
   accounting. Typed tables and explicit presence/validity distinguish unknown or
   unavailable from OFF/zero/success. Late evidence references prior publications;
   published packets are never mutated.
4. `org.mantis.TriggerEvent` schema 1 distinguishes requested triggers, controller
   acknowledgement stages and observed physical events. Scope native trigger IDs
   to controller generation. Hardware SyncQuality requires actual trigger-to-camera
   association evidence. Neither equal counters nor nearby timestamps establish
   physical simultaneity; optical exposure skew requires independent measurement.
5. `org.mantis.LaserObservation` schema 1 is bulk image/sample-space data scoped
   to one source camera image and producer stream. Retain observation/sample keys,
   original source pixel coordinates, emitter/pattern/line attribution where known,
   program/step/trigger correlation, clocks/sync, confidence/quality and exact
   calibration/algorithm/input provenance. Unknown attribution is representable.
   Extensible typed arrays, dictionaries and validity masks avoid per-sample objects.
   Zero detections is distinct from failure or an unavailable extractor.
6. No canonical XYZ is baked into LaserObservation. Future downstream algorithms
   produce new geometry artifacts. Existing camera/rig calibration stays immutable
   and explicitly referenced; laser geometry will have separate v0.5 artifact meaning.
7. Bulk observations/evidence stay on the explicit data plane; service DTOs are
   bounded controls, references and summaries. Normal X1 ownership and semantic
   boundary remain on Q6A and transport-independent under ADRs 007/031/032/039.

The [evidence model](../architecture/v0.4-laser-acquisition.md#7-acquisition-evidence-and-the-canonical-recording-unit)
and [LaserObservation contract](../architecture/v0.4-laser-acquisition.md#8-laserobservation-semantic-schema-1)
freeze these field meanings; executable schemas are later packages.

## Consequences

v0.5 can supply an extractor without redesigning illumination provenance or remote
measurement semantics. Software evidence and physical measurements remain distinct.
Unknown valid namespaced attributes can survive storage/forwarding even when no
specialized consumer is installed. L0 adds no schema.hpp type or extraction code.

## Alternatives considered

Free-form strings as authoritative state lose type/presence semantics. Replacing
images with XYZ prevents later triangulation with explicit revised inputs.
Promoting commanded light or same native counters to physical evidence is rejected.
Moving extraction into v0.4 misreads ADR-031 and contradicts the roadmap.
