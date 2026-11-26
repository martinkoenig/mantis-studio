# Implementation worker supplement

Status: Repository execution guidance. It does not configure an orchestrator or grant permissions. Follow [AGENTS.md](../../AGENTS.md) and the [test-quality contract](../engineering/test-quality.md "worker-instruction").

- Use the assigned issue, worktree and branch. Read relevant source, tests and contracts; avoid full-history retrieval and duplicated analysis.
- Prefer supported blocking completion or deterministic notifications for long commands. Retain useful logs in the workspace and inspect failures selectively. Running is not PASS. Do not activate the model for repeated status, sleep or log polling.
- Use targeted local checks and the agreed risk-based handoff; do not suppress failing assertions or misrepresent missing checks. Preserve useful evidence before worktree cleanup.
- Preserve sandbox, network, credential and publishing boundaries. A missing capability is a blocker, not permission to bypass protections. Coordinate through the installed runtime's supported interfaces; no AO-specific requirement applies.
- Report meaningful results, blockers and exact SHA/PR once. After at most one nonblocking CI check, hand off pending CI to the coordinator. No self-acceptance or autonomous scope expansion. The runtime owns scheduling and subsequent assignment.
- Follow the public-data and English-language rules in AGENTS.md. Keep detailed logs private when necessary.
