# CUT-0C — Shared Development Baseline and Integration Policy

**State:** `develop` created directly from the independently accepted CUT-0B revision. The documentation-only CUT-0C commit and required GitHub Actions remain subject to final independent review. This record does not assert that GitHub branch protection is configured.

**Date:** 2026-10-25.

## 1. Immutable acceptance and promotion evidence

- Repository: `martinkoenig/mantis-studio` (modular monorepo).
- CUT-0B accepted source SHA: `e0a8652f2854a58aa611daa11c5c52051b303497`.
- Accepted source [CI run 40189227138](https://github.com/martinkoenig/mantis-studio/actions/runs/40189227138): **SUCCESS**, five required jobs on that exact SHA. Studio ON 184/184 on x86_64 and native ARM64; headless 144/144 on both; ASan/UBSan 144/144. The Studio ON UI-route repetition gates also passed on both architectures.
- `develop` was created with **exactly** that source commit as its initial HEAD. The CUT-0C documentation-only follow-up commit is its linear descendant. Do not confuse the original accepted source SHA with the later documentation commit SHA or claim that their CI results are interchangeable.
- Original `main` SHA: `ec045dd33628742297e015d0ac565a39d9d1e9b8`, preserved as the public default/release baseline. No main merge, tag, force-push or default-branch switch is authorized here.
- Existing Core, UI, calibration and integration histories remain reachable. No worktree is removed, reset, rebased or modified by this GitHub-only promotion.
- Relevant historical evidence: [CUT-0B qualification](cut-0b-integration-qualification.md), [C01 investigation](cut-0b-c01-project-capabilities.md), [C02 correction](cut-0b-c02-project-capabilities.md).

This is acceptance of the **combined software integration** only. It does not grant final acceptance to L8a-1a, the most recent UI correction, all of v0.4, physical calibration, Q6A/laser/firmware safety, or the unresolved project/replay isolation defect. Studio New/Open remain gated.

## 2. Branch roles and task lifecycles

| Ref | Purpose | Governance |
| --- | --- | --- |
| `main` | Stable public/release presentation | Unchanged by CUT-0C. Release promotion requires a separate explicit approval. |
| `develop` | Reviewed shared software integration baseline | Accept work only after an independently reviewed package and exact-revision combined CI. No unreviewed worker push. |
| `feature/*` | One scoped implementation package per isolated worktree and worker | Start from an explicitly recorded `develop` SHA and preserve independent branch history; work only inside assigned scope. |
| `integration/*` | Optional short-lived controlled merge qualification | Use when several accepted packages need joint semantic/CI qualification. Do not silently integrate unfinished packages. |
| Existing `feature/v0.3-*`, `feature/v0.4-*`, `feature/ui-*` | Historical and independently advancing source streams | Preserve refs and user worktrees. Their exact acceptance status remains separate. |

The GitHub repository default branch **remains `main`**, so newly created PRs and automation must explicitly target `develop`. Do not infer the correct base from GitHub defaults.

For each implementation package: (1) independent planning/review owner defines scope, contract boundaries, test evidence and exact base SHA; (2) a Codex worker gets a dedicated worktree/branch and completes applicable local builds/tests before push; (3) the reviewer inspects the actual diff, surrounding implementation, tests and exact-SHA remote CI; (4) outcome is **ACCEPT**, **CHANGES REQUIRED**, or **BLOCKED**; (5) only accepted packages enter controlled integration, where merged-state tests and CI are independently checked before updating `develop`. A passed feature-branch check is not a passed combined-state check. No force-push, branch deletion, automated self-approval, or automatic merge without a separately approved policy.

## 3. Parallel workstreams: ready for expansion, initially one worker

Initial Orchestrator pilot: **one Studio UI worker on the Framework 16**, with independent human-directed planning and review. Orchestrator is **not installed, configured or activated by CUT-0C**; a separate AO-1 package is required. Additional independent workstreams can be added later without changing the branch model:

| Workstream | Typical responsibilities | Coordination required |
| --- | --- | --- |
| Studio UI | QML views, Qt presentation/controllers and UI fixtures | Client-facing bridge, protocol and Core behavior changes require shared-contract review. |
| Runtime/Core | `mantisd`, services, jobs, plugin/device authority, memory and storage | UI-visible behavior, data/schema and realtime/device control are shared boundaries. |
| Acquisition/recording | Device plugins, image capture, lossless recorder and replay qualifications | Runtime ownership, hardware interlock/STOP, storage schemas and pacing contracts require shared review. |
| Algorithms/calibration | Typed processing, geometric calibration, reconstruction and tests | Coordinate/time provenance, pipeline/semantic data and ABI/SDK contracts require shared review. |
| Shared contracts/integration | Protobuf, public SDK/ABI, central build and CI, accepted ADRs | Integration-owner approval before dependent workers implement incompatible assumptions. |

The mapping is **coordination guidance**, not enforced filesystem isolation. Shared hotspots include `protocol/protobuf/mantis.proto`, C ABI/SDK headers, `src/client/`, `src/services/services.cpp`, `apps/studio/bridge.cpp`, root `CMakeLists.txt`, `AGENTS.md`, and `.github/workflows/`. A worker requiring changes outside its assignment must first raise a contract-change proposal specifying producer/consumer impact, versioning, migration, error semantics and tests. Do not give two agents writable authority over the same worktree.

Keep the Studio runtime + Qt client + shared contract code in this monorepo for now. Separate firmware and Unreal VirtualLab repositories retain independent toolchains and release decisions. The unmerged `docs/parallel-development-study` branch is **non-normative background**, not an accepted repo-split or new ADR.

## 4. Integration, safety and CI requirements

Required combined CI on the exact revision proposed for `develop`:

- Linux Ubuntu 24.04 x86_64: Studio ON and OFF, full CTest.
- Native Linux ARM64: Studio ON and OFF, full CTest.
- ASan/UBSan headless: full CTest, leak/error detection.
- Mandatory UI-route repetitions within Studio ON jobs and all registered Core/StudioBridge, projected-light, storage, PTY and regression tests. Do not skip the intentionally retained project-isolation test or the negative lossless-recorder test.

Codex must run all applicable local builds/tests before committing and pushing; Codex does **not** wait or repeatedly poll remote CI. The independent reviewer checks final CI on the **exact SHA**, then records ACCEPT / CHANGES REQUIRED / BLOCKED. Unverified, failing or skipped required checks block integration. Require explicit human authorization for merges and release/main promotion.

**Safety:** This development promotion does not authorize HIL, USB passthrough, Q6A flashing, serial hardware, laser enable, firmware changes or physical measurements. Such work remains under separately accepted safety/authorization boundaries. An Orchestrator pilot must not expose host secrets, credentials or unrestricted hardware access to workers; use bounded permissions and preserve the existing isolation policies.

## 5. GitHub enforcement — not yet active

CUT-0C creates the `develop` ref and documents the required process. The available GitHub connection cannot modify branch protection/rulesets, so **no enforcement has been asserted or silently enabled**. Before autonomous or multi-worker integration, a repository administrator should review GitHub Settings → Rules → Rulesets (or Branch protection) for `develop` and configure:

1. Required current five CI job contexts, including both ARM64 jobs and sanitizer; ensure the UI route gate remains mandatory through Studio ON.
2. Pull-request-oriented integration with an explicit independent review decision. As a single-maintainer repository, avoid a platform approval requirement that makes legitimate merges impossible; an AI reviewer is not a GitHub human approver.
3. Prevent force pushes and accidental deletions of `develop`; deliberately plan any administrator bypass and emergency recovery.
4. Verify settings actually apply on the repository plan/visibility and to any actors/bots used. Do not treat documented intentions as enforced controls.

The default branch remains `main`. Ruleset configuration and Orchestrator activation are deferred, not implied completed.

## 6. Current test inventory and documentation provenance

[The CUT-0B inventory](cut-0b-test-inventory.json) is **historical evidence** of the original integrated candidate: 183 Studio ON / 143 Studio OFF registrations. C02 added one Linux-only regression, `project-public-capabilities-negative-writer`, without removing or disabling any original registration. The independently verified [CUT-0C promoted inventory](cut-0c-test-inventory.json) is derived from that original inventory plus the reviewed one-test C02 delta and cross-checked against the five successful exact-SHA CI logs: **184 Studio ON / 144 Studio OFF / 144 sanitizer**. The two inventories intentionally have different scopes; never rewrite historical evidence.

This policy and the status pointers in README/ROADMAP/ARCHITECTURE/UI README are documentation-only. Source and test behavior remain identical to accepted `e0a8652f`. The follow-up documentation commit nevertheless requires its own GitHub CI before final CUT-0C acceptance.
