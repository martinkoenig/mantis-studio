# Milestone coverage and deliberate limits

| Requirement | v0.1 implementation / evidence |
| --- | --- |
| C++23 / CMake / Qt 6 / Protobuf / SQLite | Native targets; generated C++/Python wire code; SQLite metadata only |
| Qt-free foundations | Include-contract test plus complete Qt-disabled build |
| Explicit memory model | Immutable aligned views; lifetime ownership; memory domains; optional readiness fence |
| Extensible semantic data | ImageFrame, FrameSet, PointCloud, Mesh, Tensor, Metadata, generic published Packet |
| Time / space / calibration | Clock domains, sequences, sync groups, mappings; versioned directed transform graph; double matrices; referenced calibration |
| Public device plugin | Capability-based Virtual Scanner with start/stop and deterministic image stream |
| Stable binary boundary | Versioned C function tables/opaque buffers; ergonomic C++ buffer wrapper |
| Plugin lifecycle/isolation | Manifest discovery, ABI checks, initialize/shutdown, isolated processor/exporter host, failure/disable/re-enable |
| Typed pipeline | JSON recipes; type/version/cycle validation; CPU selection; bounded edges; batch and streaming entrypoints |
| Queue policies | BLOCK, LOSSLESS, DROP_OLDEST, DROP_NEWEST, LATEST_ONLY; blocking, cancellation and stress tests |
| Artifact store | Immutable finalized chunks; provenance; SQLite + object files; fsync/rename journal; recovery |
| Jobs | Queued/Running/Completed/Failed/Cancelled, progress, diagnostics, cooperative cancellation, result artifact |
| Authoritative daemon | Separate services behind Protobuf adapter; runtime-owned sessions |
| C++ / CLI / Python | Same service protocol, no direct runtime linkage |
| Studio | Dark QML shell, devices/capture, recipes, jobs, artifacts, geometry view, plugins, diagnostics, PLY export |
| Worker | Compileable descriptor contract and truthful executable placeholder |
| ARM64 | No x86-specific path; native Linux ARM64 CI configured |
| ADRs | All 20 frozen decisions recorded with context/consequences/alternatives |

## Historical v0.1 scope limits

- No real device or scanner algorithms, subpixel extraction, tracking, fusion, metrology or CAD integration.
- Reference recipe processes frame zero. It does not reconstruct the entire capture.
- The scheduler is sequential CPU execution. It supports one external source and one output per node in v0.1; broader public descriptors remain present. Multi-GPU, remote scheduling, caching and transfer planning are not implemented.
- Only host-pageable allocation and read-only mapped file data are implemented. Other domains and fences are public extension points, not working GPU backends.
- Native device plugins run in process when explicitly approved. Isolated processing/export are implemented; isolated device streaming and Python processing plugins are not yet implemented. The Python **client** is fully usable and out of the daemon process.
- Permission declarations exist; no full plugin sandbox, package signing or marketplace exists.
- The processing/export C tables provide one synchronous operation. Internal virtual NodeInstance/ImageStream classes are runtime adapters, never the plugin binary interface. The reference algorithm has no adjustable parameters; recipes with nonempty parameters are rejected explicitly until parameter dispatch is implemented.
- The renderer is CPU painting for the small synthetic cloud, with orbit/zoom and canonical axes. No LOD/large-cloud renderer is claimed.
- Events have monotonic sequence numbers and a bounded 512-entry in-memory history plus persisted logs. Clients poll; subscriptions and persistent job replay are future work.
- The local data plane assumes the client shares the daemon's filesystem. No remote authenticated transport is implemented.
- API/ABI v1 documents establish the initial contract. Long-term compatibility needs future cross-version fixtures; this single release cannot prove compatibility with a release that does not yet exist.

See the validation report for actually executed checks. Platform support is explicitly narrower than the architectural targets.

## v0.2 — Real Acquisition Foundation

Continues the same daemon, public SDK, typed pipeline, SQLite project store and
dark QML shell. Architecture v1 remains frozen; ADRs 021–026 document additive extensions.

