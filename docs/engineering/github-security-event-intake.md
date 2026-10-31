# SEC-EVENT-1 — GitHub security signals and AO handoff (proposal)

**Status:** Proposed only; **no webhook, scanner, GitHub setting, credential, AO automation, worker or CI gate is activated by this document**.
**Scope:** Mantis Studio repository `martinkoenig/mantis-studio`, GitHub native security findings and Dependabot pull requests. This is a process/integration study; a live receiver or new trust boundary requires separate implementation authorization, risk analysis and, where architectural, an ADR.
**Related policy:** [REVIEW-QA-1](review-and-test-qualification.md), [CI-STAB-2](ci-stab-2-qualification.md), [ADR-033](../adr/033-reliability-performance-release-gates.md), [AO orchestrator proposal](../agents/orchestrator-execution-rules.md).

## Observed repository facts (2026-10-31; configuration must be rechecked)

- GitHub default branch is **`main`**, while active implementation and architecture CI normally use **`develop`**. `main` is protected; `develop` was not reported protected by the repository branch API at inspection.
- The `develop` tree has `architecture` and `agent-efficiency` workflows; its `architecture` workflow handles PRs and pushes to `develop`, `main`, `integration/**`. Neither `.github/dependabot.yml` nor `.github/dependabot.yaml` existed on **either `main` or `develop`** at subsequent direct content checks.
- GitHub's default-branch Dependabot dependency graph/alerts and security-update PRs do **not** automatically certify the newer `develop` dependency set. `target-branch: develop` is supported for **version-update** PRs, **not security-update** PRs. Default-branch and protected-branch CodeQL configurations require independent inspection. Do **not** change the default branch merely to suppress this mismatch.
- GitHub Settings > Advanced Security screenshots supplied on 2026-10-31 **visually confirm enabled UI toggles**, but are not equivalent to successful completed scans or enumerated alerts. The available connector cannot retrieve the effective private security configuration, alerts or per-language CodeQL success. GitHub CodeQL default setup can exist without a committed workflow file. Neither the screenshots nor the repository API show successful scanning coverage of current `develop` source.

