# Read-only Git shadow collection

Issue110's collector records what a change-impact policy would select. It never
runs tests, grants selected-only admission or changes any gate. Every decision
retains the five native Architecture profiles and all applicable local final
suites, sanitizer/detector controls and original UI repetitions. Current source
closure, runtime safety and historical failure recall remain unqualified.
For the proposed conditions for ever omitting work, see the
[future selective-CI design](selective-ci-proposal.md). It is not activated.

## Local interface

```sh
python3 -B tools/ci_shadow.py --repository . --event push \
  --ref refs/heads/develop --base BEFORE_SHA1 --head PUSH_SHA1

python3 -B tools/ci_shadow.py --repository . --event pull_request \
  --ref refs/pull/NUMBER/merge --base BASE_PARENT_SHA1 \
  --head PR_HEAD_SHA1 --merge MERGE_SHA1
```

Use exact immutable 40-character commit IDs. The collector verifies that every
object exists, reads its tree and raw parent identities, checks the event's local
ref and actual checkout, then obtains the complete bounded raw NUL-delimited
base-to-tested-commit diff. PR merge parents must exactly match base and PR head;
the tested revision is the merge, not the PR head. Push comparisons use the supplied
pre-push base and checked push head. It rechecks ref/checkout after acquisition.
Both `refs/pull/N/merge` and checkout@v4's `refs/remotes/pull/N/merge` mapping are
supported; conflicting mappings are errors. Missing refs never fall back to HEAD.
Diff paths remain repository-root-relative even when `--repository` names a
subdirectory and repository configuration enables `diff.relative`; neither may
omit committed changes outside the invocation directory.

Endpoint assertions must come from the invocation's event context. Local Git
checks do not authenticate a provider event or prove which commit a server called
`before`; the collector does not claim that authority. It does not attest dirty
working-tree contents. It reads commit trees, never worktree diffs or issue labels.
Every PR is treated as untrusted; fork origin is reported UNKNOWN. No credentials,
network operation, privileged path, test executor or arbitrary command option is
provided. Replacement objects, ambient Git overrides, external diff/textconv,
hooks, fsmonitor, global/system configuration and transport protocols are disabled
for the collector's child commands. This changes no host or repository settings.

There is no caller `complete` flag. Completeness is set only after successful Git
exit, bounded parsing and the final identity checks. Git diagnostics, including
warnings with zero exit, fail acquisition. Commands have a five-second bound;
stdout is bounded (raw diff at 1 MiB), diagnostics at 4096 bytes, changes at 4096,
and commit metadata at 64 KiB. Overflow, timeout, corruption, shallow/missing
history, unsupported encoding, ref changes or malformed records retain full
selection and explicit error codes. The Linux pidfd/`waitid(WNOWAIT)` mechanism observes leader exit without reaping
it. On every outcome, including exit0/exit1 with closed-stream descendants, the
owned group is signalled before the leader identity can be reused, then reaped and
checked absent. Cleanup is bounded separately and reported as a secondary cause
without replacing an initiating command failure. Unconfirmed retirement fails
closed and prevents subsequent acquisition. Orphans that another owner has not
reaped remain unconfirmed; no escaped-session or arbitrary kernel-I/O preemption
claim follows. Unsupported containment platforms fail closed. Raw stderr, commit
messages and environment are never emitted.

Rename pairs, deletions, mode changes and gitlinks are retained in the acquisition
manifest. Exact-content renames use Git's 100% similarity detection; other moves
may appear as addition/deletion. Both forms retain full selection. Submodule
changes are unqualified. Raw UTF-8 filenames, including tabs/newlines, survive
JSON escaping; unsupported policy paths still retain full selection. Empty diffs
are complete acquisitions but do not authorize omission.

The optional pair `--inventory PATH --inventory-sha256 SHA256` permits diagnostic
source/target/test impact reporting from a pinned static inventory. The isolated
`ci_shadow_inventory.py` worker rejects nonregular paths before open, uses
nonblocking/no-follow open flags, validates the actual descriptor, bounds bytes
and compares descriptor/path identity and metadata before/after reading. FIFO,
symlink, replacement, size and read failures retain full fallback. The parent
bounds worker lifetime/output independently of synchronous filesystem calls and
records `inventory_acquisition`; timeout requires owned worker cleanup. If the OS
cannot complete retirement within that bound it is explicitly unconfirmed, never
reported as successful arbitrary I/O preemption. Byte integrity
is not authenticity or dependency completeness. The accepted inventory schema is
`ci110-inventory-v1` with scope `reused107`; synthetic/unknown inventories are
rejected. Even a caller marking closure complete cannot reduce the matrix.
Omitted, corrupt, oversized or stale inventories retain full profile identities
without fabricating unavailable current test registrations. The policy is adapted
from the accepted inert prototype; current generated/header/runtime closure and
representative labelled failure outcomes are still missing.

Diagnostic target reachability uses a reverse-dependency index and a worklist:
each target is discovered at most once and each dependency edge is indexed and
visited a bounded number of times, rather than repeatedly rescanning the graph.
Memory is proportional to the supplied targets/edges; deterministic reason/output
sorting is additional work proportional to the reported data. For a target with
no direct changed source, reasons list **all reached immediate dependencies**;
direct source reasons take precedence. Cycles terminate without recursion and
unreachable cycles are not impacted. Renaming/reordering targets cannot alter
logical reachability. This is bounded-input diagnostic computation, not an
end-to-end wall-clock guarantee, selection qualification or CI speedup claim.

