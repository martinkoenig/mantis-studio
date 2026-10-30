# ENG-EFF-2A — Repository instruction integration

Base: independently accepted PR #6 integration
`664544d38c6418ba21c5f90449f311174ef69987`. All seven accepted file blobs and
the complete base tree were compared with the original accepted head
`6ecedbffad4c6fa95aada4b6fbd61684288cb0ae` before implementation. No accepted
commits were recreated or cherry-picked.

## Integration and role boundary

The repository-root [AGENTS.md](../../AGENTS.md) adds one role-scoped reference
to the accepted [worker execution supplement](worker-execution-rules.md).
Implementation workers must read that file and follow its operational guidance.
Its unactivated-proposal label continues to describe **AO project configuration**;
this package adopts the worker guidance through the repository entry point.
No AO configuration field or activation mechanism is required.

Existing AGENTS.md policies are retained verbatim. The accepted worker guidance,
[orchestrator proposal](orchestrator-execution-rules.md), and
[1A investigation](execution-efficiency.md) are unchanged. The orchestrator
proposal is not added to the mandatory reference chain. Repository guidance
does not assign implementation duties to orchestrators or independent reviewers,
override their role instructions, authorize additional workers, or relax any
security, validation, architecture, SDK/plugin, hardware or review gate.

The link is an instruction to the assigned worker to **read the document**.
It is not a Markdown include, an automatic transitive import, a custom loader,
or an AO `agentRulesFile` setting. No installed binary, model, effort,
permission inheritance, sandbox/network setting or product behavior changes.

## Installed loading evidence and limits

Inspected executable: Codex CLI **0.166.0**, at the installed standalone Linux
release path. Version/help inspection made no model request or session launch.
Only allowlisted file/policy metadata and instruction-presence booleans were
extracted; no private prompts or session content were exported.

Before editing, this existing worker's native rollout had one root
`# AGENTS.md instructions for ...` packet containing the exact 7,338-byte
repository AGENTS.md, across ten task starts. This directly verifies initial
native loading for this worker. It does not prove reloading changed files or
following links automatically. No global AGENTS/override or repository-root
override file was present, and the inspected user config had no explicit
instruction override, fallback-name or byte-limit setting.

After editing, during this same task turn, the one native packet still matched
the original root file and none matched the updated 7,700-byte file. That is
not a next-turn or resume reload test. The new reference was available through
the authorized file inspection/editing context, not a demonstrated native reload.

