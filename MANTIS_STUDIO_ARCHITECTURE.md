# Mantis Studio Architecture

**Status:** Architecture v1 — Frozen Baseline\
**Project:** Mantis Studio\
**Scope:** Desktop, embedded ARM64, headless, remote compute, future mobile clients\
**Primary implementation language:** C++23\
**Desktop/embedded UI:** Qt 6 / QML\
**Canonical spatial unit:** millimeter\
**Canonical world convention:** right-handed, +X right, +Y forward, +Z up

---

# 1. Purpose

Mantis Studio is an open, high-performance 3D acquisition, reconstruction, processing, inspection, and metrology platform.

Mantis scanners are reference hardware for the platform, but Mantis Studio must not be architecturally coupled to Mantis hardware.

The platform must support:

- Mantis X1 and X1 Pro
- third-party scanners
- community reverse-engineered device integrations
- completely custom DIY scanners
- cameras and external sensors
- turntables and motion devices
- future robotic scanning systems
- local and remote compute
- native C++ extensions
- Python automation and processing
- desktop, embedded, headless, and future mobile clients

The architecture must optimize for performance, reproducibility, extensibility, crash isolation, developer accessibility, and long-term binary compatibility.

---

# 2. Core Architectural Principle

The platform is daemon-centric.

`mantisd` is the authoritative runtime and owns application state associated with devices, capture, projects, pipelines, jobs, plugins, and processing.

Mantis Studio, Mantis Embedded, the CLI, Python clients, C++ clients, and future mobile applications are clients of that runtime.

Conceptually:

```text
Desktop Studio ─┐
Embedded UI ────┤
Mobile ─────────┤
CLI ────────────┤
Python ─────────┼── Mantis Client API ── mantisd
C++ Client ─────┘                         │
                                         ├─ Devices
                                         ├─ Capture
                                         ├─ Pipelines
                                         ├─ Projects
                                         ├─ Artifacts
                                         ├─ Jobs
                                         └─ Plugins
```

A local desktop installation may start a local `mantisd` automatically.

An X1 Pro runs `mantisd` directly on the Q6A and connects its embedded touchscreen UI locally.

A desktop computer may connect remotely to the same X1 Pro runtime.

The UI must never be the owner of an active capture session.

A UI crash must therefore not inherently terminate an ongoing scan.

---

# 3. Architecture Layers

The system is separated into five conceptual layers.

```text
Frontends
────────────────────────────────
Studio Desktop
Embedded UI
Mobile
CLI
Python Client
C++ Client

Application / Services
────────────────────────────────
mantisd
DeviceService
CaptureService
PipelineService
ProjectService
ArtifactService
JobService
PluginService
DiagnosticsService

Runtime
────────────────────────────────
Device Runtime
Pipeline Runtime
Compute Runtime
Artifact Store
Job Runtime
Plugin Runtime

Foundation / Domain
────────────────────────────────
Base
Memory
Schema
Data
Time
Spatial
Calibration
Device API
Pipeline API
Artifact API

Platform / Backend
────────────────────────────────
Linux / Windows / macOS
CPU / Vulkan / CUDA / Metal
IPC / Shared Memory
Filesystem
```

Dependencies must flow downward.

Lower layers must not depend on higher layers.

---

# 4. Cross-Cutting Architecture Rule

`mantis-core` and its foundational libraries must contain no semantic assumptions about:

- Mantis X1
- Mantis X1 Pro
- any specific third-party scanner
- Qt or QML
- desktop windows
- CUDA availability
- any particular GPU vendor
- any particular plugin implementation
- Fusion 360 or another external application

The core understands generic concepts such as data, capabilities, coordinate frames, clocks, pipelines, devices, artifacts, jobs, commands, and events.

Hardware-specific behavior belongs in device plugins.

Algorithm-specific behavior belongs in pipeline plugins.

UI-specific behavior belongs in frontend modules.

---

# 5. Module Layout

The baseline module layout is:

```text
mantis-base
mantis-platform

mantis-memory
mantis-schema
mantis-data
mantis-time
mantis-spatial
mantis-calibration

mantis-device-api
mantis-pipeline-api
mantis-artifact-api

mantis-device-runtime
mantis-pipeline-runtime
mantis-compute
mantis-artifact-store
mantis-jobs
mantis-plugin-runtime

mantis-services
mantis-protocol
mantis-client

mantis-render
```

`Mantis::Core` may exist as a CMake convenience target, but it must not become one large monolithic library.

---

# 6. Foundation Modules

## 6.1 mantis-base

`mantis-base` contains only fundamental infrastructure.

Typical concepts include:

```text
UUID
Stable identifiers
Semantic versioning
Result<T>
Status
Error
CancellationToken
Hash
Logging interfaces
Build metadata
Small immutable utility types
```

