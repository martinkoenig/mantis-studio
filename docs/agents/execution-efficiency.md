# ENG-EFF-1A: execution efficiency evidence and unactivated proposal

Scope: developer analysis and guidance only, based on
`d2614b96304ff5fd45db8f09076da058afb9230d` on
`feature/eng-eff-1a-agent-efficiency`. Production acquisition, calibration,
rendering, CMake/CTest registration and selection, accepted qualification
evidence, protected worktrees and installed AO binaries are unchanged.
This document does not assert milestone acceptance or activate live rules.

## Reproduce the sanitized accounting

Use Python 3.10+ on Linux. Supply explicit local rollout files to
[the analyzer](../../tools/agent_efficiency.py):

```text
python3 -B tools/agent_efficiency.py worker=/local/worker.jsonl guardian=/local/guardian.jsonl completion=/local/completion.jsonl
python3 -B -m unittest discover -s tests/unit -p agent_efficiency_test.py -v
```

The analyzer opens regular input files read-only, rejects symlinks/devices/FIFOs,
and has no output-path, execution, network or live-configuration operation.
Redirect its sanitized stdout only to an already authorized working/evidence
location. It creates no directories or files. It emits no source paths, command
arguments, justifications, messages, private reasoning, prompts or arbitrary
configuration strings. Unknown names/settings become `other`. Command strings
are reduced to private in-memory digests before correlation; digests are removed
from the export. The export includes input hashes, validated session/call IDs,
timestamps, finite categories and token counters. Errors discard partial output
and suppress raw filenames, arguments, records and tracebacks.

The input hashes establish the particular local records analyzed, not independent
authenticity of the provider's accounting. Original inputs remain read-only.
No raw input or copied private configuration belongs in Git or review artifacts.

## Observations, boundaries and accounting

The collection uses these distinct native-session boundaries, in UTC:

| Role / native session | First record | Last record | Tasks / calls |
| --- | --- | --- | --- |
| Worker `01a18c93-9559-70e1-ae57-8d9001c95d0c` | 11:30:44.815 | 15:02:15.950 | 4 tasks / 167 calls |
| Guardian `01a18c93-bc1d-7ce0-858f-204204278336` | 11:30:54.639 | 15:00:21.182 | 147 tasks / 0 calls |
| Completion `01a18d58-993b-7b21-9c4b-7569dafcea1c` | 15:05:56.365 | 15:06:12.026 | 1 task / 2 calls |

All dates are 2026-10-30. Session spans include idle/overlapping time; do not sum
them as CPU time or attribute all tokens to CTest. These are historical 1B
observations, not a new qualification run or a definition of waste.

| Role | Input | Cached input (subset) | Uncached input | Output | Reasoning (output subset) | Input + output |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Worker | 36,195,667 | 35,831,296 | 364,371 | 236,149 | 169,714 | 36,431,816 |
| Guardian, including identified compaction | 20,312,585 | 17,561,600 | 2,750,985 | 12,274 | 2,356 | 20,324,859 |
| Completion | 66,278 | 40,576 | 25,702 | 446 | 56 | 66,724 |
| Combined | 56,574,530 | 53,433,472 | 3,141,058 | 248,869 | 172,126 | 56,823,399 |

Accounting sums **unique response-level `usage`**, deduplicated by response ID;
it never sums cumulative thread/turn counters. Conflicting duplicate IDs,
inconsistent counters and foreign native thread IDs fail closed. Reviewer
accounting can share a runtime `session_id` with another thread; it is the
record's `thread_id` that must match the rollout identity. A fallback for older
records uses cumulative deltas and explicit reset segments, rejecting ambiguous
partial resets. Missing accounting is labeled unavailable. The available
`last_usage_includes_reasoning=true` metadata confirms reasoning is already
included in output. Cached input is already included in input. Neither is added
again. No financial price, cost conversion, CPU or unrecorded activation is
inferred from tokens; cached and uncached input have different practical costs.

