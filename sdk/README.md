# Standalone native Plugin SDK (internal Stage A)

This entry point installs only the four existing public C/C++ headers, CMake
INTERFACE targets/configuration/version files, and the repository `LICENSE`.
It does not configure the product, detect Qt/Protobuf, link runtime internals,
install a daemon/host/plugin binary, or make a C++ ABI/SONAME promise.
Names and version `0.2.0` remain provisional/internal. The package version is
**not** the public C ABI version (`MANTIS_ABI_V1 == 1`). Package discovery currently
requires the exact version; no package version-skew compatibility is promised.

## Install to a private prefix

CMake 3.24+ is sufficient to install; the producer uses `LANGUAGES NONE` and
requires no compiler or product dependencies. A C11 toolchain consumes the C
target. The C++ target propagates C++23, matching the current source build policy
(the wrapper uses modern standard-library facilities); it also propagates the C
target. It is header-only, not a cross-compiler C++ binary contract.

From the repository root, choose your own writable build and private prefix:

```sh
cmake -S sdk -B /your/build/sdk -DCMAKE_INSTALL_PREFIX="/your/private SDK"
cmake --install /your/build/sdk
```

Default relative layout:

```text
include/mantis-plugin-sdk-0.2.0/c/mantis/{plugin,projected_light,semantic_views}.h
include/mantis-plugin-sdk-0.2.0/cpp/mantis/sdk.hpp
lib/cmake/MantisPluginSDK-0.2.0/{MantisPluginSDKConfig,MantisPluginSDKConfigVersion,MantisPluginSDKTargets}.cmake
share/licenses/MantisPluginSDK-0.2.0/LICENSE
```

The include roots are deliberately separate: C++ consumers obtain the C headers
only through the imported C dependency. GNU install-directory variables
`CMAKE_INSTALL_INCLUDEDIR`, `CMAKE_INSTALL_LIBDIR`, `CMAKE_INSTALL_DATADIR`, and
`MantisPluginSDK_INSTALL_CMAKEDIR` accept relative paths, including spaces.
Absolute paths and parent traversal are rejected to preserve a self-contained
relocatable tree. `DESTDIR` staging is supported on the tested Linux route.
Move the entire installed tree together, then configure a fresh consumer.
For the default `lib` layout use `-DCMAKE_PREFIX_PATH="/your/relocated SDK"`.
For `lib64` or custom metadata directories use an explicit hint such as
`-DMantisPluginSDK_DIR="/your/relocated SDK/lib64/cmake/MantisPluginSDK-0.2.0"`;
CMake on Debian-derived systems need not search `lib64` automatically.

## External consumers

C only (no C++ compiler is needed):

```cmake
cmake_minimum_required(VERSION 3.24)
project(MyCPlugin LANGUAGES C)
find_package(MantisPluginSDK 0.2.0 EXACT CONFIG REQUIRED)
add_library(my_plugin MODULE plugin.c)
target_link_libraries(my_plugin PRIVATE MantisPluginSDK::mantis-sdk-c)
```

For a C++ plugin use `LANGUAGES CXX` and
`target_link_libraries(my_plugin PRIVATE MantisPluginSDK::mantis-sdk-cpp)`.
Do not add SDK source/build include paths or link product implementation targets.
Use `<mantis/plugin.h>` / `<mantis/projected_light.h>` in C and
`<mantis/sdk.hpp>` in C++. See the [plugin guide](../docs/plugin-development/README.md)
for entry tables, exception containment, buffer ownership and runtime discovery.
The Client SDK is a separate control/service API, not this plugin package.

## Reproducible Linux qualification

Run only with the repository's native resource custody: one bounded campaign,
serial execution and one compiler job. System CMake, Ninja, C/C++ compilers and
Python 3 (stdlib only) are required. No dependency/toolchain build is performed.
Use fresh roots **outside the repository**, for example under an authorized
scratch directory; the runner refuses to reuse/delete existing output roots:

```sh
python3 -B tests/sdk/test_qualifier.py --output "$TMPDIR/sdk-runner-controls"
python3 -B tests/sdk/qualify.py --output "$TMPDIR/sdk-installed-evidence"
```

The inventory is derived from actual installed files, imported target properties,
compiler commands/dependencies and serial CTest registrations, not test counts.
It includes fresh C/C++ consumers with intentionally low language defaults,
a C-only configure without enabling C++, the existing C contract and frozen-L2
ABI oracle, private-prefix spaces, `lib`, `lib64`, custom directories, `DESTDIR`,
relocation while the original installed/source-copy/build paths are unavailable,
and strict ASan/LSan/UBSan execution of all four C/C++ contract executables.
The tiny Linux/GCC sanitizer fixtures use non-PIE to avoid shadow/ASLR collisions;
this does not alter public targets or suppress a detector.

Negative controls require specific primary diagnoses: unsupported package
version, missing header/export/C dependency, source-contaminated export, rejected
ABI/short tables, callback exceptions and ownership/allocation/read-map failures.
A disposable installed-wrapper ABI-check bypass must compile then fail the real
consumer; exact header restoration then reruns the complete consumer inventory.
The runner separately rejects unrelated errors, successful exits, signals and
timeouts as negative proof; it kills/reaps only its owned process group, retains
first-cause receipts and restores its process-local Linux subreaper setting.
Original failures, commands, exits, tool/header hashes, target origins, compiler
dependency origins, inventories and cleanup state are retained in the output.
The standalone workflow runs the same entry points on native x86_64 and ARM64.

## Evidence and scope limits

This is temporary-prefix SDK ergonomics, **not** an approved shared shipping
layout or complete Issue #76 delivery. The test-only C entry and host callbacks
are not installed production-host loading. Product root CMake, production target
definitions, presets and all public header bytes remain unchanged; standalone
checks are not fresh full-product Studio ON/OFF or product-sanitizer evidence.
All five existing Architecture jobs and both standalone SDK jobs at the candidate
revision remain mandatory independent acceptance gates. Locally unexecuted
architectures/platforms are not PASS.

Product install integration, shared role/prefix/lookup/service ownership (#73),
Debian/RPM/source release composition, removal/upgrade/prior-SDK version skew,
installed production-host loading, platform/hardware qualification, rights and
notice clearance (#33), final branding (#40–42), registries and releases remain
separate OPEN gates. Installing the existing `LICENSE` is provenance evidence,
not legal/distribution approval. No shim binary, new ABI, release, merge, issue
closure or whole-product acceptance is established here.
