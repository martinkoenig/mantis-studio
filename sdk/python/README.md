# Mantis Studio Python Client SDK (internal/provisional)

Pure-Python local control/calibration client; no C++ runtime bindings or embedded
daemon. Distribution `mantis-studio-client` and import `mantis`, version `0.1.0`,
remain provisional. This is not an authorized PyPI release or cleared branding.

From the repository root, use a Python >=3.10 venv with `build==1.2.2.post1`:

```sh
SOURCE_DATE_EPOCH=1700000000 python -m build --outdir dist sdk/python
python -m pip install dist/mantis_studio_client-0.1.0-py3-none-any.whl
```

PEP 517 isolation installs exact build tools from `pyproject.toml`. The backend
uses compiler distribution `protoc-wheel-0==21.11` (not PATH), whose binary
reports exactly `libprotoc 3.21.11`, with build protobuf 3.20.0.
This compiler emits builder-style code compatible with the advertised runtime
`protobuf>=3.20,<7`; the build runtime does not replace that runtime requirement.
Binary compiler-wheel availability is a build-platform prerequisite. Never fall
back to a newer compiler or dependency source compilation to hide an unavailable
wheel. Python or OS versions not exercised in qualification remain unverified.

The only schema source in a checkout is `protocol/protobuf/mantis.proto`.
Builds regenerate into private temporary roots and reject stale source-generated
code if present. Nothing generated is source-maintained. An sdist carries exact
canonical schema bytes, generated code and `_schema/provenance.json`; rebuilding
verifies these together without accessing the original repository. Wheels carry
`mantis.proto` and `_provenance.json` beside generated code. Hashes detect stale or
corrupt inputs, not a malicious publisher who rewrites the entire manifest.
Verify artifacts against a trusted source and retain their external hashes.
Build tools, native binaries, checkout paths and credentials are not wheel inputs.

Provenance **format 2** records `compiler_distribution` (name/version) separately
from the executed `compiler_version` string. The distribution version is not a
compiler version. Format 1 artifacts remain historical evidence and are not
relabelled; a changed or old manifest fails the new backend's strict comparison.
Portable provenance intentionally omits architecture-specific binary hashes and
host paths so a same-source sdist can rebuild on another qualified architecture.
Qualification receipts, not the portable manifest, record the actual compiler
path/hash/ELF architecture, interpreter identity and expected/observed platform.

For byte reproducibility, keep Python/build/runtime tool versions fixed and set
`SOURCE_DATE_EPOCH` (>=315532800 for wheel ZIP timestamps). The backend normalizes
sdist timestamps/owners/order; setuptools uses the epoch for wheel timestamps.
Different toolchain versions are not promised byte-identical archives.

An sdist is self-contained: unpack it in a new directory and run `python -m build
--wheel` there. No `PYTHONPATH`, source-generated module, native build tree or
original checkout is required. See `tests/sdk/artifacts_test.py` for executable
isolated artifact, descriptor, wire and failure regressions.

The Linux workflow retains x86_64 Python 3.10/3.12/3.14 and adds native
`ubuntu-24.04-arm` Python 3.12. Its expected-platform check verifies Linux,
Python version/machine and interpreter/compiler ELF architecture, selects the
distribution's installed executable, validates its RECORD digest and executes
`--version`. Wrong-platform and version-spoofing substitute controls must fail,
then the actual selected binary must pass again. These are bounded identity and
integrity checks, not source authenticity, emulation detection or OS isolation.
ARM success requires the completed native job; x86_64 execution cannot prove it.

Importing `mantis` or `mantis.calibration` performs no connection. `mantis.Client`
requires a token (argument or `MANTIS_TOKEN`); actual commands connect only to
`127.0.0.1`, with the existing local daemon authentication and timeout semantics.
Do not put real tokens in examples/logs. SDK, application, schema and wire protocol
versions are separate identities: package 0.1.0 does not change wire protocol v1.

## Installed control qualification

