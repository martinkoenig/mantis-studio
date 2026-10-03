# ADR-015: Canonical geometry uses millimeters and right-handed coordinates

Status: Accepted — Architecture v1 frozen baseline

## Context

Interoperable plugins need one explicit world/CAD convention.

## Decision

Use +X right, +Y forward, +Z up, millimeters; keep transforms double precision and name T_target_from_source.

## Consequences

Attributes carry units and sensor frames remain explicit; exporter documents millimeter positions.

## Alternatives considered

Implicit camera conventions or untagged unit changes would make geometry and measurements ambiguous.
