# Contributing

Read the frozen architecture and relevant ADR before changing a public contract. Preserve the distinction between the Plugin SDK and the Client SDK.

For all new work, also follow the binding
[reliability, performance and validation standard](docs/architecture/reliability-performance-and-validation.md).
Measurement-critical and hot-path code is not accepted on happy-path functionality
alone: failure semantics, bounded resources, diagnostics and relevant performance
evidence are part of the feature contract.

## Required checks

1. Build Studio and run `ctest --preset linux-debug`.
2. Build with Qt disabled and run the headless tests.
3. Use the sanitizer build for changes to ownership, threads, ABI handling or storage.
4. Keep Linux ARM64 in the native CI matrix; avoid CPU-specific dependencies in public headers.
5. Run `clang-format -i` on changed C/C++ files using the repository style.
6. Add a focused regression test for a bug or architectural behavior. Do not add tests that merely repeat implementation constants.
7. For changes to critical streaming/hot paths, document or update the relevant throughput/latency/memory/copy/queue evidence and reject material unexplained regressions.
8. For changes to recovery, distributed ownership or persistent state, test negative paths and interrupted transitions; never infer production/HA readiness from a happy-path fixture.

Foundation headers must not expose Qt, SQLite, OpenCV, Eigen, CUDA or Vulkan types. First-party plugins may include only the public SDK plus their own dependencies. Studio and CLI must not link to runtime implementation libraries. The boundary contract test checks includes, while the Qt-disabled build proves dependency separation.

## Compatibility

ABI structures use size/version prefixes, fixed-width integers, UTF-8, opaque handles and explicit ownership. Never throw across C callbacks. Append compatible fields only with size-aware handling; incompatible changes require a new queried interface version. Application, ABI, schema, recipe, project and algorithm versions are independent.

Reserve removed Protobuf field numbers and names. Do not reassign them. Protocol messages describe services, not screens or widgets.

Architectural changes require a new ADR describing the problem, inadequate extension points, alternatives, compatibility impact and migration path. Feature convenience alone is insufficient.

## Pull requests

Explain the problem, behavior change and verification. Include a reproducible fixture for storage or plugin crashes. State which platforms were actually tested. Keep dependency updates separate from semantic changes where practical.

The repository is licensed under Apache-2.0; see [LICENSE](LICENSE).