Its public interface should depend almost exclusively on the C++ standard library.

Qt, OpenCV, CUDA, Vulkan, SQLite, Eigen types, scanner APIs, and application services are forbidden in its public API.

## 6.2 mantis-platform

`mantis-platform` provides thin operating-system abstractions.

Responsibilities include local IPC primitives, process spawning, dynamic-library loading, shared memory, memory-mapped files, file locking, native handles, monotonic clocks, thread naming, and optional thread-affinity helpers.

Platform-specific implementations live below a common internal interface.

Plugin developers should normally never interact directly with this layer.

---

# 7. Canonical Data Model

Semantic meaning and physical storage must be separate concepts.

A `PointCloud` describes what data means.

A `Buffer` describes where and how bytes are stored.

Canonical data families include:

```text
Acquisition
ImageFrame
FrameSet
DepthMap
LaserObservation
IMUSample
IMUSeries
TriggerEvent
TelemetrySeries

Geometry
PointCloud
Mesh
Polyline
Curve
Primitive
Volume

Spatial
Pose
PoseSeries
Transform
CoordinateFrame

Analysis
ConfidenceMap
UncertaintyMap
NormalMap
Mask
Selection

Generic
Tensor
Table

System
Calibration
Artifact
Provenance
Metadata
```

Data objects are schema based and extensible.

A point cloud may, for example, contain attributes such as:

```text
position      float32[3]
normal        float32[3]
color         uint8[4]
confidence    float32
intensity     float32
timestamp     int64
```

Plugins may add namespaced attributes without modifying Mantis Core.

Example:

```text
org.example.surface_probability
```

---

# 8. Numeric Precision

Large geometry buffers should generally use `float32` unless an algorithm has a specific reason to require higher precision.

Examples include point positions, normals, confidence values, depth buffers, and most GPU geometry.

High-precision calculations use `float64`.

This includes:

```text
calibration
camera intrinsics
laser models
optimization
coordinate transforms
metrology
measurement calculations
uncertainty propagation
```

Transforms use double-precision translation and double-precision rotation representations.

---

# 9. Units

The canonical spatial unit inside Mantis is:

```text
1 engine unit = 1 millimeter
```

Unit metadata remains explicit.

Importers and exporters are responsible for converting external unit systems.

No importer, plugin, or algorithm may silently assume units.

---

# 10. Data Immutability

Pipeline output becomes immutable once published.

A downstream consumer must not modify an upstream object that may be shared by another graph branch.

Modifications conceptually produce new data.

The runtime may perform in-place execution only when ownership is exclusive and the scheduler can prove that mutation is safe.

This rule enables reliable:

```text
parallel execution
branching
caching
replay
undo
comparison
remote execution
artifact provenance
```

---

# 11. Memory Model

`mantis-memory` owns the explicit memory abstraction.

Core concepts include:

```text
Buffer
BufferView
MemoryDomain
Allocator
BufferPool
DeviceMemoryHandle
SharedMemoryHandle
Fence
Ownership
Lifetime
```

Memory domains include at minimum:

```text
Host Pageable
Host Pinned
Shared Memory
Device Local
External Device Memory
Unified / Managed Memory
```

A buffer may additionally specify:

```text
backend
device ID
alignment
shape
stride
access policy
synchronization state
```

Example:

```text
Memory: DeviceLocal
Backend: Vulkan
Device: GPU-0
Access: ReadOnly
ReadyFence: 8273
```

No semantic data object may hard-code CUDA as its storage model.

---

# 12. Zero-Copy Views

Data slicing must support views without physical copies wherever possible.

Examples include:

```text
Image ROI
PointCloud selection
Tensor slice
Submesh view
Frame subset
```

A view retains safe ownership or lifetime references to its underlying storage.

Large-data APIs should prefer views and handles over copying containers.

---

# 13. Synchronization Fences

GPU and asynchronous producers must be able to publish data that becomes usable after a fence or event has completed.

The runtime must not introduce unnecessary device-wide synchronization barriers.

Consumers wait only when the required data is actually consumed.

This is required for efficient pipelined GPU execution.

---

# 14. Provenance Data

Fine-grained provenance is supported but must not bloat every point by default.

Large geometry may reference optional sidecar provenance structures.

A point may eventually resolve to information such as:

```text
capture frame
camera
source pixel
laser projector
laser line
calibration
observation
triangulation parameters
```

This enables developer and metrology features such as point-source inspection without forcing every scan to carry maximum provenance overhead.

---

# 15. Pipeline Architecture

Mantis uses a typed Directed Acyclic Graph.

The data-processing graph itself must remain acyclic.

Two execution modes are supported by the same graph system:

```text
STREAMING
continuous data
capture
tracking
live reconstruction
quality monitoring

BATCH
finite artifact processing
global registration
meshing
texture generation
export
```

