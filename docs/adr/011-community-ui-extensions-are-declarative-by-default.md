# ADR-011: Community UI extensions are declarative by default

Status: Accepted — Architecture v1 frozen baseline

## Context

Native UI execution inside Studio expands the failure and trust surface.

## Decision

Prefer declarative parameter metadata; reserve unrestricted native UI extensions for separate high-trust approval.

## Consequences

ParameterSchema exists; the example node has no configurable parameters and no community QML loader.

## Alternatives considered

Automatically executing arbitrary plugin QML in the main process weakens crash and permission boundaries.