The integrated artifact qualifier also runs the existing real-loopback control
suite from clean installed prefixes (`python -I`, no source-tree `PYTHONPATH`).
Direct wheels, wheels rebuilt from an unpacked sdist without its original source
root, and pip's isolated-overlay sdist rebuild each exercise protobuf 3.20.0 and
6.33.5. Receipts bind actual installed client/calibration/generated/runtime paths
and hashes to the qualified source and canonical schema. Cases include malformed,
truncated, oversized and mismatched replies, application/transport errors,
unavailable peers, shared deadlines without retry, legacy receive compatibility,
recovery and teardown. The peer transports real canonical Protobuf over loopback;
it is not a running `mantisd` or evidence of a daemon's final operation outcome.

A small historical-method fixture reconstructs the **complete** ca04 pre-control
client, checks its immutable SHA-256, builds a real wheel and installs it in fresh
prefixes. Its trickling-deadline assertion and malformed-response containment
fail at both protobuf edges. Restoring the accepted client bytes must reproduce
the qualified wheel byte-for-byte and pass the full installed control suite.
Internally valid artifacts with a different client source are also rejected.
Original packaging, compiler identity, lifecycle and command-process negative
controls remain in the same qualifier; no additional duplicated test family is
needed. Logs, frozen inputs, installed identities and restoration receipts are
retained separately from portable provenance format 2.

Timeout or disconnection does not cancel daemon-owned work: its outcome may be
**UNKNOWN**. No automatic retry is performed. Live-daemon qualification,
whole-Issue75 qualification, non-Linux support, naming/legal/notice review,
distribution checks, TestPyPI, signing and production publishing remain separate
gates. No publishing workflow or remote-network activation is provided.

## Installed Job deadline qualification

The existing `tests/sdk/artifacts_test.py` matrix additionally runs selected
canonical Job/control regressions through `tests/sdk/installed_job_probe.py` in
all four external consumers: wheel and independently rebuilt sdist wheel, each
with protobuf 3.20.0 and 6.33.5. The probe requires an isolated private venv
(`-I -B`), loads checkout fixtures by explicit filename without changing
`sys.path`, and records installed SDK, Client, Job method and wire origins/hashes
in the actual test process. Fixtures are not the SDK under test. No source
SDK/generated-binding fallback is allowed.

Owned real loopback peers carry canonical request/response bytes. Phase-observed
virtual clocks exercise completion/report parsing, shared budgets, trickles,
late success, exhausted polling, Job/protocol/application errors, legacy client
call shapes, UNKNOWN outcomes without implicit cancel/retry, and peer/socket
failure unwind. A synchronized combined exchange/peer failure checks primary
identity, errno and traceback plus rendered secondary diagnostics and owned
socket/listener/thread retirement, including the existing Python3.10 CI consumer.
Restoration/receipt failure controls use real errno-bearing exceptions and
rendered exception chains; diagnostics do not depend on Python3.11 `add_note`
or on changing `OSError.args`. These are contract checks, not measured real-time
guarantees.
The qualifier also deliberately omits the post-JSON expiry guard in one disposable
installed consumer, requires the intended assertion/nonzero RED (without a peer
teardown failure or unrelated unittest ERROR), restores exact
installed package bytes, and requires GREEN. Original artifacts and checkout SDK
bytes must remain unchanged; combined stdout/stderr and exits are retained.
Unisolated invocation, absent fixture, missing installed wire, unrelated negative
failures and unsuccessful restoration fail closed.

The combined qualifier preserves the installed control/native-ARM stage and the
installed Job stage in one source candidate. It additionally runs both suites in
the same installed SDK process and rejects a disposable installed implementation
that ignores remaining receive time inside the outer budget after a preceding
control failure. The original
parent verdicts qualify only their respective sources, not this composition.

This is **not** live `mantisd` integration, OS-level isolation, security, hardware,
non-Linux/platform or release readiness. Stage A packaging evidence and standalone
Job evidence retain their original scopes.
Live-daemon/fault-protocol tests, whole-Issue75 qualification, naming/legal/notice
review, distribution checks, TestPyPI, signing and production publishing remain
separate gates. No publishing workflow or remote-network activation is provided.
