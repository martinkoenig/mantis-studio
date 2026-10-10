# Mantis Studio Roadmap

Status: living product/engineering roadmap  
Current release line: `feature/v0.3-geometric-calibration`

v0.4 planning/development branch: `feature/v0.4-laser-acquisition`

This document is the canonical high-level release roadmap for the Mantis Studio / Mantis X1 scanning path. Detailed architecture, implementation contracts, ADRs and validation evidence remain authoritative for their respective releases.

The roadmap intentionally distinguishes between **implemented/accepted**, **current**, and **planned** work. v0.4 has FINAL ACCEPTED L0 architecture, L1 canonical semantics, L2 contracts and L3 sequencer and L4 evidence/storage; L5 is FINAL ACCEPTED; L6 is FINAL ACCEPTED; L7a is implemented, acceptance pending; L7b–L7d and L8 remain planned. Versions after v0.4 remain planning boundaries whose detailed contracts may be refined before implementation starts.

## Release sequence

| Version | Theme | Status | Primary outcome |
| --- | --- | --- | --- |
| v0.1 | Architecture/runtime foundation | Complete | Daemon-centric runtime, typed data/pipeline model, plugins, artifacts, jobs, clients and Studio shell |
| v0.2 | Real acquisition foundation | Complete | Real dual-OV9281 X1 acquisition, FrameSets, recording/replay and Q6A hardware acceptance |
| v0.3 | Geometric calibration foundation | Current | Physically scaled, versioned camera/stereo calibration with Studio workflow and real-hardware acceptance |
| v0.4 | Laser acquisition foundation | L0–L6 FINAL ACCEPTED; L7a implemented, acceptance pending | Deterministic projected-light control, timing and capture semantics with typed LaserObservation infrastructure |
| v0.5 | Laser geometry and triangulation | Planned | Calibrated laser geometry, subpixel line extraction and metric 3D observations |
| v0.6 | Tracking, registration and fusion | Planned | Multi-frame pose estimation, registration and fused point-cloud reconstruction |
| v0.7 | Surface, mesh and texture | Planned | Surface reconstruction, mesh generation and RGB texture integration |
| v0.8 | Metrology and accuracy validation | Planned | Evidence-driven dimensional validation, uncertainty characterization and scanner accuracy claims |

## v0.3 — Geometric Calibration Foundation

**Current release.**

Goal:

> From real dual-OV9281 acquisitions, Mantis can deterministically produce a physically scaled, versioned and validated geometric X1 calibration, activate it, and bind it reproducibly to future captures and replay.

M0–M7 are implemented. M8 real-hardware calibration acceptance is still pending; M8a acceptance tooling is implemented and awaits physical execution/review.

Canonical detail:
- [v0.3 architecture baseline](docs/architecture/v0.3-geometric-calibration.md)
- [milestone coverage](docs/architecture/milestone.md)
- [real X1 calibration validation procedure](docs/hardware/x1-geometric-calibration-validation.md)

## v0.4 — Laser Acquisition Foundation

**L0 FINAL ACCEPTED; L1 FINAL ACCEPTED; L2 FINAL ACCEPTED; L3 FINAL ACCEPTED; L4 FINAL ACCEPTED; L5 FINAL ACCEPTED; L6 FINAL ACCEPTED; L7a implemented, acceptance pending; L7b–L7d and L8 planned.** See the
[v0.4 architecture baseline and package map](docs/architecture/v0.4-laser-acquisition.md).

L7a provides the [hardware-free X1 projected fixture and controller-readiness boundary](docs/hardware/x1-projected-light-integration.md). Physical enablement remains blocked.

The purpose of v0.4 is to make projected-light acquisition a first-class, deterministic part of the same Mantis runtime instead of introducing a separate scanner-specific side path.

Planned scope:

- model the L1 and L7 projectors as explicit device capabilities;
- define safe, explicit laser states and transitions;
- implement deterministic laser/camera sequencing;
- add hardware-trigger/timing support required for repeatable optical acquisition;
- preserve commanded, acknowledged and exposure-effective projector state as distinct evidence, with unknown/unavailable values where appropriate;
- implement the typed `LaserObservation` semantic boundary/infrastructure and synthetic/test producers; real image-to-observation extraction remains v0.5;
- keep capture authority on `mantisd` running on the X1/Q6A;
- keep transport independent from scanner semantics so USB, Wi-Fi/network and local IPC use the same data model;
- add replayable evidence and validation for sequencing/timing failures, dropped observations and fault handling.

