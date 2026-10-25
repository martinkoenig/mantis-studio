# ADR-033: Reliability, data integrity and measured performance are release-gating requirements

Status: Accepted cross-milestone engineering requirement

## Context

Mantis is intended to grow from individual scanners to production deployments
where failures, silent data corruption, prolonged stalls or severe performance
regressions can have material downstream cost. Correctness and performance cannot
be postponed until after feature implementation without accumulating structural
technical debt in capture, storage, scheduling, reconstruction and distributed
execution.

Absolute absence of failures cannot be guaranteed. The engineering requirement is
instead to constrain failure domains, detect and report invalid states, preserve
data integrity, recover deterministically where supported and use hardware
resources efficiently.

## Decision

1. Correctness and measurement validity take precedence over convenience. When the
   runtime cannot establish validity, it fails or degrades explicitly rather than
   silently publishing plausible-but-untrusted output.
2. Silent data loss and silent corruption are forbidden. Lossy behavior is valid
   only for data classes that explicitly declare a lossy QoS policy, such as UI
   preview, and must never be confused with lossless measurement/recording paths.
3. Every streaming/resource boundary is bounded. Unbounded queues, unbounded retry
   loops and unbounded memory growth are architectural defects.
4. Failure containment is mandatory: a UI, plugin, worker, GPU task, storage job or
   remote connection should not crash unrelated runtime domains when process and
   contract boundaries can reasonably isolate it.
5. Recovery semantics are explicit and testable. A component must not claim
   transparent recovery unless authoritative state, provenance and idempotency are
   preserved or reconstructed deterministically.
6. Error handling must preserve root-cause diagnostics and state transitions.
   Hidden fallback, swallowed exceptions/errors and fabricated continuity are
   forbidden in measurement-critical paths.
7. Performance is an architectural concern. Hot paths minimize unnecessary copies,
   serialization, encoding/decoding, allocations, host/device transfers and global
   synchronization. Zero-copy/views, buffer pools, batching, asynchronous fences
   and device-resident processing are preferred when they are safe and measurably
   beneficial.
8. Implementation simplicity is not a reason to accept material recurring runtime
   waste. Additional code/complexity is acceptable when it provides measured,
   maintainable improvements in throughput, latency, memory traffic or hardware
   utilization and remains fully tested.
9. Optimization is evidence-driven. Critical paths carry explicit performance
   budgets and benchmarks for latency distributions, throughput, memory/VRAM,
   transfer/copy counts, queue high-water marks and relevant utilization.
10. Production readiness requires more than happy-path unit tests. Critical
    components require failure, recovery, concurrency, replay, resource-exhaustion,
    regression and long-running/scale validation appropriate to their risk.
11. Claims are evidence scoped. Simulation, fixtures and virtual hardware do not
    substitute for required real-hardware acceptance; passing functional tests does
    not imply metrology, high availability or industrial certification.
12. Reliability maturity and performance evidence must be documented independently
    of feature availability. No "production", "industrial", HA or similar claim is
    made until its defined acceptance gates are satisfied.

## Consequences

Engineering work may intentionally take longer than a minimally functional
implementation. Foundational shortcuts that create hidden copies, ambiguous
ownership, weak failure handling or untestable recovery are rejected even when
they accelerate a demo.

The project remains pragmatic: future cluster features are not implemented before
they are needed. Extension points, state contracts and failure semantics are,
however, designed so scaling does not require replacing the foundation.

See [Reliability, performance and validation](../architecture/reliability-performance-and-validation.md).