Stdout is canonical JSON (`mantis-git-shadow-v1`). Zero exit means a manifest was
emitted, including fail-closed manifests; it never means qualification. Unsupported events produce a full fallback; argument
usage errors return nonzero. The `acquisition` object holds checked identities,
raw diff digest and records/completeness/errors. The `decision` object records
known target/test impact, suite decisions, provenance/errors and unconditional
full-gate obligations. No timestamp, absolute repository path or raw diagnostics
are embedded. Source evidence must be reviewed before public artifact upload.

## Disposable regression fixtures

```sh
python3 -B tests/ci/shadow_collection_test.py
python3 -B tests/ci/shadow_resource_test.py
```

The Git/resource controls use Python's standard library and small independent
local Git repositories. They cover actual commit/ref/merge relationships, complete source
changes and transitive consumer oracles, PR checkout mappings, shallow/missing and
corrupt objects/refs, ambient overrides/replacement objects, rename/deletion/type
changes/gitlinks, raw paths, empty diffs, resource/error controls and CLI behavior.
Graph controls exercise equivalent favorable/unfavorable chain renamings through
the actual CLI, count dependency visits independently of elapsed time, and verify
reachable/unreachable cycles, complete reasons and invalid/stale input recovery.
Resource controls use gated owned Python descendants and real FIFO/symlink/
descriptor races. Probe owners adopt/reap only their own descendants, including
on assertion failure. The tiny setup helper also scopes Linux subreaper ownership
and TERM/INT cancellation handlers to each owned command and restores both. It
observes leader exit without reaping, retires the owned process group before
releasing its identity, and reaps adopted descendants even when a successful or
failed leader leaves inherited streams open or closed. Bounded cleanup reuses
the collector's group-retirement primitive; this is not escaped-session or
hostile-process containment. Setup receipts retain the initiating exception,
partial bounded stdout/stderr, exit status, elapsed/budget and separate cleanup
errors on success, nonzero, timeout and cancellation. Receipt failure cannot
replace an initiating command error.

The tiny helper's TERM/INT handlers record cancellation without raising inside
spawn/ownership publication or destructive-read/buffer transactions. Explicit
safe points observe it only after the handle or bytes have been retained; a
50 ms maximum selector wait bounds observation of otherwise idle cancellation
(Python can restart interrupted selectors). This does not extend the campaign
or cleanup deadline. The handler remains non-raising through retirement, draining,
receipt publication and secondary-error accounting; caller dispositions are restored
in an outer `finally` only afterward. Late cancellation is retained as the outcome
on success or a note on an established command/timeout error. If the first signal
arrives after receipt serialization, exactly one `publication-cancellation` JSONL
amendment records the final error/signal for the immediately preceding command;
it is not another command receipt or a retry. Receipt/amendment I/O errors remain
secondary to an established primary. This scopes finalization, not cancellation
immunity after return. Real TERM/INT
boundary regressions handshake descendant entry and inject at successful spawn
return before handle assignment and after stdout/stderr consumption before
buffering. Independent wait/group oracles and exact output markers detect both
ownership and diagnostic loss, including safe unwind of rejected implementations.
Final-publication controls additionally deliver real TERM/INT/repeated signals at
receipt open, after real serialization, at stream close and during error accounting,
with success/nonzero/timeout and real receipt-open failure controls. They assert
primary/output retention, bounded receipt/amendment evidence, independent leader/
group retirement and restored caller dispositions/subreaper state.

Configured diagnostic controls additionally configure and serially compile an owned tiny
multi-target fixture with Ninja and Ninja Multi-Config, and obtain real CTest JSON
with `--show-only=json-v1`. They never execute fixture test commands or product
targets. CMake, CTest, Ninja and a C++ compiler are required for this extension.
No installation or external network is used. Fixture-local commits/config/refs are disposable and never affect the project
repository. The same modules are registered as `ci-shadow-collection` and
`ci-shadow-resources` in Linux CTest configurations with `BUILD_TESTING=ON`,
including Studio ON/OFF and sanitizers. They use the configured Python interpreter
and retain the unchanged outer CTest30. Both configured fixture producers share
one 12-second setup campaign deadline (including intervening input pinning),
with at most two additional seconds for owned-group cleanup on failure, rather
than resetting a 30-second timer for every setup command. The collector and reader
modules acquire no processes, network connections or inventory data on import;
regressions audit import and ensure uncertain inventory cleanup blocks further
Git acquisition. These regressions do not replace any applicable native final check.

