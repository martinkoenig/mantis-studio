# Commercial modules and licensing architecture

Status: Accepted architectural/product-security baseline; implementation is future work

This document defines how a future Mantis Studio Pro distribution can coexist
with the open-source Mantis Studio platform without turning the open runtime into
a DRM-centric codebase.

See [Mantis Studio editions and product boundary](../product/editions.md) for the
feature/product policy and
[ADR-034](../adr/034-open-core-pro-modules-and-licensing.md) for the normative
decision.

## 1. Threat model and objective

The licensing system is intended to prevent low-effort misuse such as:

- editing an open-source `isPro()` check and rebuilding;
- changing `"pro": false` to `true` in a local file;
- copying one customer's license file to an unrelated installation;
- copying signed Pro plugin binaries to another unlicensed company and using them;
- exposing hidden Pro UI whose full implementation was already shipped in Free.

It is explicitly not intended to make piracy mathematically impossible on a
machine fully controlled by a determined reverse engineer.

No client-side licensing design can guarantee that. The product therefore avoids
punishing legitimate customers with invasive anti-debugging, kernel drivers,
constant online checks or brittle hardware fingerprinting.

The security objective is:

```text
simple source patch / file edit / copy-and-share
    -> must not unlock Pro

deliberate binary reverse engineering of proprietary modules
    -> outside the low-effort threat model
```

## 2. Separate implementation, not hidden code

The primary protection is architectural separation.

The open distribution contains the open implementation. Future Pro functionality
is delivered as separate proprietary modules/packages and is not merely dormant
code hidden behind a boolean.

Conceptually:

```text
public/open repository
────────────────────────────────────
mantisd
Studio frontend
device/pipeline/artifact runtimes
compute backends
SDKs
standard algorithms and integrations
open first-party/community plugins

             stable public contracts
                       │
                       ▼

private/commercial modules
────────────────────────────────────
metrology-pro
automation-pro
robotics-pro
production-pro
fleet-pro
enterprise-auth-pro
reporting-pro
commercial integrations
```

Removing a UI check in the open source must not materialize an implementation
that is absent from the installed distribution.

## 3. Pro modules use platform contracts

Where technically practical, Pro modules use the same public plugin, service,
data and capability contracts as first-party open and community components.

The open core must not accumulate broad private bypass APIs solely for Pro.

This preserves:

- one runtime architecture;
- compatibility between Free and Pro projects;
- the ability for community plugins to overlap commercial functionality;
- testability of public extension surfaces;
- separation between platform policy and commercial packaging.

A Pro module may require private implementation details internally, but its
integration with the platform should prefer explicit versioned contracts.

## 4. Capability-driven availability

The runtime/UI discovers functionality through versioned capabilities.

Examples:

```text
org.mantis.metrology.gdt
org.mantis.metrology.inspection-plan
org.mantis.robotics.path-planning
org.mantis.production.serial-workflow
org.mantis.fleet.scheduler
```

The core should not be littered with product-tier branches or a growing
`FREE/PRO/PRO_MAX` enumeration.

A capability can originate from an open plugin, a customer plugin, a community
plugin or an official Pro module. Commercial entitlement determines whether an
official proprietary Pro module may initialize/use its paid capability.

## 5. Two independent signatures

Package authenticity and customer entitlement are separate problems.

### 5.1 Package signature

Official Pro packages are signed by Mantis release infrastructure.

The signature establishes:

- package publisher/authenticity;
- package identity/version;
- integrity of the shipped module/manifest;
- resistance to casual replacement or modification.

A package signature does **not** grant a license. Copying an authentic signed
package to another machine leaves the package authentic.

### 5.2 License/entitlement signature

A license is an asymmetrically signed entitlement document.

Conceptually:

```json
{
  "format_version": 1,
  "license_id": "…",
  "customer_id": "…",
  "customer_name": "Example GmbH",
  "license_model": "node_locked",
  "entitlements": [
    "org.mantis.pro.metrology",
    "org.mantis.pro.automation"
  ],
  "seats": 1,
  "perpetual": true,
  "maintenance_until": "…",
  "binding": {
    "type": "installation_key",
    "public_key": "…"
  },
  "issued_at": "…"
}
```

The document may remain human-readable. Confidentiality is not required for its
ordinary fields; authenticity is supplied by the digital signature.

The private signing key never ships with client software. Clients contain only
the verification material required to validate licenses.

The concrete signature algorithm remains versioned/replaceable. Ed25519 is a
preferred initial candidate where deployment/library constraints permit it; the
format must not make cryptographic agility impossible.

## 6. License checks belong inside proprietary modules too

The open host may avoid loading a Pro module when no matching entitlement exists,
but that host-side check is not a sufficient security boundary because the host
is open source.

A proprietary Pro module must validate the entitlement needed for its own paid
operations or consume a cryptographically trustworthy entitlement context whose
security boundary is not just a patchable open boolean.

This does not mean scattering hundreds of license checks through hot paths.
Checks should be concentrated at meaningful module/service boundaries.

## 7. Node-locked installation identity

For a normal node-locked license, activation binds the license to an installation
cryptographic identity rather than to a fragile hash of incidental hardware.

Typical flow:

```text
installation
  -> generate installation key pair
  -> private key remains local/non-exportable where supported
  -> public key is included in activation request
  -> licensing authority issues signed entitlement bound to that public key
```

Preferred secure storage can use TPM/OS-backed key facilities where available.
A software-key fallback may be required for supported systems without suitable
hardware-backed storage.