| Requirement | Implemented evidence / boundary |
| --- | --- |
| Hardware discovery and roles | Linux media topology + V4L2, compatible version-1/version-2 JSON profiles, sensor/bus identity, explicit LEFT/RIGHT; transient node numbers are diagnostic |
| Public composite device | Queried acquisition table on unchanged ABI-v1 root, selected parent opening, generic parent/child descriptors; frozen v0.1 C DSO still loads |
| Scoped native setup | Explicit disabled-link route discovery, necessary conflict handling, ACTIVE pad formats, VBLANK set/read-back, verified setup with rollback; no global reset |
| Packed RAW10 | Y10P byte-oriented attribute plus logical layout metadata; exact payload recording/replay, consumer-only sample view and Qt grayscale conversion; RAW8 unchanged |
| Dual observation | Immutable FrameSet with native sequences, timestamps/clocks, host arrival, role/identity, raw mode, sync and calibration metadata |
| Pairing and failures | Timestamp-nearest software pairing, fixed two-slot camera queues, at most 32 counted startup exclusions and at most two counted steady-state exclusions between published pairs; strict hardware-mode counters, native continuity and clock checks; cadence is diagnostic-only |
| Raw QoS | Capacity-32 LOSSLESS writer queue, timed saturation failure, depth/high water; clean stop drains published FrameSets; startup/steady-state pairing exclusions and terminal lookahead are counted separately |
| Preview | Independent capacity-one LATEST_ONLY branch; bounded immutable local leases, client-only grayscale conversion, actual dual Studio views tested |
| RawCapture v2 | Sequential segments, record checksums/boundaries, segment durability/index transactions, bounded replay mappings |
| Recovery | Process-kill/restart exposes RECOVERABLE; explicit job scans/validates complete records, truncates only incomplete tail, rejects corruption/index mismatch |
| Replay | Generic ImageStream source, real-time/ASAP, two-pass canonical verification; no physical device required |
| Clients | Additive protocol, CLI/C++/Python capture status, diagnostics, preview/replay/recovery; existing Virtual Scanner workflow passes |
| Compatibility | Project/SQLite schema stays 1; RawCapture schema 2 and MANTIS02 are explicit additions; v0.1 packet/capture path remains readable |
| Validation | Seventeen suites; local Studio/headless/sanitizers and native x86_64/ARM64 CI; official real-Q6A full harness PASS |

**COMPLETE AND MERGED:** accepted real Q6A full-harness evidence was produced on
`fad4df6439e88c7ba4f265c532343c3317e93da4`; v0.2 was merged to main as
`f0030515d01f547e8a922fa392d2b1296185da41`.

| Accepted real Q6A check | Result |
| --- | --- |
| Debug / Release tests | 17/17 PASS / 17/17 PASS |
| Disabled-link discovery / plugin-owned media setup | PASS / PASS |
| Media-bus / capture format | Y10_1X10 / Y10P, 1280×720 |
| Stride / sizeimage / LEFT and RIGHT VBLANK | 1600 / 1,152,000 / 196 and 196 |
| Software correspondence | timestamp-nearest; 5,000,000 ns tolerance |
| Native offset | +8 |
| Startup / steady-state / shutdown unmatched LEFT and RIGHT | 8/0 / 0/0 / 0/1 |
| Final selected timestamp delta / pairing failures | -3.652 ms / 0 |
| Produced / committed FrameSets | 123 / 123 |
| Raw drops / queue saturation | 0 / 0 |
| RawCapture / replay / raw integrity | FINALIZED / PASS / PASS |
| LEFT observed latest / half / max period | 8,327,000 / 4,163,500 / 8,514,000 ns |
| RIGHT observed latest / half / max period | 8,252,000 / 4,126,000 / 9,252,000 ns |

