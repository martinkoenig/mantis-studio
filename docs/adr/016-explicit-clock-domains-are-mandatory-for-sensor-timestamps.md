# ADR-016: Explicit clock domains are mandatory for sensor timestamps

Status: Accepted — Architecture v1 frozen baseline

## Context

Sensor clocks differ from each other and from wall-clock time.

## Decision

Represent device timestamps with clock identity, host monotonic receipt, sequence and synchronization metadata.

## Consequences

The Virtual Scanner emits a deterministic clock; calibration/processing does not infer timing from wall time.

## Alternatives considered

Treating every timestamp as a universal integer invites silent synchronization errors.
