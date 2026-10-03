# Skeleton implementation map

## Dependencies

| Layer | Modules | Responsibility |
| --- | --- | --- |
| Foundation | base, memory, schema, time, spatial, calibration, data | IDs/results/cancellation, immutable buffer views, extensible typed attributes, clocks, double transforms, calibration references |
| Platform | platform | Dynamic libraries, process execution, file mappings, locking, durable sync, loopback byte transport |
| Public domain | device-api, pipeline-api, artifact-api | Capability descriptors, typed ports/recipes, bounded QoS queues, artifacts and provenance |
| Runtime | plugin-runtime, device-runtime, pipeline-runtime, compute, artifact-store, jobs | ABI adaptation, capture threads, graph compilation, CPU selection, transactional storage, asynchronous jobs |
| Application | services | Device, Capture, Pipeline, Project, Artifact, Job, Plugin and Diagnostics services |
| Wire/adapters | protocol, service-adapter | Versioned Protobuf schema, bounded framing, wire/domain conversion |
| Client | client, sdk/python | Remote service operations; local immutable data-plane mapping |
| Frontend | apps/studio, apps/cli, render | Qt Quick shell, CLI commands, synthetic point rendering |

Each foundation/domain module is a separate CMake target. Small header-only modules have real types and validation rather than dummy translation units. Runtime libraries are separate compiled targets. The only native plugin binary contract is `sdk/c/include/mantis/plugin.h`; C++ virtual runtime interfaces do not cross a dynamic plugin boundary.

## Ownership and concurrency

The daemon's control loop serializes service mutations. Each capture owns one producer and one writer thread; recording uses a bounded LOSSLESS queue with capacity eight. A full queue blocks production. Disk errors fail the capture explicitly, close queues, emit diagnostics and leave recoverable data. Frame zero is retained as an immutable shared buffer for the deterministic reference job.

The Job Manager has one worker, a maximum of 64 queued jobs and 256 retained snapshots. Finished job tasks release captured buffers. A job executes a compiled graph; independent clients observe progress via snapshots/events. The Studio bridge makes protocol calls on a QtConcurrent thread, never the QML thread.

A compiled graph retains node/port/resource requirements, validates schema versions, rejects duplicate producers and cycles, and selects CPU only when allowed. Batch execution uses bounded per-edge connections in topological order. Streaming execution retains node instances across frames and applies the same plan repeatedly to a bounded input stream. Branches share immutable buffer storage; all terminal outputs are available by node ID. The convenience `output` is the last topological result used by the reference service.

The v0.1 planner has one external source and one output port per node. Multiple inputs and fan-out are represented. Unsupported resource locations/backends fail explicitly. There is no hidden fallback from a requested GPU or remote worker to CPU.

## Memory and data plane

`BufferBuilder` owns writable host memory. Moving it into publication consumes its writable owner. `BufferView` carries shared immutable lifetime, offset, extent and effective alignment. Mapped reads of non-host-addressable memory fail with `unsupported`; a fence can gate reads. Host pinned, shared, device local, external and unified domains exist without vendor-specific public types.

The producer must not retain writable spans after publication. Logical immutability is an API/ownership contract, not an OS security boundary for in-process code.

The reference pipeline passes image buffers through the ABI without copying payloads. The processor allocates only its new output. Recording serializes payload once per chunk. The renderer reads the final packet by read-only file mapping. Isolated processors use temporary packet files on the separate data plane; this is observable file I/O, not a claim of GPU or device zero-copy interoperability.

## Runtime continuity

Studio owns no capture, project store or job thread. Disconnecting any client leaves runtime state intact. Runtime restart preserves project/artifact/chunk state and classifies unfinished artifacts as RECOVERABLE. Ephemeral job/capture handles are not restored as running jobs after a daemon restart; their committed artifacts remain available.

The plugin host is launched without a shell, has a bounded timeout and is killed/reaped on cancellation. Host exit status becomes a failed plugin/job plus a persistent diagnostic. Disabling or re-enabling a failed processor is available through the same service/protocol model.
