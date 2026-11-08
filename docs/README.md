# Mantis Studio documentation

Choose a route for your task, then follow the linked guides and specifications
for detail. Implemented software, accepted future architecture and physical
validation have separate scopes.

## Start here

- **Understand the project:** read the [project overview](../README.md),
  [current capabilities and limits](../README.md#development-status), and
  [roadmap](../ROADMAP.md).
- **Try the interface:** follow [Studio Free and demo](#studio-free-and-demo)
  and [build and run](#build-and-run).
- **Integrate an application or device:** choose the
  [Client SDK](#automate-with-the-client-sdk) or
  [plugin and device path](#extend-devices-and-processing).
- **Work on the project:** start with [contributor onboarding](#contributor-onboarding)
  and the [validation requirements](#reliable-validation-and-ci).

For live engineering status and review gates, use
[Engineering Status & Active Work #14](https://github.com/martinkoenig/mantis-studio/issues/14).
The roadmap defines scope; dated evidence records describe their original revisions.

## Studio Free and demo

The [Free / Pro policy](product/editions.md) defines the product boundary as
features are implemented. Studio Pro modules are planned; the policy's feature
lists are not a claim of a complete delivered scanning workflow.

- [Explore offline](../README.md#explore-offline) launches illustrative **Demo / Mock**
  content without a daemon or physical device.
- The [UI guide](ui/README.md#launch-and-data-sources) explains workspaces and data
  modes. Authenticated **fixture** captures still use synthetic data.
- The [virtual acquisition rig](plugin-development/virtual-rig.md) and its
  [shared scene](plugin-development/virtual-rig-scene.md) support software simulation
  and recording/replay tests. They do not establish physical optics, scanner
  accuracy or sustained recording throughput.

## Build and run

Start with the [README prerequisites and build commands](../README.md#build-and-test).
The [build guide](../BUILDING.md) covers the Linux reference environment, Qt-free
headless builds and sanitizers. For a running local system, follow
[connect to a local daemon](../README.md#connect-to-a-local-daemon), including its
token and shutdown instructions.

This is a developer source build. Windows and macOS are architectural targets,
not supported releases; see the [getting-started limits](../README.md#getting-started).

## Software architecture and accepted decisions

`mantisd` owns devices, capture, projects, plugins and jobs. Studio, the CLI and
Client SDKs access that runtime through public boundaries. Closing a client does
not transfer or stop daemon-owned capture.

- [Architecture authority](../ARCHITECTURE.md) explains how the
  [frozen v1 specification](../MANTIS_STUDIO_ARCHITECTURE.md), release baselines and
  [Architecture Decision Records (ADRs)](adr/README.md) relate. Accepted designs may
  describe future implementation; consult each document's scope.
- The [implementation map](architecture/implementation.md) traces the foundation
  and acquisition extensions; the [protocol guide](protocol/README.md) describes
  the control and local data boundaries.
- The [geometric calibration baseline](architecture/v0.3-geometric-calibration.md)
  and [projected-light package map](architecture/v0.4-laser-acquisition.md) describe
  release-specific contracts and implementation boundaries.

## Automate with the Client SDK

Use the [C++ / Python Client SDK guide](client-sdk/README.md) to control `mantisd`
from an external application. It covers authentication, capture, jobs, cancellation
and data access. Client applications do not instantiate device or storage runtimes;
the Plugin SDK serves a different extension role.

## Extend devices and processing

- [Developing a native plugin](plugin-development/README.md) introduces the current
  C ABI, C++ wrapper, device/processor/exporter interfaces and first-party examples.
  Begin with its [examples and tests](plugin-development/README.md#examples-and-tests).
- For a device integration, use that guide's
  [enumerated acquisition path](plugin-development/README.md#v02-enumerated-acquisition)
  and capability contracts.
- The [custom sensor guide](plugin-development/custom-sensors.md) and
  [device bridge architecture](architecture/device-integration-and-bridge.md)
  describe **accepted future architecture**. Their proposed sensor APIs and bridge
  protocol are not currently available integration interfaces.

## X1 hardware and calibration

- [X1 / Q6A acquisition validation](hardware/x1-q6a-acquisition-validation.md)
  records scoped physical acquisition evidence and the procedure's prerequisites
  and limits. Receive rate, software pairing and physical exposure synchronization
  are separate claims.
- [Real X1 geometric calibration validation](hardware/x1-geometric-calibration-validation.md)
  explains the physical M8 characterization procedure and evidence requirements.
  Real-hardware calibration acceptance remains a separate gate; software or fixture
  success does not establish it.

Read the procedures before physical work. A historical acquisition PASS does not
qualify sustained full-rate storage, projected-light operation or scanner accuracy.

## Contributor onboarding

Read [CONTRIBUTING](../CONTRIBUTING.md) for boundaries and required checks,
[SECURITY](../SECURITY.md) for vulnerability reporting, and the
[license](../LICENSE) for use and redistribution terms.
Implementation agents must also follow the repository's [AGENTS.md](../AGENTS.md);
agent instructions supplement the engineering references rather than replace them.

## Reliable validation and CI

The [reliability, performance and validation standard](architecture/reliability-performance-and-validation.md)
and [test-quality contract](engineering/test-quality.md) define evidence and
failure-case expectations. [CONTRIBUTING's required checks](../CONTRIBUTING.md#required-checks)
govern local validation; the [workflow source](../.github/workflows/build.yml)
defines CI. Passing software checks does not qualify hardware or production readiness.

For specialist qualification, see the
[virtual-rig Recorder/storage guide](validation/virtual-rig-storage.md) and
[Studio sanitizer qualification](ui/qualification-current-studio-sanitizers.md).
Their supported configurations and remaining gaps are part of the evidence.

## Historical evidence

The [validation evidence index](validation/README.md) and
[validation record](architecture/validation.md) retain dated results, original
failures and platform limits. The [CI platform record](architecture/ci.md) and
[integration qualification record](architecture/cut-0b-integration-qualification.md)
also describe specific historical checkpoints. Read revision and scope labels;
these records are not current CI results or acceptance of a different source tree.