Feedback control does not create graph cycles.

Camera auto-exposure, laser control, and similar feedback are handled by a separate command/event control plane.

---

# 16. Typed Pipeline Ports

Every processing node declares typed input and output ports.

The graph compiler validates compatibility before execution.

A node descriptor may declare:

```text
identity
plugin ID
algorithm ID
version

input ports
output ports

parameters
parameter mutability

streaming / batch support
execution class

stateful / stateless
deterministic / nondeterministic

CPU requirements
GPU requirements
memory requirements

parallelism
latency expectations
queue policy
```

Community plugins use the same contracts as first-party algorithms.

---

# 17. Pipeline Compilation

User-authored or preset pipelines are not executed directly.

They are compiled into an execution plan.

Conceptually:

```text
Recipe
  ↓
Validation
  ↓
Type Resolution
  ↓
Capability Resolution
  ↓
Backend Selection
  ↓
Memory Planning
  ↓
Transfer Planning
  ↓
Scheduling Plan
  ↓
Execution
```

This compilation step allows Mantis to detect incompatibilities before starting a scan.

It also allows backend selection and hidden memory transfers to be inspected and profiled.

---

# 18. Pipeline Execution Classes

Nodes classify their execution requirements.

Baseline classes are:

```text
REALTIME
INTERACTIVE
BACKGROUND
OFFLINE
```

`REALTIME` means latency-sensitive soft real-time, not mathematically guaranteed hard real-time.

Hardware timing that requires strict microsecond guarantees belongs in scanner firmware or dedicated hardware.

Python nodes are normally classified as `OFFLINE` or `BACKGROUND` unless their performance-critical work is delegated to native implementations.

---

# 19. Backpressure and QoS

Every streaming connection has an explicit queue policy.

Supported policies include:

```text
BLOCK
DROP_OLDEST
DROP_NEWEST
LATEST_ONLY
LOSSLESS
```

Example usage:

```text
Raw capture recording → LOSSLESS
UI preview → LATEST_ONLY
Telemetry dashboard → LATEST_ONLY
Tracking → algorithm dependent
```

Queues must have bounded capacity.

Unbounded buffering is forbidden.

If a lossless path cannot sustain the incoming rate, the user must receive an explicit warning or the capture must transition to a safe failure state.

Silent raw-data loss is unacceptable.

---

# 20. Capture Branching

Capture pipelines may branch.

A typical configuration may be:

```text
Camera
  ├─ Raw Recorder
  ├─ Tracking
  │    └─ Live Fusion
  │         └─ Preview
  └─ Quality Analyzer
```

Raw recording and live preview therefore do not need identical performance or frame-retention policies.

---

# 21. Compute Architecture

Algorithms are decoupled from hardware backends.

The compute abstraction includes:

```text
ComputeDevice
ComputeBackend
ExecutionQueue
KernelCapability
MemoryTransfer
Fence
```

Expected backend modules include:

```text
mantis-compute-cpu
mantis-compute-vulkan
mantis-compute-cuda
mantis-compute-metal
```

CPU is always available.

An algorithm may register multiple implementations.

Example:

```text
LaserExtractor
  CPU implementation
  Vulkan implementation
  CUDA implementation
```

The planner chooses a compatible implementation.

The core algorithm contract must not contain direct assumptions such as `if CUDA is available`.

---

# 22. Remote Compute

Execution location is a scheduling property.

A processing node may describe constraints such as:

```text
ANY
LOCAL
DEVICE
REMOTE
CPU
GPU
```

A future `mantis-worker` process advertises its available compute resources and supported algorithm implementations.

This allows an X1 Pro to perform capture and live tracking locally while a desktop machine performs high-quality fusion or meshing.

The pipeline graph remains logically unchanged.

---

# 23. Device Architecture

Mantis uses a capability-based Device Graph.

The architecture must not assume that every connected device is a complete scanner.

General device categories may include:

```text
Sensor
Emitter
TriggerDevice
MotionDevice
StorageDevice
ComputeDevice
```

A Mantis X1 Pro is a composite device that may contain cameras, laser projectors, IMU, trigger controller, compute, storage, and telemetry devices.

A DIY scanner may expose an entirely different topology.

A physical device is **not required to run `mantisd`**. Constrained or external
hardware may be represented by host-side `mantisd` through a bridged device
adapter. Native runtime devices, bridged devices and result-only/imported devices
must converge on the same capability/data semantics rather than separate
product-specific models. See
[ADR-039](docs/adr/039-constrained-and-external-scanners-use-a-host-side-device-bridge.md)
and the
[device integration/bridge architecture](docs/architecture/device-integration-and-bridge.md).

---

# 24. Device Capabilities

Functionality is discovered through capabilities rather than scanner-specific conditionals or large inheritance trees.

Example capability identifiers may include:

