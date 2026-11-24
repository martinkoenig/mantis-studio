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
uses the packaged protoc 3.20.1 binary (not PATH) with build protobuf 3.20.0.
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

For byte reproducibility, keep Python/build/runtime tool versions fixed and set
`SOURCE_DATE_EPOCH` (>=315532800 for wheel ZIP timestamps). The backend normalizes
sdist timestamps/owners/order; setuptools uses the epoch for wheel timestamps.
Different toolchain versions are not promised byte-identical archives.

An sdist is self-contained: unpack it in a new directory and run `python -m build
--wheel` there. No `PYTHONPATH`, source-generated module, native build tree or
original checkout is required. See `tests/sdk/artifacts_test.py` for executable
isolated artifact, descriptor, wire and failure regressions.

Importing `mantis` or `mantis.calibration` performs no connection. `mantis.Client`
requires a token (argument or `MANTIS_TOKEN`); actual commands connect only to
`127.0.0.1`, with the existing local daemon authentication and timeout semantics.
Do not put real tokens in examples/logs. SDK, application, schema and wire protocol
versions are separate identities: package 0.1.0 does not change wire protocol v1.

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

This extension is **not** live `mantisd` integration, PR132 composition, OS-level
isolation, security, hardware, non-Linux/platform or release readiness. Stage A
packaging evidence and standalone Job evidence retain their original scopes.
Live-daemon/fault-protocol tests, whole-Issue75 qualification, naming/legal/notice
review, distribution checks, TestPyPI, signing and production publishing remain
separate gates. No publishing workflow or remote-network activation is provided.