For a local grant requiring BUILD to retire before an active CTest campaign, the
same test-only helper can hoist **exactly the existing two** tiny producer setups:
`python3 -B tests/ci/shadow_configured_fixture.py --prepare OWNED_FIXTURE_ROOT`.
Its private receipt pins both original manifests, actual producer/fixture source,
interpreter and aggregate setup cost under the unchanged single setup12 budget;
normal command receipts retain actual compilation and metadata-query retirement.
Supply the completed receipt's digest as `MANTIS_SHADOW_PREPARED_SHA256` and the
unchanged original root as `MANTIS_SHADOW_PREPARED_FIXTURE` to the existing suite.
The suite rejects a changed receipt, source, interpreter, relocated manifest or
changed original input bytes and **never rebuilds/falls back** under the active
CTest parent. All real diagnostic/collector/replay assertions still execute;
changed graph/PR/policy-rejected inputs still acquire fresh collector evidence.
The outer CTest30 remains unchanged, and the separately measured setup12 cost is
not hidden or reset. Default CI still creates precisely these two fixtures when
no prepared root is supplied. This is only test setup sequencing, not a production
configured-input producer framework, cached validator or extra test fixture.

## Configured dependency diagnostics (opt-in, record-only)

The existing invocation accepts an additional **pair**:

```sh
python3 -B tools/ci_shadow.py --repository . --event push \
  --ref refs/heads/develop --base BEFORE_SHA1 --head PUSH_SHA1 \
  --configured-inputs PRIVATE_INPUT_MANIFEST.json \
  --configured-inputs-sha256 MANIFEST_SHA256
```

Without this option the `mantis-git-shadow-v1` envelope and static-inventory
behavior are unchanged. With it the envelope is explicitly
`mantis-git-shadow-v2`, containing a `configured_diagnostic` with schema
`ci110-configured-diagnostic-v1`. Consumers accepting only v1 must reject v2,
not reinterpret it as permission to select tests. The legacy `decision` remains
unchanged and FULL remains unconditional, even on a successful ingestion. The
configured path uses the existing policy's reachability routine, not another
selection engine. The static `--inventory` remains independent and optional.

### Pinned producer inputs

The importer **does not configure, compile, run CTest or execute any command**
from metadata. A separate caller-owned producer must create a CMake File API
query for `codemodel-v2`, `cache-v2` and `cmakeFiles-v1`, configure its own build
root, and save `ctest --test-dir BUILD -C CONFIG --show-only=json-v1` as a regular
JSON file. Native test binaries need not exist: missing CTest command expansion
is an explicit unknown, not a reason to build a product or fabricate a command.
Pin the exact chosen index rather than discovering a supposedly latest reply.
A private manifest contains these exact fields:

- `schema`: `ci110-configured-inputs-v1`.
- `source`: `{ "commit": SHA1, "tree": SHA1 }`, matching the collector's checked
  **tested** source, including the merge identity on PRs, not the PR head alone.
- `roots`: `source` and `build`, canonical absolute local directory paths. These
  are private inputs; output uses root digests and root-relative paths only.
- `configuration`: exact nonempty codemodel configuration name, e.g. `Debug`.
- `generator`: exact index generator name, e.g. `Ninja` or `Ninja Multi-Config`.
- `index`: build-relative pinned `.cmake/api/v1/reply/index-….json` path.
- `ctest`: build-relative saved CTest JSON path.
- `files`: unique records `{ "root": "source" | "build", "path": RELATIVE_PATH,
  "sha256": SHA256 }`. Include the index, every referenced codemodel/cache/
  cmakeFiles/target reply, CTest JSON, relevant configure inputs and source/header/
  Protobuf/QML metadata. Pinning `CMakeCache.txt` also binds its raw bytes. All
  listed files are read and digest-checked; unlisted paths are **never opened**.

All supplied files must be stable regular files. Symlink leaves/ancestry,
absolute or traversing relative record paths, duplicate JSON keys/records,
truncation, invalid/nonfinite JSON, unsupported document major versions,
dangling reply references, duplicate target/test identities, wrong metadata
roots/configuration/generator/source and changed byte pins fail closed. Missing
source metadata remains explicitly unresolved. Limits are 2,000,000 bytes per
file/manifest, 16,000,000 aggregate input-file bytes, 8192 files and 16384 records
per collection and edges/unknowns per emitted category. The isolated reader has
the existing five-second command bound and 2,000,000-byte output bound, with owned
group retirement. No synchronous Python filesystem call claims kernel-I/O
preemption. Imports remain inert.

The manifest digest binds the exact raw bytes, including private local root
identities; it is not a signature or authentication. File hashes bind the bytes
actually consumed. Caller-reported source/configuration context is not proof
that a particular CMake/CTest invocation generated those bytes, or that dirty
source matches a Git tree. In particular CTest JSON has no authenticated source
or configuration binding. These limits remain explicit unknowns even after
successful ingestion. Freeze raw manifests, producer commands and full outputs
privately; inspect normalized output before public upload. Raw command strings,
external absolute paths, environment, cache contents and parser exceptions are
not emitted. Command values are represented only by a byte digest.

### One generated C++ Protobuf edge (explicit opt-in)

For a clean, caller-owned Studio OFF Debug configuration with single-config
Ninja, acquire the following before building, from the same tree as the pinned
File API replies. Bound the queries, check success and verify that `build.ninja`
and `CMakeFiles/rules.ninja` are byte-identical before and after capture.

```sh
ninja -C "$BUILD" -t query mantis.pb.cc
ninja -C "$BUILD" -t query mantis.pb.h
```

Keep the exact stdout separately from stderr. Use the existing manifest fields
with `schema: ci110-configured-inputs-v2` and pin the two Ninja files, the input
`protocol/protobuf/mantis.proto`, and the build-relative fixed-name receipt
`mantis-protobuf-ninja-query.json` in `files`. The receipt has exactly:

