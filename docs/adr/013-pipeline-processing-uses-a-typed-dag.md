# ADR-013: Pipeline processing uses a typed DAG

Status: Accepted — Architecture v1 frozen baseline

## Context

Branching, reproducibility and backend planning require explicit dependencies.

## Decision

Compile typed input/output connections into an acyclic execution plan; keep feedback in the control plane.

## Consequences

Schema versions, cycles, fan-in ownership and unsupported backends are validated before execution.

## Alternatives considered

Unstructured callbacks or cyclic data flow conceal dependencies and backpressure semantics.
