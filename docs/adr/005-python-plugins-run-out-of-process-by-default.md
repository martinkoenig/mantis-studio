# ADR-005: Python plugins run out of process by default

Status: Accepted — Architecture v1 frozen baseline

## Context

Python dependencies and interpreter failures must not destabilize capture.

## Decision

Future Python processing plugins use isolated host processes and separate control/data transport.

## Consequences

The v0.1 Python Client SDK already lives outside mantisd; a Python processing host is reserved, not falsely implemented.

## Alternatives considered

Embedding arbitrary plugin interpreters in mantisd couples crashes and package environments.