- `schema`: `ci110-mantis-protobuf-ninja-query-v1`.
- `source`, `roots`, `configuration`, `generator`: the same values as the
  manifest. The index and File API cache must also agree on the relevant
  configuration, generator, roots and Studio mode.
- `sha256`: raw-byte digests keyed by `index`, `cache`, `owner` (the selected
  `mantis-wire` codemodel target reply), `build.ninja`, `CMakeFiles/rules.ninja`,
  and `protocol/protobuf/mantis.proto`. These match the already pinned bytes.
- `queries`: `mantis.pb.cc` and `mantis.pb.h`, each holding its complete query
  stdout (including its last newline). The receipt is limited to 32 KiB and each
  query to 8192 characters within the existing inventory bounds.

Each query must show the exact output of a `CUSTOM_COMMAND`, the exact absolute
schema path as an **explicit** input, and the observed Ninja order dependency
for `mantis-wire`. The codemodel must map each exact build output uniquely to
that target as generated, with the `.cc` in a C++ compile group. Neither matching
basenames nor having `.pb.*` in a target's sources is sufficient. The importer
only compares pinned bytes and recognizes this bounded query shape; it does not
parse Ninja build files or shell commands, run Ninja, or open any path named by
the receipt or query.

Only this match adds the observed `ninja-query-protobuf-cxx` edge from the schema
to `mantis-wire`. The existing traversal follows configured reverse dependencies
(including `mantis-protocol`) and existing CTest artifact matches. Missing,
stale, ambiguous, unsupported or mismapped evidence adds an explicit unknown
instead of this edge; invalid outer pins still reject ingestion. V1 inputs do
not interpret the receipt. The diagnostic and corpus envelopes are unchanged.
The two prepared fixture configurations still share the same setup bound; the
tiny single-config fixture has a stand-in generator and a real CTest consumer,
not a test of `protoc`. It verifies the proto-only change, an unrelated consumer,
loss of the query's input edge after repinning, and exact restoration.

The receipt is a consistency check for an owned capture, not authentication of
the query producer or attestation of a build.

### One generated Python Protobuf edge (separate opt-in)

In the same Studio OFF, Debug, single-config Ninja lane, use the same manifest
fields with `schema: ci110-configured-inputs-v3`. This input version leaves v1/v2
diagnostics and saved corpus replay unchanged. V3 uses the same C++ receipt and
validation as v2, independently of Python. To supply Python evidence, run the
following bounded query before building, with the same stdout/stderr and graph
byte checks described above:

```sh
ninja -C "$BUILD" -t query python/mantis/mantis_pb2.py
```

Pin `mantis-protobuf-python-ninja-query.json` at the build root in `files` in
addition to the graph, proto, and File API pins. This receipt uses the C++
receipt's fields and bounds, except that `schema` is
`ci110-mantis-protobuf-python-ninja-query-v1`, `query` is the single complete
stdout string (instead of `queries`), and `sha256.owner` pins the `mantis-python`
target reply. `index`, `cache`, both Ninja files, the exact proto bytes and the
source/tree, roots, configuration and generator must still agree.

Admission requires the exact `CUSTOM_COMMAND` output, the absolute proto path
as an explicit input, and the observed `mantis-python` consumer and relative
and absolute `CMakeFiles/mantis-python` nodes. The selected File API target
must be a `UTILITY` without compile groups or artifacts, uniquely listing the
exact generated `CMakeFiles/mantis-python.rule` and
`python/mantis/mantis_pb2.py.rule` paths. No Python output file needs to exist.
The receipt never names paths to open or commands to execute.

Only that match adds the observed `ninja-query-protobuf-python` edge to
`mantis-python`. Reachability follows the existing codemodel links, including
`mantis-alg03-ply-probe-daemon` in the checked Studio OFF graph; it is not inferred
from Ninja consumer names, Python imports or script arguments. Missing,
unsupported, stale, ambiguous or mismapped Python evidence yields an explicit
Python unknown without removing a valid C++ edge. Invalid outer file pins still
reject ingestion. The tiny fixture maps `python-generator-contract` through a
configured dependency and leaves `peer-contract` untouched. It checks actual
Ninja input removal, wrong File API association, stale/crossed pins, and exact
restoration. It does not run these fixture executables or test Python imports.

Other generated sources, Python runtime dependencies and overall closure
remain unqualified. The diagnostic and corpus envelopes are unchanged. FULL,
`execution_authorized=false` and `closure_complete=false` remain binding.

### One direct ALG03 enclosure script edge (separate opt-in)

Use the same manifest fields with `schema: ci110-configured-inputs-v4` for this
observation. V4 retains both independent Protobuf checks from v3. Keep the
exact File API index, cache, codemodel and target replies, CTest show-only JSON
and `tests/unit/alg03_enclosure.py` pinned from the clean tested source. Also
pin `CMakeFiles/<index CMake version>/CMakeSystem.cmake` under `build`. The
importer recognizes the generated native Linux host/target record without
evaluating CMake. Only a Studio OFF Debug single-config Ninja configuration
with matching cache roots is supported.

