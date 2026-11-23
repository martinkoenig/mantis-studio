# Configured CMake toolchain provenance (internal v1)

`tools/ci_toolchain_provenance.py` is a standalone, Python 3.10+ / POSIX utility.
It **reads configured metadata and fingerprints current files**. It does not run
CMake, Git, compilers, launchers, a shell, network requests or version probes.
Importing its module performs no acquisition or filesystem writes.

This bounded Issue #111 stage is not integrated into the product build or the
Architecture workflow. Product reporting integration remains separate. The
standalone `configured-toolchain` workflow exercises ordinary native Ubuntu
24.04 x86_64 and ARM64 fixtures, not the product dependency graph.

## Explicit capture and frozen verification

Before configuring, request the CMake File API toolchains v1 reply. For example,
with **absolute**, separate source/build roots and a previously absent report:

```bash
mkdir -p "$BUILD/.cmake/api/v1/query"
touch "$BUILD/.cmake/api/v1/query/toolchains-v1"
cmake -S "$SOURCE" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD" --parallel 1
ctest --test-dir "$BUILD" --output-on-failure -j 1

python3 -B tools/ci_toolchain_provenance.py capture \
  --source "$SOURCE" --build "$BUILD" --revision "$REVISION" --dirty clean \
  --input CMakeLists.txt --input main.cpp --input value.c \
  --report "$REPORT"
```

Capture prints the SHA-256 of the **complete report bytes**. Retain that value
independently in the submitting evidence ledger, then pass that frozen value as
`$FROZEN_REPORT_SHA256` (never recompute it from a candidate during verification):

```bash
python3 -B tools/ci_toolchain_provenance.py verify \
  --source "$SOURCE" --build "$BUILD" --revision "$REVISION" --dirty clean \
  --input CMakeLists.txt --input main.cpp --input value.c \
  --report "$REPORT" --report-sha256 "$FROZEN_REPORT_SHA256"
```

Success: exit 0 and `VERIFIED configured-toolchain-v1`. Input/verification errors:
exit 2 with one bounded primary diagnosis on stderr. No raw input values or
exception paths are printed. Capture refuses existing files, directories and
symlinks, including a concurrent no-replace publication collision. It writes a
mode-0600 temporary, flushes/fsyncs it, then hard-links the complete file into the
absent destination. Failed publication removes the private temporary when possible;
a cleanup failure does not replace the initiating diagnosis. A crash or cleanup
failure can retain a private `.provenance-*` temporary; this is **not** a published
report. Parent directories must already exist. No overwrite/repair mode exists.
This is not a power-loss-durable evidence store (no directory fsync or signing).

Verification only reads. It never captures, writes, repairs, reseals or updates an
expectation. The digest is an out-of-band integrity expectation, **not** a signature:
a caller able to replace both the report and trusted digest can substitute evidence.
Inputs/configuration/tools must be quiescent throughout capture/verify. This is
not an atomic snapshot or a defense against a concurrently malicious filesystem.

## Schema and origins

The UTF-8 JSON object has exactly these top-level fields; all nested expected data
is compared without dropping unknown keys. No schema migration is implicit.

| Field | Meaning and origin |
| --- | --- |
| `schema` | Literal `configured-toolchain-v1`; internal report format, not a product/SDK contract. |
| `binding` | SHA-256 of resolved absolute source/build root bytes (`os.fsencode`). Cache HOME/CACHEFILE directory values must be absolute and resolve to those exact roots. Relocation requires new explicit capture. |
| `context` | Explicit caller-declared revision (40/64 lowercase hexadecimal), `dirty` (`clean`/`dirty`), and `origin=caller-declared`. The CLI does not independently inspect Git or assert these describe configure-time source. |
| `inputs` | Sorted caller-selected source-relative names to exact current byte SHA-256; includes `CMakeLists.txt`. No directory traversal or resolved escape from source is allowed. |
| `configured` | Generator/build type; C/CXX compiler ID/version/path and executable fingerprints from `toolchains-v1` cross-checked against cache compiler paths; CMake version/path from the File API index cross-checked against `CMAKE_COMMAND`; generator executable from `CMAKE_MAKE_PROGRAM`; optional C/CXX launchers from cache; optional `CMAKE_TOOLCHAIN_FILE` path/content fingerprint. Each tool has configured-path, resolved-path and file-byte SHA-256, never raw paths. Compiler/CMake origins are explicit; build-tool version is `UNVERIFIED` (no executable version probe). Launcher identities describe cache configuration only, not target-level overrides or actual use. |
| `configured.settings_sha256` | Selected normalized cache values: C/CXX flags and Debug/Release/RelWithDebInfo/MinSizeRel variants; executable/shared linker flags; generator platform/toolset. Values are SHA-256 of UTF-8 strings; absent is null, empty is SHA-256 of empty bytes. Arbitrary flags are not published. Other settings remain covered by the full cache fingerprint, not claimed normalized semantically. |
| `observed` | Exact current byte SHA-256 of `CMakeCache.txt`, the sole File API index and its referenced toolchains reply. Unselected/unknown CMake data is not published but changes are not ignored. Even harmless metadata regeneration invalidates a frozen report. |
| `unverified` | Fixed claim limits: actual tool invocation, source consumed, dependency closure, package authenticity, performance, release provenance and OS isolation. |

