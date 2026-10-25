# CUT-0B repository integration and qualification

**IMPLEMENTED / AWAITING INDEPENDENT REVIEW.** This is an integration candidate,
not a new feature release, package acceptance, physical qualification or Orchestrator
activation. `develop`, `main`, repository settings and existing development branches
are unchanged.

## Immutable inputs and history

All four remote heads were checked with `git ls-remote --heads origin` before
creation and matched the approved CUT-0A inputs on 2026-10-25.

| Input | Commit | Original tree |
| --- | --- | --- |
| main | `ec045dd33628742297e015d0ac565a39d9d1e9b8` | `1bc97e0da79880ea7cc4a484f917cde091aa3ee1` |
| v0.3 | `ce34998cc467fa4ba6261917d7c9b81415f82cf6` | `07de45428c1331d95d88370ddf2c30d89c2aba03` |
| Core | `24c4d87939b538877695fce0c16d3c04dd4c3920` | `0c725addd69e9394b6854bdfde1958083d9ae269` |
| UI | `62428b07daa0cbffc7cf779bc78617be9300d812` | `55717d68a5e453b1f9f4eac3c7eedd8b2c79fcee` |

The new worktree is `/home/martin/src/mantis-studio-cut-0b`; branch
`integration/cut-0b` was created directly at pinned main with a clean index/worktree.
Merge bases: main/Core and main/UI `f0030515d01f547e8a922fa392d2b1296185da41`;
Core/UI `ea40d12777db391b00d10e8f378bc4c447f18fa0`;
v0.3/Core `90ecbabc7c8f311a627388d7267cf0fd9dcca51d`.

Normal `--no-ff` merges preserve both source histories:

- Core merge `a2e489d700c683038cbe1f07e1c805f0e0a1297f` (first parent pinned main, second parent pinned Core).
- UI merge `f58488f8a9e86ce52b78721742ab680a97faadb3` (first parent Core merge, second parent pinned UI).
- A subsequent focused qualification commit records documentation and the combined
  regression. Its exact SHA is supplied in the handoff and CI receipt.

No squash, rebase, cherry-pick or source-history rewrite occurred. UI-M0/M1/M2 are
ancestors of UI-M3. The only v0.3-head commits absent by ancestry are `b384bc1` and
`ce34998`; their instruction/workflow results are represented in Core. No v0.3
feature merge or earlier UI merge is needed. Core/UI standards commits `78516f8`
and `201e60d` are patch-equivalent; root AGENTS blobs are identical.

## Resolution and module boundaries

The Core merge conflicted only in README. The resolution retains main's visitor
presentation, four exact showcase image blobs and original `f5fefe7` capture
provenance, while retaining Core architecture/roadmap links, calibration and
acquisition limitations, software maturity and physical gates. It does not choose
one complete source README. Candidate build instructions and current development
status are reconciled; the screenshot images are not regenerated or relabelled as
physical results. Current roadmap, architecture authority and UI introduction link
this record. Historical acceptance/validation reports are unchanged.

The UI merge auto-resolved CMake and the workflow without textual conflict.
The five-job matrix, concurrency cancellation, GDB diagnostics, exact-revision
receipts, ten-test UI repetition and uploaded UI evidence remain intact. No public
headers, protocol fields, Store schemas, F2 bytes or source-package assertions are
changed by qualification. Studio still links the Client SDK/render/Qt layers,
not private services, Runtime or Store. Test-only PTY/storage fixtures remain
uninstalled and absent from BUILD_TESTING=OFF. No physical backend is activated.

## Generated test inventory

[Machine-readable inventory](cut-0b-test-inventory.json) records generated CTest
names and source registration hashes. Source configurations were read without
running tests or writing into the protected worktrees. Candidate inventories are
also checked against `ctest --show-only=json-v1`.

| Configuration | Core source | UI source | Candidate | Explanation |
| --- | --- | --- | --- | --- |
| Studio ON | 145 | 81 | 183 | Union, excluding one previously superseded name, plus `studio-core-daemon` |
| Studio OFF / default sanitizers | 142 | 42 | 143 | Same union and pre-existing replacement; no Qt-only addition |

No Core test is removed. The single old UI name `x1-pairing-stream` was already
replaced in accepted Core commit `c32c6a39ae1fcb17d4db910ae142a8d7a21af2f2` by
56 separate finite GREY/Y10P/software/hardware-shaped scenarios. Their exact names
are in the inventory; the same executable's scenario assertions remain enabled.
This integration does not restore the obsolete monolithic registration or remove
its coverage. Source Core's headless cache additionally had opt-in
`x1-f2-live-interoperability`; it is explicitly excluded from default-count comparison.
That private-checkout target remains optional and unchanged. Default E2 firmware
reference-frame regressions remain mandatory and run locally/CI.

## Combined functional evidence

