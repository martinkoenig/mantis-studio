## Scope and tracking

- Work issue: `Refs #<number>` (or: **No issue — explain why this is a genuinely trivial one-off**)
- GitHub Milestone: <existing milestone or N/A>
- Target: `develop` unless explicitly authorized otherwise
- Objective, boundaries and relevant ADRs:

## Production changes and failure modes

- Changed components, behavior and integration interfaces:
- Critical lifecycle/threading/storage/QoS/error-precedence/ABI/performance considerations:
- Edge cases, rollback and explicitly unverified hardware/platform behavior:

## Local verification (worker)

- Exact feature HEAD SHA and required local build/test commands + PASS/FAIL:
- New negative/regression tests and how they detect the old defect:
- Any failure, unsatisfied gate or limited evidence — **never call PENDING a PASS**:

## Independent AO review / CI / issue synchronization

- AO reviewer findings / outstanding threads (reviewer is independent of implementer):
- Remote CI SHA and jobs (PENDING allowed at Codex handoff, **not** at final ACCEPT):
- [ ] Work issue has up-to-date confirmed/disproven findings, exact SHA/test links and open risks, or coordinator has an explicit evidence handoff to make that update.
- [ ] Hub #14 has been updated **only if** priority, blocker, acceptance or integration status materially changed, or intentionally left unchanged.
- [ ] No worker self-ACCEPT or unauthorized issue/milestone closure.

> This PR template is a handoff aid, **not automatic independent approval**. On GitHub, repository templates become active only after promotion to the default `main` branch.

<!--
English-only public GitHub records. Significant independent review findings
and exact-SHA CI decisions belong in chronological comments on the linked
work issue. Keep the work issue body synchronized with current status,
rather than appending dated history. Do not post private logs or secrets.
-->

