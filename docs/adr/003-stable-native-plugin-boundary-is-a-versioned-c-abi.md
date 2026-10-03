# ADR-003: Stable native plugin boundary is a versioned C ABI

Status: Accepted — Architecture v1 frozen baseline

## Context

Native plugins may use different compilers and standard libraries.

## Decision

Use queried C function tables with fixed-width fields, opaque handles, struct sizes and ABI versions.

## Consequences

Allocation/release and exceptions are explicit; implementation classes remain private.

## Alternatives considered

C++ virtual classes, STL containers and Qt types would expose compiler/runtime ABI dependencies.
