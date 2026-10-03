# Validation record — Skeleton v0.1.0

Validation date: 2026-10-03 (UTC; 2026-10-04 locally in Europe/Berlin for the resumed final run). The evidence belongs to this source delivery; it is not a promise of production scanner performance or unexecuted platform support.

## Executed environment

Ubuntu 24.04 x86_64, GCC 13.3.0, CMake 3.28, Qt 6.4.2 (offscreen software rendering for automation), Protobuf 3.21.12, SQLite 3.45.1, Python 3.12.3. All application/plugin targets compiled with warnings enabled. The final native builds produced no project-source warnings.

| Configuration | Build | CTest result |
| --- | --- | --- |
| Debug desktop, Qt enabled | Passed | 4/4 suites passed |
| Debug headless, Qt disabled | Passed | 4/4 suites passed |
| Headless ASan + UBSan | Passed | 4/4 suites passed; no ASan/UBSan diagnostics; final run uses `detect_leaks=0` |

The sanitizer build enables address/undefined behavior instrumentation and frame pointers and halts on undefined behavior. The earlier pre-interruption run passed with leak detection enabled. In the restarted execution environment, LeakSanitizer cannot inspect `/proc/<pid>/task` and terminates with an environment/ptrace error. The final ASan/UBSan repeat therefore used `ASAN_OPTIONS=detect_leaks=0`; no final leak-detection pass is claimed. The failed environment probe is retained in `docs/validation/leaksanitizer-environment-limitation.log`. CI keeps leak detection enabled for normal native runner environments. The deliberately crashing plugin terminates only its child host; a successfully detected child crash is expected in that fixture.

## Coverage

- **Core:** buffer publication/lifetime/alignment; non-host mapping rejection; host ABI allocation/release and immutable maps; attribute extent validation; double transform composition/versioning; Virtual Scanner DSO loading and lifecycle; deterministic frames and geometry; recipe graph typing/schema versions/cycles/backend rejection; fan-out; retained stateful streaming instances; every queue policy, blocking/cancellation and ordered stress; mapped packet round-trip; project lock; immutable final artifacts; interrupted capture with an invalid trailing partial; successful isolated processing and export; job cancellation.
- **C ABI:** the SDK header compiles as C and validates size/version prefix placement.
- **Boundaries:** forbidden foundation includes, frontend/runtime separation and first-party public SDK usage. CMake also checks actual transitive target dependencies at configure time.
- **Integration:** real daemon/CLI/Python/Qt/host processes; local token rejection; unknown-field tolerance and protocol-version rejection; identical Python/CLI/Studio/replay PLY payloads; capture continues during unrelated plugin crash; plugin failure/state/diagnostics; actual Studio viewport has nonzero dimensions and populated geometry; actual Studio restart; hard daemon kill/restart; explicit capture recovery and reprocessing.

## Reference workflow acceptance

| Steps | Evidence |
| --- | --- |
| 1–3: daemon, Studio, discovery | Real process launch, plugin device descriptor, asynchronous Studio snapshot |
| 4–8: capture, public contracts, processing, artifact, visualization | Studio automation uses the same bridge slots as controls; viewport-ready condition verifies populated, visible geometry |
| 9: plugin export | Studio-created export matches deterministic reference bytes |
| 10–11: CLI and Python | Independent control clients create/export through the daemon |
| 12–13: isolated crash and survival | Host abort is a failed plugin/job; daemon and Studio remain alive; raw frame count increases |
| 14–15: UI restart and state | Qt client exits/restarts; runtime capture remains active; finalized artifacts remain valid |

The actual Qt screenshot in `docs/images/studio.png` was inspected after a layout correction. It shows the 3,072-point reference cloud, capture status, artifact state, completed jobs and runtime diagnostics. It is not a mockup.

## Performance observation

A local Debug run of `mantis-benchmark` transferred 100,000 integer queue items in approximately 1,357 ns/item, reached the configured high-water mark of 64 and dropped zero items. This is a workstation observation, not a throughput guarantee, an ARM measurement, or scanner benchmarking. Pipeline jobs emit individual node timings into diagnostics. Capture snapshots expose frame count, dropped count and queue high-water mark.

## Not executed / not claimed

- Native Linux ARM64 CI is configured but was not run on GitHub during this task.
- Windows x86_64 and macOS ARM64 were not built or tested.
- No physical Q6A, camera, laser, GPU interoperability or real scanner accuracy tests were performed.
- Process-kill recovery is demonstrated; hardware power-loss durability across storage devices/filesystems is not certified.
- No packaging installer, long-running soak, large-cloud renderer benchmark or adversarial sandbox/security audit is claimed.

Reproduce with the commands in `BUILDING.md`. CTest evidence logs from the final successful validation and the separate LeakSanitizer environment limitation are included in `docs/validation/`.