| Requirement | Direct executable evidence and limit |
| --- | --- |
| INT-01 shared surfaces | Existing `studio-scan-surfaces`, `studio-scan-capture-wire`, `studio-scan-stress`, `studio-ui-m0-qml`, calibration QML/daemon tests: canonical pointer/attachment counts, Scan/Classic/Calibration/source/compact transitions, late results, disconnect/reconnect, preserved orbit/previews and offline interaction. No duplicate ownership implementation is added |
| INT-02 capture authority | New `studio-core-daemon` uses actual mantisd, accepted isolated X1 projected fixture and production StudioBridge/Client. A finite synthetic OFF/camera program holds the parent with blocked next; camera Start receives structured BUSY, projected RunId/GenerationId and zero-bundle state remain unchanged, camera Stop is unavailable and cannot stop the owner. Owner cancellation finalizes, then real Bridge camera Start/Stop succeeds. Discovery capability authorizes a request intent, not proof of resource availability; camera snapshot is not projected-run status |
| INT-03 project isolation | Existing `project-public-capabilities` intentionally reproduces the known replay-context defect; it does not certify safe switching. Existing Projects/M0/Scan/Bridge tests preserve disabled New/Open, A→B→A frontend generation invalidation and stale-result rejection. No QML workaround or backend redesign |
| INT-04 artifacts | New test rejects actual finalized schema-3 RawCapture as PointCloud selection with invalid_argument, no selected cloud or export authority, and verifies real schema-3 replay through Core. Focused DTO cases preserve OPEN/RECOVERABLE/unknown states and LaserObservation type without deriving PointCloud or action capabilities. Existing full Core suites validate actual semantic persistence/replay and failed recordings; this DTO test does not fabricate a persisted observation |
| INT-05 errors/cancel | New real-daemon cancellation preserves USER_CANCEL and FINALIZED recording, distinct from the rejected camera operation. Existing Bridge operation/snapshot/artifact failure tests, retry budget, generation fences, Core STOP/heartbeat/partial-TX, storage faults/crashes and cleanup-outcome regressions remain enabled |

The new test adds no private service/Store linkage to Studio. It reuses
`tests/integration/x1_projected.py` with an explicit `--studio-core` mode, the same
owned temporary project, synthetic profile, fixture guards and bounded daemon
lifecycle; the original nine capture/replay modes remain unchanged. OFF-only here
means synthetic commanded state, not an electrical or optical OFF observation.
The F2 one-step controller scope is unchanged (the existing L7a fixture supplies
the separate camera-ownership test).

## Rendered smoke evidence

[Three retained actual Qt captures](../ui/evidence/cut-0b/README.md) show Home
Demo / Mock, synthetic active Scan and disconnected Classic with retained local
geometry. They were inspected for obvious clipping/layout regressions, source and
stale labels and unavailable hardware controls. Existing screenshots, warning
assertions and geometry/resource tests remain enabled. These are software-only
fixtures, not physical observation or hardware acceptance.

## Maturity and holdouts

- v0.2's recorded camera acceptance remains historical evidence. v0.3 M0–M7 is
  implemented; M8 physical-calibration acceptance remains pending.
- Accepted Core software through L7b-2b-3a and L8a-0 keeps its package-specific
  status. L8a-1a remains IMPLEMENTED / AWAITING INDEPENDENT REVIEW. The latest UI
  revision/correction retains its independent exact-SHA acceptance gate.
- Candidate integration acceptance is separate from either stream's successful
  source CI. L8a as a whole is not accepted; G02–G05 and later UI work are not
  implemented here. No new projected controls, UI-M3d or generation API is added.
- Seven original worktrees, their branch heads, tracked diffs and every untracked
  file were fingerprinted before integration and compared again before handoff.
  Old v0.3 retains three dirty tracked files/seven untracked Virtual Scanner files;
  UI retains ten task Markdown files/four review PNGs. None are imported.
- `docs/parallel-development-study` at remote
  `f3a2f54b5a07bfff5626f4eb1a0391e194ab7ef2` is not merged. Its single
  `docs/architecture/parallel-development-study.md` proposal is explicitly
  non-normative; a later separately approved documentation change may retain it
  as background, without turning its repository/ownership proposals into ADRs.
  Only its objects were retrieved for inspection; its remote branch is preserved.
- No `develop`, release tag, default-branch change, permission change, Orchestrator
  configuration or promotion is part of this assignment.

## Qualification-only changes beyond the merge union

- README resolution and current pointers in ROADMAP, ARCHITECTURE, the v0.4 package
  introduction and UI README. No historical report or authoritative ADR is changed.
- This record, generated test inventory and three unedited review screenshots.
- One Linux/Studio/BUILD_TESTING-only CMake target and
  `tests/integration/studio_core.cpp`; an opt-in mode in the existing
  `x1_projected.py` harness. Existing nine modes, offline replay checks, assertions
  and default behavior remain enabled. No production C++ is changed.

