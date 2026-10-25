# Mantis Studio editions and product boundary

Status: Accepted product baseline; implementation of Mantis Studio Pro is future work

This document defines the intended product boundary between the free/open-source
Mantis Studio distribution and the future commercial Mantis Studio Pro offering.
It is a product policy, not a statement that all listed functionality is already
implemented.

The architectural companion is
[Commercial modules and licensing](../architecture/commercial-modules-and-licensing.md).
The normative architectural decision is
[ADR-034](../adr/034-open-core-pro-modules-and-licensing.md).

## 1. Product principle

Mantis Studio Free is a complete 3D-scanning workstation for hobbyists,
makers, researchers, small businesses and semi-professional users.

Mantis Studio Pro is an industrial production, inspection, automation and
enterprise platform.

The Free edition must not be a deliberately degraded demonstration of Pro.
A user must be able to install Mantis Studio, connect a supported scanner,
capture data, reconstruct it at the best available quality, process it and
export useful results without registration, payment or a subscription.

The intended distinction is therefore:

```text
Mantis Studio
  -> scan, reconstruct, process, inspect, develop and export

Mantis Studio Pro
  -> operate repeatable industrial processes at scale
```

Pro earns its price through integrated industrial workflows, validation,
automation, orchestration, collaboration, compliance-oriented functionality
and commercial support rather than artificial restrictions in the base product.

## 2. Non-negotiable Free principles

The open-source edition must not use artificial restrictions to manufacture a
reason to purchase Pro.

The following are specifically not acceptable product gates:

- deliberately lower reconstruction or meshing quality;
- point-count, triangle-count, resolution or export-size limits whose purpose is
  commercial upsell rather than a real technical limit;
- disabling GPU acceleration merely because the user does not own Pro;
- watermarks on user geometry or exports;
- withholding crash recovery, project integrity or safety/reliability fixes;
- withholding raw capture or deterministic replay;
- requiring an account merely to operate local hardware;
- mandatory telemetry;
- a time-limited "community" edition;
- forbidding ordinary commercial use of the open-source code where its license
  permits it.

Free functionality may of course have real capability limits imposed by hardware,
algorithms, memory, storage, supported formats or implementation maturity. Such
limits must be technical and truthful rather than licensing theatre.

## 3. Registration, telemetry and cloud

Mantis Studio Free requires no registration for local use.

A future optional Mantis account may provide explicitly requested convenience
features such as settings synchronization. Account use must remain optional for
the normal local scanning workflow.

No mandatory product-usage telemetry is part of the Free proposition. Diagnostics
needed to operate the local scanner/runtime remain local unless the user
explicitly exports or submits them.

Mantis Studio Pro licensing may require activation according to the selected
license model, but normal operation must support offline/air-gapped industrial
deployments as defined in the licensing architecture.

## 4. What belongs in Free

Free contains the functionality needed to get the full technical value from a
scanner and to build on the Mantis platform.

This includes, as those capabilities are implemented:

- supported Mantis, third-party and DIY scanner integration;
- capture, live preview, tracking and scanner control;
- best available first-party reconstruction/fusion quality;
- standard CPU/GPU compute backends;
- global registration, point-cloud processing, meshing and texture generation;
- ordinary cleanup operations such as smoothing, decimation and hole filling;
- raw capture and deterministic replay;
- calibration required to operate supported scanners correctly;
- projects, immutable artifacts, provenance and processing history;
- ordinary import/export formats for which redistribution/licensing permits it;
- useful manual measurement tools such as distance, angle, radius, plane and
  section measurements;
- useful manual/basic scan-to-reference comparison;
- basic reverse-engineering aids such as primitive fitting and sections;
- Pipeline Recipes and the ordinary pipeline authoring model;
- CLI and headless operation;
- public native Plugin SDK;
- public C++ Client SDK;
- public Python Client SDK;
- community and custom algorithms/devices/exporters/integrations;
- local multi-device operation supported by the generic Device Graph;
- ordinary diagnostics and performance inspection;
- plugin crash isolation and other reliability mechanisms;
- a simple explicitly configured remote worker where the generic distributed
  execution implementation supports it.

The last point is important: remote execution is an architectural platform
capability, not itself an Enterprise paywall. Enterprise orchestration of many
workers/scanners is a separate Pro concern.

## 5. What belongs in Pro

Pro focuses on productized industrial workflows that save engineering,
integration, operator and quality-assurance effort.

### 5.1 Inspection and metrology

Examples include:

- advanced GD&T/form-and-position evaluation;
- datum systems and reusable tolerance definitions;
- reusable inspection/measurement plans;
- automatic feature extraction for inspection;
- automated pass/fail evaluation;
- serial-number-bound measurement results;
- statistical process/trend analysis;
- production comparison against golden masters or CAD;
- controlled/traceable report generation;
- report templates and approval-oriented workflows;
- metrology-oriented validation packages where actually performed and claimed.

Free may still provide useful manual measurements and visual deviation analysis.
Pro turns those tools into repeatable industrial inspection processes.

### 5.2 Advanced reverse engineering

Examples include:

- advanced automatic feature recognition;
- automatic section generation;
- constraint inference;
- parametric feature reconstruction;
- surface patch generation;
- NURBS/auto-surfacing workflows;
- CAD feature-tree oriented workflows;
- first-party commercial CAD integrations or native formats that require
  commercial licensing.

Free remains useful for manual and semi-automatic reverse engineering.

### 5.3 Robotics and automation

Examples include:

