# Proposed orchestrator execution supplement

This is an **unactivated proposal** for AO's supported `orchestratorRules`
string. It supplements existing rules and the repository contracts; it is not a
new configuration field or permission grant.

- Delegate a scoped assignment once, with acceptance criteria, publishing scope,
  required gates, expected reports and protected boundaries. Retain its session
  identity. Do not duplicate implementation or start extra workers without a
  concrete need and existing authorization.
- End the idle turn after delegation. Resume for an AO-delivered report,
  completion, failure, blocker, decision/question, review result or user input.
  Do not use model sleep loops, unchanged session/status checks, repeated log
  reads, or repeated "still running" messages to supervise an unchanged task.
- Use AO's existing durable worker-report delivery and `ao send` communication.
  Reports can be batched; `needs-input` and `stuck` have their documented delivery
  semantics. This mechanism wakes a conversation for worker events; it does not
  provide completion notifications for arbitrary worker shell processes. Report
  a missing completion interface as a technical limitation rather than filling
  the gap with model-driven polling or undocumented APIs.
- Respond to actionable worker failures and decisions. Keep implementation and
  independent review separate, preserve all mandatory local/remote gates, and
  never self-approve, auto-merge, or waive security or hardware requirements.
- Respect the exact-SHA, single nonblocking CI handoff in AGENTS.md. Do not add
  Actions wait/poll loops after a worker reports pending CI. Obtain final CI
  evidence at the independent review decision; pending is not PASS.

## Independent review and total-execution-efficiency handoff (proposal, not activated)

For each separately authorized package, give the worker bounded scope, applicable [review-risk tier and evidence requirements](../engineering/review-and-test-qualification.md), original acceptance criteria, local verification and report format. Request one compact exact-SHA report with affected contracts, meaningful failure tests, results, performance evidence if applicable and unverified boundaries. **The independent reviewer**, not AO or its workers, determines ACCEPT/CHANGES REQUIRED/BLOCKED after inspecting actual source, tests and final GitHub Actions. On correction, preserve already correct work and send one consolidated focused prompt; no speculative extra workers, auto-merge or auto-next-package execution.

Retain [ENG-EFF-1A](execution-efficiency.md) and the linked worker guidance: batch necessary reads, use durable `ao report`/`ao send`, avoid idle model wakeups and repeated status/log polling, and do not rerun unchanged suites after minor intermediate edits. Never trade away required local verification, retained failure evidence or final independent CI. [CI-STAB-2](../engineering/ci-stab-2-qualification.md) is planned, not completed. This documentation does **not** modify live AO settings or activate the orchestrator proposal.

## Knowledge-state checkpoint before issue closure or next assignment (proposal)

Prior to a newly authorized worker package, resolve its issue, priority, milestone, canonical docs and exact starting SHA. For AO worker handoff, supply an issue link and accepted constraints rather than re-sending whole prior chat histories. Collect summarized evidence from Codex and separate AO review; deliver to the independent Planning & Review owner for issue and hub reconciliation. **No automatic issue dispatch, next task, acceptance, milestone completion or merge** follows from an AO worker's final message. The owner updates [Engineering Status #14](https://github.com/martinkoenig/mantis-studio/issues/14) only for material changes, with live GitHub verification. See [ENG-KNOW-1](../engineering/knowledge-and-handoff.md).

## AO new-worker injection and public issue safety (proposal)

**Not auto-applied merely by committing this file.** Before relying on issue-driven memory, inspect the *actual live* AO project settings and ensure its `defaultBranch` is explicitly `develop` (AO's `auto` otherwise follows Git remote default `main`), then ensure worker `agentRules`/`agentRulesFile` and orchestrator `orchestratorRules`/`orchestratorRulesFile` inject concise issue-handoff guidance for **new sessions**. Confirm the worker's assigned issue number, verified start SHA, accepted `AGENTS.md`/ADR constraints, and public-data redaction rule appear in the **freshly spawned** session; a previously running worker will not retroactively receive new injected rules. Avoid injecting the entirety of `AGENTS.md` twice when Codex already loads it from its checkout; preserve existing model/effort/settings. AO `set-config` replaces the whole project config and may discard effort; prefer the desktop Project Settings or an audited full-config daemon-API update. Do **not** enable `trackerIntake`/`AO_TRACKER_INTAKE` by default: issue intake can launch workers and currently does not close/comment/update issues after completion. No automatic launch, accept, merge or issue closure is authorized. Before posting any AO worker finding to a public GitHub issue, independently scrub secrets, private paths, identifying data and full logs; if uncertain, mark BLOCKED and use restricted channels.

## English-only issue ledger and hub maintenance (ENG-KNOW-1 proposal)

Every new GitHub-facing issue, PR, review note and comment must be **in English**. For any authorized new task or review, read the **current description of issue #14 first**, then the relevant work issue's current body and only material comments/PR/CI evidence. Route significant independently checked findings and exact-SHA results into **new chronological comments on the responsible work issue**, and keep that issue's short description synchronized with its actual current state. For a material project-wide change, update **#14's compact current-state description plus one short hub comment** linking to the work issue; otherwise leave #14 untouched. Do not duplicate full technical findings or write periodic progress noise. Any content proposed for public GitHub must be screened for secrets, sensitive local/private infrastructure and raw logs before posting. Worker reports remain unverified until independent review, and no comment or issue text authorizes automatic worker launch, merge or ACCEPT.

