[![Architecture CI](https://github.com/martinkoenig/mantis-studio/actions/workflows/build.yml/badge.svg)](https://github.com/martinkoenig/mantis-studio/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)]()

# Mantis Studio — v0.2 Acquisition Foundation (development)

Continues the validated v0.1 implementation of the frozen [Mantis Studio Architecture v1](MANTIS_STUDIO_ARCHITECTURE.md).
The daemon owns devices, captures, projects, jobs and plugins. Qt Quick Studio, the C++ CLI and the Python SDK are independent protocol clients.

![Mantis Studio with synthetic geometry](docs/images/studio.png)

**This is a development milestone, not production scanner software.** The existing Virtual Scanner workflow remains. v0.2 adds a Linux Mantis X1 plugin, explicit dual-camera FrameSets, segmented recoverable raw recording, dual grayscale preview and deterministic hardware-free replay. The user has validated Q6A ARM64 build/tests, native CAMSS setup, dual Y10P streaming near 119.22 FPS and a finalized 141-FrameSet recording with deterministic replay. Corrected 4 ms software pairing and sustained recording remain **PENDING USER EXECUTION**. No reconstruction algorithms are added.

## Quick start

Install dependencies and build as described in [BUILDING.md](BUILDING.md). From the repository root:

```bash
export MANTIS_TOKEN="$(python3 -c 'import secrets; print(secrets.token_hex(24))')"
export MANTIS_PORT=47321
./build/debug/bin/mantisd --project "$PWD/Demo.mantis" &
./build/debug/bin/mantis-studio
```

Run the UI from the same shell, or export the same token and port in every client shell. Closing Studio leaves `mantisd` and capture running. Use **Stop capture**, then `mantis-cli shutdown`, to shut down cleanly.

```bash
./build/debug/bin/mantis-cli devices list
./build/debug/bin/mantis-cli workflow "$PWD/example.ply"
PYTHONPATH=build/debug/python /usr/bin/python3 examples/python/workflow.py python-example.ply
./build/debug/bin/mantis-cli shutdown
```

Export filenames must not already exist. Exports belong outside the `.mantis` project directory.

## Real Acquisition Foundation

- Stable media/V4L2 discovery with a versioned explicit LEFT/RIGHT profile.
- Additive acquisition C interface; ABI v1 and Virtual Scanner remain supported.
- Native scoped media-link/pad/timing setup, packed Y10P RAW10 and optional RAW8 MMAP capture with one acquisition copy; DMABUF is deferred.
- Bounded timestamp-nearest software pairing with explicit startup exclusions; strict counter/timestamp checks when hardware synchronization is configured.
- Bounded LOSSLESS recorder for published FrameSets and independent LATEST_ONLY preview.
- Sequential RawCapture v2 segments, batched durability/SQLite indexing and recovery.
- Real-time/ASAP replay, two-pass canonical byte/metadata verification.
- Shared service semantics in C++, Python, CLI and the existing dark Studio shell.

See the [architecture gap review](docs/architecture/v0.2-acquisition-review.md),
[storage format](docs/architecture/storage.md) and
[exact Q6A acceptance procedure](docs/hardware/x1-q6a-acquisition-validation.md).
The current Q6A reference profile selects **1280×720 Y10P**, Y10_1X10 upstream
and VBLANK=196, with requested target 120 FPS. The user has validated this plugin-owned setup and streaming mode. Independent
V4L2 counters showed a ~51.8 ms same-counter offset; timestamp pairing now selects
observations within the unchanged 4 ms tolerance and needs a Q6A rerun.
Dual packed payload is **276.48 MB/s** before overhead. The reported microSD
benchmark was 32.04 MB/s; sustained recording awaits suitable storage testing.
Deterministic fixtures are not physical scanner evidence.

```bash
export MANTIS_X1_PROFILE="$PWD/profiles/x1-q6a.json" # review physical roles, bus/entity routes and native formats first
./build/debug/bin/mantis-cli devices list
./build/debug/bin/mantis-cli devices info DEVICE_ID
./build/debug/bin/mantis-cli capture start DEVICE_ID
./build/debug/bin/mantis-cli capture status CAPTURE_ID
./build/debug/bin/mantis-cli capture stop CAPTURE_ID
./build/debug/bin/mantis-cli captures list
./build/debug/bin/mantis-cli replay verify RAW_ARTIFACT_ID
```

Set the profile in the daemon's environment before launching it. No profile
means no assumed physical left/right assignment. The explicit test-only backend
uses `MANTIS_X1_FAKE=normal` with a fixture profile (`bus_identity: fixture`); never enable it for hardware acceptance.

## Preserved v0.1 reference workflow

1. Discover Virtual Scanner through the public device plugin ABI.
2. Start runtime-owned capture with a bounded, lossless recording queue.
3. Journal immutable image chunks outside SQLite.
4. Compile a versioned JSON recipe into a typed execution plan.
5. Process frame zero through the public processing plugin ABI.
6. Finalize a point-cloud artifact with source, recipe, calibration and sequence provenance.
7. Map its immutable data file in the Studio renderer without putting geometry into control messages.
8. Export through the public PLY exporter plugin as a cancellable job.
9. Repeat from CLI/Python, restart Studio, or deliberately crash an isolated plugin host.
10. Restart the daemon after abrupt termination and recover committed capture chunks.

The frame-zero reference is intentional: client timing does not change the demonstration geometry. All acquired frames are recorded for later replay. Full-capture reconstruction is a subsequent milestone.

## Scope and platform status

| Platform | Architectural target | Validation in this delivery | Support commitment |
| --- | --- | --- | --- |
| Linux x86_64 | Yes | Ubuntu 24.04; GCC 13; Qt 6.4; native build and process tests | Skeleton development reference |
| Linux ARM64 | Tier 1 | Native GitHub Actions Studio ON/OFF passed for v0.2; user-validated Q6A setup/streaming/replay; corrected pairing pending | Linux build and fixture tests validated |
| Windows x86_64 | Yes | Platform implementation present; not built/tested here | Not yet supported |
| macOS ARM64 | Yes | POSIX implementation used; not built/tested here | Not yet supported |

No x86 intrinsics or pointer-width assumptions are used in public structures. The current packet codec explicitly supports little-endian targets, including the four target architectures.

## Documentation

- [Build and run](BUILDING.md)
- [Implementation architecture and module map](docs/architecture/implementation.md)
- [Acceptance evidence](docs/architecture/validation.md)
- [Milestone coverage and limitations](docs/architecture/milestone.md)
- [Plugin development](docs/plugin-development/README.md)
- [C++ / Python Client SDK](docs/client-sdk/README.md)
- [Protocol and local data plane](docs/protocol/README.md)
- [Storage and recovery](docs/architecture/storage.md)
- [Architecture Decision Records](docs/adr/README.md)
- [Contributing](CONTRIBUTING.md), [security model](SECURITY.md)

The architecture specification is included unchanged. No production hardware algorithms, GPU backends, marketplace, cloud deployment or mobile application are included.
