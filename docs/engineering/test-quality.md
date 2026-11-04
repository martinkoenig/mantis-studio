# Test design and implementer self-review

Status: Binding implementation and self-verification contract, adopted by
[AGENTS.md](../../AGENTS.md) and [CONTRIBUTING.md](../../CONTRIBUTING.md).
This extends [ADR-033](../adr/033-reliability-performance-release-gates.md) and the
[reliability standard](../architecture/reliability-performance-and-validation.md).
Architecture, security, Plugin SDK/Client SDK boundaries, ABI/schema compatibility
and hardware acceptance requirements retain their authority.

## Before implementation

Classify behavioral risk and identify the binding invariant, realistic failure
model, actual production control/data/ownership path and decisive test oracle.
Use existing coverage where it proves the property; close a specific gap rather
than multiplying test families or imposing test-count quotas.

| Risk | Design and evidence depth |
| --- | --- |
| R1 — high consequence | Data integrity, acquisition/storage/recovery, ownership/lifetime, concurrency/cancellation/thread affinity, plugin ABI, security/authority and calibration/provenance. Trace relevant boundaries and test negative, recovery and teardown behavior. |
| R2 — elevated | Service/protocol, configuration and UI behavior. Verify realistic integration and failure cases; promote to R1 where a critical boundary is crossed. |
| R3 — scoped | Isolated documentation, presentation or non-semantic changes. Check accuracy, links and policy interactions; promote when execution or acceptance gates can change. |

Scale validation to the consequences and affected contracts. A short risk and
check plan is sufficient; a documentation edit must not silently weaken an R1 gate.

## Design tests that detect incorrect behavior

- Execute the intended production path, including relevant ownership/integration
  boundaries. Use independently meaningful outcomes, recorded/golden data or
  contract-derived expectations; do not merely repeat implementation constants
  or assert a mock's own behavior. Preserve necessary end-to-end composition.
- For R1, demonstrate representative negative sensitivity: reuse strong existing
  negative controls or apply bounded fault injection/disposable mutation to an
  independently selected realistic regression. Identify the intended assertion
  and primary diagnostic, observe the nonzero failure and prove restoration.
  Assertion existence and green execution alone do not prove effectiveness.
  Mutating every function or adding duplicate permanent CTests is unnecessary.
- For concurrent work, observe entry into the intended state using controlled
  gates, barriers or explicit observables before cancellation/failure/recovery.
  Timeouts bound hangs; they do not prove scheduler ordering. Do not use sleeps,
  retry-until-green or larger deadlines/queues to conceal incorrect behavior.
- Make assertion-failure unwind safe: release gates before thread/future joins,
  transport worker exceptions, retire callbacks and preserve resource ownership
  through teardown. Secondary cleanup/logging failures must not replace the
  initiating error. Prove bounded cleanup for representative failure controls.
- Exercise applicable partial I/O, EIO/ENOSPC, interrupted commits, corruption,
  durable prefixes/replay, backpressure/capacity, cancellation/shutdown and
  resource exhaustion. Verify explicit contained failure or deterministic recovery,
  retained provenance and bounded resources; never fabricate continuity or loss.

Inspect production and test code independently when a test fails. Distinguish a
source defect, fixture/oracle defect, unclear contract and environment limitation.
Retain the original failure and investigate it; a later green run is no causal
explanation. Correct the cause within authorized scope, or report a focused blocker.

## Before PR handoff

Perform adversarial implementer self-review: **Can a realistic incorrect
implementation pass these tests?** Inspect the production path and test code,
including failure unwind; challenge the oracle and the demonstrated negative
case. Compare existing coverage and justify additions and CI cost.

Complete all applicable local checks required by CONTRIBUTING, BUILDING and the
assignment before commit/push: Studio ON/OFF, full configured suites, relevant
sanitizers, regressions, UI repetitions, platform and performance checks. Use
focused checks while iterating; right-sizing never omits an applicable required
gate. Record actual registrations/modes, source/input identity, commands, exits
and relevant timings rather than historical test counts. Report unavailable or
failed requirements explicitly; never call running, skipped or missing proof PASS.

Keep a concise handoff mapping selected invariants to production paths,
registered tests/fixture modes, decisive assertions, realistic wrong behavior,
negative outcomes/restoration and evidence limits. Distinguish source inspection,
runtime execution, synthetic verifier regression and measured platform evidence.
State GPU/HIL, Qt runtime, x86/ARM, non-Linux, proprietary-driver, physical timing,
calibration and long-soak limits explicitly where applicable.

Implementer self-review, green CTest, generated fixtures and worker/AO reports
are not independent acceptance. External review must inspect production **and
test** code and verify required exact-SHA CI and applicable evidence gates.
Preserve the existing single nonblocking CI handoff; this contract introduces no
new CI job, live AO rule activation, merge permission or release qualification.
Checked-in reference/adoption checks do not prove a fresh worker loaded the
instructions; record operational propagation OPEN until separately demonstrated.