## Local reproduction and results

The complete local configurations below passed. Builds use
isolated directories in the integration worktree. GCC 15.2 / Qt 6.9 / OpenCV 4.10
on Linux x86_64; dependencies from the previous owned extracted Ubuntu package
cache were copied into ignored `build/deps`, without changing source dependencies
or writing into another worktree. Standard dependency/preset instructions remain
[BUILDING.md](../../BUILDING.md) and [CONTRIBUTING.md](../../CONTRIBUTING.md).

| Local check | Result |
| --- | --- |
| Complete Studio ON Debug build / full CTest | PASS 183/183, 485.26 s |
| Complete Studio OFF Debug build / full CTest | PASS 143/143, 145.51 s |
| Repository headless ASan/UBSan build / full CTest | PASS 143/143, 355.72 s; leak detection, halt on errors, no suppressions |
| BUILD_TESTING=OFF headless product build | PASS; no Qt discovery, new test targets or test-only PTY/storage instrumentation |
| Existing ten-route CI repetition gate | PASS, 10 tests × 3 = 30 executions, 324.63 s |
| New real Core/Bridge regression repetition | PASS, 20 fresh daemon/Bridge lifetimes, 4.45 s |
| Generated CTest inventory | PASS exact JSON names 183 ON / 143 OFF / 143 sanitizer, with the documented pre-existing replacement |
| ABI/catalog / repository and binary boundaries | PASS; frozen 129 structs, 10 callbacks, 816 fields, 113 constants and 22 IDs; unchanged product SDK/protocol/Core implementation |
| Format, whitespace, Python compile, links, screenshot hashes, history | PASS; final scoped checks before commit |

Complete suites include accepted schema-1/2/3 and storage goldens, new storage
fault/crash/recovery cases, semantic observations/processing, daemon recording and
controller-free replay, F2/E1/E2 reference frames, PTY/serial/R1 abandonment,
camera pairing/acquisition and calibration. `project-public-capabilities` passes
by detecting the known isolation defect; it is not evidence that defect is fixed.
No source test/assertion was disabled or weakened. The repository sanitizer
preset is headless; this does not claim a new Qt-enabled sanitizer matrix or
native ARM64 execution. Optional live firmware testing was not enabled; the
unchanged self-contained E2 cases ran in every complete configuration.

```bash
export LD_LIBRARY_PATH="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu"
export OPENCV_OPENCL_RUNTIME=disabled
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3 -DOpenCV_DIR="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu/cmake/opencv4"
cmake --build --preset linux-debug --parallel 6
ctest --preset linux-debug
cmake --preset headless -DPython3_EXECUTABLE=/usr/bin/python3 -DOpenCV_DIR="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu/cmake/opencv4"
cmake --build --preset headless --parallel 4
ctest --preset headless
cmake --preset sanitizers -DPython3_EXECUTABLE=/usr/bin/python3 -DOpenCV_DIR="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu/cmake/opencv4"
cmake --build --preset sanitizers --parallel 5
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/sanitizers --output-on-failure
cmake -S . -B build/no-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMANTIS_BUILD_STUDIO=OFF -DBUILD_TESTING=OFF -DPython3_EXECUTABLE=/usr/bin/python3 -DOpenCV_DIR="$PWD/build/deps/root/usr/lib/x86_64-linux-gnu/cmake/opencv4"
cmake --build build/no-tests --parallel 3
ctest --test-dir build/debug -R '^(studio-home-qml|studio-projects-qml|studio-projects-resize|studio-projects-scroll|studio-devices-qml|studio-devices-stress|studio-scan-qml|studio-scan-stress|studio-scan-surfaces|studio-ui-m0-qml)$' --repeat until-fail:3 --output-on-failure
ctest --test-dir build/debug -R '^studio-core-daemon$' --repeat until-fail:20 --output-on-failure
python3 tests/contract/boundaries.py .
python3 tests/contract/l2/generate_catalog.py --check
git diff --check
```

Full configure/build/CTest logs and repetition logs remain in ignored
`build/cut0b/`; the generated inventory and selected screenshots are retained in
source control for review. The final original-worktree comparison matched all
seven HEADs/statuses, tracked-diff hashes and all 21 untracked file hashes.
Remote main/Core/UI/v0.3 and study heads still matched their pinned inputs.

No local native ARM64 run or physical operation is claimed. Required remote jobs
are x86_64 ON/OFF, native ARM64 ON/OFF and ASan/UBSan at the exact pushed candidate.
One nonblocking status check is made at handoff; independent review decides acceptance.
No real UART, camera, Q6A, ESP8266, GPIO, USB-HIL, firmware flashing or laser
operation was performed. No firmware was modified.
