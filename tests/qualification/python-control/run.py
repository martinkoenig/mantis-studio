#!/usr/bin/env python3
"""Standalone source-level Python control qualification; not daemon/artifact QA.

Uses only this checkout, installed protoc/protobuf, temporary generated bindings
and real loopback peers. No product build, dependency download or daemon launch.
"""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import selectors
import shutil
import signal
import subprocess
import sys
import tabnanny
import tempfile
import time
import tokenize

from google.protobuf import __version__ as protobuf_version
from google.protobuf import descriptor_pb2

ROOT = Path(__file__).resolve().parents[3]
MAX_OUTPUT = 128 * 1024 * 1024
RESERVE = 1024 * 1024 * 1024
RECEIPT_ALLOWANCE = 32 * 1024


def output_budget(value):
    try:
        result = int(value)
        if not 1 <= result <= MAX_OUTPUT:
            raise ValueError
    except ValueError:
        raise argparse.ArgumentTypeError(
            "output budget must be an integer from 1 to 134217728 bytes") from None
    return result


class OutputBudget:
    """Bound all retained run files, reserving room for the failure receipt."""
    def __init__(self, root, limit):
        self.root, self.limit = root, limit

    def used(self):
        return sum(p.stat().st_size for p in self.root.rglob("*") if p.is_file())

    def check(self, extra=0, *, receipt=False):
        ceiling = self.limit if receipt else self.limit - RECEIPT_ALLOWANCE
        if self.used() + extra > ceiling:
            raise RuntimeError("Python-control output budget exceeded")
        if shutil.disk_usage(self.root).free < RESERVE + extra:
            raise RuntimeError("Insufficient disk reserve for Python-control qualification")

    def write(self, path, data, *, receipt=False):
        previous = path.stat().st_size if path.exists() else 0
        self.check(max(0, len(data) - previous), receipt=receipt)
        path.write_bytes(data)


