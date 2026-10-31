# SEC-EVENT-1 — GitHub security signals and AO handoff (proposal)

**Status:** Proposed only; **no webhook, scanner, GitHub setting, credential, AO automation, worker or CI gate is activated by this document**.
**Scope:** Mantis Studio repository `martinkoenig/mantis-studio`, GitHub native security findings and Dependabot pull requests. This is a process/integration study; a live receiver or new trust boundary requires separate implementation authorization, risk analysis and, where architectural, an ADR.
**Related policy:** [REVIEW-QA-1](review-and-test-qualification.md), [CI-STAB-2](ci-stab-2-qualification.md), [ADR-033](../adr/033-reliability-performance-release-gates.md), [AO orchestrator proposal](../agents/orchestrator-execution-rules.md).

## Observed repository facts (2026-10-31; configuration must be rechecked)

- GitHub default branch is **`main`**, while active implementation and architecture CI normally use **`develop`**. `main` is protected; `develop` was not reported protected by the repository branch API at inspection.
- The `develop` tree has `architecture` and `agent-efficiency` workflows; its `architecture` workflow handles PRs and pushes to `develop`, `main`, `integration/**`. Neither `.github/dependabot.yml` nor `.github/dependabot.yaml` existed on `develop` during inspection.
- GitHub's default-branch Dependabot dependency graph/alerts and security-update PRs do **not** automatically certify the newer `develop` dependency set. `target-branch: develop` is supported for **version-update** PRs, **not security-update** PRs. Default-branch and protected-branch CodeQL configurations require independent inspection. Do **not** change the default branch merely to suppress this mismatch.
- The user's newly enabled Dependabot, secret-scanning, CodeQL, push-protection and other security settings/alerts could not be independently enumerated through the available integration. Their **effective enablement, findings, available permissions and successful scan coverage remain unverified**. GitHub CodeQL default setup may live outside a committed workflow file.

