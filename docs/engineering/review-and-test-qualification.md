# REVIEW-QA-1 — Independent, adversarial code review and test qualification

**Status:** Proposed documentation policy; effective only after independent acceptance and merge into `develop`.
**Authority:** [ADR-033](../adr/033-reliability-performance-release-gates.md), [reliability/performance/validation standard](../architecture/reliability-performance-and-validation.md), existing architecture contracts and [CONTRIBUTING.md](../../CONTRIBUTING.md).
**Scope:** planning, independent review, correction loops and validation evidence. This process policy does not alter architecture, ABI, hardware gates or acquisition QoS.

## 1. Responsibilities and acceptance boundary

- **Implementing Codex worker:** implement only authorized scope, check affected production code and tests, complete all applicable local builds/tests/sanitizers before committing and pushing, and hand off precise commit SHA, changed files, commands/results and limitations. Never self-approve.
- **AO orchestrator, if explicitly activated:** coordinate only authorized work, delegate bounded packages, receive durable worker reports and pass evidence to the independent reviewer. Never approve/merge, silently waive required gates, or advance packages without explicit ACCEPT.
- **Independent reviewer:** independently inspect the real diff **and relevant unchanged surrounding production paths**; evaluate contracts, fault behavior, test effectiveness, performance and required remote GitHub Actions for the exact candidate SHA. Do not accept worker claims or historical green runs as a substitute for evidence.
- **User:** retains authorization of scope and architectural decisions. Separate independent acceptance precedes each new implementation package.

Decisions are **ACCEPT** (all blocking requirements and required evidence verified), **CHANGES REQUIRED** (specific blocking defect or failed required check, with severity, file/component and executable focused correction prompt), or **BLOCKED** (required infrastructure/hardware/evidence unavailable or CI pending). Pending/missing/skipped-required CI is not PASS; known failed required tests cannot be ACCEPT. A worker's exit status is not acceptance. Do not claim physical, GPU, safety, cross-platform or performance validation from simulation alone.

## 2. Risk-classify by behavioral impact

| Tier | Examples | Independent review depth |
| --- | --- | --- |
| **R1 — Critical** | recording/data integrity, storage/recovery, threading/Qt affinity, ownership/cancellation, plugin ABI/lifecycle, calibration provenance, safety/security, authority/distributed execution, failure handling on critical paths | Trace control/data/ownership paths across modules, construction and shutdown, concurrent interleavings, failure/cleanup and recovery; inspect actual negative/fault assertions, resource bounds, compatibility and applicable performance evidence. |
| **R2 — Elevated** | service/protocol, UI state, configuration, noncritical APIs | Verify contracts, realistic failure scenarios, integration/functional tests, compatibility and visible behavior (including UI accessibility/responsiveness when relevant). |
| **R3 — Scoped** | isolated documentation or non-semantic changes | Verify accuracy, links, policy interactions, scope and lightweight checks; promote if the edit affects execution or gates. |

A previously reviewed design is not proof of robust runtime behavior. For R1, challenge not only implementation correctness but also whether a compliant design is unnecessarily vulnerable to scheduling, transient storage latency or partial failure. Alternatives changing accepted contracts require a separate design decision/ADR and approval, not opportunistic refactoring.

## 3. R1 failure-mode checklist

Use applicable items and explicitly justify N/A:
- partial construction; destructor/RAII order; failed initialization and restart;
- producer/consumer scheduling, queue capacity/backpressure, lossless-vs-lossy semantics, timeouts, blocked I/O and stop/join;
- ownership/buffer lifetime, Qt object/thread affinity, callback reentrancy, races, lock inversion, cancellation and shutdown;
- **exceptions and returned errors from every boundary**, including logging/diagnostic callbacks, cleanup during exception unwinding and preservation of the **original root cause** versus secondary cleanup errors;
- EIO/ENOSPC, short writes, fsync, SQLite transactions, corruption, crash recovery, durable prefixes, provenance and deterministic replay;
- capacity/latency/copy budgets, ABI/schema evolution, cross-platform differences and isolation/fault containment.