Only the registration named `alg03-enclosure` is eligible. Its entire argv
must be the cache's absolute configured interpreter (`_Python3_EXECUTABLE`,
which must agree with `Python3_EXECUTABLE` when present), `-B`, the exact
absolute source-root script, and the exact absolute build-root artifact of the
configured `mantis-alg03-numeric-tests` executable. That target must have one
unambiguously owned artifact; neither the interpreter nor the artifact is
resolved through the filesystem on the basis of argv. The artifact need not
exist for show-only to expand this registration. No basename, generic Python,
shell, extra-argument or import matching is performed.

V4 reports `file_test_edges` (empty or one `observed` edge with reason
`ctest-alg03-enclosure-argv-match`). The existing closure reads it as a direct
file-to-test association: a script-only diff impacts `alg03-enclosure`, not
`alg03-numeric` or another consumer. Missing, ambiguous or unsupported scoped
evidence leaves an ALG03 unknown and no direct edge; invalid outer pins or
malformed core metadata still reject ingestion. A missing optional script or
system-file pin is an unknown; a supplied stale pin is a rejection. V1–v3
retain their prior output bytes on identical inputs; the shadow and corpus
envelopes are unchanged. With the same tiny fixture, a same-basename decoy
causes a diagnostic and corpus miss; exact restoration reproduces both outputs.
The fixture also checks wrong argv/artifact/context and invalid pins. This
observation does not validate runtime dependencies or test
execution. FULL, `execution_authorized=false` and `closure_complete=false`
remain unconditional.

### One configured Python calibration COPYONLY edge (separate opt-in)

Use the same manifest fields with `schema: ci110-configured-inputs-v5`. V5
retains the v4 ALG03 edge and both independent Protobuf checks. The new edge
requires native Linux x86_64, Studio OFF, testing ON and Debug single-config
Ninja, with the same pinned system record, File API index/cache/roots and
CTest show-only JSON as above. Pin these additional files:

| Root | Paths |
| --- | --- |
| Source | `CMakeLists.txt`, `tests/unit/python_calibration.py`, `sdk/python/mantis/calibration.py`, `sdk/python/mantis/__init__.py` |
| Build | `python/mantis/calibration.py`, `python/mantis/__init__.py`, `mantis-python-calibration-copy-trace.json` |

The single `python-calibration` registration must have exactly the configured
interpreter, the absolute source-root test script and the absolute build root
as argv, with that same working directory and no CTest environment override.
Its backtrace must match the scoped top-level `add_test` declaration. The
importer also checks `cmakeFiles` lists each source and that each pinned source
matches its exact `python/mantis/` copy. Neither a listed input, a literal
statement in an inactive branch, nor equal bytes establish who produced a copy.

Capture the successful, fresh configure with `--trace-expand
--trace-format=json-v1 --trace-redirect=<private-trace-path>` (no
`--trace-source` filter), and collect the File API and CTest show-only data from
that same build. Keep the full trace private and outside the manifest. Pin a
receipt of at most 32,768 bytes using these exact fields:

- `schema`: `ci110-mantis-python-calibration-copy-trace-v3`. Earlier draft
  projections omitted possible writers and are unsupported.
- `source`, `roots`, `configuration`, `generator`: as in the input manifest.
- `sha256`: digests keyed by `index`, `cache` (File API cache reply), `ctest`,
  and each source/copy path in the table, excluding the receipt itself.
- `commands`: the trace records relevant to either copy route, retaining only
  `cmd`, `file`, `line`, `frame`, `global_frame`, and expanded `args`. Start
  from a positive scope: known metadata operations and traced CMake calls,
  reviewed native operations outside the package, and the recognized stock
  toolchain probes from the installed CMake modules before the first package
  copy. The probes assume a trusted native toolchain and a fresh owned build;
  the producer does not qualify configured compiler launchers or toolchain
  files. It uses the CMake
  module root reported by the File API and cache; it does not resolve
  directory-copy mappings, read paths from trace arguments, or interpret
  subprocess code. Retain unclassified operations and copy-command overrides,
  even when they name neither Python file. For example, a native directory
  copy with `DESTINATION python` or a project `execute_process` that copies the
  directory can overwrite the package. Retain the competing record or omit
  the receipt if the trace or projection cannot be represented within its
  bounds; never truncate the projection to just the expected calls. New
  bootstrap operations can remain unresolved until inspected.

The importer accepts exactly two records, one active top-level `configure_file`
per pair, with both frame depths equal to 1, the exact source recipe line,
the source-relative input, the exact absolute destination and `COPYONLY`. A
missing, repeated, redirected or competing invocation is not qualified, even
if the final file bytes match. This is caller-captured evidence about these
two configure-time routes, not an authenticated trace or a general filesystem
provenance check.

Matching copies alone are not import evidence. The importer recognizes the
reviewed full-file digests of the test script and package initializer: the
script puts `argv[1]/python` first on `sys.path`, imports `mantis` and calls
`mantis.Calibration`; the initializer re-exports `Calibration` from
`.calibration`. Changes to either import-bearing file require reinspection
before updating its recognized digest. The calibration module itself has no
fixed digest: its caller-pinned source and copy must match. This rule uses only
already pinned bytes, never paths to open from argv, Python execution or a
general CMake evaluator or import graph.

