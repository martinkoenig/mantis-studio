# ADR-034: Open-core Pro modules and signed entitlement licensing

**Status:** Accepted

## Context

Mantis Studio is intended to remain a generous open-source 3D-scanning platform
while a future Mantis Studio Pro product funds industrial inspection,
automation, production and enterprise functionality.

Two failure modes must be avoided:

1. Free becomes artificially degraded to force upgrades.
2. The full Pro implementation ships inside the open/free build and can be
   unlocked by changing a trivial source-code or configuration check.

The frozen Architecture v1 already defines stable plugin/client contracts,
capability-oriented devices, a daemon-centric runtime, typed pipelines and
separate deployment roles. The newer distributed-runtime design additionally
states that generic distributed contracts do not imply that enterprise
orchestration is supplied by default.

## Decision

Mantis adopts an open-core product boundary with these rules.

### Open platform

The open-source Mantis Studio platform remains a complete scanner workstation.
Quality, supported GPU acceleration, raw capture/replay, reliability/data
integrity, ordinary calibration, public SDKs/CLI, Recipes and open extension
contracts are not commercial gates.

No mandatory registration or telemetry is required for ordinary local Free use.

### Commercial implementation boundary

Future Pro functionality is delivered as separately distributed proprietary
modules/packages. It is not implemented as dormant functionality inside the
Free binary guarded only by `isPro()`, configuration flags or UI visibility.

Free and Pro are package compositions over one platform; they are not divergent
runtime forks.

### Public-contract preference

Official Pro modules integrate through the same versioned public
plugin/service/data/capability contracts used by open first-party and community
extensions wherever technically practical.

The core discovers capabilities rather than depending on a product-tier enum.

### Licensing

Official Pro packages and customer entitlements solve separate problems:

- package signatures establish official package identity/integrity;
- asymmetrically signed entitlement licenses establish what a customer may use.

A node-locked entitlement can bind to an installation public key whose private
key remains local, preferably hardware/OS protected when available. Floating and
Site/Enterprise models use organizational/seat scope appropriate to those
deployments.

A Pro module must not depend solely on an open-host boolean for entitlement
enforcement; paid capability boundaries perform or consume a trustworthy
entitlement validation.

### Customer experience

Offline/air-gapped activation and continued local license verification are
first-class requirements.

Routine hardware replacement must have reasonable transfer/recovery paths.
Licensing must not require constant phone-home and must not be coupled to
mandatory product telemetry.

Aggressive anti-debugging, kernel DRM, fragile hardware fingerprinting and
similar mechanisms are outside the baseline threat model.

## Consequences

### Positive

- Free remains genuinely useful and trustworthy.
- Pro can justify its price through industrial workflow value rather than
  artificial degradation.
- Trivial source edits cannot unlock code that is not shipped.
- Simple copying of a license to an unrelated installation is preventable.
- Free and Pro share one project/runtime/SDK ecosystem.
- Community extensions can overlap commercial functionality without architectural
  special cases.
- Air-gapped industrial deployment remains practical.

### Trade-offs

- Proprietary Pro modules require separate release/signing infrastructure.
- Licensing/signing keys become security-critical operational assets.
- Node-locked licensing needs transfer/recovery tooling.
- Fully controlled client machines cannot be made piracy-proof; determined binary
  patching remains possible.
- Enterprise floating/site licensing adds server/lease lifecycle work when that
  product tier is implemented.

## Non-goals

This decision does not define Pro pricing, exact bundle names, exact seat counts,
a final cryptographic library, a licensing service vendor or implementation
milestone.

The signed license format must remain versioned and cryptographically agile.

## References

- [Mantis Studio editions and product boundary](../product/editions.md)
- [Commercial modules and licensing architecture](../architecture/commercial-modules-and-licensing.md)
- [Distributed runtime and compute-pool architecture](../architecture/distributed-runtime.md)
- [Mantis Studio Architecture v1](../../MANTIS_STUDIO_ARCHITECTURE.md)
