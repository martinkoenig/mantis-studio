# Shared Studio QML resource for the app and calibration test

[Issue #175](https://github.com/martinkoenig/mantis-studio/issues/175) replaces two RCC generation and compile pairs with one internal object in the Studio build. `mantis-studio` and `mantis-studio-calibration-qml` each link that object directly after their own objects and before their libraries. This retains the static initializer in each program without a static archive or a new consumer dependency. The other six Studio resource producers are unchanged.

## Matched build observations

Measured on 2026-11-28 UTC. Baseline `develop`: `7df3ff966c95aa0206f0d9440b1a279692f30298` (tree `562a801a4ddf2d93d0afa2d79b4447ef18774007`). Candidate before this report: `3591b94141b79de5ff9bee21ed9c6d79f3291031` (tree `2a1797eb8499aed5087f9ce56c452984b718dba0`, root CMake blob `21829c49fae8a19ad3622fe2e1d9ec8b5c55b46a`). The disposable source snapshots matched their Git bytes and modes and differed only in `CMakeLists.txt`; their input mtimes matched before the experiment.

Ubuntu 24.04.5 x86_64; GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1, Qt/RCC 6.4.2 and `protoc` 3.21.12. Both trees used Studio ON, testing ON, Debug, sanitizers OFF and empty C/C++ compiler launchers. Under one `mantis-heavy-build` lease, each ran `ninja -j 2 -d explain -v mantis-studio mantis-studio-calibration-qml` in the phase order below, baseline first. Cold means fresh build outputs after configure, not a flushed OS cache. Configure and tests are excluded; phases and tests did not overlap.

| Phase | Baseline wall (s) | Candidate wall (s) | Completed commands, baseline → candidate | RCC / resource C++ / executable links, baseline → candidate |
| --- | ---: | ---: | ---: | --- |
| Cold | 110.794 | 109.205 | 57 → 55 | 2 / 2 / 2 → 1 / 1 / 2 |
| Unchanged | 0.028 | 0.028 | 0 → 0 | 0 / 0 / 0 → 0 / 0 / 0 |
| `ui/shell/Main.qml` mtime only | 5.119 | 5.064 | 6 → 4 | 2 / 2 / 2 → 1 / 1 / 2 |
| Original input mtime restored | 0.027 | 0.036 | 0 → 0 | 0 / 0 / 0 → 0 / 0 / 0 |

The Ninja command counts reconcile with the phase-local log rows after grouping by configured producer. All 51 cold prerequisite commands remained, and both executables relinked in both active phases. Both no-ops left Ninja timing and dependency logs unchanged. Restoring the input did not restore cold output bytes: the rebuilt C++ retained one changed line encoding the new `Main.qml` timestamp, and each rebuilt resource object retained four changed bytes in that timestamp field in `.rodata`. The resource names and payload bytes were unchanged.

## Ownership and behavior

- **Resource and link equivalence:** RCC's configured map and Ninja inputs agreed on 78 `:/ui/...` aliases covering 1,810,951 source bytes. The actual linked resource arrays matched their generated C++ and object bytes; the cold arrays matched across both trees. Each ELF had one registration path from `.init_array` to `qInitResources_*` and `qRegisterResourceData`, with the same position in its initializer sequence. The ordered non-resource link operands and `DT_NEEDED` entries matched. Driver AUTOMOC, bridge ownership, generated install rules and all 224 Studio ON CTest registrations remained unchanged. The six other resource owners retained their RCC, compile and link commands.
- **Actual binaries:** The original `studio-ui-m0-cli` and `studio-calibration-qml` CTests passed serially on both linked pairs (2/2 each), with unchanged commands, environments and timeouts. Omitting only the direct resource object from a disposable calibration relink made the original test fail at `qrc:/ui/shell/Main.qml` (CTest exit 8). Restoring the original ELF without rebuilding it preserved its exact identity and bytes; the same test passed.
- **Build boundaries:** Both separate Studio ON/testing OFF trees built the standalone `mantis-studio` target without Qt Test and passed the existing CLI smoke script directly, using the registered offscreen/software environment and 45-second bound. The Studio OFF/testing ON graphs and 181 registrations matched; neither configured Qt or Studio targets.

These are paired observations at one native configuration, not a general speedup estimate, ARM64 timing, or hardware or full rendering qualification. The scoped command reduction does not resolve controlled APT, authenticated provenance or broader equivalence under [#111](https://github.com/martinkoenig/mantis-studio/issues/111). Raw evidence is retained privately for independent review.