The previously supplied Guardian figure **20,086,929** is reproduced by its final
cumulative event. A separately identified compaction response has 235,768 input
(5,888 cached) plus 2,162 output = **237,930** tokens, omitted by that cumulative
event but present in response-level usage. Thus the supplied combined observation
56,585,469 and the response-level total above differ by exactly that compaction.
This is an accounting-scope correction, not an efficiency regression.

The established observation of 85 worker calls during CTest, approximately
20.92M worker / 11.68M Guardian tokens in that interval, is retained as an
approximate historical observation, not remeasured or assigned a causal meaning.
The separate orchestrator's 56 poll/wait operations were mostly before 1B; they
are excluded from this ledger and must not be charged to 1B. No comparable
orchestrator token ledger or financial accounting is claimed here.

## Requested reviews and status work

All **147** structured Guardian requests name `exec_command` with
`require_escalated`. Requested shell-command heads include `tail` **68**, `cat`
13, `python3` 12, `rg` 6, `sed` 5, `git` 3, `mkdir` 2, `cp` 1 and other/compound
heads 37. These are requested-action categories, not verdicts or root-cause
proof. The collector makes no attempt to export Guardian reasoning or policy.
All 147 requested working directories match the historical worker's directory;
that does not establish that every command's target paths were within it, nor
does it establish cross-worktree access as the reason for review.

There are **39 repeated exact command requests** beyond first occurrences.
Repeated commands do not prove unchanged results. Source-text classification of
worker calls finds 87 containing `write_stdin`, 9 explicit cell-wait calls,
19 file-inspection, 19 Git, 11 build/inspection, 9 test/inspection and 13 other
calls. Classification is lexical, may include referenced code or combined
operations, and does not count actual nested tool executions. It cannot label
every call as redundant polling. Final result retrieval, error diagnosis,
cancellation and independent work remain necessary; the export does not have
enough state to separate all those intents reliably.

Matching exact command literals with preceding worker-call timestamps yields
**108 unique candidate joins**, leaving **39 unmapped/ambiguous requests**.
No stable cross-system causal ID is available; these joins are correlations,
not an asserted Guardian-task-to-worker-call bijection. A single worker call can
contain several nested operations, and identical commands can recur.

Supported conclusion: repeated status/log inspection is a substantial review
category and a candidate for reducing *operations*, especially unchanged reads.
It is not evidence that 147 approvals, or all 85 CTest calls, were avoidable.
Necessary builds/tests and legitimate permission reviews must remain.

## Effective configuration and installed behavior

Read-only inspection used the installed AO release
`20261029233428-9192` (CLI version reports `dev`), CLI help, its embedded OpenAPI
schemas and lifecycle symbols, report-delivery metadata, Codex user-config
categories and actual rollout `turn_context`. No installed code was patched.

- Historical worker: `gpt-6.1-sol/high`, `workspace-write`, `on-request`,
  `auto_review`, network disabled. Guardian is a separate `codex-auto-review/low`
  read-only reviewer thread with its own policy; its `never` setting is not the
  worker's approval policy and is not a proposed change.
- Current worker: actual runtime independently verified as
  `gpt-6.1-sol/high`, `workspace-write`, `on-request`, `auto_review`, network
  disabled, with a managed filesystem policy. Project role overrides explicitly
  configure worker `gpt-6.1-sol/high` and orchestrator `gpt-6.1-sol/medium`.
  User `config.toml` has no top-level model, effort, sandbox or approval setting.
  Project role overrides have no explicit permission field. Those absences do
  not imply unrestricted access: effective runtime/managed enforcement controls.
  Available metadata does not uniquely establish which harness/default/managed
  layer supplied each inherited policy value.