References: [Dependabot alerts](https://docs.github.com/en/code-security/concepts/supply-chain-security/dependabot-alerts), [Dependabot target branch restriction](https://docs.github.com/en/code-security/reference/supply-chain-security/dependabot-options-reference), [CodeQL setup types](https://docs.github.com/en/code-security/concepts/code-scanning/setup-types), [GitHub webhook events](https://docs.github.com/en/webhooks/webhook-events-and-payloads), [AO lifecycle events](https://docs.orchestrator.inc/configuration/lifecycle-automation/), [AO project configuration](https://docs.orchestrator.inc/configuration/projects/).

## Screenshot-confirmed UI configuration (2026-10-31; not a scan-success claim)

The user supplied screenshots of GitHub **Settings > Advanced Security**, showing:

| Setting in UI | Observed state | Verification consequence |
| --- | --- | --- |
| Private vulnerability reporting | Enabled (visible `Disable` control at cropped top) | Responsible disclosure entry point; does not by itself guarantee timely review. |
| Dependency graph / Dependabot alerts / malware alerts | ON / ON / ON | Detection features selected, **actual dependency inventory and findings not enumerated**. |
| Automatic dependency submission | Disabled | Assess supported manifests and additional C/C++ system/CMake dependencies before enabling or declaring complete SBOM coverage. |
| Dependabot auto-triage | 1 rule enabled | **Rule content unknown**; inspect for suppressive dismissals before trusting an empty alert list. |
| Dependabot security updates | ON | Can create patch PRs for supported vulnerable dependencies on default `main`; it does not automatically update newer `develop` sources. |
| Grouped security updates | OFF | Separate security updates currently selected. |
| Dependabot version updates | `Create dependabot.yml` shown | No version-update schedule configured; repository content checks confirm no config on `main` or `develop`. Security updates can still be enabled through settings. |
| CodeQL analysis | Default setup, last scan listed ~8 hours earlier | **Red configuration error:** `Code scanning configuration error`; **yellow warning:** `The default setup for CodeQL failed to enable JavaScript`. Successful JavaScript coverage is **not** established. This does not prove other languages failed or succeeded. Investigate tool-status per-language reports. |
| AI Scan for repositories | ON | Screenshot explicitly says **default branch only**, i.e. `main`, not independent proof of current `develop` coverage. |
| AI Scan for pull requests | ON | Supplemental AI findings for non-CodeQL languages; actual PR runs/coverage unverified. |
| Scan for AI applications | OFF | No AI application-specific scan selected. |
| Copilot Autofix | ON | Fix suggestions enabled; **not** approval, automatic merge or correct-patch guarantee. |
| Agentic autofix | OFF | No autonomous agentic-fix policy selected. |
| Code scanning protection rule UI | High or higher security level; standard alerts: only errors | These are **displayed thresholds**, not proof that an enforcing branch ruleset exists or applies to `develop`. |
| Secret Protection / Push protection | ON / ON | Secret detection and push blocking selected; effectiveness and bypass/alert history need authorized verification. |

### Immediate remediation: CodeQL setup failure before AO event automation

**SEC-CODEQL-01 (HIGH, configuration/coverage):** Open GitHub **Security > Code scanning > Tool status** and inspect the error for the default-setup JavaScript/TypeScript configuration, per-language successful/failed timestamps, percent of files scanned, actual `main` scans and whether C/C++ and Python are enabled. Follow the specific error; do not blindly switch to advanced setup, omit JavaScript solely to turn the UI green, or infer that `Last scan` means PASS. A documented `N/A` for a language is valid only if the current intended branch has no relevant production/test source in it. Re-run and verify affected analysis, preserving error details. GitHub's tool-status view reports **default-branch** tool health, not a guarantee of `develop` coverage.

**SEC-BRANCH-01 (HIGH, coverage policy):** Because `main` is default and protected while `develop` is the active integration branch, verify a reliable **CodeQL C/C++ and Python analysis path on `develop` and PRs targeting it**. The documented default setup runs on default/protected branches and PRs targeting them; `develop` may require a separately approved branch ruleset/protection or explicit advanced workflow. Review actual settings and avoid unreviewed branch-protection changes. In addition, evaluate version-update automation and distinct security-alert coverage for dependencies introduced on `develop`; changing `target-branch` is not a security-update fix.

**SEC-DEPS-01 (MEDIUM, inventory):** Verify which manifests and resolved dependency versions GitHub actually recognizes. The C++/Qt/OpenCV/SQLite/Protobuf/CMake/apt toolchain includes dependencies that are not necessarily representable in ordinary Dependabot ecosystems. Enable automatic submission only if a supported detector adds useful validated coverage; use an authenticated inventory/SBOM submission process otherwise, subject to separate permissions and reviewer approval.

**SEC-RULES-01 (MEDIUM, enforcement):** Inspect the enabled Dependabot auto-triage rule and the actual ruleset binding Code Scanning merge-protection thresholds. A severity dropdown shown under Settings is not evidence of merge blocking. An empty alert list can mean **no completed scan, stale branch, unsupported manifest, auto-dismissal or genuine no findings**; distinguish these possibilities.

Do not treat any of these observations as authorization to toggle GitHub settings, expose security alerts, open public vulnerability issues, start autonomous remediation or activate AO hooks.

### CodeQL tool-status follow-up (user screenshots, 2026-10-31)

The subsequent GitHub `Security and quality > Code scanning > Tool status > CodeQL` screenshot shows the following **successful** default-branch language configurations: **C 18/18 files, C++ 247/248 (UI rounded to 100%), Python 46/46, and GitHub Actions 1/1**. These file counts/green indicators describe **`main` at `d2614b96`**, not a completed analysis of the newer `develop` branch. File coverage is not proof of vulnerability detection, effective C++ dataflow/build configuration, or 100% security assurance.

**JavaScript/TypeScript is the sole shown failed configuration**, reporting `CodeQL exited with errors`, `No JavaScript/TypeScript code found`, `No code scanning results` and `Could not process some files due to syntax errors: Unexpected token`. Live Git-tree checks at `d2614b96304ff5fd45db8f09076da058afb9230d` (`main`) and `c068462d35fd20a1d882084ffd86056ee23c18a7` (`develop`) found exactly **one** `.js` file on each branch: `ui/projects/ProjectsTags.js`. Its first line, `.pragma library`, is a Qt/QML JavaScript directive, not ordinary ECMAScript syntax. It is a **plausible** origin of `Unexpected token`, but the screenshot does not identify the rejected file; do not state the parser diagnosis as proven.

The user then displayed CodeQL default configuration with **JavaScript/TypeScript unchecked**, while GitHub Actions, C/C++ and Python remained checked; **`Save changes` was still visible**, so saving and a subsequent successful scan were **not yet verified**. For the current QML-only JavaScript footprint, deliberately disabling unsupported/unhelpful JS/TS CodeQL analysis is reasonable **if documented**; it must be revisited when conventional JS/TS code is introduced. Do not alter the QML source merely to appease a generic JS parser. After saving, confirm a new successful scan for the three selected language groups, no continuing active-configuration error, and actual `develop`/PR coverage separately. Inspect the CodeQL failed-run file link if the language is retained instead.

Do not reinterpret **SEC-CODEQL-01** as a failure of C/C++ or Python: those language analyses show successful statuses for the inspected default-branch revision. The remaining priority is making the coverage and protection of `develop` evidence-based. Note that the CodeQL C++ default setup may use build mode `none`; successful file scanning alone is insufficient to establish the depth of interprocedural analysis for Qt/CMake-specific paths.

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
