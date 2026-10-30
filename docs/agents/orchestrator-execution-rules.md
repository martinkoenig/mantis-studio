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