- Current default sandbox launch fails **before executing the command** with
  `bwrap: loopback: Failed RTM_NEWADDR: Operation not permitted`. Read-only checks
  therefore require existing reviewed escalation. This is direct current-session
  infrastructure evidence, not proof that every historical review had that cause.
  Instruction changes cannot repair the sandbox or safely remove those reviews.
- The installed project schema supports `agentRules`, `agentRulesFile`,
  `orchestratorRules`, and role `agentConfig` fields `model`, `effort`, `mode`,
  `permissions`. The CLI says `set-config` **replaces** the project configuration;
  its typed config mirror omits `effort`, and there is no effort flag. In this
  release `ao project get --json` omits both role effort fields, while even
  `set-config --config-json` drops them during CLI serialization. Supplying a
  complete JSON object does not make this writer lossless. The installed
  using-ao project guide explicitly documents both losses.
  No `orchestratorRulesFile` or generic shell-completion subscription is exposed
  by this CLI. Current project rules already contain efficiency guidance; the
  proposed supplements refine it rather than replace all standing instructions.
- AO report documentation describes durable delivery and immediate
  `needs-input`/`stuck` handling, informational batching and done settlement.
  Installed `WaitForWake`/report symbols corroborate lifecycle support; this
  worker's initial hold report was acknowledged in one delivery attempt. An
  orchestrator can end an idle turn and resume on delivered reports, without
  repeated model sleeps. This is worker-report delivery, not arbitrary process
  completion. Symbols alone do not prove every lifecycle edge case.
- The exposed Codex shell tools support bounded blocking/yield and result
  retrieval. No supported durable, independently delivered completion receipt
  for an arbitrarily long shell job has been established. A rule cannot prove
  that a three-hour build completes without model re-entry. EFF-B04 is therefore
  stopped with this limitation: **no detached executor or background service is
  added**. The smallest follow-up belongs in the harness: a supported owned-job
  wait/completion event with exact exit/log retention, cancellation and cleanup,
  interrupted-supervisor handling, and fail-closed missing/corrupt receipts.
  It requires its own implementation and lifecycle/security tests.

