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

## Scope limits that do not change the frozen architecture

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
