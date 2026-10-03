# ADR-007: Control plane and high-bandwidth data plane are separated

Status: Accepted — Architecture v1 frozen baseline

## Context

Camera and geometry buffers should not burden command serialization.

## Decision

Send references/commands over the control protocol and large arrays via explicit data-plane mechanisms.

## Consequences

The local Skeleton maps immutable packet files; isolated jobs have explicit staging I/O.

## Alternatives considered

Sending every array through ordinary command messages would introduce hidden copies and poor scaling.
