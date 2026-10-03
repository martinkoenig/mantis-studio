# ADR-009: Qt/QML exists only above core/runtime boundaries

Status: Accepted — Architecture v1 frozen baseline

## Context

Headless and embedded runtimes must not require GUI framework types.

## Decision

Keep Qt in the Studio and rendering layer, outside foundational and runtime contracts.

## Consequences

A Qt-disabled configure builds the entire headless workflow; include checks protect public headers.

## Alternatives considered

Using QObject/QVariant throughout the engine would make Qt a core dependency.
