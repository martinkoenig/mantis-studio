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
dark QML shell. Architecture v1 remains frozen; ADRs 021–025 document additive extensions.

| Requirement | Implemented evidence / boundary |
| --- | --- |
| Hardware discovery and roles | Linux media topology + V4L2, compatible version-1/version-2 JSON profiles, sensor/bus identity, explicit LEFT/RIGHT; transient node numbers are diagnostic |
| Public composite device | Queried acquisition table on unchanged ABI-v1 root, selected parent opening, generic parent/child descriptors; frozen v0.1 C DSO still loads |
| Scoped native setup | Explicit disabled-link route discovery, necessary conflict handling, ACTIVE pad formats, VBLANK set/read-back, verified setup with rollback; no global reset |
| Packed RAW10 | Y10P byte-oriented attribute plus logical layout metadata; exact payload recording/replay, consumer-only sample view and Qt grayscale conversion; RAW8 unchanged |
| Dual observation | Immutable FrameSet with native sequences, timestamps/clocks, host arrival, role/identity, raw mode, sync and calibration metadata |
| Failure detection | Deterministic pairing, gaps/repeats/mismatch, backward timestamps, lag/stall/disconnect; failures are explicit |
| Raw QoS | Capacity-32 LOSSLESS writer queue, timed saturation failure, depth/high water; clean stop drains already acquired observations |
| Preview | Independent capacity-one LATEST_ONLY branch; bounded immutable local leases, client-only grayscale conversion, actual dual Studio views tested |
| RawCapture v2 | Sequential segments, record checksums/boundaries, segment durability/index transactions, bounded replay mappings |
| Recovery | Process-kill/restart exposes RECOVERABLE; explicit job scans/validates complete records, truncates only incomplete tail, rejects corruption/index mismatch |
| Replay | Generic ImageStream source, real-time/ASAP, two-pass canonical verification; no physical device required |
| Clients | Additive protocol, CLI/C++/Python capture status, diagnostics, preview/replay/recovery; existing Virtual Scanner workflow passes |
| Compatibility | Project/SQLite schema stays 1; RawCapture schema 2 and MANTIS02 are explicit additions; v0.1 packet/capture path remains readable |
| Validation | Thirteen suites; local Studio/headless/sanitizers and native x86_64/ARM64 CI; hardware status separate |

**USER-REPORTED HARDWARE PASS:** Q6A ARM64 build and automated tests, X1 plugin
loading, real CAMSS graph traversal, stable LEFT/RIGHT sensor identities and
dynamic capture-node identification. The initial GREY 1280×800 STREAMON failed
EPIPE against an upstream Y10_1X10 1280×720 graph.

**IMPLEMENTED BUT REQUIRES Q6A VALIDATION:** plugin-owned media setup and Y10P
STREAMON, actual dual FrameSets/receive rate, sustained recording on suitable
storage, real replay/crash recovery and physical synchronization. These remain
**PENDING USER EXECUTION**. The reference is 1280×720 Y10P / Y10_1X10 / VBLANK=196;
1280×800 crop bounds are not evidence of a validated acquisition mode.

**DEFERRED:** DMABUF/external-buffer zero-copy, physical trigger programming and
true exposure-skew measurement, investigation of 1280×800 Y10P, sophisticated
replay seek/pause, remote data transport and isolated device streaming. No laser,
calibration, triangulation, tracking, fusion, meshing or metrology algorithms are
introduced. The example algorithm remains image-only; a FrameSet-aware native
processing ABI is deferred to the algorithm milestone.

One MMAP-to-owned-buffer acquisition copy is explicit. Hardware sync configured
is an operator assertion, separate from sequence agreement, V4L2 delta and host
arrival delta. SyncQuality remains software; no optical timing accuracy is claimed.
See the [review](v0.2-acquisition-review.md), [storage format](storage.md),
[validation](validation.md), and [Q6A procedure](../hardware/x1-q6a-acquisition-validation.md).
