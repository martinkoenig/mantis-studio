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

For consequential asynchronous, multiprocess, storage/recovery, Qt and concurrency
tests, expose where feasible the specific failed predicate and phase with the
smallest bounded, safe, actionable state/metrics needed to distinguish likely
causes. Preserve the initiating error when evidence or cleanup fails. An ordinary
deterministic unit test generally needs only an expressive assertion and source
location. This is a proportionate design/review criterion, not blanket logging,
success-path tracing, a mass retrofit, a new framework or an additional CI gate.

Inspect production and test code independently when a test fails. Distinguish a
source defect, fixture/oracle defect, unclear contract and environment limitation.
Retain the original failure and investigate it; a later green run is no causal
explanation. Correct the cause within authorized scope, or report a focused blocker.

## Validation by phase

- During implementation, use the narrowest checks that give reliable feedback. Reuse existing coverage.
- Before a draft PR, check the changed scope sufficiently to explain the candidate; explicitly disclose failed, pending or unavailable checks. Draft publication does not imply acceptance.
- Before independent acceptance, perform risk-appropriate verification. R3 usually needs static/content/link checks; R2 needs relevant functional and integration tests; R1 needs affected critical paths, representative negative/recovery/teardown cases and relevant sanitizer or performance evidence. Run full local suites when a specific risk, uncertainty or unique required evidence warrants it. Mandatory CI supplies the complete configured platform matrix; do not mechanically duplicate equivalent results locally.
- Independent reviewers inspect actual production and test code, examine meaningful outcomes and the required exact-candidate CI. Reuse valid evidence when the tested property, relevant source/configuration, fixtures, executable/runtime and conditions remain applicable; document the scope and rerun affected checks when they change. Do not present an old run as a new run.
- Do not waive an explicit substantive acceptance requirement. Hardware, physical timing, accuracy, scale, release and other unique requirements remain separately evidenced. No unperformed test is PASS.

## Failure handling and handoff

Classify the failure: product, fixture/oracle, performance regression, invalid measurement, infrastructure, missing prerequisite or unclear requirement. Investigate the first meaningful failure; retain it and test a concrete changed hypothesis, not retry-until-green. After two consecutive cycles without meaningful new evidence/progress, reconsider scope and method; continue productive investigation when justified, otherwise report a concise blocker and allow other work. An issue does not authorize recursive infrastructure development. Never invent extra mandatory gates, weaken assertions or silently modify acceptance thresholds.

Report a short mapping of requirements to tests/results, exact head, pending CI, and remaining evidence limits. Independently verify before ACCEPT; a green test suite alone does not establish test quality. Preserve the single nonblocking CI handoff. This contract does not add CI jobs, alter branch protection, authorize release or configure the runtime.

The risk-based delivery rules replace generic historic AO-era instructions requiring a universal full local matrix before every commit/push. Specific product validation, owner-approved thresholds and protected decisions still apply. Reconcile stale issue text before dispatch; escalate genuine substantive conflicts rather than assuming a waiver.