```text
org.mantis.camera.image-stream.v1
org.mantis.camera.exposure-control.v1
org.mantis.camera.gain-control.v1
org.mantis.trigger.hardware.v1
org.mantis.emitter.power-control.v1
org.mantis.motion.position-feedback.v1
org.mantis.telemetry.temperature.v1
```

Mantis Studio presents functionality based on available capabilities.

The UI must not contain code such as:

```text
if scanner == X1
if scanner == Raptor
```

unless a deliberately scanner-specific user experience is implemented outside core device semantics.

---

# 25. Multi-Device Capture

A capture session owns a set of participating devices.

The runtime must not assume a single `activeScanner`.

This enables future configurations involving multiple scanners, turntables, external trackers, robot systems, or auxiliary sensors.

---

# 26. Time Architecture

Sensor processing uses explicit clock domains.

A frame may include:

```text
sequence number
device timestamp
clock domain
host receive timestamp
exposure start
exposure end
sync group
synchronization quality
```

Wall-clock time and monotonic processing time are separate concepts.

Wall-clock time is used for user-facing timestamps and logs.

Sensor synchronization uses monotonic clock domains.

Clock relationships may be represented by a mapping such as:

```text
host_time = scale × device_time + offset
```

with an associated uncertainty estimate.

This allows clock drift and synchronization quality to become measurable rather than implicit.

---

# 27. Hardware Synchronization Groups

Measurements known to originate from the same physical trigger may share an explicit synchronization group.

For example:

```text
CameraLeft frame 81234
CameraRight frame 81234
Laser state L7
Trigger event 81234
```

This relationship must not depend solely on approximate timestamp comparison.

---

# 28. Coordinate System

Mantis uses a canonical right-handed world/CAD convention:

```text
+X = right
+Y = forward
+Z = up
length = millimeter
```

Sensors may retain native optical coordinate systems.

A versioned Transform Graph describes all relationships.

Example:

```text
camera_left_optical ─┐
camera_right_optical ├─ scanner_body ─ world ─ object
rgb_optical ─────────┤
imu ─────────────────┘
```

Transform naming must remain unambiguous.

The preferred notation is:

```text
T_target_from_source
```

Example:

```text
T_world_from_scanner
```

transforms scanner-space data into world space.

---

# 29. Spatial Precision and Uncertainty

Transforms use double precision.

Spatial APIs should be designed so optional uncertainty information can be attached later without redesigning the transform model.

Potential examples include pose covariance, calibration uncertainty, and measurement uncertainty.

Metrology-oriented features must therefore not require a future replacement of the spatial model.

---

# 30. Calibration

Calibration is immutable, versioned, and explicitly referenced by captured data.

A calibration set may contain:

```text
camera intrinsics
lens model
camera extrinsics
laser planes
projector models
IMU alignment
temperature metadata
uncertainty metadata
validity information
```

A capture must reference the calibration identity used during acquisition.

Processing must never silently substitute the current calibration for an older scan.

---

# 31. Project Model

A Mantis project is not a single mutable document.

It is a transactional artifact store plus processing history.

A typical on-disk project may resemble:

```text
Gearbox.mantis/
  manifest.json
  project.sqlite
  objects/
  capture/
  cache/
  journal/
```

SQLite stores metadata, graph relationships, indexes, state, and other structured information.

Large binary data is stored outside normal relational rows.

---

# 32. Immutable Artifacts

A completed meaningful result is represented as an Artifact.

Examples include:

```text
Raw Capture
Rectified Frames
Point Cloud
Registered Point Cloud
Mesh
Texture
Calibration
Measurement Result
```

Artifacts include information such as:

```text
artifact ID
artifact type
schema version
producer
producer version
input artifacts
parameters
content hash
provenance
creation metadata
```

Completed artifacts are immutable.

Changing a parameter produces a new artifact.

---

# 33. Processing History

Artifact lineage naturally forms processing history.

Example:

```text
Raw Capture A
    ↓
Fusion 1.3
    ↓
Point Cloud B
    ↓
Mesher 2.1
    ↓
Mesh C
```

Changing meshing parameters produces `Mesh C2`.

`Mesh C` is not overwritten.

This model provides a foundation for:

```text
undo
redo
branching
comparison
reproducibility
cached processing
```

---

# 34. Cache

Cache data and artifacts are separate concepts.

Artifacts are meaningful project state.

Cache is disposable optimization state.

Clearing a project cache must never destroy source scans or user-selected processing results.

For deterministic operations, cache keys may include:

```text
algorithm ID
algorithm version
canonical parameters
input artifact hashes
```

A compatible cache hit avoids recomputation.

---

# 35. Determinism

Algorithms declare whether they are deterministic.

A randomized algorithm may be reproducible if an explicit random seed is supplied.

Relevant seeds and algorithm parameters are stored in provenance information.