The result is at most one new `file_test_edges` entry, from
`sdk/python/mantis/calibration.py` to `python-calibration`, with evidence
`conservative-inference` and reason `ctest-python-calibration-copyonly-import-match`.
It records a direct association from configured metadata and inspected source,
not a runtime import observation or evidence about any other consumer. Missing,
ambiguous or unsupported scoped evidence produces a `python-calibration-*`
unknown and no new edge. Invalid outer pins, including stale supplied copies,
still reject ingestion. V1–v4 diagnostics and saved corpus replay keep the
same bytes on identical inputs. No new output envelope, runner, execution
authority, dependency completeness or selection change is introduced.

### Edge and uncertainty semantics

- `observed`: codemodel-listed file-to-target and dependency-to-consumer edges,
  plus the opt-in generated Protobuf edges and direct ALG03 script edge above,
  with exact pinned document/target identity. These are **listed observations**,
  not evidence that CMake listed every semantic dependency.
- `conservative-inference`: the direct Python calibration association above,
  literal quoted include-to-target edges (with the
  including source in the reason), and target-to-test edges inferred by matching
  CTest's first command argument to a configured artifact. Include traversal is
  iterative and uses only pinned source bytes and listed include directories.
  It is not a preprocessor: conditionals/comments may overapproximate; macros,
  system/generated/unresolved includes stay unknown. Shared headers flow to
  downstream target consumers using the existing policy's worklist.
- `unresolved`: generated sources/configure inputs, object-library artifacts,
  Protobuf/QML/resource generation, fixtures/runtime dependencies, external/unpinned sources/includes,
  unsupported languages, missing/unmapped/ambiguous commands and configuration
  coverage remain explicit. Cycles terminate and are reported; absent or omitted
  dependency edges never establish no consumers. Unknown changed paths also
  remain unresolved. The selected configuration is imported; other configurations
  are not unioned or silently claimed qualified.

`status=ingested` means the bounded records were parsed, **not** complete product
closure, historical recall, provider-event authentication, signatures, OS
isolation, runtime coverage or performance qualification. `closure_complete` and
`execution_authorized` are always false; `full_matrix_would_select` is always true.
Every existing required local and remote gate remains binding. No workflow,
ruleset, scheduler, selection activation or skipped-test policy is changed.

## Pinned impact corpus replay (opt-in, record-only)

```sh
python3 -B tools/ci_shadow_corpus.py --corpus PRIVATE_CORPUS.json \
  --corpus-sha256 CORPUS_SHA256
```

This companion consumes **saved** `mantis-git-shadow-v2` CLI manifests and the
original pinned configured-input closure. It does not acquire Git history,
configure/build anything, execute imported commands, run tests, fetch artifacts
or contact a network. The existing collector CLI and schemas are unchanged.
The replay explicitly rejects v1/unknown envelopes, incomplete acquisition and
unsupported comparison/observation schemas. As with the collector, exit zero
means bounded JSON evidence was emitted, **not** that inputs were admitted or
any test/qualification passed. Usage errors are nonzero. Inspect `status` and
`errors`; a rejected corpus emits **no partial cases**.

### Caller-pinned corpus and observations

`ci110-impact-corpus-v1` has exactly `schema` and `cases`. Each case has exactly:

- `id`: unique bounded label.
- `context`: the exact fields `event`, `ref`, `identities`, `tested_commit`,
  `configuration`, `generator`, `configured_manifest_sha256` and
  `test_inventory_sha256`. Copy acquisition event/ref/identities/tested commit
  from the saved manifest. Identities include commit, tree and ordered parents;
  a PR must pin base, branch head **and the actual two-parent tested merge**.
  Pushes pin base and head. Configuration/generator must match the imported
  codemodel. Pin the raw configured manifest digest and SHA256 of canonical
  `ci_shadow_policy.canonical(configured_diagnostic['tests'])` bytes.
- `shadow`, `configured_inputs`, `observation`: each exactly
  `{ "path": RELATIVE_FILE, "sha256": SHA256 }`, relative to the corpus directory.
  The configured input manifest and its roots/files retain the preceding schema.
  Supply the exact original regular files, not a newly discovered latest reply.

`ci110-impact-corpus-v2` is an explicit opt-in extension with the same top-level
fields. Every case additionally **must** contain `static_inventory`: either
`null` for the normal absent optional input, or exactly
`{ "path": RELATIVE_FILE, "sha256": SHA256 }` pinning the **original raw bytes**
read by the collector's `--inventory`. References use the same corpus-relative
safe-path rules, per-file/aggregate byte and file bounds, Snapshot identity/digest
checks and final whole-input recheck as the other evidence. Missing, extra,
unpinned, contradictory or unsafe references reject the entire corpus. The
independent digest must equal both original inventory digests in the regenerated
decision; it is never inferred from a saved projection. V1 cases cannot contain
this field, even when null; neither version implicitly upgrades the other.

`ci110-impact-observation-v1` has exactly `schema`, `context`, `kind`, `origin`,
`trust`, `test_ids`, `basis`. `context` must equal the case's fully pinned
context. `test_ids` is a unique list of registered identifiers or `null` for
unknown; no executable commands are supplied here. `origin` and `basis` are
bounded labels for the caller's evidence/adjudication reference, not executable
instructions or a claimed verified signature. Retain supporting evidence with
the caller's private archive. `trust` must be `caller-asserted-unverified`.

