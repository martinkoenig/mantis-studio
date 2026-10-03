Implement the initial **Mantis Studio Architecture Skeleton v0.1**.

This is an architecture-validation milestone.

Do **not** attempt to build the complete Mantis Studio product yet.

Do **not** implement production Mantis X1 scanning, laser extraction, triangulation, tracking, fusion, meshing, metrology, CAD integration, or other advanced product functionality unless explicitly required below.

The primary objective is to prove that the frozen Mantis Studio Architecture v1 can be implemented cleanly.

# Source of truth

Use the supplied `MANTIS_STUDIO_ARCHITECTURE.md` as the architectural source of truth.

Do not silently simplify or redesign fundamental boundaries.

If an architectural requirement cannot reasonably be implemented during this milestone, preserve the correct interface and provide a minimal implementation behind it rather than collapsing architectural layers.

# Technology baseline

Use:

```text
C++23
CMake
Qt 6
QML / Qt Quick
Protocol Buffers
SQLite where required by the project store
```

Target architecture must remain compatible with:

```text
Linux x86_64
Linux ARM64
Windows x86_64
macOS ARM64
```

Linux should be the primary development environment.

ARM64 must not be treated as a future port.

# Required repository structure

Create the repository according to the architecture document, including the major boundaries for:

```text
src/base
src/platform
src/memory
src/schema
src/data
src/time
src/spatial
src/calibration

src/device-api
src/pipeline-api
src/artifact-api

src/device-runtime
src/pipeline-runtime
src/compute
src/artifact-store
src/jobs
src/plugin-runtime

src/services
src/protocol
src/client
src/render

apps/studio
apps/mantisd
apps/cli
apps/plugin-host
apps/worker

sdk/c
sdk/cpp
sdk/python

plugins/first-party
plugins/examples

protocol/protobuf

tests
benchmarks
docs
```

Do not fill directories with meaningless placeholders merely to make the tree appear complete.

A directory may remain intentionally minimal when functionality belongs to a later milestone.

# Strict dependency rules

The foundational libraries must remain Qt-free.

Qt types must not appear in public interfaces of:

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

Do not expose CUDA, Vulkan, Eigen, OpenCV, SQLite, or Qt implementation types through stable public contracts.

Do not add Mantis X1-specific logic to the core.

# mantis-base

Implement minimal production-quality primitives required by the Skeleton, including:

```text
UUID / stable IDs
Status
Error
Result<T>
CancellationToken
Semantic version representation
Hash representation
logging interface
build/version metadata
```

Avoid unnecessary framework dependencies.

# mantis-memory

Implement the beginning of the explicit buffer model.

At minimum support:

```text
Buffer
BufferView
MemoryDomain
ownership/lifetime
size
alignment
read-only/read-write state
```

For Skeleton v0.1, full CUDA/Vulkan external-memory interoperability is not required.

However, the public interfaces must leave room for:

```text
Host Pageable
Host Pinned
Shared Memory
Device Local
External Device Memory
Unified / Managed
```

Do not design the API in a way that assumes all buffers are normal CPU pointers.

# mantis-schema

Implement a minimal extensible schema system supporting:

```text
scalar type
shape
stride
attribute descriptors
namespaced attribute names
data type identifiers
```

# mantis-data

Implement enough canonical data types for the reference workflow.

At minimum:

```text
ImageFrame
FrameSet
PointCloud
Mesh
Tensor
Metadata
```

PointCloud must support extensible attributes.

Data published by pipeline nodes should be logically immutable.

Avoid APIs that encourage arbitrary downstream mutation of shared input data.

# Time and spatial modules

Implement minimal but real types for:

```text
ClockDomain
DeviceTimestamp
MonotonicTimestamp
SequenceNumber
SyncGroup

CoordinateFrame
Transform
Pose
TransformGraph
```

Use the canonical Mantis spatial convention:

```text
right handed
+X right
+Y forward
+Z up
millimeters
```

Transforms use double precision.

Use unambiguous transform semantics consistent with:

```text
T_target_from_source
```

# Public Device API

Implement a capability-based Device Plugin API.

Do not introduce a hard-coded scanner class hierarchy.

The API must support:

```text
device identity
device descriptors
capability discovery
image stream capability
basic lifecycle
start/stop streaming
```

Use namespaced and versioned capability identifiers.

# Virtual Scanner

Implement a first-party **Virtual Scanner Plugin** through the same public Device Plugin API exposed to third parties.

It must not use privileged internal shortcuts.

The Virtual Scanner should generate deterministic synthetic frame data.

The generated data should be suitable for exercising:

```text
device discovery
streaming
pipeline execution
artifact creation
CLI control
Python control
Studio visualization
```

No physical scanner is required.

# Native Plugin ABI

Implement a minimal versioned **C ABI**.

Do not make C++ ABI stability part of the plugin contract.