The system must never claim deterministic reproducibility when an algorithm cannot provide it.

---

# 36. Capture Recovery

An active capture is a provisional artifact.

Typical states are:

```text
OPEN
FINALIZING
FINALIZED
RECOVERABLE
```

Capture data is written incrementally in recoverable chunks and journaled.

A process or power failure should invalidate only incomplete trailing data rather than the entire capture.

This is mandatory for standalone X1 Pro operation.

---

# 37. Jobs

Long-running operations are represented by the Job system.

Job concepts include:

```text
Queued
Running
Paused
Completed
Failed
Cancelled
```

Jobs provide progress, diagnostics, cancellation, dependencies, priority, resource information, and result artifacts.

Scanning, processing, transfer, export, calibration, benchmarking, and plugin operations may all use the same job infrastructure.

The UI must not block while performing long-running operations.

---

# 38. Plugin Architecture

Mantis first-party algorithms and community extensions use the same plugin contracts.

First-party functionality must not use privileged private APIs merely for convenience.

Primary plugin categories include:

```text
Device
Pipeline Algorithm
Importer
Exporter
CAD Integration
Analysis
Automation
UI Extension
```

Pipeline plugins may expose individual processing nodes rather than only monolithic algorithms.

---

# 39. Native Plugin ABI

The only stable binary plugin boundary is a versioned C ABI.

The ABI uses:

```text
opaque handles
plain C structs
function tables
integer enums
UTF-8 strings
explicit ownership rules
```

The ABI must not expose:

```text
std::string
std::vector
std::shared_ptr
C++ exceptions
Qt types
Eigen types
C++ virtual-class ABI
```

ABI-facing structures should provide size/version metadata to permit compatible extension.

Example pattern:

```c
struct MantisSomethingV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    /* fields */
};
```

Interfaces are queried by versioned identifiers.

---

# 40. C++ Plugin SDK

A modern C++ SDK wraps the stable C ABI.

Plugin authors may use ergonomic C++ classes and strongly typed wrappers without making the C++ ABI itself part of the compatibility promise.

Memory crossing the plugin ABI must follow explicit allocator and release contracts.

Memory must not be implicitly allocated in one runtime and freed through an incompatible runtime on another side of the boundary.

---

# 41. Python Plugins

Python plugins are first-class but run out of process by default.

Conceptually:

```text
mantisd
  ↓
mantis-plugin-host-python
  ↓
plugin virtual environment
```

Different Python plugins may use isolated environments.

Control traffic uses IPC.

Large arrays and frame data use shared-memory or other high-bandwidth data-plane mechanisms wherever practical.

A Python exception or interpreter crash must not crash the Mantis runtime.

---

# 42. Plugin Isolation

Plugins have trust levels.

Trusted first-party or explicitly approved native plugins may run in process where necessary for maximum performance.

Community plugins should normally run inside a plugin-host process.

A crashed plugin host should result in a controlled plugin failure, not termination of the complete runtime.

Active raw capture should survive an unrelated processing-plugin crash where technically possible.

---

# 43. Plugin Permissions

The plugin model must support permission declaration.

Potential permissions include:

```text
project data
filesystem
network
USB devices
camera access
GPU access
external process execution
UI extension
```

Future plugin discovery or installation UX should present sensitive permissions to users before activation.

---

# 44. Services

`mantis-services` is the application orchestration layer.

Baseline services include:

```text
DeviceService
CaptureService
ProjectService
PipelineService
ArtifactService
JobService
PluginService
AutomationService
DiagnosticsService
```

Frontends interact through these service contracts rather than internal runtime classes.

---

# 45. Command and Event Model

UI interactions produce commands.

They do not directly mutate runtime-owned objects.

The same commands may originate from:

```text
Desktop UI
Embedded UI
CLI
Python
C++ Client
Mobile
Automation
```

The event system communicates state changes such as:

```text
device connected
capture started
tracking lost
job completed
artifact created
plugin crashed
storage warning
```

This model is essential for remote operation and automation.

---

# 46. Control Plane and Data Plane

Control traffic and high-bandwidth data are separate.

The control plane handles messages such as commands, state changes, progress, settings, and events.

The data plane transports large objects such as camera frames, point clouds, meshes, and textures.

Large image streams must not be serialized through ordinary command messages.

Local transports may use shared memory or external GPU-memory handles.

Remote transports may use dedicated chunked or compressed streaming channels.

---

# 47. Protocol

Service and event schemas are defined using Protocol Buffers.

Example protocol files may include:

```text
device.proto
capture.proto
pipeline.proto
project.proto
artifact.proto
jobs.proto
plugins.proto
events.proto
```

The wire schema is separate from the transport.

Local systems may use Unix-domain sockets or Windows equivalents.

Remote systems may use a secure network transport.

Transport choice must not leak into core application semantics.

