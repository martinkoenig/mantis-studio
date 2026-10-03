# ADR-002: Frontends are clients rather than engine owners

Status: Accepted — Architecture v1 frozen baseline

## Context

Desktop, embedded, mobile and automation must present the same system.

## Decision

Every frontend uses service commands and events through the Client API.

## Consequences

The Studio bridge contains presentation state only; work remains asynchronous and runtime-owned.

## Alternatives considered

A separate embedded application or direct UI-to-engine bindings would fragment behavior.
