# ADR-006: First-party and community plugins use the same contracts

Status: Accepted — Architecture v1 frozen baseline

## Context

A privileged first-party implementation would conceal gaps in the extension API.

## Decision

Virtual Scanner, example processor and exporter use only the public plugin SDK.

## Consequences

Contract tests reject runtime includes in all plugin source; trusted execution affects location, not functionality.

## Alternatives considered

Private shortcuts would leave community plugins unable to reproduce the reference workflow.