The Mantis service/client protocol is distinct from the future
[Mantis Device Bridge Protocol](docs/protocol/device-bridge.md). The latter is a
hardware-facing, constrained-device-friendly contract for scanners that cannot host
`mantisd`; it must map into the same Device Graph and typed data semantics.

---

# 48. Client SDKs

Two different C++ developer surfaces are maintained.

The **Plugin SDK** is used for algorithms, devices, exporters, and integrations.

The **Client SDK** is used by external applications that control Mantis.

The Python API is primarily a Client SDK and should expose the same service concepts used internally by Mantis Studio.

External applications must not link directly against runtime implementation classes.

---

# 49. Rendering

`mantis-render` is independent of scanning and processing logic.

It consumes data types such as:

```text
PointCloud
Mesh
Texture
Selection
Annotations
```

and provides scalable visualization.

The renderer must be designed for very large datasets and future support for:

```text
LOD
streaming
selection
confidence overlays
normal visualization
measurement overlays
depth maps
volumes
Gaussian splats
CAD geometry
```

The concrete renderer backend is not frozen by Architecture v1.

The render API boundary is frozen.

---

# 50. Qt Boundary

Qt and QML are permitted in frontend and UI layers.

They are forbidden from foundational and runtime contracts.

At minimum, the following modules must remain Qt-free:

```text
mantis-base
mantis-memory
mantis-schema
mantis-data
mantis-time
mantis-spatial
mantis-calibration
mantis-device-api
mantis-pipeline-api
mantis-artifact-api
```

Qt-specific types must never appear in stable SDK or plugin contracts.

---

# 51. UI Architecture

The UI is modular and supports deployment profiles.

Conceptual layout:

```text
ui/
  design-system/
  shell/
  viewport/
  workspaces/
    capture/
    process/
    inspect/
    cad/
    developer/
  components/
  dialogs/
  models/
  profiles/
    desktop/
    embedded/
    mobile/
```

Dark Mode is the primary design system, not an optional afterthought.

The desktop and X1 Pro interfaces share components but may expose different workflows and density.

The embedded interface is not a separate fork of Mantis Studio.

---

# 52. UI Plugin Policy

Community plugins should not receive unrestricted arbitrary native QML execution inside the main UI process by default.

Most plugin UI is declarative.

A plugin may expose parameter metadata such as:

```text
name
type
unit
range
enum choices
description
default
read-only state
```

Mantis Studio renders appropriate controls.

A higher-trust native UI-extension mechanism may be introduced separately for approved extensions.

---

# 53. Deployment Profiles

Architecture v1 recognizes at least:

```text
desktop
embedded
headless
mobile
worker
```

The same domain and service architecture is retained across profiles.

Capabilities and enabled modules vary by deployment.

ARM64 is a Tier-1 architecture from the beginning.

Linux ARM64 must be continuously considered in CI and dependency selection.

---

# 54. Versioning

Independent components have independent version domains.

At minimum:

| Component                  | Independent version   |
| -------------------------- | --------------------- |
| Application                | Mantis Studio version |
| Project                    | Project format        |
| Artifact types             | Artifact schema       |
| Plugin binary interface    | Plugin ABI            |
| Plugin developer interface | Plugin SDK            |
| Pipeline recipes           | Recipe schema         |
| Device communication       | Device protocol       |
| Firmware                   | Device firmware       |
| Algorithms                 | Algorithm version     |
| Calibration                | Calibration schema    |

Application version must never be used as a substitute for all compatibility decisions.

---

# 55. Schema Migration

Project schema upgrades occur through sequential migrations.

Example:

```text
v1 → v2
v2 → v3
v3 → v4
```

Large immutable binary objects should remain stable where practical.

Metadata migration must not require unnecessary rewriting of large raw-capture datasets.

---

# 56. Recipes

Pipeline Recipes are portable, human-readable, versioned configuration objects suitable for Git.

Recipes identify:

```text
pipeline schema
plugins
algorithm versions
parameters
execution preferences
```

A recipe does not contain machine-specific cache state.

Before scanning, users may select first-party or community algorithms through Recipes.

Raw capture enables later reprocessing with a different Recipe.

---

# 57. Failure Model

Failure must be explicit and localized.

The architecture must account for:

```text
plugin crash
device disconnect
camera timeout
GPU out-of-memory
disk-full condition
insufficient recording throughput
remote worker disconnect
corrupt project metadata
process crash
power loss
```

Errors propagate as structured failures rather than arbitrary exceptions crossing subsystem boundaries.

Recovery should preserve as much valid capture state as possible.

---

# 58. Performance Principles

Performance is a first-class architectural concern.

The implementation should prefer:

```text
zero-copy data movement
bounded queues
preallocated pools
explicit ownership
asynchronous execution
GPU fences instead of global synchronization
batching
streaming
memory locality
hardware-aware backend selection
minimal serialization
```