Observed cadence is diagnostic-only; actual cross-camera timestamps enforce the
5 ms software correspondence bound. This is not hardware synchronization or
optical exposure-skew validation. The temporary 100 ms / ~51.8 ms equal-counter
offset and favorable 4 ms starts are **historical diagnostic evidence**. A later
cold 4 ms start failed; those historical successes are not current acceptance.
See the detailed [validation record](validation.md#final-v02-real-q6a-acquisition-acceptance--2026-10-05).

**SEPARATE PENDING WORK:** sustained NVMe/equivalent recording, real hardware
process-crash recovery after the final policy, physical hardware trigger
synchronization, optical exposure-skew measurement and 1280×800 investigation.
No sustained-storage acceptance or 1280×800 validation is claimed.

**DEFERRED:** DMABUF/external-buffer zero-copy, physical trigger programming and
true exposure-skew measurement, investigation of 1280×800 Y10P, sophisticated
replay seek/pause, remote data transport and isolated device streaming. No laser,
calibration, triangulation, tracking, fusion, meshing or metrology algorithms are
introduced. The example algorithm remains image-only; a FrameSet-aware native
processing ABI is deferred to the algorithm milestone.

One MMAP-to-owned-buffer acquisition copy is explicit. Hardware sync configured
is an operator assertion, separate from camera-local counter equality, selected
V4L2 timestamp delta and host arrival delta. SyncQuality remains software; no optical timing accuracy is claimed.
See the [review](v0.2-acquisition-review.md), [storage format](storage.md),
[validation](validation.md), and [Q6A procedure](../hardware/x1-q6a-acquisition-validation.md).


## v0.3 — Geometric Calibration Foundation

The [v0.3 architecture baseline](v0.3-geometric-calibration.md) defines the full
milestone intent and deferred work. Current implementation state:

| Package | Implemented evidence / boundary |
| --- | --- |
| M0 | Architecture and accepted-state housekeeping |
| M1 | Typed target definitions, validation and measured physical geometry; [ADR-027](../adr/027-calibration-target-geometry-and-physical-scale.md) |
| M2 | OpenCV target detection and common observations |
| M3 | Deterministic calibration datasets and finalized RawCapture ingestion; [ADR-028](../adr/028-target-observations-and-deterministic-calibration-datasets.md) |
| M4 | Camera/stereo solving with separate training, held-out and final-fit evidence; [ADR-029](../adr/029-calibration-solve-validation-and-stereo-policy.md) |
| M5 | Immutable artifacts, revisions, persistence/migration, active binding and capture snapshots, including capture-start physical identity and image-geometry compatibility; [ADR-030](../adr/030-calibration-artifacts-revisioning-and-active-binding.md) |
| M6 | Implemented: CalibrationService, compact additive protocol v1 controls, C++/Python SDKs and CLI; [ADR-035](../adr/035-calibration-control-api-and-job-orchestration.md) and [API contract](calibration-api.md) |
| M7 | Implemented: Devices/System guided Calibration workspace, focused client-only Qt controller, immutable-artifact resume, independent daemon Jobs, evidence review and explicit activation; [ADR-036](../adr/036-studio-calibration-workspace-uses-public-calibration-api.md) and [workspace contract](calibration-workspace.md) |
| M8 | Pending: real-hardware calibration acceptance. M8a acceptance tooling implemented; real characterization pending user execution/review. [ADR-037](../adr/037-real-hardware-calibration-acceptance-is-evidence-driven.md), [operator procedure](../hardware/x1-geometric-calibration-validation.md) |

M0–M7 are implemented. This does not claim full v0.3 hardware acceptance.
M8 real-hardware calibration acceptance remains pending. Architecture v1 remains frozen; these are additive extensions.

## v0.4 — Laser Acquisition Foundation (L7a FINAL ACCEPTED)

[L0](v0.4-laser-acquisition.md) and ADRs
[040](../adr/040-projected-light-programs-and-safe-state-ownership.md)–[043](../adr/043-versioned-projected-light-recording-and-replay.md)
freeze projected-light program/state ownership, typed acquisition evidence and
LaserObservation semantics, additive plugin contracts and opt-in recording/replay.
L0 architecture, L1 canonical domain semantics, L2 plugin contracts and L3
deterministic run supervision and L4 storage/evidence are **FINAL ACCEPTED**: exact typed pre-run headers, MANTIS03 bundles,
MRAWREC3 recovery and deterministic mapped replay. See [the exact storage format](rawcapture-v3.md).
L5 daemon controls are **FINAL ACCEPTED**; see the
[control API](projected-light-control-api.md). L6 processing is **FINAL ACCEPTED**; see the
[observation boundary](laser-observation-processing.md). L7a is **FINAL ACCEPTED**; see the [X1 fixture/readiness record](../hardware/x1-projected-light-integration.md). L7b-1 is FINAL ACCEPTED; L7b-2a IMPLEMENTED / AWAITING INDEPENDENT REVIEW; L7b-2b, L7c, L7d and L8 remain planned; no real laser acceptance is claimed. Production L1/L7
extraction, subpixel localization and laser geometry/triangulation remain v0.5;
ADR-031 describes their final deployment partition. L0 does not complete v0.3 M8.


## Accepted cross-milestone future architecture

ADRs [031](../adr/031-x1-remote-observation-boundary.md),
[032](../adr/032-distributed-runtime-and-worker-pools.md) and
[033](../adr/033-reliability-performance-release-gates.md) freeze requirements for
future scanning/reconstruction/distributed work without changing v0.3 scope or
claiming implementation.

- Normal X1 remote/tethered deployment keeps `mantisd` and hardware/capture
  authority on the Q6A. The reference semantic remote boundary is
  `LaserObservation`: source-proximal preprocessing/extraction on Q6A,
  triangulation/reconstruction downstream by default.
- USB, network/Wi-Fi and X1 Pro local IPC are transports of the same typed
  observation semantics, not distinct scanner algorithms.
- Distributed execution is many-to-many: independent scanner runtimes may use a
  shared resource-aware `mantis-worker` pool. Thin UI clients are not mandatory
  data proxies and central workers do not become owners of physical capture.
- Stateless failover may be reassigned with explicit authority fencing; stateful
  failover requires compatible checkpoint/replay/restart semantics. No transparent
  HA claim exists until split-brain, recovery and failure tests pass.
- Reliability, no-silent-corruption/no-silent-loss, bounded resources, fault
  containment and measured performance are release-gating engineering requirements.
  Production/industrial maturity is evidence-based and separate from feature
  implementation.

Detailed references:
[X1 processing partition](../hardware/x1-processing-partition.md),
[distributed runtime](distributed-runtime.md), and
[reliability/performance/validation](reliability-performance-and-validation.md).

**CURRENT STATUS:** architecture only for these additions. v0.3 does not implement
laser extraction, remote observation transport, worker discovery/scheduling,
distributed failover, checkpointing or industrial HA.
