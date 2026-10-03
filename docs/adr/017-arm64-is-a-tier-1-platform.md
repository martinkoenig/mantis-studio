# ADR-017: ARM64 is a Tier-1 platform

Status: Accepted — Architecture v1 frozen baseline

## Context

Embedded Mantis runtimes must not be deferred ports of an x86-only design.

## Decision

Choose portable APIs/dependencies and include native Linux ARM64 in the CI matrix from the beginning.

## Consequences

No x86 intrinsics enter core contracts; platform validation remains explicitly separate from the target declaration.

## Alternatives considered

Postponing ARM concerns would allow architecture-specific assumptions to harden into public APIs.
