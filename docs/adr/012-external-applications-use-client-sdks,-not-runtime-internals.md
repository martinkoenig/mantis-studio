# ADR-012: External applications use Client SDKs, not runtime internals

Status: Accepted — Architecture v1 frozen baseline

## Context

Automation should remain compatible when scheduling/storage implementation changes.

## Decision

Expose service-oriented C++/Python clients and keep runtime implementation classes internal.

## Consequences

CLI and Studio link only to Client API, data and presentation modules; process tests verify real IPC.

## Alternatives considered

Linking external tools to the artifact store or device runtime would bypass authority and locking.