Performance-critical paths must be benchmarked.

Hidden data copies should be observable through diagnostics.

---

# 59. Diagnostics

Mantis must expose developer-level diagnostics for the runtime.

Examples include:

```text
frame rates
dropped frames
queue occupancy
pipeline latency
node execution time
CPU utilization
GPU utilization
GPU memory
memory transfers
USB/network throughput
clock drift
synchronization quality
plugin health
storage throughput
```

Diagnostics are intended both for Mantis development and advanced users.

---

# 60. Testing Strategy

Testing must include:

```text
unit tests
integration tests
plugin contract tests
protocol compatibility tests
project migration tests
crash-recovery tests
fuzz testing
deterministic replay tests
performance benchmarks
accuracy regression datasets
```

Recorded datasets should allow core algorithms to be tested without physical scanner hardware.

---

# 61. Deterministic Replay

Any operation that does not fundamentally depend on a live external source should be executable from recorded input.

Recorded capture datasets act as Virtual Scanners.

This enables:

```text
algorithm development without hardware
reproducible bug reports
performance testing
accuracy regression
A/B algorithm comparison
CI testing
community development
```

Deterministic replay is a core architectural requirement, not merely a debugging feature.

---

# 62. Repository Structure

The repository should initially follow this structure:

```text
mantis-studio/
  CMakeLists.txt
  CMakePresets.json

  cmake/

  docs/
    architecture/
    adr/
    sdk/
    protocol/
    plugin-development/

  protocol/
    protobuf/

  src/
    base/
    platform/
    memory/
    schema/
    data/
    time/
    spatial/
    calibration/

    device-api/
    pipeline-api/
    artifact-api/

    device-runtime/
    pipeline-runtime/
    compute/
    artifact-store/
    jobs/
    plugin-runtime/

    services/
    protocol/
    client/
    render/

  apps/
    studio/
    mantisd/
    cli/
    plugin-host/
    worker/

  ui/
    design-system/
    shell/
    viewport/
    workspaces/
    profiles/

  sdk/
    c/
    cpp/
    python/

  plugins/
    first-party/
      devices/
      algorithms/
      exporters/
      integrations/

    examples/

  backends/
    cpu/
    vulkan/
    cuda/
    metal/

  tests/
    unit/
    integration/
    contract/
    replay/
    migration/
    fuzz/

  benchmarks/
  datasets/
  tools/
```

Not every directory must contain a production-ready implementation in the first milestone.

The architectural boundary should nevertheless exist from the beginning.

---

# 63. Dependency Rules

Dependency direction is mandatory.

Examples:

```text
base
↑
memory/schema/time/spatial
↑
data/calibration
↑
public domain APIs
↑
runtime implementations
↑
services
↑
protocol/client
↑
frontends
```

Forbidden dependency examples include:

```text
PointCloud → QColor
PipelineNode → CUDA-specific public type
Device API → X1 implementation
Artifact API → SQLite
Core → QML
Core → Fusion 360
```

Architecture tests or build-time checks should enforce important layer rules where practical.

---

# 64. Public API Policy

Stable and externally documented contracts include:

```text
Plugin C ABI
Plugin C++ SDK
Python Client SDK
C++ Client SDK
Protocol schemas
Recipe schema
Canonical data schemas
Device capability identifiers
Plugin manifest schema
```

The following remain internal implementation details:

```text
scheduler internals
SQLite tables
runtime implementation classes
Qt models
renderer internals
platform abstraction implementation
```

Internal interfaces may evolve without creating unnecessary third-party compatibility obligations.

---

# 65. Architecture Decision Records

The initial ADR set is:

| ADR     | Decision                                                                                 |
| ------- | ---------------------------------------------------------------------------------------- |
| ADR-001 | Daemon-centric runtime using `mantisd`                                                   |
| ADR-002 | Frontends are clients rather than engine owners                                          |
| ADR-003 | Stable native plugin boundary is a versioned C ABI                                       |
| ADR-004 | C++ Plugin SDK wraps the C ABI                                                           |
| ADR-005 | Python plugins run out of process by default                                             |
| ADR-006 | First-party and community plugins use the same contracts                                 |
| ADR-007 | Control plane and high-bandwidth data plane are separated                                |
| ADR-008 | Protocol Buffers define service and event schemas                                        |
| ADR-009 | Qt/QML exists only above core/runtime boundaries                                         |
| ADR-010 | Compute backends are abstracted and CUDA is optional                                     |
| ADR-011 | Community UI extensions are declarative by default                                       |
| ADR-012 | External applications use Client SDKs, not runtime internals                             |
| ADR-013 | Pipeline processing uses a typed DAG                                                     |
| ADR-014 | Completed pipeline data and project artifacts are immutable                              |
| ADR-015 | Canonical geometry uses millimeters and right-handed coordinates                         |
| ADR-016 | Explicit clock domains are mandatory for sensor timestamps                               |
| ADR-017 | ARM64 is a Tier-1 platform                                                               |
| ADR-018 | Raw capture and deterministic replay are first-class features                            |
| ADR-019 | Active capture uses transactional chunked recovery                                       |
| ADR-020 | Scanner functionality is represented through capabilities, not product-specific branches |

