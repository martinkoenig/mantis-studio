#!/usr/bin/env python3
"""Standalone source-level Python control qualification; not daemon/artifact QA.

Uses only this checkout, installed protoc/protobuf, temporary generated bindings
and real loopback peers. No product build, dependency download or daemon launch.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tabnanny
import tempfile
import tokenize

from google.protobuf import __version__ as protobuf_version
from google.protobuf import descriptor_pb2

ROOT = Path(__file__).resolve().parents[3]
MAX_OUTPUT = 128 * 1024 * 1024
RESERVE = 1024 * 1024 * 1024


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_capacity(path):
    # Reserve the whole output allowance before creating a new run.
    if shutil.disk_usage(path).free < RESERVE + MAX_OUTPUT:
        raise RuntimeError("Insufficient disk reserve for Python-control qualification")


def normalize_descriptor(message):
    # protoc descriptor sets explicitly include derived json_name; Python's
    # embedded descriptor may omit it. Do not normalize any wire/schema fields.
    for field in message.field:
        field.ClearField("json_name")
    for nested in message.nested_type:
        normalize_descriptor(nested)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--protoc", type=Path, default=Path("/usr/bin/protoc"))
    parser.add_argument("--negative-controls", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("output must be under this checkout's ignored build directory")
    require_capacity(ROOT)
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    package = run / "python/mantis"
    package.mkdir(parents=True)
    receipt = {"scope": "standalone-python-control-peers-not-mantisd",
               "python": sys.version, "python_executable_sha256": digest(Path(sys.executable).resolve()),
               "protobuf": protobuf_version, "machine": platform.machine(),
               "protoc_sha256": digest(args.protoc.resolve()), "inputs": {}, "commands": []}
    snapshot = run / "inputs"
    snapshot.mkdir()
    inputs = [*sorted((ROOT / "sdk/python/mantis").glob("*.py")),
              ROOT / "protocol/protobuf/mantis.proto", ROOT / "tests/unit/python_control.py",
              ROOT / "tests/integration/acceptance.py",
              ROOT / "tests/unit/python_capture.py", ROOT / "tests/unit/python_calibration.py",
              ROOT / "tests/contract/protocol_fields.py", ROOT / "tests/contract/boundaries.py",
              ROOT / "tests/ci/python_control_workflow_test.py",
              ROOT / ".github/workflows/python-control.yml", ROOT / "docs/client-sdk/README.md",
              Path(__file__).resolve()]
    inputs = [p for p in inputs if p.name != "mantis_pb2.py"]
    for source in inputs:
        relative = source.relative_to(ROOT)
        receipt["inputs"][relative.as_posix()] = digest(source)
        target = snapshot / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if source.parent == ROOT / "sdk/python/mantis":
            shutil.copyfile(source, package / source.name)

    def command(name, argv, *, expected=0, diagnostic=None):
        log = run / f"{name}.log"
        entry = {"name": name, "argv": [str(x) for x in argv], "expected_exit": expected}
        receipt["commands"].append(entry)
        # Fixed owned scripts do not spawn descendants. subprocess.run kills and
        # reaps its direct child on timeout; a timeout is always a failed gate.
        with log.open("wb") as stream:
            try:
                result = subprocess.run(entry["argv"], cwd=ROOT, stdout=stream,
                                        stderr=subprocess.STDOUT, timeout=30, check=False,
                                        env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
            except subprocess.TimeoutExpired:
                entry["timeout"] = True
                raise
        entry["exit"] = result.returncode
        entry["log_sha256"] = digest(log)
        if log.stat().st_size > 4 * 1024 * 1024:
            raise RuntimeError(f"{name}: output bound exceeded")
        text = log.read_text()
        print(f"{name}: exit={result.returncode} expected={expected}", flush=True)
        print(text, end="", flush=True)
        if result.returncode != expected:
            raise RuntimeError(f"{name}: unexpected exit {result.returncode}")
        if diagnostic and (diagnostic not in text or "Ran 1 test" not in text or "ERROR:" in text):
            raise RuntimeError(f"{name}: intended negative diagnostic missing")
        return text

    try:
        receipt["git_head"] = command("git-head", ["git", "rev-parse", "HEAD"]).strip()
        command("protoc-version", [args.protoc, "--version"])
        schema = snapshot / "protocol/protobuf/mantis.proto"
        descriptor = run / "canonical.pb"
        command("generate", [args.protoc, "-I", schema.parent, f"--python_out={package}",
                             f"--descriptor_set_out={descriptor}", schema])
        sys.path.insert(0, str(run / "python"))
        import mantis
        from mantis import wire
        canonical = descriptor_pb2.FileDescriptorSet.FromString(descriptor.read_bytes())
        if len(canonical.file) != 1:
            raise AssertionError("Expected the single canonical schema")
        actual = descriptor_pb2.FileDescriptorProto.FromString(wire.DESCRIPTOR.serialized_pb)
        expected = canonical.file[0]
        for file in (actual, expected):
            for message in file.message_type:
                normalize_descriptor(message)
        if actual != expected:
            raise AssertionError("Generated descriptor does not match canonical schema")
        for source in (ROOT / "sdk/python/mantis").glob("*.py"):
            if source.name != "mantis_pb2.py" and digest(source) != digest(package / source.name):
                raise AssertionError("SDK source copy changed")
        if not mantis.__file__ or Path(mantis.__file__).resolve() != package / "__init__.py":
            raise AssertionError("Did not import generated test-only SDK tree")
        golden = b"\x08\x01\x12\x05fixed\x1a\x0dfixture-token\x52\x00"
        request = wire.Request(protocol_version=1, request_id="fixed", token="fixture-token",
                               snapshot=wire.Empty())
        if request.SerializeToString() != golden:
            raise AssertionError("Request differs from independent protocol-v1 golden bytes")
        receipt["generated_binding_sha256"] = digest(package / "mantis_pb2.py")
        receipt["descriptor_sha256"] = digest(descriptor)
        receipt["binding_check"] = "PASS: exact source, canonical descriptor and golden request"
        print(receipt["binding_check"], flush=True)
        print(json.dumps({k: receipt[k] for k in ("python", "protobuf", "machine", "protoc_sha256",
                                                "generated_binding_sha256", "descriptor_sha256")}), flush=True)

        python = [sys.executable, "-B"]
        control = snapshot / "tests/unit/python_control.py"
        command("control", [*python, control, run, "-v"])
        command("capture", [*python, snapshot / "tests/unit/python_capture.py", run, "-v"])
        command("calibration", [*python, snapshot / "tests/unit/python_calibration.py", run])
        command("protocol-fields", [*python, snapshot / "tests/contract/protocol_fields.py", run])
        command("boundaries", [*python, snapshot / "tests/contract/boundaries.py", ROOT])
        command("workflow", [*python, ROOT / "tests/ci/python_control_workflow_test.py", "-v"])
        # Compile without writing pycache or touching source/generated modules.
        for source in [*snapshot.rglob("*.py"), package / "mantis_pb2.py"]:
            compile(source.read_bytes(), str(source), "exec")
            with tokenize.open(source) as stream:
                tabnanny.process_tokens(tokenize.generate_tokens(stream.readline))
            if any(line.rstrip() != line for line in source.read_text().splitlines()):
                raise AssertionError(f"Trailing whitespace: {source.name}")
        command("whitespace", ["git", "diff", "--check"])
        if args.negative_controls:
            source = package / "__init__.py"
            original = source.read_text()
            mutations = [
                ("receive-compatibility", "def _receive(sock, count, deadline=None):",
                 "def _receive(sock, count, deadline):",
                 "Control.test_existing_acceptance_two_argument_receive_call_shape",
                 "AssertionError: existing two-argument receive caller is incompatible"),
                ("deadline-renewal", 'raise TimeoutError("Control exchange timed out; daemon outcome may be unknown")',
                 "return 5  # WRONG: renew an expired whole-call budget",
                 "Control.test_trickling_progress_cannot_extend_exchange_deadline",
                 "AssertionError: TimeoutError not raised"),
                ("blocking-budget", "sock.settimeout(Client._remaining(deadline))", "sock.settimeout(5)",
                 "Control.test_real_header_and_body_stalls_use_remaining_blocking_budget",
                 "AssertionError: blocking receive did not use remaining budget"),
            ]
            for name, before, after, test, diagnostic in mutations:
                if original.count(before) != 1:
                    raise AssertionError(f"{name}: mutation target must be unique")
                try:
                    source.write_text(original.replace(before, after))
                    receipt[f"{name}_source_sha256"] = digest(source)
                    command(name, [*python, control, run, test, "-v"], expected=1,
                            diagnostic=diagnostic)
                finally:
                    source.write_text(original)
                if digest(source) != receipt["inputs"]["sdk/python/mantis/__init__.py"]:
                    raise AssertionError("Exact production source restoration failed")
                command(f"{name}-restored", [*python, control, run, test, "-v"])
        for relative, expected_hash in receipt["inputs"].items():
            if digest(ROOT / relative) != expected_hash:
                raise AssertionError(f"Input changed during qualification: {relative}")
        receipt["syntax_indentation_whitespace"] = "PASS"
        receipt["result"] = "PASS"
    finally:
        receipt["output_bytes"] = sum(p.stat().st_size for p in run.rglob("*") if p.is_file())
        if receipt["output_bytes"] > MAX_OUTPUT or shutil.disk_usage(ROOT).free < RESERVE:
            receipt["result"] = "FAIL: resource bound"
        (run / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(f"Receipt: {run.relative_to(ROOT)}/receipt.json", flush=True)
    if receipt.get("result") != "PASS":
        raise RuntimeError("Qualification failed")


if __name__ == "__main__":
    main()
