---
name: Engineering work package or incident
about: Create a scoped implementation package, reliability incident or security-safe investigation with evidence and acceptance gates.
title: ""
labels: ["status:planned"]
---

## Objective, scope and priority
<!-- One independently valuable deliverable or defect; include relevant architecture links and intended milestone. -->
- Work ID / title:
- Priority: P0 / P1 / P2 / P3 (justify P0)
- Related milestone (existing only):
- Scope and explicit non-goals:

## Current evidence and epistemic status
<!-- Cite exact commit/CI/PR/ADR links, not chat recollections. -->
- CONFIRMED:
- REPORTED / NOT YET INDEPENDENTLY VERIFIED:
- HYPOTHESIS / OPEN:
- DISPROVEN:

## Acceptance and verification
<!-- Include meaningful negative tests and applicable local/remote CI, hardware limits. -->
- [ ] Requirements, production/test contracts and failure modes covered
- [ ] Focused negative/regression validation on real paths
- [ ] Applicable local builds/tests verified
- [ ] Separate AO review and independent exact-SHA CI ACCEPT

## Handoff
- Worker/PR/exact SHA (only if verified):
- Reviewer/decision and next action (not worker self-ACCEPT):
- Link [Engineering Status #14](https://github.com/martinkoenig/mantis-studio/issues/14) if this is new active priority

> This template will only appear in the GitHub issue picker once deployed to the default `main` branch. Never publish secret-scanning payloads in a public issue.

<!--
Maintenance contract: write all issue content and future comments in English.
Keep this description as the concise current state. Post material findings,
disproven hypotheses, exact-SHA CI evidence and review decisions as new
chronological comments with public-safe evidence links. Update the body
only when its current status/acceptance gates change. Never post secrets.
-->