Use:

```text
opaque handles
plain C structures
function tables
UTF-8
explicit ownership
struct size
ABI version
```

Do not expose:

```text
std::string
std::vector
std::shared_ptr
Qt classes
C++ exceptions
virtual-class ABI
```

Provide an ergonomic C++ wrapper SDK above the C ABI.

# Plugin runtime

Implement:

```text
plugin discovery
manifest loading
ABI compatibility checks
plugin registration
plugin lifecycle
basic diagnostics
```

Support an out-of-process plugin-host mode.

A full security sandbox is not required in v0.1, but the architecture must not require all plugins to run inside `mantisd`.

# Plugin crash test

Provide an example plugin designed to intentionally terminate its plugin-host process.

Verify that:

```text
mantisd survives
Studio survives
the failure is reported
the plugin is marked failed
diagnostics are produced
```

Do not allow the plugin-host crash to terminate the complete application.

# Pipeline API

Implement a typed DAG.

At minimum support:

```text
NodeDescriptor
NodeFactory
NodeInstance
InputPort
OutputPort
ParameterSchema
PipelineGraph
PipelineRecipe
```

The graph must reject incompatible port connections.

Cycles in the data graph are forbidden.

# Pipeline runtime

Implement the minimal graph compilation flow:

```text
validation
type resolution
execution-plan generation
execution
```

Full multi-GPU scheduling is not required yet.

However, resource/backend requirements must exist in the descriptors so that they can be implemented later without redesigning the public API.

# Streaming queues

Implement bounded streaming connections.

Support at minimum:

```text
BLOCK
DROP_OLDEST
LATEST_ONLY
LOSSLESS
```

Do not use unbounded queues.

Add tests demonstrating that backpressure policies work.

# Example processing plugin

Create a simple processing plugin that receives deterministic Virtual Scanner input and produces deterministic geometry.

The algorithm itself may be intentionally trivial.

For example, synthetic frame information may be converted into a simple point cloud.

The purpose is to validate plugin and pipeline architecture, not reconstruction quality.

The processing plugin must use the same plugin interfaces available to future community plugins.

# Artifact API

Implement:

```text
ArtifactId
ArtifactType
ArtifactDescriptor
ArtifactReference
ArtifactState
basic provenance
```

Support at least:

```text
OPEN
FINALIZING
FINALIZED
RECOVERABLE
```

# Artifact Store

Implement a minimal project store using:

```text
manifest.json
SQLite metadata
external object storage
journal directory
cache directory
```

Do not store all large binary data as SQLite blobs.

Completed artifacts are immutable.

Implement enough journaling to demonstrate recovery from an interrupted provisional artifact.

# Example exporter plugin

Implement a simple exporter plugin.

PLY is a suitable first format for PointCloud export.

The exporter must be loaded through the public plugin system.

# Job system

Implement a reusable Job abstraction supporting at minimum:

```text
Queued
Running
Completed
Failed
Cancelled

progress
status
cancellation
diagnostics
result artifact
```

Pipeline processing and export should execute through Jobs rather than blocking UI calls.

# mantisd

Implement `mantisd` as the authoritative runtime process.

It should host at minimum:

```text
DeviceService
CaptureService
PipelineService
ProjectService
ArtifactService
JobService
PluginService
DiagnosticsService
```

For v0.1 these services may be small, but they must respect the architecture boundaries.

# Protocol

Define service and event schemas using Protocol Buffers.

At minimum cover:

```text
device discovery
capture start/stop
pipeline execution
project open/create
artifact listing
job state
plugin state
diagnostic events
```

Keep transport separate from wire schema.

A simple local transport is sufficient for Skeleton v0.1.

Do not design the protocol around UI-specific concepts.

# Client SDK

Implement a C++ Client SDK used by external clients.

The Client SDK must communicate through the service/protocol layer rather than directly calling runtime implementation objects.

# CLI

Create `mantis-cli`.

It must be able to demonstrate the complete Skeleton workflow.

Example conceptual usage:

```text
mantis-cli devices list

mantis-cli capture start --device virtual-scanner

mantis-cli pipeline run --recipe example

mantis-cli artifacts list

mantis-cli export <artifact> output.ply
```

Exact syntax may differ if a cleaner command structure is identified.

# Python Client SDK

Provide a minimal usable Python Client SDK.

Example target experience:

```python
import mantis

studio = mantis.connect()

devices = studio.devices.list()
scanner = devices[0]

capture = studio.capture.start(scanner)

job = studio.pipeline.run(
    capture=capture,
    recipe="example"
)

artifact = job.wait()

studio.export(
    artifact,
    "output.ply"
)
```

The exact API may be adjusted for correctness, but the client must communicate through the same runtime service model as Studio and CLI.

Do not bind Python directly to internal C++ runtime objects.

# Mantis Studio shell

