# Distributed runtime and compute-pool architecture

Status: Accepted future architecture; current `mantis-worker` remains a truthful placeholder

Normative decision: [ADR-032](../adr/032-distributed-runtime-and-worker-pools.md).

## 1. Topology

Mantis distributed execution is intentionally many-to-many.

```text
X1-01 / mantisd ─┐
X1-02 / mantisd ─┼──────────────┐
X1-03 / mantisd ─┤              │
...              │              ▼
X1-N  / mantisd ─┘        Compute worker pool
                           ├─ worker A / GPU(s)
Thin UI clients            ├─ worker B / GPU(s)
├─ station 01              └─ worker M / CPU/GPU
├─ station 02
└─ station N
```

Scanner runtimes own physical capture. Workers own only explicitly assigned
compute execution. Frontends own neither.

## 2. Design objectives

The architecture must scale without a different scanner software stack from:

- one X1 plus one laptop;
- several X1 devices plus ordinary desktop workers;
- many fixed optical heads plus thin operator clients;
- 50+ scanner sessions sharing a controlled GPU pool.

Scaling does not mean every deployment receives enterprise orchestration by
default. The same contracts must permit it without an architectural rewrite.

## 3. Worker discovery and capability advertisement

A future worker advertises versioned, authenticated information such as:

- worker identity and generation;
- CPU architecture/capacity;
- compute backends and device identities;
- GPU model/capabilities and usable VRAM;
- RAM and relevant transfer capabilities;
- supported algorithm IDs/versions/backends;
- current reservations/allocations;
- health and draining state.

The scheduler must distinguish "algorithm exists" from "capacity is safely
available". Hidden fallback from requested GPU/worker execution to another backend
is forbidden unless the plan/policy explicitly permits and records it.

## 4. Resource scheduling

Scheduling is resource-aware, not merely host-aware. Plans account for at least
CPU/GPU execution, RAM/VRAM, expected transfer volume, queue/backpressure policy,
latency class and isolation/QoS requirements.

A worker serving multiple scanners requires quotas/fairness/reservations so a large
batch job cannot silently starve active real-time scan sessions.

Reservations and state transitions must be observable for diagnostics and capacity
planning.

## 5. Failure domains

At minimum treat these as separate failure domains:

- scanner/Q6A runtime and physical hardware;
- operator UI/client;
- worker process;
- individual GPU/backend/device;
- network path/partition;
- project/storage service/path;
- individual plugin/algorithm job.

Where technically practical, failure in one domain must not crash unrelated domains.

Examples:

```text
UI crash              -> capture/runtime continue
worker process crash  -> scanner capture remains owned and defined
one GPU unavailable   -> unrelated workers/scanners continue
one X1 unavailable    -> unrelated X1 sessions continue
```

"Continue" never means silently inventing results. The affected session can be
DEGRADED, RECOVERING or FAILED according to explicit policy.

## 6. Stateless and stateful failover

### Stateless nodes

A deterministic/stateless node can be rescheduled when the input and exact
algorithm/configuration remain available and duplicate publication is fenced.

### Stateful nodes

Tracking, SLAM, incremental registration and fusion may depend on substantial
history. Automatic failover requires a compatible state model, for example:

```text
checkpoint K
  + immutable observations after K
  + exact algorithm/configuration/calibration versions
  -> restore/replay on replacement worker
  -> validated LIVE transition
```

If compatible state cannot be reconstructed, Mantis must declare the limitation
and choose an explicit restart/fail policy. It must not continue from an unknown
partial state.

## 7. Split brain and publication authority

Network partitions can leave two compute nodes believing they own the same task.
Before automatic HA is claimed, the system needs explicit generation/lease/fencing
semantics or an equivalent authority mechanism.

Only the current authoritative execution generation may publish final state/artifact
transitions. Late messages and results from superseded generations are rejected or
retained only as non-authoritative diagnostics.

## 8. Suggested state vocabulary

Exact public schemas remain future work, but implementations should distinguish
states rather than reduce them to connected/disconnected.

Worker examples:

```text
AVAILABLE
RESERVED
ACTIVE
DEGRADED
DRAINING
UNREACHABLE
FAILED
```

Session examples:

```text
INITIALIZING
READY
CAPTURING
DEGRADED
RECOVERING
COMPLETED
FAILED
```

Transitions require reasons, monotonic event identity and diagnostics.

## 9. Data-path principle

A thin UI machine is not a mandatory proxy:

```text
                    ┌── control/status ─► mantis-studio
scanner mantisd ────┤
                    └── typed data ─────► mantis-worker
```

This avoids unnecessary copies, bandwidth through the UI host and a failure
dependency on the operator workstation.

## 10. Validation before industrial claims

A worker-pool/HA implementation is not production-ready until appropriate tests
cover, at minimum:

- concurrent multi-scanner load and fair resource allocation;
- worker/process/GPU termination under load;
- one-way and full network partitions;
- delayed, duplicate and late control/result messages;
- worker rejoin and generation fencing;
- checkpoint interruption/corruption;
- replay/restore of stateful nodes;
- resource exhaustion and queue saturation;
- server reboot and rolling drain/maintenance;
- multi-hour/day soak and leak detection;
- performance and latency under failure/recovery load.

The current v0.3 tree does not implement or claim these capabilities.
