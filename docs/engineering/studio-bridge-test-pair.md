# Shared Studio bridge build for two test targets

[Issue #171](https://github.com/martinkoenig/mantis-studio/issues/171) tests a narrow change suggested by the [ten-object baseline](studio-bridge-build-baseline.md) from #159. The Home and UI M0 test executables share a test-private `OBJECT` target for `bridge.cpp` and the moc output from `bridge.hpp` and `preview.hpp`. Their drivers and QML resources still have separate owners. In the measured pair, actual bridge compilations fell from **two to one**, both on a cold build and after touching only `bridge.cpp`. The existing seven affected CTests passed on both builds.

## Paired run

Measured on 2026-11-28 UTC at `develop` `5b3deaca4ace8104b7c9d9467d7b9ccf28c728b1` (tree `746ec23ffc4595910e35bbc9c213b143a6e132b0`). The candidate source was `12da25a86ff5701430dcd831e70c2f5eb7abbe3d` (tree `8cdba5a1cd1bef63359c5ced9c2dcbc142e0cc9c`, CMake blob `b9246b5f0e5634e0272555a7a4582d3a464d4fa5`), before adding this report. Both owned source snapshots matched their Git blobs and modes. The base moved after admission; the affected build declarations, bridge, QML, wire schema and tests were unchanged from #159's measured inputs.

The paired profile was Ubuntu 24.04 x86_64, GNU C++ 13.3.0, CMake 3.28.3, Ninja 1.11.1, Qt 6.4.2 and `protoc` 3.21.12. Both build trees were configured with Ninja Debug, `MANTIS_BUILD_STUDIO=ON`, `BUILD_TESTING=ON`, `MANTIS_SANITIZE=OFF`, exported compile commands, `/usr/bin/cc`, `/usr/bin/c++` and `/usr/bin/python3`, with both compiler launchers empty. Tool identities were checked before and after the run. Cold means no existing objects, generated Protobuf header or Ninja work/dependency log in either configured build tree, not a flushed system cache.

Under `mantis-heavy-build`, each tree ran `ninja -j 2 -d explain -v mantis-studio-home-tests mantis-studio-ui-m0-tests mantis-python` cold, unchanged, after advancing only its disposable `bridge.cpp` mtime, and after restoring that mtime. `mantis-python` supplies the existing Home wire fixture. The source bytes, other source mtimes and generated files were unchanged by the intervention. The two builds and then the two `ctest -j 1` runs were serial; no build overlapped a test.

| Build phase | Baseline wall (s) | Candidate wall (s) | Baseline completed edges | Candidate completed edges |
| --- | ---: | ---: | ---: | ---: |
| Cold pair and wire prerequisite | 119.156 | 108.253 | 58 | 59 |
| Unchanged no-op | 0.033 | 0.033 | 0 | 0 |
| Bridge-only timestamp rebuild | 12.700 | 11.545 | 6 | 4 |
| Restored no-op | 0.032 | 0.032 | 0 | 0 |

Completed commands are reconciled to Ninja progress and deduplicated by their configured producer in each `.ninja_log` delta. The table below gives edge counts and **summed** durations; parallel edge time is neither CPU time nor wall time.

| Work | Cold baseline | Cold candidate | Timestamp baseline | Timestamp candidate |
| --- | ---: | ---: | ---: | ---: |
| Bridge compile | 2 / 21.808 s | 1 / 10.455 s | 2 / 21.619 s | 1 / 9.597 s |
| Bridge/preview header moc compilation | 2 / 18.075 s | 1 / 8.423 s | 0 | 0 |
| Pair AUTOGEN actions | 2 / 5.143 s | 3 / 3.641 s | 2 / 0.097 s | 1 / 0.041 s |
| Driver-only empty moc wrapper compilation | 0 | 2 / 0.047 s | 0 | 0 |
| Executable links | 2 / 3.479 s | 2 / 3.281 s | 2 / 3.395 s | 2 / 3.586 s |
| Other prerequisites, including test drivers and resources | 50 / 181.044 s | 50 / 180.428 s | 0 | 0 |

Baseline AUTOGEN scans the headers and a driver together per test; candidate AUTOGEN scans the headers once and still scans each driver. Qt emits two empty driver `mocs_compilation.cpp` wrappers in the candidate, so the **cold command count increases by one** even though duplicate substantive compilation is removed. The two relinks still run after the timestamp change; no moc object is recompiled. Both no-ops left the Ninja timing and dependency logs unchanged. Wall times are one observation in each tree, not a calibrated speedup or a prediction for full builds, CI or ARM64.

## Build graph and behavior

- **Effective inputs:** Actual Ninja invocations matched ordered compilation-database arguments after reconciling `-MD`, `-MF` and `-MT`. The bridge's include search changes only at the owner-specific AUTOGEN directory after normalizing the snapshot roots. The first CMake PIC flag changes from executable `-fPIE` to object-library `-fPIC`; the final Qt `-fPIC` and the effective GCC PIC macros were unchanged. All 367 common compile entries matched, including both drivers and resource objects. The bridge retained the same 642 ordered compiler dependency paths, including the generated `mantis.pb.h`, and the wire generation order dependency. The header moc object retained the same 633 ordered dependency paths after mapping its generated owner. Generated C++ and Python wire code and the header and driver moc files matched byte-for-byte; moc predefines and RCC source and registration matched after root normalization.
- **Link and resource ownership:** Each driver still includes its own `studio_home.moc` or `studio_ui_m0.moc`. Each link adds the shared bridge and header moc objects before its own objects; the ordered library and linker arguments and ELF `DT_NEEDED` list stayed the same. The linked symbols identify one provider each for `StudioBridge` and `MeasurementView`, the original driver meta-objects, and one respective `studio-home-ui` or `studio-m0-ui` resource initializer. The two binaries' initializer sequences change only by swapping the bridge and driver static initialization; the resource initializer still follows both, and the inspected initializers did not call each other's test/QML setup or resource registration. The test-driver moc wrappers have no definitions in the candidate.
- **Existing tests:** `studio-home-qml`, `studio-home-wire`, `studio-home-hidpi`, `studio-home-fractional-dpi`, `studio-home-resize`, `studio-home-states` and `studio-ui-m0-qml` passed **7/7 on each tree**, with identical normalized commands and properties. Assertions, environment and deadlines were not changed. The shipping application and the other seven bridge consumers retain their own bridge and moc. The application's configured command closure and `studio-ui-m0-cli` registration matched, but the application and CLI route were not run locally. All 224 Studio ON registrations matched. Paired Studio OFF / testing ON and Studio ON / testing OFF configures retained the same respective graphs and CTest registrations; the shared target and pair are absent in both. No Qt package was resolved in Studio OFF. The full CI matrix remains required and unchanged.
- **Negative and restoration:** In a disposable candidate copy, adding `bridge.cpp` back to the Home driver yielded a second compile producer for the pair. The configured-graph oracle rejected that extra producer (exit 3), not a failed configure or unbuilt prerequisite. Restoring the exact candidate CMake bytes reproduced the normalized configured commands, targets and CTest graph and passed the ownership check. Raw configure, build, link, CTest and control receipts are retained privately for independent review under #99.

This result is limited to the two Studio test executables. It does not qualify wider bridge or QML sharing, application runtime equivalence, APT/cache changes, authenticated provenance, or release readiness. Those boundaries remain with #111 and the existing holds.
