# ADR-001: Daemon-centric runtime using mantisd

Status: Accepted — Architecture v1 frozen baseline

## Context

Capture and project ownership must survive disconnected or failed frontends.

## Decision

mantisd owns operational state and service orchestration.

## Consequences

Sessions outlive UI processes; daemon shutdown and project recovery are explicit operations.

## Alternatives considered

An engine embedded in each UI would couple capture lifetime to presentation and duplicate state.
