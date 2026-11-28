# Studio ON bridge compilation baseline

This is the measurement-only result for [#159](https://github.com/martinkoenig/mantis-studio/issues/159), under [#111](https://github.com/martinkoenig/mantis-studio/issues/111). One local x86_64 Debug Ninja run compiled the same Studio bridge source into ten target-specific objects. A timestamp-only change rebuilt all ten in **52.652 s** wall time with two Ninja jobs. This is a baseline for the existing graph, not a measurement of savings from sharing an object.

## Source and cross-architecture inventory

Measured `develop` commit [`870f331a03615af0a8a29aeaefc560e43a861d1a`](https://github.com/martinkoenig/mantis-studio/commit/870f331a03615af0a8a29aeaefc560e43a861d1a), tree `7ed988712c55b92c43ad1dcc41f26d2aa4499d29`, root `CMakeLists.txt` blob `df2f2b90e25b17985a0c33b536e55ae93645bf70`. The two successful Studio ON jobs in [Architecture run 46086070525, attempt 1](https://github.com/martinkoenig/mantis-studio/actions/runs/46086070525/attempts/1) independently logged the same checked-out SHA (`git log -1 --format=%H` after checkout) and the same ten outputs as the local compilation database and Ninja rules.

| Retained job | Raw Build lines (1-based, inclusive) | Unique bridge outputs |
| --- | ---: | ---: |
| [Ubuntu 24.04 x86_64 ON / 140795808307](https://github.com/martinkoenig/mantis-studio/actions/runs/46086070525/job/140795808307) | 2176–2971 | 10 |
| [Ubuntu 24.04 ARM64 ON / 140795808226](https://github.com/martinkoenig/mantis-studio/actions/runs/46086070525/job/140795808226) | 2107–2906 | 10 |

Counted only complete `Building CXX object CMakeFiles/<target>.dir/apps/studio/bridge.cpp.o` progress records in step 9 (`Build`), between its command-group start and the next `Test` group. The separately labelled `Build` step matched the raw-log inventory. Each job also reported eight distinct `build/ci/.rcc/qrc_*.cpp.o` objects and the separate `tests/integration/studio_bridge.cpp.o` object; neither is part of the ten. Both jobs logged GNU C++ 13.3.0, CMake 3.28.3, Ninja 1.11.1 and Qt base 6.4.2 packages, with Ninja Debug Studio ON, `ccache` launchers and four build jobs. Those records establish graph inventory, not identical configured argv across architectures or compile durations. Parallel progress and cache hits make the hosted timestamps unsuitable for timing individual compilations.

## Local procedure and timings

Ubuntu 24.04.5 LTS, x86_64 (`x86_64-linux-gnu`), GNU C++ 13.3.0, CMake 3.28.3, Ninja 1.11.1, Qt 6.4.2 and `protoc` 3.21.12. Configured once using Ninja, `Debug`, `MANTIS_BUILD_STUDIO=ON`, `BUILD_TESTING=ON`, `MANTIS_SANITIZE=OFF`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, and `/usr/bin/python3`. Explicit `/usr/bin/cc` and `/usr/bin/c++` with both compiler launchers empty; actual bridge commands used the compiler directly. Neither package nor cache configuration was changed.

On 2026-11-28 UTC, one guarded campaign used `mantis-heavy-build` with a 1,180 s timeout inside the slot and `ninja -C "$build" -j 2 -d explain` with the ten explicit object paths below. The source and empty build roots were disposable and owned. All 1,122 extracted source files were checked against their Git blobs and modes. After configure, no object, generated Protobuf header, Ninja timing log or deps log existed. One Ninja process ran at a time; no local CTest or full build ran. The guarded setup, measurements, checks and root retirement took **379.375 s** total. “Cold” refers to the build tree, not a flushed OS page cache or a fresh host.

| Phase | Subprocess wall (s) | Bridge compile edges | Other Ninja edges | Summed bridge edge time (s) | Summed other edge time (s) |
| --- | ---: | ---: | ---: | ---: | ---: |
| CMake configure, including compiler checks | 1.928 | — | — | — | — |
| Cold ten-object request | 318.486 | 10 | 116 | 113.918 | 509.919 |
| Unchanged ten-object request | 0.035 | 0 | 0 | 0 | 0 |
| Shared-source timestamp rebuild | 52.652 | 10 | 10 | 104.452 | 0.455 |
| Restored final no-op | 0.037 | 0 | 0 | 0 | 0 |

Elapsed wall is monotonic subprocess time including Ninja startup and graph traversal. Edge times are millisecond start/end deltas from the phase-local `.ninja_log`. For multi-output rules, the 188 cold and 50 incremental log rows were grouped by their configured producers, yielding 126 and 20 completed commands, respectively, matching Ninja progress. Concurrent edge times overlap: their sums are neither CPU time nor elapsed wall time and do not predict removable wall time. Each phase is one observation, not a distribution.

Each target below represents exactly `CMakeFiles/<target>.dir/apps/studio/bridge.cpp.o`; all ten appeared once in each hosted Build step and in each measured build phase that did work. Group letters describe the comparison below, not interchangeable objects.

| Target | Group | Cold compile (ms) | Timestamp compile (ms) | Cold target AUTOGEN (ms) | Timestamp target AUTOGEN (ms) |
| --- | :-: | ---: | ---: | ---: | ---: |
| `mantis-studio` | B | 11089 | 10538 | 929 | 43 |
| `mantis-studio-bridge-tests` | A | 10860 | 10398 | 940 | 40 |
| `mantis-studio-core-tests` | A | 11047 | 11090 | 935 | 46 |
| `mantis-studio-scan-tests` | C | 11009 | 10885 | 1567 | 48 |
| `mantis-studio-devices-tests` | C | 11568 | 10202 | 934 | 46 |
| `mantis-studio-projects-tests` | C | 10785 | 10420 | 1600 | 52 |
| `mantis-studio-home-tests` | C | 10684 | 10216 | 1311 | 47 |
| `mantis-studio-ui-m0-tests` | C | 13761 | 10193 | 1368 | 47 |
| `mantis-studio-x1-role-daemon-tests` | D | 9635 | 10240 | 871 | 43 |
| `mantis-studio-calibration-qml` | B | 13480 | 10270 | 1108 | 43 |

The cold graph did more than compile these objects. Its 116 prerequisite edges were 63 other C++ compilations (483.621 summed edge seconds), 25 static archives (3.928 s), the `mantisd` link (3.630 s), the X1 projected fixture module link (0.300 s), and 26 custom commands (18.440 s). The custom commands were 17 AUTOMOC/AUTOGEN actions (15.810 s), two Protobuf generators (0.156 s), and seven `rcc` source generators (2.474 s). The X1 role daemon test's explicit order dependencies on `mantisd`, the projected fixture and `mantis-python` account for its broader cold closure. The seven `rcc` actions generated QML resource sources but did not compile resource objects; the independent X1 role QML target is outside this request. None of the ten consuming executables was linked. Only the ten bridge compiles and their ten AUTOGEN actions ran after the timestamp change.

## Configured command and dependency comparison

The actual Ninja compiler argv matched `compile_commands.json` for each output after validating the shared depfile mode (`-MD`) and accounting for destination operands (`-o`, `-MT`, `-MF`). No flags, defines or include paths were sorted or dropped to form groups. All ten used the same ordered flags `-g -std=c++23 -fPIE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -fPIC` and the same `-c` source. There are **ten distinct ordered commands** even without output bookkeeping, because each starts its include search in its own `<target>_autogen/include`. Replacing only that operand with a target-relative placeholder, for comparison rather than equivalence, yields four groups:

| Group | Extra definitions in order, relative to the common definitions below | Other include order | Relevant target usage differences |
| --- | --- | --- | --- |
| A | Test definitions after `QT_QUICK_LIB` | Client-first project order, Qt Quick-first system order, then Qt Test | Both use Qt Test and Concurrent. Core also links `mantis-studio-scan`; the two have different drivers and AUTOGEN inputs. |
| B | `QT_QUICKCONTROLS2_LIB` before `QT_QUICK_LIB` | Render-first project order, Qt Quick-first system order, then QuickControls2 | App and calibration QML test use the Studio model libraries and QuickControls2, without Qt Test. Link order, drivers and resource registrations differ. |
| C | QuickControls2 definition before `QT_QUICK_LIB`, Test definitions after it | Same as B, followed by Qt Test | These five use the Studio model libraries with QuickControls2 and Qt Test; each still has its own driver, AUTOGEN path and resource registration. |
| D | None | Client-first project order, Qt Core-first system order | Role daemon test uses Studio calibration, Quick and Concurrent, without Qt Test or QuickControls2; it has the additional order-only dependencies above. |

- The common definitions, in order, are `QT_CONCURRENT_LIB`, `QT_CORE_LIB`, `QT_GUI_LIB`, `QT_NETWORK_LIB`, `QT_OPENGL_LIB`, `QT_QMLINTEGRATION_LIB`, `QT_QMLMODELS_LIB`, `QT_QML_LIB`, `QT_QUICK_LIB`. The Test suffix is `QT_TESTCASE_BUILDDIR`, `QT_TESTCASE_SOURCEDIR`, `QT_TESTLIB_LIB`; the two path values are the same configured build and source roots within this trial.
- After `<target>_autogen/include` and `apps/studio`, the project `-I` order in A/D is client, protocol, build root, platform, base, data, memory, schema, time, calibration, spatial, render. In B/C it is render, data, memory, base, schema, time, calibration, spatial, client, protocol, build root, platform. Component names here stand for their `src/<component>/include` paths.
- The Qt `-isystem` order in A/B/C starts Quick, Qt include root, Core, Linux GCC mkspecs, Gui, Qml, QmlIntegration, Network, QmlModels, OpenGL, Concurrent, followed by the group-specific suffix. D uses Core, Qt include root, Linux GCC mkspecs, Concurrent, Quick, Gui, Qml, QmlIntegration, Network, QmlModels, OpenGL.
- Ninja dependency records showed the same ordered 642 compiler input paths for all ten. The only generated file actually read by `bridge.cpp` in this configuration was `mantis.pb.h`, generated from `protocol/protobuf/mantis.proto`. That read set is not the whole graph: per-target AUTOMOC scans `bridge.hpp` (`Q_OBJECT`), `preview.hpp`, the common source and a target-specific driver. Its `moc_bridge.cpp` and `mocs_compilation.cpp` copies matched by digest across these ten targets but are separate generated outputs, not inputs read by `bridge.cpp`. The File API link fragments and Ninja order-only edges retain the target distinctions above.

All ten bridge objects also had one common SHA-256 in each compiling phase. This is a useful observation for **this exact checkout, toolchain and configuration**. Neither matching bytes here nor placeholder-matched argv prove that a shared target preserves usage requirements, generated outputs, QML initialization, link composition or behavior in other configurations or after source changes. Keep all four groups inconclusive for sharing.

## Controls, evidence and limits

After the first no-op, only the disposable `apps/studio/bridge.cpp` mtime was advanced, past all cold bridge-object mtimes. Ninja explained both the ten object and ten AUTOGEN invalidations with that input. The source bytes, other source bytes and mtimes, other compiler inputs and measured generated contents matched across the intervention. The bridge mtime was restored to its original value. Both no-ops completed with unchanged timing and dependency logs and all ten objects present.

For the negative control, temporarily moved the object selected by the **last** anchored bridge completion in the independent x86_64 Build log (`mantis-studio-x1-role-daemon-tests`). The reconciliation rejected the missing object at its actual path (exit **3**, not a build or setup failure). Putting the same file back restored its inode, size and mtime and reproduced the original ten-output identity manifest byte-for-byte. The final Ninja no-op then confirmed zero work. The guarded local run completed without failed or partial timing phases. A hosted-log extraction preflight initially missed an ANSI-colored configure echo; the corrected parser uses the configure group marker and checks both original raw-log digests. That preflight was not a timing sample.

Exact invocations, raw logs and their digests, configured graph and command data, phase-local Ninja records (including individual prerequisite timings), source and output checks, and the negative/restored receipts are retained privately for independent review. [PR #145](https://github.com/martinkoenig/mantis-studio/pull/145) and its [integration record](https://github.com/martinkoenig/mantis-studio/issues/111#issuecomment-7166550145) supply prior incremental-correctness evidence, not real-product timing or a substitute for normal required CI on this report's PR.

No candidate, ARM64 native timing, full Studio link or runtime equivalence, QML resource refactor, or APT/cache savings were measured. The separate QML investigation, controlled APT, actual before/after full-build and application qualification, and held PR #130 / #39 remain outside this result.