The design must avoid making routine CPU, RAM, GPU, NIC or storage replacement
unnecessarily invalidate a legitimate license.

Copying only the Pro binaries and license file to another independently created
installation must therefore not satisfy the installation binding.

## 8. Activation and offline operation

Industrial deployments may be isolated or air-gapped. Online connectivity must
not be required for normal continued operation of a valid offline-capable license.

### Online activation

A connected client may send the minimum activation information needed to obtain
a signed entitlement for its installation.

### Offline activation

An offline installation can create an activation request file, for example:

```text
activation-request.mreq
```

The request can be transferred to a connected machine, submitted through a
licensing portal/tool, and exchanged for a signed license file that is transferred
back to the offline installation.

Normal runtime validation is local cryptographic verification.

The licensing service must not receive project names, scans, CAD files,
measurement values, captured images or other customer project data merely to
validate a license.

## 9. Supported license models

The architecture should support at least these future models without rewriting
Pro modules.

### 9.1 Node locked

Suitable for individual workstations and smaller companies.

- bound to an installation identity;
- self-service activation/deactivation where practical;
- reasonable recovery/re-hosting for hardware replacement;
- no recurring online validation required for perpetual/offline licenses.

### 9.2 Floating/concurrent

Suitable for organizations with more installations than simultaneous users.

A customer-controlled license server can issue short-lived signed/validated seat
leases to registered clients.

Temporary loss of the license server must have an explicit grace policy so a
brief infrastructure failure does not instantly stop a production process.

Floating entitlement scope is organizational; possession of a reachable server
address alone is not intended to authorize arbitrary outside companies.

### 9.3 Site/Enterprise

Suitable for larger deployments.

Possible scope includes a legal entity, defined site(s), or explicitly licensed
corporate group. Exact contractual scope is commercial policy, but the technical
format should support it without per-machine package variants.

## 10. License transfer and recovery

Legitimate hardware replacement must not become a DRM support trap.

The product should support:

- normal self-service deactivation/re-hosting;
- administrative reset for failed/lost hardware;
- a reasonable transfer policy;
- explicit recovery paths for offline installations.

A license must not be permanently lost merely because the licensed workstation's
SSD or motherboard failed.

## 11. Perpetual and maintenance semantics

The architecture should support perpetual licenses cleanly.

A perpetual entitlement continues to authorize versions covered by the license
even after optional maintenance expires.

A maintenance date may govern eligibility for later releases, updates or support;
it must not silently disable an already-covered perpetual version.

Subscription licensing may also be offered when a customer prefers procurement
that way, but the technical system must not require subscription-only economics.

## 12. No mandatory phone-home and no telemetry coupling

Runtime licensing and product telemetry are separate concerns.

Do not require a network request at every launch or at arbitrary periodic
intervals merely to keep an otherwise valid offline-capable license alive.

Do not use licensing as a covert telemetry channel.

Licensing may process information needed for licensing, such as:

- license/customer identifier;
- requested entitlements;
- installation identity/public key;
- seat/lease state;
- software/package version where needed for maintenance eligibility.

Licensing does not need ordinary customer scan/project content.

## 13. Sharing between unrelated companies

A signed Pro package can be copied; package authenticity alone does not prevent
sharing.

The entitlement binding is what prevents ordinary sharing.

Example:

```text
Company A
  signed Pro package          yes
  signed license              yes
  matching installation key   yes
  -> Pro capability available

Company B copies A's files
  signed Pro package          yes
  signed license              yes
  matching installation key   no
  -> entitlement unavailable
```

Floating/Site licensing uses customer/server/client scope rather than treating a
network-reachable license server as universal authorization.

No client-side design can prevent a determined attacker from cloning or patching
all relevant state on fully controlled hardware. The requirement is that simple
file sharing does not work.

## 14. VM and virtualized deployment

VM use must not be prohibited merely because it complicates node locking.

Supported options may include:

- installation-key binding inside a VM;
- vTPM-backed identities where available;
- floating licensing for VM fleets;
- explicit host/site policies for enterprise deployments.

Plain copying of a VM containing an exportable software private key is a known
limit of pure software node locking. Higher-assurance deployments should prefer
hardware/vTPM-backed identity or floating/site licensing.

## 15. What Mantis deliberately does not do

The baseline does not call for:

- kernel DRM drivers;
- hostile process/debugger scanning;
- broad VM detection/blocking;
- pervasive code virtualization;
- constant cloud validation;
- fragile multi-component hardware fingerprints;
- hidden degradation of scan quality after licensing failures;
- collecting project data to enforce licensing.

If a license cannot be validated, the affected paid capability fails explicitly.
The open/free platform remains usable according to its own capabilities.

## 16. Distribution model

Free and Pro are different package compositions over one platform, not separate
engine forks.

Example:

```text
Mantis Studio distribution
  open platform
  open first-party plugins

Mantis Studio Pro distribution
  same open platform
  same compatible project/runtime contracts
  + selected signed proprietary Pro packages
```

Air-gapped customers can receive a complete offline Pro bundle.

All customers should normally receive identical binaries for a given Pro module
version. Customer-specific entitlement lives in the signed license, not in
customer-specific builds such as `metrology-pro-customer-A.so`.

## 17. Security boundary summary

The intended low-effort piracy resistance is layered:

```text
Pro implementation absent from Free
        +
signed official Pro package
        +
signed entitlement
        +
installation/customer binding
        +
entitlement check inside proprietary boundary
        =
no trivial source edit or file-copy unlock
```

This is sufficient for the intended threat model while preserving an honest,
offline-capable and low-friction experience for paying customers.