Implement a minimal but polished Qt 6 / QML desktop shell.

Dark Mode is mandatory and should be the primary appearance.

This is not the final Mantis Studio UI.

The Skeleton UI only needs enough functionality to demonstrate the architecture.

Provide:

```text
application shell
device list
Virtual Scanner status
start/stop capture
basic pipeline selection
job/progress view
artifact list
minimal point-cloud visualization
plugin status view
diagnostics/log view
```

Keep the UI clean and modern.

Do not spend excessive time on detailed final-product styling during this milestone.

# UI runtime boundary

The Studio UI must use the Mantis Client API.

Do not allow QML to directly mutate internal runtime objects.

Do not link Studio directly against private pipeline-runtime or device-runtime implementation classes merely for convenience.

# Renderer

Provide the minimum renderer required to display the synthetic point cloud.

Keep renderer interfaces independent of scanner and capture logic.

Do not over-engineer the final renderer backend during this milestone.

# ARM64

Ensure the codebase does not assume x86_64.

Avoid x86-specific intrinsics unless properly isolated behind architecture-specific optimized paths.

The build architecture must remain compatible with Linux ARM64.

Where CI infrastructure permits, configure an ARM64 build check.

# Worker

Create the architectural placeholder for `mantis-worker`.

A complete distributed execution implementation is not required during Skeleton v0.1.

Do not prematurely implement a large remote-compute system.

The relevant contracts should simply remain compatible with future worker discovery and execution.

# Tests

Add automated tests for the fundamental architecture.

Required tests include:

```text
core libraries build without Qt dependencies

Virtual Scanner loads through Device Plugin API

pipeline rejects incompatible port types

pipeline executes deterministic example workflow

bounded queue policies behave correctly

first-party plugin uses public plugin interfaces

artifact store creates immutable finalized artifacts

interrupted provisional artifact can be detected/recovered

CLI communicates through Client API

Python communicates through Client API

plugin-host crash does not terminate mantisd

Studio restart does not destroy runtime-owned project state
```

# Documentation

Create professional GitHub-quality documentation.

At minimum:

```text
README.md
BUILDING.md
CONTRIBUTING.md
SECURITY.md
ARCHITECTURE.md or link to the supplied architecture document

docs/plugin-development/
docs/client-sdk/
docs/protocol/
docs/architecture/
docs/adr/
```

Document how to build and run the Skeleton on Linux.

Avoid claiming support for a platform that has not yet been successfully built or tested.

Clearly distinguish:

```text
architecture target
currently tested
currently supported
```

# ADRs

Create Architecture Decision Records corresponding to the frozen decisions in the architecture specification.

Do not duplicate the entire architecture document into every ADR.

Each ADR should concisely record:

```text
context
decision
consequences
alternatives considered
```

# Code quality

Treat this as a serious long-lived open-source codebase.

Use:

```text
clear namespaces
RAII
const correctness
explicit ownership
strong types where useful
warnings enabled
sanitizer-friendly code
consistent formatting
small testable modules
useful errors
structured logging
```

Avoid premature abstraction where no architectural requirement exists.

Avoid convenience shortcuts that violate the frozen module boundaries.

# Performance

Do not optimize the trivial synthetic algorithm unnecessarily.

Do validate the architecture's performance properties.

Avoid unnecessary copies of frame and point-cloud buffers.

Instrument enough timing information to demonstrate that pipeline-node timing and queue behavior can be observed.

# Explicit non-goals

Do not implement:

```text
production Mantis X1 hardware support
production laser extraction
subpixel laser fitting
triangulation
SLAM
ICP
global registration
TSDF fusion
advanced meshing
texture reconstruction
metrology
Fusion 360 bridge
plugin marketplace
cloud infrastructure
final mobile applications
final polished product UX
```

Those are future milestones.

# Final acceptance test

The Architecture Skeleton is successful when this sequence works:

```text
1. Launch mantisd.

2. Launch Mantis Studio.

3. Studio discovers the Virtual Scanner through the normal plugin system.

4. Start a synthetic capture.

5. Synthetic frames pass through the public data and pipeline contracts.

6. Run the example processing plugin.

7. A PointCloud Artifact is created in the project store.

8. Studio visualizes the resulting point cloud.

9. Export the PointCloud through the example exporter plugin.

10. Repeat the same processing workflow from mantis-cli.

11. Repeat the same processing workflow through the Python Client SDK.

12. Crash the intentionally faulty example plugin host.

13. Verify that mantisd and Studio remain operational.

14. Restart Studio.

15. Verify that runtime/project/artifact state remains valid.
```

Do not proceed to real scanner algorithms until this acceptance sequence is reliable and covered by automated tests where practical.

The deliverable is not judged by how many product features exist.

It is judged by whether the resulting codebase provides a clean, professional, testable foundation on which the full Mantis Studio product can safely be built.