[Official OpenAI instruction-discovery documentation](https://learn.chatgpt.com/docs/agent-configuration/agents-md)
describes startup discovery from the project root to the working directory,
override-file precedence, root-to-leaf ordering, and a default 32 KiB limit.
The small root addition fits that default. Different profiles, directories,
overrides, or limits can change discovery; size alone does not prove loading.

| Session case | Established behavior | Unverified boundary |
| --- | --- | --- |
| New implementation worker | Initial native root loading was observed in this worker. A worker started on a checkout containing the new AGENTS.md is instructed to read the linked supplement. | No new worker was launched with this revision. Discovery and role-obedience are not an end-to-end test of future sessions. |
| Existing worker | The current worker explicitly read AGENTS.md and the accepted supplement during this authorized task; temporary session rules continue. | Editing a file is not proof of hot reload. Re-read applicable repository instructions explicitly within an existing authorized assignment; do not restart or create a session merely to test loading. No next-turn or resume reload guarantee is claimed. |
| New orchestrator using Codex | The native repository mechanism is not AO-role-specific; the new reference explicitly applies to implementation workers, preserving the orchestrator assignment. | No fresh orchestrator was started, so the new orchestration path is inferred from the provider contract, not observed. Its working directory, instruction overrides, and prompt precedence still matter. The orchestrator proposal is not activated by this change. |

No fresh-session, restart, restore, model-run, or prompt-render experiment was
performed. The installed debug help exposes a prompt-input renderer, but help
alone did not establish that it avoids creating session state; it was not run.
No automation or configuration change is hidden behind the reference.

## Verification

The existing `agent_efficiency_test.py` entry point now also checks the actual
repository reference chain. Tests resolve local file targets and traverse Markdown,
verify the worker reference is confined to its section, reach CONTRIBUTING and
ADR-033, and ensure the orchestrator proposal is not bootstrapped. Fixtures
exercise broken targets, parent/symlink escapes, anchors, external links without
network access, cycles and terminal non-Markdown files such as LICENSE. The
reference auditor is test code, not a Codex instruction parser or a new runtime
security boundary.

The scoped-reference regression failed before the AGENTS.md addition. An
additional local Git comparison checks that the entire accepted AGENTS.md is
retained as a byte-identical prefix and that accepted guidance, contribution,
security/architecture documents, build files and CI registration are unchanged.
These checks protect structure and policy preservation. They do not prove that
a model will interpret natural-language conditions correctly; role scope and
compatibility with mandatory instructions also require independent review.

Run the existing entry point:

```text
python3 -B -m unittest discover -s tests/unit -p agent_efficiency_test.py -v
```

The existing workflow already runs this entry point when its test file changes.
No workflow path filters or build/test registration are changed. Therefore a
future AGENTS-only edit does not automatically trigger this narrow workflow;
run the command explicitly when reviewing instruction/reference changes.

Applicable instruction checks are the full focused Python suite, Python
syntax/static checks, Markdown reference resolution, and the Git
scope/policy-preservation comparison. Product code and build/test registration
are unchanged. The C++ fixture correction below also requires the relevant
compiled and sanitizer validation. Historical qualification is not repeated.
Required remote CI remains an independent-review gate. No model-efficiency
savings, universal session loading, hardware validity or permanent AO rule
activation is claimed.

## CI recovery: fixture delivery load

The sanitizer push run for the initial ENG-EFF-2A head failed `projected-controls`
with an explicit LOSSLESS queue-saturation error, without a sanitizer memory
diagnostic. Its camera-only contract fixture emitted at approximately 1,000
frames/second although its sole ordinary-capture caller tests camera/projected
ownership exclusion, not storage throughput. Slow durable writes could fill
the real lossless recorder before that ownership test completed.

The fixture now supplies one startup FrameSet per start, then honors the
acquisition ABI's not-ready return with a bounded timeout. A deterministic ABI
regression checks repeated empty reads, invalid timeouts, stop and restart;
the ownership case also verifies exactly one produced/committed frame, no
saturation and no capture error. The regression fails against the old fixture.
Production queue capacity, admission deadline, explicit saturation failure,
negative writer tests, test selection and registrations are unchanged.

The separate sanitizer PR run failed the valid GREY `phase-jitter-right`
recorder scenario during capture construction. Its catch block replaced the
runtime error with `CHECK(!valid)`, so that CI log cannot establish the original
exception. Inspection found that the recorder scenarios did not select the
existing finite-rate fake-camera option documented in
[projected-light control validation](../architecture/projected-light-control-api.md).
Their phase sources therefore delivered at CPU speed rather than the profile's
120 Hz host rate. Algorithm-only scenarios intentionally retain that behavior.

Recorder scenarios now explicitly select `MANTIS_X1_FAKE_PACE=1`, preserving
every native timestamp, sequence, pixel, exclusion, accounting and exact-replay
assertion. A 150 ms first-writer pause exercises the storage/scheduler-stall
condition; it is fault injection, not a new throughput claim. The catch block
also retains the actual runtime diagnostic for unexpected valid-case failures.
No production fake-backend implementation, queue bound, admission deadline,
test timeout or detector option is changed.

With the injected pause and pacing disabled, the local sanitizer baseline
failed the existing three-second recorder-readiness assertion. With pacing
selected, the same GREY jitter case passes ASan/UBSan with leak detection;
all 18 recorder scenarios also pass headless. This establishes local
stall sensitivity and the correction's effect, not the exception hidden in
the original CI log.
