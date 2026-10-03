# ADR-014: Completed pipeline data and project artifacts are immutable

Status: Accepted — Architecture v1 frozen baseline

## Context

Shared branches, history and replay require stable published inputs.

## Decision

Publish const semantic data and immutable buffer views; completed artifacts cannot be appended or overwritten.

## Consequences

Changing an algorithm or parameter produces a new artifact; output ownership and provenance remain explicit.

## Alternatives considered

Arbitrary downstream mutation would invalidate branches, caches and processing history.
