# ADR-020: Scanner functionality is represented through capabilities

Status: Accepted — Architecture v1 frozen baseline

## Context

Third-party sensors and composite devices cannot fit one product hierarchy.

## Decision

Expose generic device identities/graphs and versioned namespaced capabilities.

## Consequences

Capture owns a set of devices; Studio discovers image-stream support without Mantis X1 branches.

## Alternatives considered

Hard-coded scanner subclasses and global activeScanner state would restrict composite and custom systems.