Representative acquisition sequence:

```text
dark -> L1 -> dark -> L7 -> ...
```

The exact sequence is configuration/pipeline policy, not a hard-coded universal scanner algorithm.

Explicitly outside v0.4: production L1/L7 line extraction, subpixel localization,
seven-line disambiguation, laser-plane/projector geometric calibration,
triangulation and reconstructed metric point output. These remain v0.5; later
tracking/fusion/meshing/metrology remain v0.6+. ADR-031 defines the final deployment
partition with source-proximal extraction on Q6A, not the milestone in which the
extractor first becomes available.

Relevant frozen future architecture:
- [X1 remote observation boundary](docs/adr/031-x1-remote-observation-boundary.md)
- [X1 processing partition](docs/hardware/x1-processing-partition.md)
- [distributed runtime](docs/architecture/distributed-runtime.md)
- [reliability/performance/release gates](docs/architecture/reliability-performance-and-validation.md)

## v0.5 — Laser Geometry and Triangulation

**Planned; detailed milestone split still provisional.**

Primary objective: turn calibrated camera/projector observations into physically meaningful 3D measurements.

Expected work includes:

- laser-plane/projector geometric calibration;
- L1 line extraction with subpixel localization;
- L7 multi-line extraction;
- robust line identity/disambiguation for the seven-line projector;
- confidence/quality metadata on extracted observations;
- triangulation using the active geometric camera calibration and calibrated laser geometry;
- metric 3D point observations with complete calibration/capture provenance;
- CPU implementation first where appropriate, with acceleration remaining backend-selectable rather than changing semantics.

The exact split between single-line work, multi-line identity and triangulation may be expressed as separate internal milestones when v0.5 planning is frozen.

## v0.6 — Tracking, Registration and Fusion

**Planned.**

Primary objective: combine individual 3D observations from a moving handheld scanner into a stable reconstruction.

Expected work includes:

- scanner/object motion estimation;
- frame-to-frame and local/global registration;
- robust outlier handling;
- tracking confidence and failure detection;
- multi-frame point fusion;
- bounded-history/relocalization strategy;
- reproducible reconstruction from recorded observations;
- resource-aware partitioning between Q6A and downstream compute without changing the semantic pipeline.

## v0.7 — Surface, Mesh and Texture

**Planned.**

Primary objective: turn the fused metric point representation into usable geometry.

Expected work includes:

- surface reconstruction;
- mesh generation and cleanup;
- normals and topology handling;
- RGB-camera integration;
- texture/colour projection;
- scalable rendering/LOD for larger reconstructions;
- export through the existing artifact/job/plugin architecture.

## v0.8 — Metrology and Accuracy Validation

**Planned.**

Primary objective: establish evidence-based scanner performance rather than inferring accuracy from calibration residuals.

Expected work includes:

- dimensional reference artifacts and repeatable test procedures;
- accuracy, precision and repeatability characterization across the working volume;
- uncertainty/error-budget analysis;
- thermal/time/reassembly sensitivity testing;
- independent-dataset validation;
- regression gates for reconstruction quality;
- explicit separation between measured performance and marketing/specification claims.

A low reprojection RMS is not a millimetre scanner-accuracy claim. Accuracy specifications must be supported by physical measurement evidence.

## Cross-release engineering requirements

Reliability, bounded resources, fault containment, no silent corruption/no silent loss, deterministic replay where applicable, and measured performance are **release gates across the roadmap**. They are not postponed until v0.8.

Likewise, the daemon-centric ownership model, typed semantic data, immutable/versioned artifacts, explicit time/coordinate/calibration provenance and transport-independent remote execution remain architectural constraints rather than release-specific conveniences.

See:
- [Mantis Studio Architecture v1](MANTIS_STUDIO_ARCHITECTURE.md)
- [reliability, performance and validation](docs/architecture/reliability-performance-and-validation.md)
- [distributed runtime](docs/architecture/distributed-runtime.md)

## Roadmap maintenance rule

When a future release is formally planned, its scope should move from this high-level roadmap into a dedicated architecture/work-package document and ADRs as needed. This file then remains the concise cross-release index and must not become a duplicate implementation specification.
