# Parallel Development and Repository Strategy — Comparative Study

**Status:** PROPOSAL / NON-NORMATIVE. Prepared 2026-10-11. This is a decision input for the post-v0.4 integration/engineering milestone, **not** an accepted ADR, a permission to reorganize code, or evidence of completed validation.

**Scope:** Git repository topology, module ownership, independent Codex implementation/review, dependency and compatibility management, branch integration, and evidence-based CI. The accepted Mantis Studio architecture, ADRs, and release gates remain authoritative. No active v0.3, v0.4, UI, firmware, or VirtualLab development branch is changed by this study.

## 1. Comparative evidence from established projects

| Project | Observed development organization | Lesson for Mantis |
| --- | --- | --- |
| [LLVM](https://github.com/llvm/llvm-project), [developer policy](https://llvm.org/docs/DeveloperPolicy.html) | LLVM, Clang, LLDB, LLD and related modules coexist in a monorepo. LLVM explicitly cites atomic cross-component commits, simpler CI and collaboration; modular builds can select components. | Keep the tightly coupled runtime, core domain types, wire contracts, SDKs, and cross-layer tests atomically verifiable while providing small buildable modules. |
| [Qt super-repository](https://github.com/qt/qt5) | Independent module repositories (e.g. qtbase, qtdeclarative) are pinned as Git submodules by an integration super-repository, representing a CI-tested combination. | For independently shipped hardware/firmware/simulator products, record **exact validated cross-repository revisions**. Do not casually add Git submodules: evaluate their operational costs first. |
| [Chromium](https://github.com/chromium/chromium/blob/main/docs/contributing.md), [OWNERS](https://chromium.googlesource.com/chromium/src/+/main/docs/code_reviews.md) | A large primary codebase combines internal modules with external dependencies and explicit directory-level reviewers/owners and mandatory code review. | Assign an accountable contract owner for shared paths and require module-specific review on cross-boundary changes, rather than depending on isolated worktrees alone. |
| [Kubernetes](https://github.com/kubernetes/community), [owners rules](https://www.kubernetes.dev/docs/guide/owners/), [repository policy](https://github.com/kubernetes/community/blob/main/github-management/kubernetes-repositories.md) | A central project coexists with SIG-owned subprojects, some in separate repositories; directory-level review and approval roles govern contributions. | A hybrid repo strategy can preserve core integration while allowing separately owned, independently versioned peripheral components. |
| [ROS 2 repository manifest](https://github.com/ros2/ros2/blob/rolling/ros2.repos), [colcon workspaces](https://colcon.readthedocs.io/en/released/user/what-is-a-workspace.html) | Many independently hosted repositories are composed into source workspaces; the workspace tool builds/tests packages and their dependencies. Some manifest entries track moving branches, so a release manifest must use immutable commits/tags for reproducibility. | For future multi-repo Mantis releases, maintain a machine-readable, validated component lock manifest with exact SHAs and integration tests. |

These are **different valid engineering models**, not evidence that a larger number of repositories automatically improves developer throughput or quality.

## 2. Preliminary recommendation

**Keep mantis-studio a modular monorepo for now. Maintain a small number of satellite repositories for components with genuinely independent hardware, build, versioning, or release lifecycles.**

Current/future candidates:

- **Stay in `mantis-studio`**: authoritative `mantisd`, shared domain/API/ABI/protocol, typed acquisition and calibration, storage, pipeline/compute services, public client SDKs, CLI, Studio Qt client, first-party device/plugin contracts, and cross-layer regression tests.
- **Already separate / preserve independence**: `mantis-x1-firmware` (microcontroller toolchain, hardware safety and release lifecycle); Mantis VirtualLab / Unreal project (Unreal asset/toolchain lifecycle), subject to verifying its actual repository state at the integration milestone.
- **Evaluate later, do not split prematurely**: Mantis Mobile as a standalone Android product; substantial external SDKs/plugins only if independent versioning, ownership, distribution, compatibility gates and testability justify it.
- **Do not split Studio UI yet**: it is logically a client, but currently shares Qt/CMake infrastructure, wire data types, source-tree integration tests, and versioned contracts. A separate repository now would introduce SDK distribution, version pinning, contract tests, packaging and release coupling without demonstrably solving the current workflow bottleneck.

### Repo-split decision criteria

Only propose a split after documenting:

1. Independent release cadence and compatibility contract (including an explicit minimum/maximum supported protocol/API version).
2. Independent build/toolchain/dependency and CI needs.
3. Ownership and security boundaries (firmware/device authority versus UI).
4. Availability of versioned SDK/binary or source-dependency consumption.
5. Reproducible cross-repository integration using immutable revisions.
6. Migration cost, performance/testing implications, bisectability and regression risks.
7. Evidence that the split improves **total reviewed-and-integrated throughput**, not just that its Git diffs become smaller.

Treat inability to commit a multi-component API update atomically as a real cost of multi-repo development.

## 3. Module boundaries within the Studio monorepo

Canonical data/control flow stays:

```text
Qt/QML views -> presentation models/controllers -> public client SDK
            -> versioned control/data transport -> mantisd
            -> service/runtime/device/compute/artifact modules
```

- Studio does not link private services, Store, DeviceRuntime, calibration OpenCV implementation, or SQLite. Maintain/enforce the CMake transitive dependency checks for **all** current and future Studio presentation targets (not only the original aggregate targets).
- UI workspaces should own their presentation models and UI behavior. The public SDK should expose typed, testable clients; use production-compatible fake clients for deterministic UI fixtures, failures, missing capabilities, stale snapshots and cancellation. A fake is **not** hardware evidence.
- Keep control and high-rate pixel/data planes separate. Do not force image bytes through Protobuf; preserve daemon ownership and metadata/provenance.
- Gradually move module-specific CMake lists/registrations out of a high-contention central `CMakeLists.txt` when doing so measurably reduces shared edits; preserve build compatibility and tests.
- Avoid implementing a speculative plugin framework solely to avoid touching `src/services/services.cpp`. Favor small explicit registration seams justified by real parallel tasks.
- Treat `protocol/protobuf/mantis.proto`, C plugin ABI, calibration/storage schemas, hardware safety and synchronization contracts as **controlled shared interfaces**, never arbitrary per-agent edit territory.
- A contract proposal must specify changes, compatibility/migration behavior, producer/consumer responsibilities, failure semantics, versioning, fixtures and executable contract tests before dependent agents implement against it.

## 4. Ownership and Codex work allocation

Retain manual implementation/review separation:

1. A responsible planning/review chat defines the package and its integration contracts.
2. Codex implements/builds/tests/commits/pushes in an isolated branch + worktree.
3. The planning/review chat independently inspects the **actual SHA, diff, surrounding code, tests and CI**; outcome is ACCEPT, CHANGES REQUIRED or BLOCKED.
4. Accepted independent work then undergoes a separate **integration review** against an agreed base, with combined builds/tests/required CI.
5. Only then does the integration owner authorize progression into the shared integration/release line.

Recommended initial *logical* owners: runtime/device authority; calibration & observations; algorithms & reconstruction; Studio UI; shared API/protocol/SDK; firmware (separate repo). Ownership is a review responsibility, not a prohibition on cross-module patches. Each task records its owner and any required cross-owner approval.

Shared files such as `CMakeLists.txt`, `AGENTS.md`, `protocol/protobuf/mantis.proto`, `src/services/services.cpp`, `apps/studio/bridge.cpp` and central CI workflows need explicit integration coordination.

**Do not automatically adopt Kubernetes OWNERS or GitHub CODEOWNERS:** for a single human user with AI agents, the code owner remains a human authorization role; AI agents do not become substitute GitHub approvers. A simple checked-in ownership map and contract-change rule can precede platform enforcement.

## 5. Branches, merging and release evidence

- Prefer short-lived package branches from an explicitly named, reviewed SHA. Never have two Codex agents mutate the same worktree or share a writable implementation branch.
- The **base SHA and dependency commits** are part of the task contract. Additional pushes to an upstream branch do not silently change the task's base.
- A package can be independently accepted on its own branch while its integration remains **PENDING**. Do not conflate these states.
- Use temporary/staged integration branches where multiple independently approved changes need combined evaluation; do not multiply long-lived integration branches without a concrete need.
- Integration requires inventory of commit ancestry and branch diffs, explicit handling of divergence, conflict-free test merge **plus semantic review**, and an end-to-end test matrix on the integrated SHA. Textual merge success does not imply correct behavior.
- Prefer fast-forward/merge-based shared history when collaboration would make rebases of published commits hazardous. Never force-push shared work without explicit authorization.
- No automatic cleanup of old branches: verify commits/content/reachability, retained tags/evidence, release notes, and worktrees before deletion; maintain recovery references.
- Require local applicable builds/tests before Codex pushes. Codex does **not** wait/poll remote Actions; it reports PENDING. The independent reviewer checks exact-SHA remote CI and must not ACCEPT unverified or failing mandatory checks.
- GitHub branch protection/rulesets and an appropriate merge policy are implementation options to evaluate, not settings claimed active. Review current GitHub plan/permissions and required checks before configuring them.
- M8 optical validation and v0.4 real laser/hardware verification remain separate gates from CI. No green software build substitutes for physical evidence.

## 6. Cross-repository compatibility

Where multiple repos jointly define a product, propose a versioned manifest containing exact Git commit hashes, contract versions, tested board/firmware profiles, validation evidence identifiers and release-specific requirements.

Candidate future file (NOT created or frozen here):

```yaml
schema_version: 1
components:
  mantis-studio: {commit: "<immutable-sha>", protocol: "<version>"}
  mantis-x1-firmware: {commit: "<immutable-sha>", uart_contract: "<version>"}
  mantis-virtual-lab: {commit: "<immutable-sha>", bridge_contract: "<version>"}
validation:
  software_ci: "<exact-run/evidence-ref>"
  hardware: "<exact-evidence-ref-or-unverified>"
```

Validate the format and references during a future implementation package. Cross-repository hardware/software compatibility cannot be inferred from branch names like `main` or `v0.4`.

## 7. Engineering milestone after v0.4

The following is a **proposed engineering milestone**, not a new product version or approved release gate. Preserve running v0.4/UI/firmware work until their respective owners hand off an accepted state.

| Package | Deliverable | Objective acceptance evidence |
| --- | --- | --- |
| PDEV-1 Repository consolidation | Verify ancestry, content, conflicts and required work; integrate approved v0.3/v0.4/UI changes under controlled review, retain all worktrees/branches until proven safe. | Complete change/commit inventory, audited merge resolution, no lost functionality, combined local builds/tests and exact-SHA CI PASS. Do not declare absent hardware evidence complete. |
| PDEV-2 Module and build boundaries | Audit/decompose hot-spot registrations and CMake scopes where justified; expand transitive dependency/ownership checks. | Forbidden dependencies cause automated failures; focused module and complete regression builds pass. |
| PDEV-3 Client/UI decoupling | Typed frontend adapter/presentation seams; production-equivalent fake clients, UI modules independently testable; preserve Qt lifetime/threading/data-plane invariants. | Headless UI tests and real-daemon integration pass; no private runtime links; functional/visual regression review. |
| PDEV-4 Integration workflow | Define branches/worktrees/owned interfaces, API evolution, review gates, cross-repo compatibility manifest proposal, acceptance evidence handoff. | Reproducible written procedure + automated guards where practical; no agent unilateral merge/cleanup. |
| PDEV-5 Parallel pilot | Implement 2–3 independently bounded packages simultaneously through separate Codex worktrees, independent reviews and one combined integration gate. | Measure elapsed time, integrated throughput, review/correction cycles, merge conflicts, test failures, CI and rework; adjust policy based on evidence. |

Prioritize PDEV-1 before risky shared-core additions such as Live Calibration LC-1. PDEV-2–4 may be further decomposed based on repo inspection; PDEV-5 should use genuine product features where possible, not artificial demo code.

## 8. Decision checkpoints and non-goals

Before implementing the study:

- Refresh Git branch/commit state: v0.3, v0.4, UI and firmware evolve concurrently.
- Confirm release/software/hardware acceptance reports independently; branch history alone is not acceptance.
- Decide which components are truly coupled and which can ship independently.
- Define stable client/API contracts and shared-file ownership before assigning concurrent interface changes.
- Select the minimal viable automation for reviewed integration (build presets, targeted test matrix, complete required CI, contract compatibility, provenance audit).
- Quantify actual merge/review costs and engineer-hours saved before adding more agents/repositories.

**Non-goals now:** moving Studio to a separate repository, restructuring the entire tree, creating a generic agent orchestration service, replacing human-planned Codex workflow, accepting hardware on simulation, merging any development branch, or imposing a heavy mandatory super-repository.

## Primary references

- LLVM Developer Policy: https://llvm.org/docs/DeveloperPolicy.html
- LLVM modular CMake: https://github.com/llvm/llvm-project/blob/main/llvm/docs/CMake.md
- Qt super-repository README: https://github.com/qt/qt5
- Chromium contributing / OWNERS: https://github.com/chromium/chromium/blob/main/docs/contributing.md
- Kubernetes ownership: https://www.kubernetes.dev/docs/guide/owners/
- Kubernetes repository policy: https://github.com/kubernetes/community/blob/main/github-management/kubernetes-repositories.md
- ROS 2 source workspace manifest: https://github.com/ros2/ros2/blob/rolling/ros2.repos
- colcon workspace guide: https://colcon.readthedocs.io/en/released/user/what-is-a-workspace.html