CMake File API input is parsed as JSON data, never as CMake code. Supported reply
version is exactly toolchains **1.0**, with exactly C and CXX language records.
The index must contain a direct `toolchains-v1` query reply and exactly one index
must exist; the referenced filename cannot traverse directories. Ordinary
single-config Ninja and Unix Makefiles are supported; multi-config/IDE generators,
other languages, nonstandard build types and missing metadata fail explicitly.
CMake's ID/version strings are bounded tokens; they are configured metadata, not
independently observed version output. No fallback fabricates unavailable values.

Tool paths must be absolute regular executable files (symlinks resolve and are
bound by path/content hashes). Bare launcher names such as `ccache`, launcher
lists/arguments and relative toolchain-file paths are unsupported: explicitly
configure an absolute single launcher executable rather than resolving from the
verifier's PATH/cwd. A configured tool is only read, **never executed**, even if it
is a script. The toolchain file is fingerprinted as data, without execution.

Limits: 1 MiB report; 8 MiB each cache/index/toolchains reply; 256 MiB each tool;
16 MiB each selected source/toolchain file; 64 MiB total selected source data;
1–128 distinct selected inputs, names at most 200 characters, safe relative ASCII
names (including spaces). Nonregular inputs (including FIFOs) fail without waiting
for a writer. Unsupported inputs require a separately reviewed schema extension,
not silent omission. The explicit source list is **not dependency closure**: it
omits unlisted headers, generated inputs, external includes, included toolchain
files, CMake modules, linker/runtime components and target-level settings.

## Diagnoses and confidentiality

Malformed/truncated JSON, unsupported schema/API version, missing files, wrong
roots and unavailable tools fail before a successful comparison. A changed report
fails `report-digest-mismatch`; unknown top-level fields fail
`report-shape-mismatch` even when supplied with a matching digest. For otherwise
valid current input, comparisons use this primary order:

1. `root-binding-mismatch`;
2. `stale-revision-or-dirty-context`;
3. `source-input-mismatch` (includes changed selected list);
4. `configured-tool-or-setting-mismatch`;
5. `configuration-fingerprint-mismatch`;
6. `report-claims-mismatch`.

Cache/source root disagreement is `configured-root-mismatch`, missing/unreadable
configured tools are `configured-tool-unavailable`, and absent/ambiguous File API
indexes are `missing-or-ambiguous-file-api-index`. Invalid current metadata can
therefore be the primary diagnosis even when another comparison would also fail.

Reports deliberately omit raw roots/tool paths, environment/cache dumps, arbitrary
flags, implicit include directories and commands. Explicit source filenames,
revision/dirty context, compiler IDs/versions and hashes remain visible. **Inspect
that bounded representation before public publication**; hashes are not encryption
and can reveal equality or low-entropy values. Raw local command receipts and cache
metadata stay private. The dedicated workflow does not upload them or auto-publish
reports. These are integrity/configuration observations, not accepted authenticity,
security policy, invoked-tool attestation, a source-to-binary chain, cache hits or
speedup evidence. Do not execute metadata-supplied paths to strengthen the claim.

## Regression qualification

```bash
MANTIS_PROVENANCE_TEST_ROOT="$PWD/build/provenance-evidence/new-run" \
  /usr/bin/python3 -B tests/ci/toolchain_provenance_test.py
```

The evidence directory must not exist. Requires installed CMake, Ninja, CTest,
GCC/G++, `/usr/bin/env`, and POSIX/Linux process APIs. No installation,
product configuration or heavy workload is performed. Use the project's actual
single-HEAVY resource admission before this tiny compile campaign, one compiler
job and serial CTest. Source copies/builds (including paths with spaces), command
receipts, PID/birth/group retirement and frozen reports remain in the explicit
private evidence directory. The trusted fixture harness accepts explicit absolute
`MANTIS_PROVENANCE_CMAKE`, `MANTIS_PROVENANCE_CTEST`, and `MANTIS_PROVENANCE_NINJA`
paths (local defaults `/usr/bin/cmake`, `/usr/bin/ctest`, `/usr/bin/ninja`). CI
explicitly selects the preinstalled tools using `command -v` before running tests;
this selection is outside the collector and never derived from cache metadata.
Missing/nonabsolute selected tools fail, without alternate-path fallback.
`MANTIS_PROVENANCE_REVISION` can supply a known checkout
revision (CI passes `github.sha`); otherwise tests use an explicitly synthetic
caller-declared revision. `MANTIS_PROVENANCE_CLI` selects a disposable candidate for
negative-sensitivity qualification, without changing production code.

Tests independently compare configured GNU versions with explicit trusted fixture
compiler probes and binary/source hashes. They exercise missing/malformed inputs,
root/context/config/source/tool drift, nonexecution of a hostile launcher, inert
module startup, verification under write/process/network denial, byte-identical
inputs after verify, overwrite refusal and injected partial-publication/cleanup
failures. Negatives require the intended exit **and** primary diagnosis; unrelated
failures/timeouts cannot count as a passing rejection. Exact input restoration
must verify GREEN. Local product Studio ON/OFF/sanitizer suites are not run or
claimed fresh PASS under this standalone unchanged-product-input scope; all five
Architecture exact-head jobs plus both new native jobs remain independent gates.