The four `kind` labels are deliberately separate:

- `controlled-fixture`: independently specified controlled consumer expectation.
- `causally-adjudicated`: caller-reported causal adjudication, **not authenticated
  or independently adjudicated by this tool**.
- `caller-assertion`: an assertion without causal qualification.
- `historical-unadjudicated`: observed failed tests without an established causal
  relation to this change. A failed CI job alone is not causal ground truth.

All labels remain caller assertions. An available historical case requires the
actual pinned public source/base/head/tested-merge identities and original
configured/observation evidence closure; do not synthesize missing history from
a later green run. This slice supplies controlled fixtures, **not** a historical
product corpus. Historical recall, dependency completeness and product closure
remain unqualified when that original context is unavailable.

### Comparison and fail-closed limits

Replay revalidates the complete supported v2 envelope, inventory-acquisition
receipt and legacy decision, not just its authority flags. It reconstructs the
collector's canonical request from the validated acquisition (base/tested commit
and tree, event/ref, raw diff records/count), then calls the unchanged policy
and compares the **entire** canonical decision, including its request digest,
source, diff, nested suite/test decisions, obligations and limits. Missing,
extra, truncated, contradictory or unsupported nested fields reject the whole
corpus. Canonical comparison distinguishes JSON booleans from integers. Empty
diffs retain the collector's expected diagnostic error; they are not safe omission.

Corpus v1 supports the collector's normal **absent optional static
inventory** (`inventory_acquisition.status=not-requested`, null error, no cleanup
errors). It does not demand an inventory, trusted signatures or complete closure.
A supplied/failed static-inventory acquisition is explicitly rejected with
`corpus-static-inventory-evidence-unsupported`: the corpus schema has no reference
to pin/re-read its original static inventory, so saved static projections cannot
be verified. The original collector's optional `--inventory` CLI is unchanged.
That legacy behavior/output remains unchanged. Corpus v2 can replay a supplied
static inventory only with the explicit original-byte reference and the exact
successful receipt (`status=regular-file`, null primary error, empty cleanup
errors). Explicit null requires the exact not-requested receipt. Failed reads,
timeouts, unconfirmed cleanup and originally unpinned reads remain unsupported;
replay never synthesizes old acquisition outcomes. A different digest, receipt,
saved inventory/source/request projection or tested merge is not repaired by
repinning the corpus or its saved envelope. The unchanged policy receives the
actual original bytes and independent reference digest and the entire canonical
decision must match. The configured-input inventory remains required and
independently replayed for both versions.

Successful reading is **not** successful policy validation. V2 also supports
originally read bytes rejected by the policy (including malformed JSON or a
stale inventory base): only their exact regenerated fail-closed decision is
replayable. `status=compared` means consistency, not that the inventory was valid,
complete or authentic. No policy error is removed or relabelled successful.

Replay also revalidates the raw diff encoding/digest/count, tested source context,
configured input closure and entire canonical saved diagnostic against the unchanged
`ci_shadow_configured.diagnose/project` and `ci_shadow_policy.closure` path.
It never trusts serialized impacted IDs as the answer. Byte/context consistency
is not proof of producer authenticity, Git-object existence during replay, dirty
source equivalence, CTest execution or completeness of the supplied evidence.
The independent expectation must not be derived from the diagnostic being tested.

`ci110-impact-comparison-v1` reports sorted per-case diagnostic `hits`, `misses`,
`denominator` and `state` (`HIT`, `MISS` or `UNKNOWN`), separately from
`full_would_select_test_ids` and `full_observation_coverage`. FULL is always
counterfactual selection, **not executed coverage**. A diagnostic MISS remains a
MISS even when unconditional FULL would cover the identifier. Nonempty fixture
and caller-adjudicated expectations alone have a reported `causal_denominator`;
caller assertions and historical observations never acquire one. Empty and
unknown expectations have null denominators and UNKNOWN state, never 100%.
Group hit/miss counts are partitioned by kind; no pooled or historical causal
recall, precision, safe-omission or speedup percentage is claimed. All group
`causal_recall` values and `historical_causal_recall` remain UNKNOWN.

Successful v2 replay emits `ci110-impact-comparison-v2`, adding per-case
`evidence_sha256.static_inventory` (the raw-byte pin or null) and
`static_inventory_replay` (null when absent, otherwise exactly `decision_sha256`
and `decision_errors`). The digest binds the **entire canonical saved/regenerated
decision**, while the fixed policy error codes keep its exact failure visible.
Caller-authored static inventory origin/provenance strings are not re-exported;
retain the original collector bytes privately for full inspection. The configured
diagnostic hit/MISS oracle and all denominator/trust semantics are unchanged,
even if the static policy lists different consumers. Rejections of either corpus
version retain the existing bounded `ci110-impact-comparison-v1` error envelope,
with no partial cases or new authority.