References: [Dependabot alerts](https://docs.github.com/en/code-security/concepts/supply-chain-security/dependabot-alerts), [Dependabot target branch restriction](https://docs.github.com/en/code-security/reference/supply-chain-security/dependabot-options-reference), [CodeQL setup types](https://docs.github.com/en/code-security/concepts/code-scanning/setup-types), [GitHub webhook events](https://docs.github.com/en/webhooks/webhook-events-and-payloads), [AO lifecycle events](https://docs.orchestrator.inc/configuration/lifecycle-automation/), [AO project configuration](https://docs.orchestrator.inc/configuration/projects/).

## Event classes and intended handling

| GitHub signal | Routing / priority | Approval boundary |
| --- | --- | --- |
| `dependabot_alert`: new/reopened actionable vulnerability; malware indicator | Notify/security-triage with advisory, affected dependency, path/manifest, fixed version, relevance and source branch. Prioritize reachability/exposure and severity; do not automatically assume a fix applies to `develop`. | Human or independent reviewer authorizes remediation. Avoid automatically opening public issues containing exploit details. |
| Dependabot security-update or version-update **pull_request** | Existing AO claimed-PR observation, worker and reviewer may analyze proposal, reproducible builds and regression tests; compatibility/API/ABI effects need review. | Never auto-merge; required local and remote gates plus exact-SHA independent ACCEPT. |
| `code_scanning_alert`: introduced/reopened/regressed finding | Triage rule, trace, affected revision/branch; distinguish a new source vulnerability from tooling/false positive. | Critical/high or exploitable issue blocks relevant acceptance until resolution/explicit authorized disposition. |
| `secret_scanning_alert` or push-protection bypass | **Immediate restricted notification / credential compromise incident**. Revoke/rotate affected credentials, investigate scope and history as authorized; code cleanup alone is insufficient. | Never include secret values or raw alert payload in GitHub Issues, PRs, AO chats or logs; avoid autonomous handling of credentials. |
| Scanner workflow failure / missing analysis / skipped gate | Diagnose CI coverage and permission/configuration failure; scanner not running is not “no findings”. | Do not infer scan PASS from absent alerts or incomplete checks. |
| Security-policy/branch-protection change | Audit whether required gates or alert coverage changed and identify who authorized it where possible. | No silent security-policy weakening by an implementation worker. |

On an isolated software-detected vulnerability, follow the same **worker -> separate AO reviewer -> independent final review** pattern as other critical changes. An AO review is supporting evidence, not a replacement for security triage or branch protection.

## Phased implementation option (no activation authorized)

**E1 — Read-only inventory and branch gap audit.** With least-privileged authenticated GitHub access, enumerate active tools/settings (Dependabot, dependency graph, CodeQL/SARIF, secret scanning/push protection, branch protection/rulesets), actual alerts and bot PRs, default vs `develop` scanning, permissions and existing notifications. Never claim availability from presence of a settings toggle. Inventory C/C++ build dependencies not represented in supported manifests (CMake/system toolchains, vendored/header dependencies) and document their separate review plan. Record a sanitized, access-controlled report without secrets.

**E2 — Safe manual intake pilot.** Consume **read-only** security alert summaries through permitted GitHub UI/API or an AO Cue/manual triage; connect an existing Dependabot PR to an isolated worker only with explicit approval. Deduplicate by repository + alert type + alert ID + updated_at / PR head SHA; avoid unnecessary model activations. Map severity, exploitability, affected branch, validity and owner to a bounded queue of review tasks, not automatic code changes.

**E3 — Optional event-triggered adapter after explicit design review.** GitHub supports `dependabot_alert`, `code_scanning_alert` and `secret_scanning_alert` webhooks, but current documented AO lifecycle observers only handle claimed PR/check/review/merge events; AO's automation scheduler and issue intake are distinct. Do **not** assume a generic secure webhook receiver exists in AO. Evaluate trusted GitHub Actions/dispatch, a separately authenticated local pull-based notifier, or a minimal verified webhook receiver. A webhook design must authenticate HMAC signatures, enforce TLS, validate allowlisted repo/event/action, deduplicate delivery IDs, perform bounded/retry-safe processing, use narrow permissions, redact payloads, retain audit trail and never expose AO's unauthenticated loopback daemon to the internet. Public issue creation for sensitive findings is forbidden.

**E4 — Controlled automation and qualification.** Only after E1–E3 are independently reviewed and explicitly approved, enable notifications and optionally create review tasks/claimed PR associations. Automatic worker dispatch for security changes is off by default; no autonomous merge, policy modifications, force-push, credential operations or lowering CI gates. Test forged/duplicate/out-of-order events, permissions denial, missing scans, credential redaction and daemon-offline reconciliation. Preserve exact SHA and scanner results in final review evidence.

## Completion conditions for a future implementation

- **SE-01:** Verified GitHub scanner settings, available permissions, real default-`main`/`develop` coverage matrix and non-manifest dependency limitations.
- **SE-02:** Alert notifications or intake can be traced to original GitHub URLs and states without copying secret material or trusting unverified external text.
- **SE-03:** New/reopened events are deduplicated; resolved/dismissed events update state; daemon/CI outages reconcile without silent loss.
- **SE-04:** Human authorization, R1 source review, relevant local builds/tests and exact-SHA remote security/CI checks remain enforced for fixes.
- **SE-05:** No public exposure of AO local APIs, webhook credentials or secret payloads; unauthorized events cannot execute code or create privileged work.
- **SE-06:** Targeted negative security and integration regressions pass, and final acceptance is independent.

**Scheduling:** SEC-EVENT-1 should be triaged as a separate package; it must not interrupt the active recorder investigation or CI-STAB-2 solely because the integration is convenient. An actual critical secret exposure or exploitable vulnerability is an incident and may supersede normal milestone scheduling after explicit risk assessment.