For a failing test, consider independently: **production defect**, **wrong test assumption/fixture**, **unclear contract**, **environment/CI interference**, or a combination. A repeated green run is neither a root-cause explanation nor evidence of a fix. Separate observed symptom, reproduced mechanism, hypothetical cause and remaining uncertainty.

## 4. Tests must be meaningful, not merely green

For each R1 invariant identify the registered test(s), actual assertion, exercised production path, negative scenario and **what defect makes the test fail**. Evaluate tests adversarially:
- Use controlled barriers/gates, injected clocks and explicit state observations rather than sleeps and luck-dependent scheduling. Timeouts may bound hangs but must not create an unstated runtime contract.
- Exercise real failure semantics with bounded fault injection/negative controls or isolated mutation where practical; retain evidence that assertions detect representative regressions.
- On assertion failure, ensure test cleanup unblocks worker gates **before** futures/jthreads destructors can join. Detect masking of primary exceptions by logger/cleanup failures.
- Removing a fixture stall, synchronization point or assertion requires rationale about its original purpose **and equal or stronger regression coverage**. Keep relevant end-to-end composition (e.g. camera -> recorder -> store -> replay) tested; isolated queue tests are not a substitute.
- Never silence, skip, retry-until-green, broaden timing tolerances, increase queue capacity or relax lossless requirements just to make CI green. Production policy changes need measured evidence and architectural authorization.

## 5. Evidence-based review report

For each critical package record a compact matrix:

| ID | Invariant / risk | Production path inspected | Assertion / fault exercised | Evidence, SHA and limitation | Status |
| --- | --- | --- | --- | --- | --- |
| RQ-01 | specific requirement | functions and relevant surrounding code | test name and failure trigger | actual logs/CI job or explicitly missing proof | PASS/FAIL/BLOCKED/N/A |

Also report: baseline and final SHA; diff and related source inspected; mandatory local configurations and exact results; required x86_64/ARM64, Studio ON/OFF, sanitizer, platform/HIL/performance evidence as applicable; remote checks for the exact commit; blockers and residual risks. **Do not invent executed tests or infer their result from the worker narrative.**

A correction review should consolidate all evidenced blocking findings into one focused assignment where feasible, preserving already correct work. Efficient review means reading *relevant* code and retaining compact evidence, not omitting critical scenarios. Use focused iteration and the full required final verification; do not waste model activations on repetitive unchanged logs or remote CI polling.

## 6. Security signals and required scan evidence

Use the [SEC-EVENT-1 GitHub-security intake charter](github-security-event-intake.md) when an alert, Dependabot PR, scanner outage or security-configuration change intersects a review. Distinguish a zero-finding completed scan from a scan that was never run. For security remediation, preserve branch-specific evidence and treat secret disclosure as an immediate restricted incident, never as an ordinary public issue or automated code-only fix. This reference does not activate AO security-event ingestion.

## 7. Durable review findings and handoff

For any material incident or multi-session package, preserve confirmed/disproven findings, exact SHA/CI evidence, outstanding hypotheses and final independent acceptance in its issue, and link the live status from [ENG-KNOW-1](knowledge-and-handoff.md). Do not replace actual code inspection with an issue or duplicate the full implementation transcript. A dated issue snapshot is never itself final acceptance evidence. The related documentation remains a proposal until merged.

## 8. Recurring CI incidents and improvement

Treat intermittent failures as incidents requiring cause classification, evidence preservation and an explanation or narrowly bounded external limitation. Correlate unrelated red-test families for shared fixture, resources, scheduler and CI causes. Never declare success solely because a subsequent push is green.

[CI-STAB-2](ci-stab-2-qualification.md) provides the planned full-suite qualification and may refine this standard after review. This document supplements, but never weakens, ADR-033.
