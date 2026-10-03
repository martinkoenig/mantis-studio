# ADR-008: Protocol Buffers define service and event schemas

Status: Accepted — Architecture v1 frozen baseline

## Context

Multiple language clients need a versioned, UI-independent wire contract.

## Decision

Keep .proto schemas independent from the transport and generate language bindings.

## Consequences

C++ and Python use the same commands; field-number compatibility and message limits are tested.

## Alternatives considered

UI-specific JSON payloads or direct runtime bindings would complicate versioning and client parity.
