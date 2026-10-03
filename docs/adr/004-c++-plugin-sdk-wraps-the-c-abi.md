# ADR-004: C++ Plugin SDK wraps the C ABI

Status: Accepted — Architecture v1 frozen baseline

## Context

Plugin authors need ergonomic resource management without weakening binary compatibility.

## Decision

Provide header-only C++ RAII helpers above the C interface.

## Consequences

A plugin may use modern C++ internally; only C-compatible tables and handles cross the boundary.

## Alternatives considered

Making the C++ SDK itself the binary contract would lose cross-toolchain isolation.
