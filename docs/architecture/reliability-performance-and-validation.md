# Reliability, performance and validation standard

Status: Binding engineering standard for new Mantis work

Normative decision: [ADR-033](../adr/033-reliability-performance-release-gates.md).

Mantis is developed as a professional measurement/reconstruction platform. This
standard deliberately raises the bar above "the happy path works". It is inspired
by high-reliability engineering practice without claiming aviation, functional
safety, metrology or other certification that has not actually been performed.

## 1. Priority order

When requirements conflict, use this default order unless a documented product
requirement says otherwise:

1. correctness and measurement validity;
2. data integrity;
3. deterministic/reproducible behavior;
4. fault containment;
5. recoverability;
6. sustained performance and bounded latency/resources;
7. scalability/operability;
8. implementation convenience.

Performance is not optional; it follows correctness rather than replacing it.

## 2. Failure philosophy

A failure is acceptable only in the sense that real hardware/software can fail.
Undefined or silently wrong behavior is not acceptable.

Critical paths therefore prefer:

```text
detected invalid state
-> explicit state transition
-> affected scope contained
-> diagnostics/provenance retained
-> deterministic recovery when supported
-> safe failure when validity cannot be restored
```

over "try to keep going" with unknown data validity.

## 3. Non-negotiable rules

- No silent corruption.
- No silent loss on paths declared lossless.
- No fabricated frames, observations, state or provenance to hide discontinuity.
- No unbounded queues, retries or resource growth.
- No hidden backend/algorithm fallback in measurement-critical execution.
- No swallowing root-cause errors merely to keep a session green.
- No mutable global "current calibration" reinterpretation of historical data.
- No UI ownership of physical capture.
- No publication from a superseded distributed execution generation.

A deliberately lossy preview path remains valid when its QoS says so.

## 4. Implementation quality

Foundational and hot-path code is not written as disposable demo code. Prefer
clear ownership, RAII, explicit lifetimes, typed state, bounded containers,
versioned contracts and deterministic cleanup.

Avoid "temporary" architecture shortcuts that place product-critical behavior
behind ad-hoc globals, hidden threads, implicit copies or ambiguous ownership.

Additional implementation code is acceptable when it removes material recurring
runtime cost or increases robustness and remains maintainable/tested. Fewer lines
are not a performance or quality metric.

## 5. Performance standard

Critical hot paths should minimize:

- host/device and device/device copies;
- payload serialization/deserialization;
- image encoding/decoding that carries no semantic value;
- per-frame heap allocation/free;
- device-wide synchronization;
- unnecessary polling/wakeups;
- network/storage amplification;
- redundant format conversion.

Prefer, when supported and validated:

- immutable views and zero-copy/shareable buffers;
- reusable bounded buffer pools;
- pinned/shared/device-local memory appropriate to the backend;
- asynchronous fences/events rather than global barriers;
- batching/pipelining where latency semantics allow it;
- keeping data resident on the accelerator through adjacent nodes;
- reducing data near the sensor before expensive transport.

Optimization must be measured. A theoretically elegant GPU path that performs
worse or causes unacceptable jitter is not chosen merely because it uses a GPU.

## 6. Performance evidence

Relevant components define measurable budgets and collect appropriate evidence:

- sustained throughput;
- p50/p95/p99 and worst-observed latency for bounded test conditions;
- CPU/GPU utilization where meaningful;
- RAM/VRAM working set and high-water marks;
- allocations per operation/frame where relevant;
- copy/transfer counts and bytes;
- queue depth/high-water/saturation;
- network/storage throughput and amplification;
- thermal/throttling behavior on embedded hardware.

CI can use stable synthetic/regression benchmarks; hardware acceptance supplies
the hardware-specific evidence. Thresholds must be based on measured platforms,
not invented marketing targets.

## 7. Verification layers

Use the cheapest layer that can prove a property, but do not let a cheaper layer
stand in for required physical evidence.

```text
unit / property tests
    ↓
component + integration tests
    ↓
deterministic recorded-data replay
    ↓
fault-injection / process isolation tests
    ↓
simulation / Mantis Virtual Lab at scale
    ↓
hardware-in-the-loop
    ↓
real Optical Core / complete X1
    ↓
multi-device / soak / production-like acceptance
```

A real-field failure should, whenever practical, produce a minimized reproducible
fixture or retained capture that becomes a permanent regression test.

## 8. Fault-injection expectations

Risk-appropriate testing should exercise combinations, not only isolated failures:

- process/plugin/worker termination;
- disk full, short write, corruption and unavailable storage;
- OOM/resource reservation failure;
- queue saturation/backpressure;
- GPU/backend reset or loss;
- network loss, asymmetry, stalls and reconnect;
- late/duplicate/reordered distributed messages where protocol permits;
- restart during journal/artifact/checkpoint commit;
- client/UI disappearance;
- cancellation races and shutdown under load.

The objective is not to "survive everything". It is to prove that each tested
failure has a defined, contained and observable outcome.

## 9. Critical-feature Definition of Done

For capture, storage, calibration activation, reconstruction, distributed
scheduling, failover and similarly high-impact features, "implemented" is separate
from "production-ready".

Before a production-readiness claim, document as applicable:

- functional contract and invariants;
- negative/error behavior;
- ownership/lifetime and concurrency model;
- bounded resource/backpressure behavior;
- recovery or explicit non-recoverability;
- observability/diagnostics;
- compatibility/version migration;
- deterministic fixtures/replay;
- performance budget and regression evidence;
- platform/hardware acceptance matrix;
- soak/fault/scale tests appropriate to risk.

## 10. Maturity labels

The project may use maturity labels to prevent feature existence from being
confused with deployment readiness:

- **EXPERIMENTAL**: contract/algorithm may change substantially.
- **DEVELOPMENT**: functional implementation exists; validation incomplete.
- **VALIDATED**: stated validation matrix passed for stated platforms/conditions.
- **PRODUCTION**: production acceptance gates defined for that feature/deployment
  and passed.
- **INDUSTRIAL**: additional long-running, fault, recovery, hardware and scale
  acceptance defined for the deployment and passed.

These labels are evidence scopes, not certifications.

## 11. Scaling rule

Do not prematurely build enterprise orchestration when one-scanner functionality
is the current milestone. Do preserve contracts so later scale does not require
rewriting capture ownership, data semantics or provenance.

Ask during design review:

1. Is it correct?
2. Is data integrity explicit?
3. What fails with it, and what does not?
4. Can recovery be proven or must failure be explicit?
5. Is the hot path using hardware/resources efficiently?
6. Are latency/memory/queues bounded?
7. Does the contract still make sense at one scanner and at many scanners?
8. How will the claim be tested?

If a shortcut fails these questions in a foundational path, it is not accepted
merely because it is faster to implement.
