# Architecture Decision Records

- [ADR-043: Projected-light recording uses opt-in RawCapture schema 3 and evidence-preserving replay](043-versioned-projected-light-recording-and-replay.md)
- [ADR-042: Projected-light device and processing contracts use additive queried interfaces](042-additive-projected-light-plugin-contract.md)
- [ADR-041: Laser observations and acquisition evidence are typed semantic boundaries](041-laser-observations-and-acquisition-evidence.md)
- [ADR-040: Projected-light programs and safe-state ownership](040-projected-light-programs-and-safe-state-ownership.md)

ADRs 040–043 freeze the [v0.4 L0 architecture baseline](../architecture/v0.4-laser-acquisition.md).
L1–L8 implementation and physical laser acceptance remain planned. v0.4 supplies
acquisition semantics and LaserObservation infrastructure; production extraction,
subpixel localization and laser geometry/triangulation remain v0.5. ADR-031 retains
its final Q6A/host deployment partition.

- [ADR-039: Constrained and external scanners use a host-side device bridge](039-constrained-and-external-scanners-use-a-host-side-device-bridge.md)

ADR-039 defines how microcontroller-class, vendor-controlled and otherwise external
scanners join Mantis without running `mantisd` on the device itself. It introduces
the future transport-neutral Device Bridge Protocol and keeps bridged hardware on
the same capability/data model as native scanners. See
[device integration and bridge architecture](../architecture/device-integration-and-bridge.md)
and the [bridge protocol contract](../protocol/device-bridge.md).

- [ADR-038: Extensible sensor observations are first-class typed data with generic Studio fallback](038-extensible-sensor-observations-and-generic-studio-fallback.md)

ADR-038 defines the extension contract for community-built scanners with unusual
or previously unknown sensors. Generic typed measurements must remain recordable,
replayable and useful in Studio without requiring sensor-specific core/UI code.
See the [extensible sensor platform](../architecture/extensible-sensor-platform.md)
and [custom sensor guide](../plugin-development/custom-sensors.md).

- [ADR-037: Real hardware calibration acceptance is evidence driven](037-real-hardware-calibration-acceptance-is-evidence-driven.md)

- [ADR-036: Studio calibration workspace uses the public calibration API](036-studio-calibration-workspace-uses-public-calibration-api.md)

- [ADR-035: Calibration control API and job orchestration](035-calibration-control-api-and-job-orchestration.md)

- [ADR-034: Open-core Pro modules and signed entitlement licensing](034-open-core-pro-modules-and-licensing.md)

ADR-034 defines the product/architecture boundary between the open Mantis platform
and future separately distributed Mantis Studio Pro modules. See the
[edition policy](../product/editions.md) and
[commercial module/licensing architecture](../architecture/commercial-modules-and-licensing.md).

- [ADR-033: Reliability, data integrity and measured performance are release-gating requirements](033-reliability-performance-release-gates.md)
- [ADR-032: Distributed execution is many-to-many and scanner runtimes retain capture authority](032-distributed-runtime-and-worker-pools.md)
- [ADR-031: X1 remote operation publishes measurement observations at the scanner boundary](031-x1-remote-observation-boundary.md)

ADRs 031–033 are accepted cross-milestone requirements for the future X1
processing partition, distributed worker pools and engineering/release quality.
They define contracts and acceptance expectations; they do **not** claim remote
transport, worker scheduling, laser extraction, reconstruction or HA are already
implemented. See the [X1 processing partition](../hardware/x1-processing-partition.md),
[distributed runtime](../architecture/distributed-runtime.md) and
[reliability/performance standard](../architecture/reliability-performance-and-validation.md).


- [ADR-030: Calibration artifacts, revisioning and active binding](030-calibration-artifacts-revisioning-and-active-binding.md)

- [ADR-029: Calibration solve, validation and stereo policy](029-calibration-solve-validation-and-stereo-policy.md)