---

# 66. Architecture Skeleton v0.1

The first implementation milestone must prove the architecture rather than implement production scanning algorithms.

It must include:

```text
mantis-base
mantis-memory
mantis-schema
mantis-data

basic time/spatial types

device API
pipeline API
artifact API

minimal device runtime
minimal pipeline runtime
minimal artifact store

job system

plugin ABI
plugin discovery
plugin host

mantisd

Mantis Client SDK

mantis-cli

minimal Python Client SDK

minimal Qt/QML Studio shell
Dark Mode

Virtual Scanner plugin

example processing plugin

example exporter plugin
```

The initial renderer may be minimal.

Production X1 acquisition and reconstruction algorithms are explicitly outside the Architecture Skeleton milestone.

---

# 67. Skeleton Reference Workflow

The Skeleton is considered successful only if the following conceptual workflow works:

```text
Virtual Scanner
      ↓
Frame Data
      ↓
Example Processing Node
      ↓
Generated Geometry
      ↓
Artifact Store
      ↓
Example Exporter
```

The same operation must be launchable through:

```text
Mantis Studio
mantis-cli
Python Client SDK
```

All clients must communicate with the same runtime architecture.

---

# 68. Skeleton Crash-Isolation Test

An intentionally crashing out-of-process example plugin must be included as a test fixture.

Expected behavior:

```text
plugin host crashes
→ mantisd survives
→ Studio survives
→ crash is reported
→ diagnostic information is stored
→ plugin can be disabled or restarted
```

Where technically possible, unrelated raw recording remains active.

---

# 69. Skeleton Architecture Tests

The initial codebase should contain tests proving at minimum:

```text
Qt does not leak into core data contracts
Virtual Scanner is loaded through the public Device Plugin API
first-party plugins use the same ABI as example community plugins
Python controls mantisd through the Client API
CLI controls mantisd through the Client API
artifacts survive UI restart
plugin crash does not crash mantisd
bounded queues apply backpressure
project recovery handles interrupted capture
pipeline type validation rejects incompatible nodes
```

---

# 70. Definition of Architecture Freeze

Architecture v1 is considered frozen when this document and its ADRs are accepted.

Frozen does not mean immutable forever.

It means new features should first be implemented within the existing extension points.

Changing one of the foundational contracts requires an explicit new ADR explaining:

```text
the problem
why existing extension points are insufficient
alternatives considered
compatibility impact
migration strategy
```

Feature convenience alone is not sufficient justification for breaking a foundational contract.

---

# 71. Architecture v1 Baseline

The following principles are therefore binding:

**Data:** immutable typed semantic data with extensible schemas.

**Memory:** explicit zero-copy-aware storage with memory domains, views, ownership, and synchronization.

**Pipeline:** typed DAG supporting streaming and batch execution with explicit QoS and resource planning.

**Devices:** capability-driven graph with no Mantis-specific assumptions in core contracts.

**Time:** explicit clock domains and synchronization quality.

**Space:** versioned transform graph with right-handed millimeter-based canonical coordinates.

**Calibration:** immutable, versioned, and explicitly referenced.

**Projects:** transactional artifact store with provenance and recoverability.

**Plugins:** versioned C ABI with C++ wrappers and isolated Python execution.

**Runtime:** `mantisd` owns operational state.

**Clients:** UI, CLI, Python, C++, embedded, and mobile use the same service model.

**Compute:** hardware backends remain abstract and replaceable.

**Platform:** Linux x86_64, Linux ARM64, Windows x86_64, and macOS ARM64 are architectural targets from the beginning.

**Extensibility:** first-party Mantis components obey the same extension contracts provided to third parties.

**Performance:** data movement, synchronization, queues, resource selection, and scheduling are explicit architectural concerns.

**Reproducibility:** raw capture, provenance, versioning, and deterministic replay are foundational features.

---

# 72. Final Architecture Goal

Mantis Studio must remain simple enough that a normal user can select a scanner, select a Scan Recipe, press Scan, and obtain a high-quality model.

At the same time, the same system must allow an advanced developer to replace the tracker, inspect raw synchronized camera frames, execute a custom reconstruction algorithm, benchmark pipeline nodes, automate the entire process through Python, connect a homemade scanner, distribute processing to another computer, inspect point provenance, and export through a custom integration.

Those two experiences must be different views of the same architecture rather than separate products.
