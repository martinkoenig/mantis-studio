# ADR-032: Distributed execution is many-to-many and scanner runtimes retain capture authority

Status: Accepted architecture requirement; implementation deferred beyond v0.3

## Context

Industrial deployments may contain many simultaneous X1 scanners while heavy
compute is provided by one or more centralized GPU servers. Requiring a powerful
workstation beside every scanner wastes hardware and creates maintenance burden.
Conversely, moving capture ownership to one central server would create an
unacceptable shared failure domain: one server failure could stop every scanner.

Architecture v1 already makes `mantisd` authoritative, frontends clients and node
execution location a scheduling property. The distributed topology and failure
ownership need to be explicit before remote execution is implemented.

## Decision

1. Distributed Mantis execution is many-to-many: N independent scanner/runtime
   nodes may submit work to M compute workers, and one runtime may use multiple
   workers when a plan permits it.
2. Each physical X1 Q6A retains authoritative ownership of its hardware, capture,
   timing/safety state and scanner-local buffering/recording. A central compute
   server does not become the capture master for all scanners.
3. `mantis-studio` is a client. A workstation or panel PC must not be a mandatory
   relay between scanner and compute worker. Control/UI traffic and compute/data
   traffic may take independent routes.
4. `mantis-worker` is a replaceable compute resource. Workers advertise compatible
   algorithms/backends plus bounded CPU, GPU, VRAM, RAM and other scheduling
   resources. Scheduling must account for reservations, active allocations and QoS
   rather than merely choosing an available hostname.
5. Worker pools may serve multiple independent runtime/session owners concurrently.
   Per-session identity, authorization, quotas and resource accounting must prevent
   one scanner/job from silently monopolizing the pool.
6. Losing a UI client must not inherently stop capture or compute. Losing a worker
   must be contained to work placed on that worker. Losing one scanner runtime must
   not corrupt unrelated scanners or workers.
7. Stateless work may be reassigned after an explicit failure boundary. Stateful
   work such as tracking, SLAM, incremental registration or fusion may be moved
   only when its state is safely reconstructible through a compatible checkpoint,
   deterministic replay or an explicitly defined restart policy.
8. Failover must prevent duplicate ownership/split-brain execution from publishing
   conflicting authoritative results. Job/session generations, leases/fencing or
   an equivalent explicit mechanism are required before automatic HA claims.
9. Worker health states and session transitions are explicit and observable.
   Timeouts, network partitions, stalls, process exits, GPU failures and rejoining
   workers must not be treated as an undifferentiated boolean "connected" state.
10. A worker-pool implementation must support load balancing and horizontal growth
    without changing scanner data semantics or the logical typed DAG.

## Consequences

The same software model can scale from one X1 plus one laptop to many scanners,
thin operator stations and a centralized GPU pool. Redundant workers can provide
failover without making the central server the scanner owner.

High availability for stateful pipelines remains non-trivial and is not claimed by
the current executable placeholder. Checkpoint formats, recovery objectives,
discovery, authentication, remote data transport and resource scheduling are later
implementation work and require dedicated validation before production use.

See [Distributed runtime](../architecture/distributed-runtime.md) and
[ADR-031](031-x1-remote-observation-boundary.md).
