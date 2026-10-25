# ADR-040: Projected-light programs and safe-state ownership

Status: Accepted v0.4 L0 architecture baseline; implementation planned in L1–L8

## Context

Architecture v1 requires capability-based composite devices, explicit clocks and
hardware synchronization groups. The accepted X1 path is camera-only, free-running
software correspondence under ADR-026; no emitter/controller wiring or physical
trigger implementation has been established. A universal hard-coded DARK/L1/L7
loop would exclude other scanners and confuse requested light with physical state.

## Decision

1. Model cameras, emitters and accessible timing controllers as generic components
   of a logical acquisition parent. L1/L7 are X1 roles, not core types. Stable
   versioned capability names, descriptors and control ownership are defined in
   the [v0.4 baseline](../architecture/v0.4-laser-acquisition.md#3-capability-based-device-graph).
2. The daemon exclusively owns each run and its control resources. An immutable
   `org.mantis.AcquisitionProgram` schema-1 artifact describes complete emitter
   OFF/ON vectors, ordered finite steps/repetitions, capture/trigger intent,
   evidence requirements and timing/resource bounds. v1 has at most 256 declared
   and 1,000,000 executed steps, with explicit finite run/step/ON duration limits.
   Smaller advertised hardware limits prevail. No implicit retries or infinite loop.
3. Separate program meaning, daemon causal sequencing, plugin hardware translation
   and local hardware execution. Strict timing needs a capable validated executor;
   soft real-time software order is not a hard physical timing guarantee.
4. Track commanded, acknowledged, observed and exposure-effective state separately.
   Acknowledgement declares its stage/scope. Unavailable evidence stays unavailable;
   a software ON command cannot prove illumination during an exposure.
5. OFF is the default requested state. Completion, stop, cancellation or fault
   inhibits future ON/triggers and requests all participating emitters OFF before
   recorder drain. Reserve a priority abort/OFF path independent of ordinary queue
   saturation. Preserve initiating and cleanup errors; unknown OFF outcome is explicit.
6. A client disconnect does not cancel a finite daemon-owned run. A hardware-control
   transport disconnect is a fault. Required measurement downstream backpressure
   can continue only under prevalidated finite preservation policy, otherwise fail/OFF.
7. Independent hardware interlocks/watchdogs/fail-OFF mechanisms own physical safety
   when software cannot execute. Their actual design and tests are prerequisites for
   physical L7/L8 operation; software OFF alone is no physical safety guarantee.
   No controller technology or laser-safety certification is selected/claimed.

The [state/failure matrix](../architecture/v0.4-laser-acquisition.md#5-state-ownership-and-failure-model)
and [hardware fact table](../architecture/v0.4-laser-acquisition.md#14-hardware-integration-boundary-and-facts-required-for-l7)
are part of this decision.

## Consequences

Native and future bridged scanners share one acquisition model without requiring
every device to expose the same timing/evidence quality. Invalid programs fail
before enablement; ambiguous physical state cannot silently become valid measurement
evidence. Hardware acceptance remains separate from deterministic software tests.
The accepted 5 ms free-running mode and all historical captures keep their meaning.

## Alternatives considered

Hard-coded X1 sequencing, UI-owned control and unbounded command queues violate
existing ownership/extensibility/reliability contracts. Treating command acceptance
as optical confirmation fabricates evidence. Choosing GPIO or an MCU without
hardware facts would prematurely constrain the executor and is rejected.
