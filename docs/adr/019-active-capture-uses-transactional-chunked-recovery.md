# ADR-019: Active capture uses transactional chunked recovery

Status: Accepted — Architecture v1 frozen baseline

## Context

A runtime/process failure should not invalidate all captured data.

## Decision

Journal durable object chunks and metadata transitions; classify unfinished captures as RECOVERABLE.

## Consequences

SQLite transactions and file sync/rename ordering preserve committed prefixes; hardware power-loss certification remains separate.

## Alternatives considered

A single monolithic mutable capture file would make interrupted writes harder to isolate and repair.