def capture_command(argv, log, budget, *, cwd=ROOT, generated_paths=()):
    """Bound capture and the fixed command tree through a dedicated Linux owner.

    The owner establishes subreaping before it admits the command, and reaps its
    descendants even if a nested runner/leader exits. The existing thirty-second
    command hang bound includes reading pipes, tree retirement and owner exit.
    """
    deadline = time.monotonic() + 30
    budget.check()
    log_limit = 4 * 1024 * 1024
    file_limit = 0
    child_options = {}
    if generated_paths:
        # The frozen canonical schema generates exactly two regular files.
        # Reserve a separate log share and bound each child-written file in the
        # kernel, so even a failed/noisy protoc cannot overspend cumulatively.
        log_limit = 4096
        file_limit = (budget.limit - RECEIPT_ALLOWANCE - budget.used() - log_limit) // len(generated_paths)
        if file_limit < 1:
            raise RuntimeError("Python-control output budget exceeded")
        def limit_files():
            resource.setrlimit(resource.RLIMIT_FSIZE, (file_limit, file_limit))
        child_options["preexec_fn"] = limit_files
    owned_argv = [sys.executable, "-B", Path(__file__).with_name("supervise.py"), *argv]
    with log.open("wb") as stream, selectors.DefaultSelector() as selector:
        child = subprocess.Popen(owned_argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"}, **child_options)
        try:
            selector.register(child.stdout, selectors.EVENT_READ)
            size = 0
            while selector.get_map():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise subprocess.TimeoutExpired(argv, 30)
                for key, _ in selector.select(min(remaining, 0.1)):
                    part = os.read(key.fileobj.fileno(), 4096)
                    if not part:
                        selector.unregister(key.fileobj)
                        continue
                    size += len(part)
                    if size > log_limit:
                        raise RuntimeError("Python-control command output bound exceeded")
                    budget.check(len(part))
                    stream.write(part)
                    stream.flush()
                budget.check()
            result = child.wait(timeout=max(0, deadline - time.monotonic()))
            if generated_paths and any(p.exists() and p.stat().st_size >= file_limit for p in generated_paths):
                raise RuntimeError("Python-control generated output budget exceeded")
            return result
        except BaseException as initiating:
            try:
                if child.poll() is None:
                    # Ask the owner to retire its tree, not kill the cleanup
                    # authority before it can reap nested runners/emitters.
                    child.terminate()
                status = child.wait(timeout=5)
                if status != -signal.SIGKILL:
                    initiating.add_note(f"Owned command tree retirement unconfirmed: status {status}")
            except BaseException as cleanup:
                initiating.add_note(f"Owned child cleanup failed: {type(cleanup).__name__}")
            raise
        finally:
            child.stdout.close()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


@contextmanager
def mutated_source(source, original, replacement, budget):
    """Always restore bytes, without masking an initiating test/capture failure."""
    try:
        budget.write(source, replacement.encode())
        yield
    finally:
        initiating = sys.exc_info()[1]
        try:
            # Restoration may need the small growth held in the receipt reserve
            # after an exhausted capture. Never leave a mutant as qualified code.
            budget.write(source, original.encode(), receipt=True)
            if source.read_bytes() != original.encode():
                raise AssertionError("Exact production source restoration failed")
        except BaseException as cleanup:
            if initiating is None:
                raise
            initiating.add_note(f"Source restoration failed: {type(cleanup).__name__}")


def require_capacity(path, allowance=MAX_OUTPUT):
    # Reserve the whole output allowance before creating a new run.
    if shutil.disk_usage(path).free < RESERVE + allowance:
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
    parser.add_argument("--output-budget-bytes", type=output_budget, default=MAX_OUTPUT,
                        help="cumulative run output allowance in bytes (default: 128 MiB)")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("output must be under this checkout's ignored build directory")
    require_capacity(ROOT, args.output_budget_bytes)
    if args.output_budget_bytes < RECEIPT_ALLOWANCE:
        raise RuntimeError("Python-control output budget exceeded")
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    budget = OutputBudget(run, args.output_budget_bytes)
    package = run / "python/mantis"
    package.mkdir(parents=True)
    receipt = {"scope": "standalone-python-control-peers-not-mantisd",
               "python": sys.version, "python_executable_sha256": digest(Path(sys.executable).resolve()),
               "protobuf": protobuf_version, "machine": platform.machine(),
               "protoc_sha256": digest(args.protoc.resolve()), "inputs": {}, "commands": [],
               "output_budget_bytes": args.output_budget_bytes, "reserve_bytes": RESERVE}
    snapshot = run / "inputs"
    snapshot.mkdir()
    inputs = [*sorted((ROOT / "sdk/python/mantis").glob("*.py")),
              ROOT / "protocol/protobuf/mantis.proto", ROOT / "tests/unit/python_control.py",
              ROOT / "tests/unit/python_job.py",
              ROOT / "tests/qualification/python-control/test_budget.py",
              ROOT / "tests/qualification/python-control/supervise.py",
              ROOT / "tests/integration/acceptance.py",
              ROOT / "tests/unit/python_capture.py", ROOT / "tests/unit/python_calibration.py",
              ROOT / "tests/contract/protocol_fields.py", ROOT / "tests/contract/boundaries.py",
              ROOT / "tests/ci/python_control_workflow_test.py",
              ROOT / "tests/ci/python_sdk_workflow_test.py",
              ROOT / ".github/workflows/python-sdk.yml",
              ROOT / ".github/workflows/python-control.yml", ROOT / "docs/client-sdk/README.md",
              Path(__file__).resolve()]
    inputs = [p for p in inputs if p.name != "mantis_pb2.py"]
    def command(name, argv, *, expected=0, diagnostic=None, generated_paths=()):
        log = run / f"{name}.log"
        entry = {"name": name, "argv": [str(x) for x in argv], "expected_exit": expected}
        receipt["commands"].append(entry)
        try:
            result = capture_command(entry["argv"], log, budget, generated_paths=generated_paths)
        except BaseException as error:
            entry["failure"] = type(error).__name__
            entry["log_sha256"] = digest(log) if log.exists() else None
            raise
        entry["exit"] = result
        entry["log_sha256"] = digest(log)
        if log.stat().st_size > 4 * 1024 * 1024:
            raise RuntimeError(f"{name}: output bound exceeded")
        text = log.read_text()
        print(f"{name}: exit={result} expected={expected}", flush=True)
        print(text, end="", flush=True)
        if result != expected:
            raise RuntimeError(f"{name}: unexpected exit {result}")
        if diagnostic and (diagnostic not in text or "Ran 1 test" not in text or "ERROR:" in text):
            raise RuntimeError(f"{name}: intended negative diagnostic missing")
        return text

    try:
        for source in inputs:
            relative = source.relative_to(ROOT)
            receipt["inputs"][relative.as_posix()] = digest(source)
            target = snapshot / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            budget.write(target, source.read_bytes())
            if source.parent == ROOT / "sdk/python/mantis":
                budget.write(package / source.name, source.read_bytes())
        receipt["git_head"] = command("git-head", ["git", "rev-parse", "HEAD"]).strip()
        command("protoc-version", [args.protoc, "--version"])
        schema = snapshot / "protocol/protobuf/mantis.proto"
        descriptor = run / "canonical.pb"
        command("generate", [args.protoc, "-I", schema.parent, f"--python_out={package}",
                             f"--descriptor_set_out={descriptor}", schema],
                generated_paths=(package / "mantis_pb2.py", descriptor))
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
        command("jobs", [*python, snapshot / "tests/unit/python_job.py", run, "-v"])
        command("output-budget", [*python, snapshot / "tests/qualification/python-control/test_budget.py", "-v"])
        command("capture", [*python, snapshot / "tests/unit/python_capture.py", run, "-v"])
        command("calibration", [*python, snapshot / "tests/unit/python_calibration.py", run])
        command("protocol-fields", [*python, snapshot / "tests/contract/protocol_fields.py", run])
        command("boundaries", [*python, snapshot / "tests/contract/boundaries.py", ROOT])
        command("workflow", [*python, ROOT / "tests/ci/python_control_workflow_test.py", "-v"])
        command("artifact-workflow", [*python, ROOT / "tests/ci/python_sdk_workflow_test.py", "-v"])
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
                ("job-budget-bypass", "deadline = min(deadline, _deadline)",
                 "deadline = deadline  # WRONG: discard caller budget",
                 "Jobs.test_outer_budget_bounds_every_fragmented_io",
                 "AssertionError: 5.0 != 1 : Job outer budget was bypassed at connect"),
            ]
            for name, before, after, test, diagnostic in mutations:
                if original.count(before) != 1:
                    raise AssertionError(f"{name}: mutation target must be unique")
                with mutated_source(source, original, original.replace(before, after), budget):
                    receipt[f"{name}_source_sha256"] = digest(source)
                    suite = snapshot / "tests/unit/python_job.py" if name == "job-budget-bypass" else control
                    command(name, [*python, suite, run, test, "-v"], expected=1,
                            diagnostic=diagnostic)

                if digest(source) != receipt["inputs"]["sdk/python/mantis/__init__.py"]:
                    raise AssertionError("Exact production source restoration failed")
                command(f"{name}-restored", [*python, suite, run, test, "-v"])

            # The resource regression imports the frozen runner next to it. A
            # direct-launch bypass must expose the actual nested-emitter leak.
            owner_source = snapshot / "tests/qualification/python-control/run.py"
            owner_original = owner_source.read_text()
            before = '    owned_argv = [sys.executable, "-B", Path(__file__).with_name("supervise.py"), *argv]\n'
            if owner_original.count(before) != 1:
                raise AssertionError("command-tree-owner: mutation target must be unique")
            suite = snapshot / "tests/qualification/python-control/test_budget.py"
            test = "CommandTrees.test_outer_exhaustion_retires_actual_resource_test_tree"
            with mutated_source(owner_source, owner_original,
                                owner_original.replace(before, "    owned_argv = argv  # WRONG: bypass cleanup owner\n"),
                                budget):
                receipt["command-tree-owner_source_sha256"] = digest(owner_source)
                command("command-tree-owner", [*python, suite, test, "-v"], expected=1,
                        diagnostic="AssertionError: True is not false : owned descendant survived outer exhaustion")
            if digest(owner_source) != receipt["inputs"]["tests/qualification/python-control/run.py"]:
                raise AssertionError("Exact command ownership source restoration failed")
            command("command-tree-owner-restored", [*python, suite, test, "-v"])
        for relative, expected_hash in receipt["inputs"].items():
            if digest(ROOT / relative) != expected_hash:
                raise AssertionError(f"Input changed during qualification: {relative}")
        receipt["syntax_indentation_whitespace"] = "PASS"
        receipt["result"] = "PASS"
    except BaseException as initiating:
        receipt["result"] = "FAIL"
        receipt["failure"] = type(initiating).__name__
        raise
    finally:
        initiating = sys.exc_info()[1]
        try:
            receipt["output_bytes"] = budget.used()
            if receipt["output_bytes"] > args.output_budget_bytes or shutil.disk_usage(ROOT).free < RESERVE:
                receipt["result"] = "FAIL: resource bound"
            # Include the receipt itself, not merely its pre-write output size.
            payload = (json.dumps(receipt, indent=2) + "\n").encode()
            while receipt["output_bytes"] != budget.used() + len(payload):
                receipt["output_bytes"] = budget.used() + len(payload)
                payload = (json.dumps(receipt, indent=2) + "\n").encode()
            budget.write(run / "receipt.json", payload, receipt=True)
            print(f"Receipt: {run.relative_to(ROOT)}/receipt.json", flush=True)
        except BaseException as cleanup:
            if initiating is None:
                raise
            initiating.add_note(f"Receipt write failed: {type(cleanup).__name__}")
    if receipt.get("result") != "PASS":
        raise RuntimeError("Qualification failed")


if __name__ == "__main__":
    main()
