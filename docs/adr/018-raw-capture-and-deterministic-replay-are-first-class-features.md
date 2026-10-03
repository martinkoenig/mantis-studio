# ADR-018: Raw capture and deterministic replay are first-class features

Status: Accepted — Architecture v1 frozen baseline

## Context

Algorithms need repeatable inputs without physical hardware.

## Decision

Record canonical raw packets and allow recipes to reprocess stored capture artifacts.

## Consequences

The Skeleton replays frame zero and verifies byte-identical exported geometry across clients.

## Alternatives considered

Only storing final geometry would lose debugging, comparison and future reconstruction opportunities.
