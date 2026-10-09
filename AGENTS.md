# Codex instructions — Mantis Studio

These repository-wide instructions apply to Codex and other coding agents working in this repository. They govern **implementation and self-verification**, not the separate technical-lead planning/review process. A task-specific assignment adds requirements; do not weaken the repository's accepted architectural contracts or validation standards.

## Source of truth and preparation

- Start with the task and the **actual checked-out branch**. Use the relevant release-specific documents, not an assumed status from another branch. Distinguish implemented, accepted, provisional and hardware-pending work.
- Read only the documentation relevant to the change, following this guide:
  - `ARCHITECTURE.md` and `MANTIS_STUDIO_ARCHITECTURE.md`: architecture authority and frozen v1 baseline.
  - `docs/adr/README.md` and applicable ADRs: accepted decisions; extend rather than bypass contracts.
  - `ROADMAP.md` (when present) and release-specific `docs/architecture/` documents: scope, maturity and milestone boundaries.
  - `docs/architecture/reliability-performance-and-validation.md` (when present): **binding** reliability, data-integrity, performance and evidence standard; see ADR-033.
  - `CONTRIBUTING.md`, `BUILDING.md`, `CMakePresets.json` and `.github/workflows/`: actual coding, build and CI requirements.
- Inspect relevant implementations/tests before proposing changes. Do not regenerate existing architecture or invent new public contracts where an extension point exists.
- If documents conflict, identify the conflict and request a decision instead of silently choosing a convenient interpretation.

## Branch and workspace safety

- Before editing, check the working directory, current branch/HEAD and `git status --short`. Work **only** in the assigned repository/worktree and task scope.
- Preserve pre-existing user/agent changes and untracked files. Never discard or overwrite unexpected changes; surface conflicts and obtain direction. Do not touch adjacent worktrees.
- Never rebase, merge, cherry-pick, switch task branches, force-push or modify `main` without explicit authorization. Commit and push only as authorized by the assignment; report the resulting branch and commit.
- Keep patches focused and reviewable. Separate unrelated refactors and dependency upgrades. Do not edit generated artifacts as a substitute for changing their sources.

## Engineering standards

- Favor correctness, data integrity, explicit failure semantics, fault containment and deterministic recovery over convenient happy-path implementations. No silent corruption, fabricated provenance, hidden fallbacks, unbounded queues/retries or unreported data loss.
- Respect the daemon-centric capture/control ownership model, client/runtime and Plugin SDK/Client SDK boundaries, typed and versioned data/contracts, clock/coordinate/calibration provenance, and replay semantics.
- Preserve C ABI compatibility and Protobuf evolution rules; require an ADR and explicit migration analysis for genuine architectural changes.
- For C++/Qt work, address RAII, ownership and lifetime, thread affinity, cancellation, shutdown, race/deadlock risks, exception boundaries and error propagation. Follow the repository's CMake-selected language standard and `.clang-format` rather than assuming one from the task text.
- For acquisition and compute hot paths, avoid needless allocations, copies, polling, encoding and synchronization; enforce bounded resources/backpressure and measure performance rather than asserting it.
- For Qt/QML UI, preserve runtime boundaries and data contracts; check layout, responsive sizing, focus/input, scrolling, accessibility and reference-image fidelity when relevant.
- Distinguish simulation/software evidence from actual hardware validation. Never claim measured scanner accuracy, physical timing, safety, Windows/macOS support or production readiness without matching evidence.

## Efficient implementation and verification

- Optimize **total agent work and token consumption**, not prompt brevity. Be precise; avoid redundant file reads, oversized output dumps, speculative changes, repetitive investigations and unnecessary test cycles.
- Prefer narrow searches (`rg`), targeted inspection, minimal patches and existing build directories/caches where safe.
- For nontrivial changes, establish acceptance criteria and a short implementation/check plan; work in bounded logical stages. Avoid unrelated scope expansion.
- During iteration, run the narrowest tests that give useful feedback. Before declaring the assignment complete, perform **all applicable final validation** required by the task, `CONTRIBUTING.md` and CI (including Qt-enabled/headless, relevant sanitizers, regression, platform and performance checks). Do not replace required final checks with selective passing tests.
- For functional changes and bug fixes, add meaningful regression tests including negative/failure cases appropriate to risk. Never delete, weaken, skip or manipulate a failing test merely to obtain green results.
- If a required test fails, investigate and repair the cause, then re-run the affected checks and final verification. If blocked by unavailable hardware, infrastructure, external dependencies or an unsafe action, report the **exact unverified requirement and evidence**; do not label it PASS or loop indefinitely.
- When waiting for GitHub Actions or other asynchronous external operations, prefer one blocking wait command with a reasonable timeout (for example, `gh run watch --exit-status`) instead of repeated LLM-driven status polling. If blocking waits are unavailable, make only bounded, appropriately spaced status checks. Do not waste model calls on frequent polling, spin indefinitely, or treat a pending/timeout result as success.
- When CI applies, check results for the pushed commit where access is available. Pending, skipped or unavailable checks are not green checks.

## Completion report

Keep the final report concise and factual:
1. Implemented requirements, affected components, branch and commit(s).
2. Commands/checks executed with outcomes; relevant CI links or pending statuses.
3. Remaining risks, manual/hardware validations and explicit blockers.
4. Any deviations from the assignment and their rationale.

A successful agent exit is **not** independent acceptance. The planning/review chat performs the final external code review; provide enough evidence for it to decide ACCEPT, CHANGES REQUIRED or BLOCKED.