- first-party supported industrial robot integrations;
- robot/cell calibration;
- digital-cell configuration;
- CAD-based scan-path planning;
- collision checking;
- scan coverage simulation;
- automatic or adaptive rescan planning;
- turntable/robot/scanner coordinated execution;
- validated production-cell workflows;
- first-party PLC/industrial-protocol integrations.

The open SDK remains capable of hosting community or user-written robot and motion
plugins. Pro sells a supported integrated system, not permission to automate.

### 5.4 Production workflows

Examples include:

- unattended production queues;
- watch-folder/job-ingestion workflows;
- barcode/serial-number driven recipe selection;
- automatic part/recipe/inspection-plan selection;
- operator-station workflows;
- production retry/recovery policy;
- automatic reporting and result routing;
- first-party MES/QMS/ERP connectors;
- supported webhooks and industrial integration packages.

A user may reproduce parts of these workflows using the open Python/C++ APIs.
That is intentional. Pro provides the maintained integrated product.

### 5.5 Fleet and enterprise

Examples include:

- central scanner/worker inventory and health;
- worker pools and policy-based scheduling;
- quotas, reservations and production priorities;
- production failover/orchestration built on the generic distributed runtime;
- central configuration and update rollout;
- site/fleet diagnostics and capacity planning;
- users, roles and enterprise RBAC;
- SSO/LDAP/OIDC/SAML integrations;
- enterprise audit trails;
- team/project review and approval workflows;
- central policy enforcement;
- LTS releases, validated deployment matrices and priority support.

The generic many-to-many runtime contracts remain part of the architecture. Pro
productizes their operation at fleet/enterprise scale.

## 6. Feature boundary summary

| Area | Mantis Studio Free | Mantis Studio Pro |
| --- | --- | --- |
| Scanner capture/control | Full supported capability | Same core capability |
| Reconstruction quality | Best available | Same core quality |
| CPU/GPU acceleration | Yes | Yes |
| Raw capture/replay | Yes | Yes |
| Crash recovery/data integrity | Yes | Yes |
| Standard processing/meshing | Yes | Yes |
| Standard import/export | Yes | Yes |
| Manual/basic measurements | Yes | Yes |
| Basic deviation inspection | Yes | Yes |
| Advanced GD&T/metrology plans | No | Yes |
| Automated serial inspection/pass-fail | No | Yes |
| Basic reverse-engineering tools | Yes | Yes |
| Advanced parametric/auto-surfacing workflows | No | Yes |
| CLI/Python/C++ Client SDKs | Yes | Yes |
| Native Plugin SDK | Yes | Yes |
| User/community automation | Yes | Yes |
| Pipeline Recipes/editor | Yes | Yes |
| Local multi-device model | Yes | Yes |
| Explicit single remote worker | Platform capability | Yes |
| Worker pools/fleet scheduling/failover | No productized orchestration | Yes |
| Community/custom robot plugins | Yes | Yes |
| First-party industrial robot cell workflow | No | Yes |
| Production queues/barcode workflows | Scriptable by user | Integrated |
| MES/QMS/ERP integrations | Open APIs/community plugins | First-party supported connectors |
| Local technical diagnostics | Yes | Yes |
| Fleet/production diagnostics | No | Yes |
| Registration for local use | No | License activation only where applicable |
| Mandatory telemetry | No | No |
| Offline operation | Yes | Yes |
| LTS/priority commercial support | Community support | Yes |

The table is a boundary policy, not a release checklist. Individual rows become
real only when the corresponding functionality is implemented and validated.

## 7. Open development surfaces are not Pro features

The following must remain open platform surfaces rather than being used as
commercial gates:

- Plugin C ABI and C++ Plugin SDK;
- Python and C++ Client SDKs;
- protocol schemas intended as public contracts;
- Recipe schema;
- canonical data schemas;
- device capability identifiers;
- plugin manifest schema;
- raw capture and deterministic replay.

A Free user is allowed to write automation that approximates a Pro workflow.
A community developer is allowed to implement functionality that overlaps a
commercial Mantis Pro module.

The commercial advantage of Pro must come from implementation quality,
integration, workflow design, validation, maintenance, support and reduced
engineering effort, not artificial exclusivity of the platform contracts.

## 8. No Free/Pro fork

Mantis Studio Pro must not become a divergent fork of the open-source runtime.

There is one platform architecture, one project/artifact model, one Device Graph,
one typed pipeline model and one set of public SDK contracts.

Pro functionality is added through separately distributed capabilities/modules.
The open runtime should discover capabilities rather than contain product-wide
branches such as:

```cpp
if (license.is_pro()) {
    // alternate Mantis architecture
}
```

The UI may present a coherent Pro experience, but availability is derived from
loaded capabilities/entitlements rather than a parallel engine.

## 9. Product decision test

When deciding whether a future feature belongs in Free or Pro, ask:

1. Is this required to obtain the best technical result from one scanner?
   Prefer Free.
2. Is this required for correctness, safety, recovery or data integrity?
   Free.
3. Is this part of the open developer/platform surface?
   Free.
4. Does it primarily turn scanning into a repeatable industrial workflow,
   automated production process, managed fleet or enterprise system?
   Pro is appropriate.
5. Would putting it behind Pro intentionally make an otherwise complete scan
   worse?
   Do not gate it.
6. Could a skilled user recreate the workflow with the open SDK?
   That is acceptable; it does not invalidate the Pro product.

This policy is deliberately biased toward a generous Free edition and a
substantive Pro edition.