Bounds are eight cases, 8192 distinct files, 2,000,000 bytes per file, 16,000,000
aggregate bytes across the entire corpus/manifest/observation/configured closure,
and 2,000,000 output bytes. The existing owned worker enforces a five-second
command deadline and bounded cleanup/output. Symlink ancestry/leaves, unsafe or
traversing references, nonregular/missing files, duplicate keys/cases/tests,
unsupported versions, wrong source/configuration/test identities, stale saved
projections, mutated evidence and oversized/truncated input reject the whole
corpus. A final whole-input identity and digest recheck detects cooperative
mutation; this is **not** OS-enforced read-only isolation or hostile-host security.
Only canonical results and fixed error codes are emitted, never raw paths,
commands, cache contents, parser exceptions or logs. Screen outputs before
publication.

The existing collection/resource Python entrypoints cover real Git/CMake/CTest
metadata, independently named consumer hits, dropped-edge MISS despite FULL,
unrelated-consumer assertions, PR tested-merge mismatch, all observation kinds,
empty/unknown/invalid evidence, deterministic multi-case ordering, whole-input
mutation and read-only/inert-command/import behavior. Both existing tiny
producers finish together within the unchanged shared setup12 budget before
replay cases; outer CTest30 and all prior assertions remain unchanged. The twelve
kind/expectation combinations are batched as four independently pinned cases per
real supervised CLI invocation, sharing one immutable closure instead of
relaunching the same replay twelve times. Only the two pristine real collector
envelopes may be reused after exact command/configured-closure/static-byte
identity checks; changed graph, PR and policy-rejected inputs still acquire
fresh collector evidence, and all comparison/restoration cases use the real CLI.
Static controls reuse these same two producers and existing PR, dropped-edge,
read-only and mutation routes, not additional builds or CTest families. Exhaustive
parser variations use the actual worker function, while representative valid,
invalid, nonregular, stale and restoration cases run the real supervised CLI.
For corpus comparisons the single-threaded Linux test parent forks only the
existing disposable subreaper fixture owner, avoiding a fresh interpreter import
of the entire test module per replay. The owner still execs the real corpus CLI,
which still execs its isolated supervised worker; no inherited Python validator
or cached result substitutes for either process. The inner10/outer15 deadlines,
primary stdout/stderr/exit, child-reaping checks and all comparison assertions
remain. A failed real command with an acknowledged closed-stream child checks
actual group/child disappearance and unchanged parent subreaper state. The
original exec-based graph-renaming probes and resource/F4 helpers are unchanged.
No additional CTest family, workflow, filtering, local-final-gate change or
product/native execution is introduced. `execution_authorized=false`,
`closure_complete=false` and unconditional FULL remain binding.

## Integration boundary

Architecture's existing five jobs now invoke `tools/ci_shadow_observation.py`
(record-only, static invocation with no inventory) after their existing gates and
cache actions. The final existing cache summary still runs under its original
condition. Observation runs with `always()`; its failure is a real step failure,
not `continue-on-error`, and cannot prevent earlier full validation from running.
Only a successful observation step uploads the exact newly created
`build/ci-shadow/observation.json`, with missing-file failure and 14-day retention.
No collector output is used as `if`, paths filters, job matrices, test expressions,
admission criteria or aggregation gates. Required names, permissions, fork trust,
checkout, full tests, sanitizer controls and UI repetitions are unchanged.

Event values are supplied through environment variables, never interpolated into
shell code. `ci110-workflow-observation-v1` keeps an explicitly **asserted** event
context (base, PR head, tested merge/push SHA, run/attempt/job/profile) separate from
locally checked commit/tree/parent identities and acquisition completeness. A PR's
`GITHUB_SHA` is the tested merge, never substituted for missing PR head evidence.
Push head is `GITHUB_SHA`; its base is the supplied event `before`, not an invented
parent. Manual dispatch is recorded as unsupported acquisition/FULL. Local checks
are not provider authentication or dirty-worktree attestation. The actual three
static implementation files are separately SHA256-bound. Ref text is represented
only by a digest; untrusted branch names are not exported.

The public document is a closed typed projection: checked identities, diff
completeness/digest/size/count, allowlisted fixed errors, and the existing static
FULL suite decisions/explicit unknowns. It contains no changed paths, raw diffs,
Git version/diagnostics, inventory/provenance strings, configured manifests/cache,
commands, environment or private paths/logs. The entire canonical document is
screened before exclusive file creation (32 KiB maximum); unexpected diagnostics,
invalid context, stale files or publication errors reject publication with a fixed
nonzero diagnostic and no raw exception. No raw collector JSON is logged/uploaded.
The upload condition tests publication success, **not** a classification decision.

Shallow checkout still yields explicit `shallow-repository`/FULL, empty checked
identities and null acquired tested commit; asserted SHA context is retained but
never relabelled checked. No fetch/depth/acquisition policy changes are made.
Absent static inventory remains an error/unknown, not complete dependency closure.
This is a diagnostic/decision **observation**, not evidence of producer authenticity,
recall, executed coverage, dependency completeness, safe omission, OS isolation,
speedup or whole-Issue110 qualification. `execution_authorized=false`,
`closure_complete=false` and FULL remain unconditional.

Run the disposable regression commands as additional source checks when changing
this interface; workflow contracts live in `compiler_cache_test.py`'s existing
`WorkflowPolicy` class. Applicable actual full local qualification and exact
PR/push CI remain required before publication/acceptance. Historical precision/
recall require labelled actual outcomes; all-green jobs and seeded oracles cannot
supply them.
