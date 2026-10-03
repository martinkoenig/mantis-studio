# ADR-010: Compute backends are abstracted and CUDA is optional

Status: Accepted — Architecture v1 frozen baseline

## Context

CPU-only ARM systems and multiple GPU vendors must remain valid execution targets.

## Decision

Express resources/backends in node descriptors and select an available compatible backend.

## Consequences

The Skeleton implements CPU and rejects unsupported GPU/remote requests; vendor handles stay out of contracts.

## Alternatives considered

CUDA-specific pipeline/data types would make portability and alternate backends expensive.