- [ADR-028: Target observations and deterministic calibration datasets](028-target-observations-and-deterministic-calibration-datasets.md)
- [ADR-027: Calibration target geometry and physical scale](027-calibration-target-geometry-and-physical-scale.md)

ADRs 027–030 define additive v0.3 target geometry, observations/datasets,
calibration solve/validation, immutable artifacts and active binding policy. See the
[v0.3 architectural baseline](../architecture/v0.3-geometric-calibration.md).

- [ADR-026: Bounded software observation pairing](026-bounded-software-observation-pairing.md)

- [ADR-025: Packed monochrome byte layout](025-packed-monochrome-image-byte-layout.md)
- [ADR-024: Scoped device-owned media setup](024-scoped-device-owned-media-setup.md)
- [ADR-023: Preview leases and asynchronous storage validation](023-acquisition-preview-leases-and-storage-jobs.md)
- [ADR-022: Segmented RawCapture and replay source](022-segmented-rawcapture-and-replay-source.md)
- [ADR-021: Enumerated acquisition and FrameSet transport](021-enumerated-acquisition-and-frameset-contract.md)

ADRs 021–026 are minimal v0.2 extensions. ADRs 001–020 below remain the accepted
frozen Architecture v1 baseline; they are not redefined by these extensions.

- [ADR-001: Daemon-centric runtime using mantisd](001-daemon-centric-runtime-using-mantisd.md)
- [ADR-002: Frontends are clients rather than engine owners](002-frontends-are-clients-rather-than-engine-owners.md)
- [ADR-003: Stable native plugin boundary is a versioned C ABI](003-stable-native-plugin-boundary-is-a-versioned-c-abi.md)
- [ADR-004: C++ Plugin SDK wraps the C ABI](004-c++-plugin-sdk-wraps-the-c-abi.md)
- [ADR-005: Python plugins run out of process by default](005-python-plugins-run-out-of-process-by-default.md)
- [ADR-006: First-party and community plugins use the same contracts](006-first-party-and-community-plugins-use-the-same-contracts.md)
- [ADR-007: Control plane and high-bandwidth data plane are separated](007-control-plane-and-high-bandwidth-data-plane-are-separated.md)
- [ADR-008: Protocol Buffers define service and event schemas](008-protocol-buffers-define-service-and-event-schemas.md)
- [ADR-009: Qt/QML exists only above core/runtime boundaries](009-qt-qml-exists-only-above-core-runtime-boundaries.md)
- [ADR-010: Compute backends are abstracted and CUDA is optional](010-compute-backends-are-abstracted-and-cuda-is-optional.md)
- [ADR-011: Community UI extensions are declarative by default](011-community-ui-extensions-are-declarative-by-default.md)
- [ADR-012: External applications use Client SDKs, not runtime internals](012-external-applications-use-client-sdks,-not-runtime-internals.md)
- [ADR-013: Pipeline processing uses a typed DAG](013-pipeline-processing-uses-a-typed-dag.md)
- [ADR-014: Completed pipeline data and project artifacts are immutable](014-completed-pipeline-data-and-project-artifacts-are-immutable.md)
- [ADR-015: Canonical geometry uses millimeters and right-handed coordinates](015-canonical-geometry-uses-millimeters-and-right-handed-coordinates.md)
- [ADR-016: Explicit clock domains are mandatory for sensor timestamps](016-explicit-clock-domains-are-mandatory-for-sensor-timestamps.md)
- [ADR-017: ARM64 is a Tier-1 platform](017-arm64-is-a-tier-1-platform.md)
- [ADR-018: Raw capture and deterministic replay are first-class features](018-raw-capture-and-deterministic-replay-are-first-class-features.md)
- [ADR-019: Active capture uses transactional chunked recovery](019-active-capture-uses-transactional-chunked-recovery.md)
- [ADR-020: Scanner functionality is represented through capabilities](020-scanner-functionality-is-represented-through-capabilities.md)
