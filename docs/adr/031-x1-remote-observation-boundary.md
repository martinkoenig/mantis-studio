# ADR-031: X1 remote operation publishes measurement observations at the scanner boundary

Status: Accepted architecture requirement; implementation deferred beyond v0.3

## Context

A normal Mantis X1 supports tethered and wireless operation while the X1 Pro also
supports standalone operation. Streaming every measurement-camera RAW frame to a
host would couple normal scanning to high transport bandwidth and duplicate large
sensor traffic that can be reduced close to the source. Moving full reconstruction
onto the scanner would instead bind reconstruction quality and algorithm evolution
to Q6A resources and would weaken deterministic reprocessing with revised
calibration or algorithms.

Architecture v1 already defines typed pipeline data, `LaserObservation`, remote
execution as a scheduling property, immutable calibration references, explicit
data movement and deterministic raw replay. The X1 reference deployment needs a
concrete scanner/host partition without making that partition a universal Mantis
Core rule.

## Decision

1. The normal X1 is a Mantis runtime node. Its Q6A runs `mantisd`, the X1 device
   integration and scanner-side pipeline components. The desktop UI is a client;
   it is not the owner or proxy of the capture data path.
2. The X1 reference deployment uses typed `LaserObservation` data as the canonical
   normal remote measurement boundary.
3. Sensor-proximal work is placed on the Q6A by default: acquisition, hardware and
   emitter sequencing, synchronization metadata, exposure/control, deterministic
   signal preprocessing, laser-line extraction, subpixel localization and
   observation-quality/confidence generation.
4. Triangulation and reconstruction are downstream work by default: calibration
   evaluation, triangulation, pose/tracking, registration, fusion, point-cloud
   processing, meshing and texture processing execute on host/worker resources when
   such resources are available.
5. This is a deployment policy, not an algorithm hard-code. Typed DAG nodes retain
   explicit execution constraints/preferences and may be replanned when validated
   resource/capability information justifies another placement.
6. USB, Wi-Fi/network and local IPC are transport choices for the same semantic
   observation stream. They must not create different measurement meanings or
   separate scanner algorithms.
7. The X1 Pro uses the same logical partition and types even when producer and
   consumer execute on the same Q6A; local transport may use shared-memory or other
   zero-copy-capable mechanisms when implemented.
8. RAW capture remains a first-class parallel path for validation, diagnostics,
   algorithm development and deterministic replay. Engineering/debug deployments
   may additionally stream RAW, but normal remote scanning must not require RAW
   transport.
9. A published observation must retain enough source identity, timing, laser/line,
   confidence and exact calibration/provenance references for downstream
   reconstruction and later audit/replay. It must not silently bake an
   unversioned current calibration into irreversible geometry.
10. Downstream compute unavailability must not transfer ownership of capture to a
    UI or worker. Scanner-side behavior under backpressure/disconnect uses explicit,
    bounded policy: continue/buffer/record/degrade/fail as configured and reported;
    silent loss is forbidden.

## Consequences

The normal X1, X1 Pro, deterministic replay and virtual scanner sources can converge
on the same typed downstream contracts. Bandwidth is reduced before remote
transport without forcing final 3D reconstruction onto the Q6A. New calibration
or triangulation versions can reprocess retained observations or RAW evidence.

The policy does not claim that laser extraction, remote transport or reconstruction
are implemented in v0.3. Exact `LaserObservation` schema, transport framing,
security, buffering limits and algorithm implementation belong to later milestones.

See [X1 processing partition](../hardware/x1-processing-partition.md).
