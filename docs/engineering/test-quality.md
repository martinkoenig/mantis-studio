# Test design and implementer self-review

Status: Binding implementation and review contract. Adopted by
[AGENTS.md](../../AGENTS.md) and [CONTRIBUTING.md](../../CONTRIBUTING.md);
extends [ADR-033](../adr/033-reliability-performance-release-gates.md) and the
[reliability standard](../architecture/reliability-performance-and-validation.md).

## Plan by risk

Use R1 (high consequence), R2 (elevated) or R3 (scoped) from CONTRIBUTING.
Identify affected invariants, realistic failure models, production paths,
decisive test oracles and existing coverage. A short plan suffices. Escalate
when a critical boundary changes, including documentation that changes
execution or acceptance rules.

## Tests must detect incorrect behavior

- Exercise actual production control/data/ownership paths and required
  end-to-end composition. Use independent golden data or contract-derived
  expectations, not implementation constants or mock self-assertions.
- For R1, demonstrate representative negative sensitivity with existing
  decisive controls or bounded fault injection/disposable mutation. Observe
  intended failure and restoration; avoid redundant mutations/tests.
- For concurrency use explicit gates/barriers or observable state before
  injecting cancellation/failure. Timeouts bound hangs, not ordering. Release
  gates before joining threads/futures, transport exceptions, retire callbacks
  safely. Preserve initiating errors during cleanup.
- Cover applicable partial I/O, corruption, ENOSPC/EIO, interrupted commits,
  replay, backpressure, bounded resources, cancellation, recovery and teardown.
  Assert explicit contained failure or deterministic recovery and provenance.
- For consequential asynchronous, storage, multiprocess or concurrency tests,
  retain actionable failed predicates and bounded diagnostics; no blanket
  success-path tracing or new framework.

## Performance and failures

Use existing workloads/tools and a method appropriate to the claim, as defined
in the reliability standard. Define workload, inputs, metric, platform,
configuration and applicable budgets before timing. Retain all attempts and
uncertainty. Distinguish invalid measurement from product regression.

Inspect production and test paths on failure; classify and retain original
evidence. Do not repeatedly run unchanged failures, weaken assertions, inflate
timeouts to conceal defects or infer causality from a later green run. After
two no-progress cycles, reassess. Record genuine out-of-scope or unavailable
prerequisites as blockers instead of opening recursive infrastructure work.
A failed mandatory gate remains failed or blocked.

## Handoff and independent review

Perform adversarial self-review: could a realistic incorrect implementation
pass? Verify the applicable local plan in CONTRIBUTING and the issue's actual
technical requirements. Record checks, modes, source identity, outcomes and
limitations. Draft PRs may carry unfinished checks; handoff identifies them.

Independent review assesses actual production/test changes, meaningful negative
controls, relevant input identity and required exact-head CI. Reuse valid
evidence rather than repeat campaigns; reproduce when concrete risk, changed
input or insufficient evidence warrants it. Distinguish source inspection,
executed tests, synthetic reference evidence and physical qualification. No
self-acceptance, missing/failed check called PASS, or readiness claims beyond evidence.