Official context: [Codex advanced configuration](https://learn.chatgpt.com/docs/config-file/config-advanced)
explains configuration layers and distinguishes the automatic reviewer from the
sandbox boundary. This proposal changes neither. Installed runtime evidence
takes precedence over assumptions about a newer online product version.

## Proposed changes; activation and rollback BLOCKED

Only [worker](worker-execution-rules.md) and
[orchestrator](orchestrator-execution-rules.md) supplements are proposed.
They call for supported blocking completion, bounded and truthful handoff,
consolidated authorized reads, all applicable final gates and event-driven AO
report handling. They add no permissions or test-selection changes.
**No live configuration has been activated. Activation and rollback through the
installed CLI are unsafe and prohibited, including full-JSON replacement.**

Precise proposed delta: append the worker supplement to existing `agentRules`
and the orchestrator supplement to existing `orchestratorRules`, separated by a
blank line. Preserve all other keys and values, including `agentRulesFile` if
present. The pure offline helper now requires
`propose_rules(config, worker_text, orchestrator_text, reference_config=reference)`:
the capture must match an independently verified complete reference, including
types and all unknown fields, before any candidate is produced. Both role
model/effort pairs must be explicitly present and nonempty, even if both inputs
are identically incomplete. Missing effort,
field loss, reference drift and type changes fail closed without mutating inputs.
Comparing two copies from the same lossy source cannot establish completeness.
The helper verifies neither reference provenance nor read/write transport
fidelity. Its independent copies are not a verified backup/rollback mechanism.

The original handoff's CLI capture/application procedure is withdrawn. The
following is the current evidence and stop condition, not an activation recipe:

1. Do not use the installed `project get --json` for lossless capture, backup or
   verification. Do not use `project set-config`, any focused flags, `--clear`
   or `--config-json` for application or rollback. Both CLI capture and writing
   can silently remove effort; post-write detection would already be too late.
2. A local read-only comparison captured only model/effort field-presence
   booleans from the CLI versus authoritative saved project configuration.
   [The sanitized schema fixture](../../tests/fixtures/agent-efficiency/installed-config-capture-schema.json)
   records model presence in both, effort presence only in saved configuration,
   and whole-object inequality. No raw live configuration was exported. The
   coordinator also independently verified that the existing read-only
   `GET /api/v1/projects/mantis-studio` retains both effort fields. This establishes
   read evidence only; it does not establish a safe write mechanism.
3. No full-fidelity application/rollback mechanism has been verified in this
   worker. The installed guide points to Desktop Project Settings or the daemon
   API, but that recommendation alone does not prove their write path preserves
   all fields. No daemon write route is proposed or inferred, and no live write
   was issued to probe it. **Activation and rollback remain BLOCKED** until a
   supported mechanism and its lossless serialization are independently verified.
4. Any future separately authorized activation plan must first establish complete
   private capture, lossless writer/readback and rollback support, unknown-field
   preservation, and protection against concurrent config changes. It must
   preserve worker `gpt-6.1-sol/high`, orchestrator `gpt-6.1-sol/medium`, auth,
   identity, permissions and all other settings. Review rule composition and
   conflicts, compare the entire precondition object, verify exact readback and
   a separately authorized fresh session's actual model/effort/security context.
   Rollback must not overwrite unrelated intervening edits. These are required
   preconditions, not a verified executable procedure or activation approval.

## Verification scope and measured limits

The deterministic tests exercise response/cumulative accounting, resets,
compaction, conflicting/foreign records, malformed/partial input, missing files,
unknown accounting semantics, private sentinels, command/time ambiguity,
concurrent read-only execution, symlinks/FIFOs, invalid output options, and exact
offline configuration preservation and rejection of lossy CLI-derived captures,
missing references, missing effort/unknown fields, drift and type changes. A
safely owned protected-path stand-in
remains byte-identical; no real protected worktree is attacked. A Python audit
tripwire verifies analysis succeeds with filesystem writes, child-process launch
and network creation denied. The tripwire and analyzer are **not OS sandboxes**.

A small same-input benchmark in the test suite compares three separate analyzer
processes against one batched process, each reading the same three synthetic
100-response sessions and producing identical accounting. Process launches are
3 versus 1; wall time, child CPU and stdout byte counts are printed. This tests
consolidation of deterministic collection work, not a historical agent baseline.
No additional models or Guardians are invoked inside the benchmark. Worker
tokens, Guardian activations/tokens and model activations before/after are
**not measured**. No model/token savings percentage is claimed. Real end-to-end
polling reduction remains unverified until an authorized supported harness
completion path and comparable agent runs exist. Relevant runtime I/O and
hardware performance are outside this tooling-only workload.

One local run of this representative fixture measured the following (single
observation, not a performance threshold or model-savings estimate):

| Measurement | Separate collection | Batched collection |
| --- | ---: | ---: |
| Analyzer process launches | 3 | 1 |
| Wall seconds | 0.149169 | 0.049900 |
| Child CPU seconds | 0.148141 | 0.049545 |
| Captured stdout bytes | 4,542 | 3,794 |

Accounting is equal in both runs. Output framing differs because the separate
invocations each repeat the schema/limitations envelope. The process-launch
count is not a measured tool-call or model-activation count.

Applicable local gates are Python syntax/static analysis, all analyzer
regressions, workflow parsing and whitespace/scope/security-integrity checks.
No production/CMake/CTest behavior or registration changes, so unrelated Qt,
headless, ARM64, sanitizer and full 1B qualification runs are not re-executed
locally for this Python/documentation patch. Existing required remote architecture
jobs remain independent-review gates; pending is not PASS. No Windows/macOS,
hardware, generic-executor cancellation/cleanup or receipt-recovery validation
is claimed. Those executor tests are inapplicable because no executor is added.
