# Mantis X1 processing partition and deployment model

Status: Accepted reference architecture; future implementation where noted

This document defines the X1-specific deployment policy built on the frozen generic
Mantis Architecture v1. It does not add X1 assumptions to Mantis Core and does not
claim that laser extraction, remote data transport or remote scheduling are
implemented in v0.3.

Normative decision: [ADR-031](../adr/031-x1-remote-observation-boundary.md).

## 1. Roles

The software product is one codebase with separately deployable roles:

- `mantisd`: authoritative runtime for devices, capture, pipeline/session state,
  projects/artifacts/jobs and operational diagnostics.
- X1 device/algorithm plugins: Q6A hardware integration and scanner-side processing.
- `mantis-studio`: Qt/QML frontend and protocol client; never capture owner.
- `mantis-worker`: compute execution resource for CPU/GPU algorithms.
- embedded frontend: X1 Pro local touchscreen client when implemented.

A normal X1 does not run the desktop UI merely in a different visual mode. It runs
the runtime and hardware/measurement components needed to own the scanner.

## 2. Reference partition

```text
X1 / Q6A
────────────────────────────────────────────
mantisd
X1 device integration
capture + hardware sequencing
sync/timestamp metadata
exposure/emitter control
signal preprocessing
laser-line extraction
subpixel localization
confidence / quality
        │
        │ LaserObservation stream
        │
────────┼──── USB / network / local IPC ─────
        │
        ▼
Host / worker
────────────────────────────────────────────
calibration evaluation
triangulation
tracking / pose
registration
fusion
point-cloud processing
meshing
texture
```

The boundary is semantic, not tied to one transport. Normal USB and Wi-Fi scanning
must produce the same observation meaning. Local X1 Pro execution may collapse
the transport boundary into local IPC/shared memory without changing types.

## 3. Why observations rather than mandatory RAW transport

The current validated v0.2 dual Y10P mode produces 276.48 MB/s of packed payload
at 1280×720 before container/network overhead. Ten simultaneous scanners would
therefore exceed 2.7 GB/s before RGB or protocol traffic if all measurement RAW
were centralized continuously.

Laser observations preserve the sparse measurement information needed downstream
while allowing source-proximal rejection/correction and avoiding unnecessary
network, host-memory and server-I/O load. Exact bandwidth targets remain future
benchmark/format work.

RAW is still first-class:

```text
capture
  ├─ bounded RAW recorder / deterministic replay
  ├─ optional engineering RAW stream
  └─ scanner-side measurement pipeline
        └─ LaserObservation
```

Normal remote scanning must not require engineering RAW streaming.

## 4. Calibration and provenance

Observation production must retain exact source/camera, FrameSet/sequence,
timestamp/clock/sync, emitter/laser-line identity, quality/confidence and exact
calibration references required by downstream processing.

Triangulation remains downstream by default so a retained observation can be
reprocessed with a compatible, explicitly selected algorithm/calibration revision
without having reduced the canonical transport permanently to XYZ geometry.

Historical data never consults a mutable "current calibration" implicitly.

## 5. Deployment examples

### Normal X1, USB or Wi-Fi

```text
X1 Q6A:       mantisd + X1 runtime + scanner-side nodes
Operator PC:  mantis-studio
Compute:      local or remote mantis-worker
```

The operator PC may also host a worker when it has suitable resources. It is not a
required data relay.

### X1 Pro standalone

```text
Q6A:
  mantisd
  X1 runtime
  scanner-side nodes
  downstream reconstruction nodes as resources permit
  embedded touchscreen client
```

The typed DAG remains logically the same; placement changes.

### Industrial thin-client station

```text
X1 Q6A ───────┬──── control/status ───► fanless UI station
              │
              └──── observations ─────► central worker pool
```

Closing/restarting the UI must not inherently stop scanner-owned capture.

## 6. Scheduling policy versus hard-coded placement

The reference policy prefers sensor-proximal extraction on Q6A and heavy
reconstruction on a capable host. Individual node descriptors still expose
requirements and supported backends. A future planner may keep additional work on
Q6A or move work to another worker only when contracts, resources, data movement
and validated performance justify it.

There must be no product-specific branches such as "WiFi algorithm" versus "USB
algorithm". Transport selection and execution placement are orthogonal concerns.

## 7. Failure behavior

The scanner retains capture authority. If UI or remote compute becomes unavailable,
the Q6A follows explicit bounded policy, for example:

- continue capture with a validated bounded local observation/RAW buffer;
- continue durable local recording if provisioned and sustainable;
- enter a declared DEGRADED/RECOVERING state;
- stop safely when required resource or validity constraints cannot be maintained.

It must not silently discard required measurement data, allocate without bound or
pretend remote reconstruction succeeded.

Exact buffer capacities, reconnect windows and industrial recovery objectives are
deployment/validation parameters, not invented in this document.

## 8. Current implementation boundary

v0.2 implements real Q6A acquisition, FrameSets, bounded raw recording, preview
and deterministic replay. v0.3 adds geometric calibration foundations.

Still future work includes:

- laser/emitter runtime integration;
- `LaserObservation` executable schema/processing;
- scanner-side laser extraction/subpixel algorithms;
- authenticated remote data-plane transport;
- remote worker scheduling/execution;
- distributed state/checkpoint/replay;
- X1 Pro embedded UI and standalone reconstruction acceptance.

Those features must conform to this partition when implemented.
