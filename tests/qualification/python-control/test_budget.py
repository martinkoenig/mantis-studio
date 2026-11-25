"""Qualifier resource controls; tiny owned emitters, not SDK behavior doubles."""

import ctypes
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

RUNNER = Path(__file__).with_name("run.py")
spec = importlib.util.spec_from_file_location("control_qualifier", RUNNER)
qualifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qualifier)
REAL_POPEN = subprocess.Popen


class Budgets(unittest.TestCase):
    def test_invalid_allowance_is_rejected_before_output_creation(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "invalid-output"
            for value in ("0", "-1", "134217729", "nan", "inf", "1.5"):
                with self.subTest(value=value):
                    result = subprocess.run([sys.executable, "-B", RUNNER, "--output", output,
                                             "--output-budget-bytes", value], capture_output=True, timeout=5)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(b"output budget must be an integer from 1 to 134217728 bytes", result.stderr)
                    self.assertFalse(output.exists())

    def test_original_default_and_hard_reserve_are_preserved(self):
        self.assertEqual(qualifier.MAX_OUTPUT, 134217728)
        self.assertEqual(qualifier.RESERVE, 1073741824)
        self.assertEqual(qualifier.output_budget("524288"), 524288)
        # This is an isolated capacity predicate negative, not an override of
        # any real qualification run's disk admission or guard.
        with patch.object(qualifier.shutil, "disk_usage", return_value=SimpleNamespace(free=1073741824 + 524287)):
            with self.assertRaisesRegex(RuntimeError, "Insufficient disk reserve"):
                qualifier.require_capacity(Path.cwd(), 524288)
            with self.assertRaisesRegex(RuntimeError, "Insufficient disk reserve"):
                qualifier.require_capacity(Path.cwd())

    def test_cumulative_copies_fail_before_overrun_and_receipt_can_be_retained(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            budget = qualifier.OutputBudget(root, 33792)
            budget.write(root / "first", b"a" * 600)
            with self.assertRaisesRegex(RuntimeError, "Python-control output budget exceeded"):
                budget.write(root / "second", b"b" * 600)
            self.assertFalse((root / "second").exists())
            self.assertEqual((root / "first").stat().st_size, 600)
            budget.write(root / "receipt.json", b'{"result":"FAIL"}', receipt=True)
            self.assertEqual(json.loads((root / "receipt.json").read_text())["result"], "FAIL")
            self.assertLessEqual(budget.used(), 33792)

    def test_exhausted_capture_reaps_owned_emitter_and_does_not_return_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            budget = qualifier.OutputBudget(root, 36864)
            # An existing capture already consumes half the usable allowance.
            budget.write(root / "previous.log", b"p" * 2048)
            emitter = root / "emit.py"
            emitter.write_text("import os, signal\nos.write(1, b'x' * 4096)\nsignal.pause()\n")
            children = []
            def spawn(*args, **kwargs):
                child = REAL_POPEN(*args, **kwargs)
                children.append(child)
                return child
            with patch.object(qualifier.subprocess, "Popen", side_effect=spawn):
                with self.assertRaisesRegex(RuntimeError, "Python-control output budget exceeded"):
                    qualifier.capture_command([sys.executable, "-B", emitter], root / "capture.log", budget)
            self.assertEqual(len(children), 1)
            self.assertEqual(children[0].returncode, -signal.SIGKILL)
            self.assertTrue(children[0].stdout.closed)
            with self.assertRaises(ChildProcessError):
                os.waitpid(children[0].pid, os.WNOHANG)
            self.assertLessEqual(budget.used(), 36864)
            self.assertEqual((root / "capture.log").read_bytes(), b"", "over-budget chunk was persisted")

    def test_kernel_bounds_generator_files_not_just_captured_stdout(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            budget = qualifier.OutputBudget(root, 49152)
            emitter = root / "generate.py"
            emitter.write_text("from pathlib import Path\nPath('generated').write_bytes(b'x' * 20000)\n")
            with self.assertRaisesRegex(RuntimeError, "Python-control generated output budget exceeded"):
                qualifier.capture_command([sys.executable, "-B", emitter], root / "generate.log",
                                          budget, cwd=root, generated_paths=(root / "generated", root / "second"))
            self.assertLess((root / "generated").stat().st_size, 20000)
            self.assertLessEqual(budget.used(), 49152)

    def test_primary_error_survives_owned_child_retirement_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            budget = qualifier.OutputBudget(root, 36864)
            emitter = root / "emit.py"
            emitter.write_text("import os, signal\nos.write(1, b'x' * 8192)\nsignal.pause()\n")
            children = []
            def spawn(*args, **kwargs):
                child = REAL_POPEN(*args, **kwargs)
                children.append(child)
                real_wait = child.wait
                def wait(timeout=None):
                    real_wait(timeout=timeout)
                    raise OSError("injected after actual owned-child reap")
                child.wait = wait
                return child
            with patch.object(qualifier.subprocess, "Popen", side_effect=spawn):
                with self.assertRaisesRegex(RuntimeError, "Python-control output budget exceeded") as raised:
                    qualifier.capture_command([sys.executable, "-B", emitter], root / "capture.log", budget)
            self.assertEqual(len(children), 1)
            self.assertEqual(children[0].returncode, -signal.SIGKILL)
            self.assertIn("Owned child cleanup failed: OSError", raised.exception.__notes__)

    def test_owner_preserves_command_status_and_later_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            budget = qualifier.OutputBudget(root, 65536)
            script = root / "status.py"
            script.write_text("import os, signal\nos.kill(os.getpid(), signal.SIGTERM)\n")
            self.assertEqual(qualifier.capture_command([sys.executable, "-B", script],
                                                      root / "signal.log", budget), -signal.SIGTERM)
            script.write_text("import sys\nprint('owned success')\nsys.exit(7)\n")
            self.assertEqual(qualifier.capture_command([sys.executable, "-B", script],
                                                      root / "status.log", budget), 7)
            script.write_text("print('later owned success')\n")
            self.assertEqual(qualifier.capture_command([sys.executable, "-B", script],
                                                      root / "success.log", budget), 0)
            self.assertEqual((root / "success.log").read_text(), "later owned success\n")

    def test_unconfirmed_cleanup_status_does_not_replace_primary_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            budget = qualifier.OutputBudget(root, 36864)
            emitter = root / "emit.py"
            emitter.write_text("import os, signal\nos.write(1, b'x' * 8192)\nsignal.pause()\n")
            def spawn(*args, **kwargs):
                child = REAL_POPEN(*args, **kwargs)
                real_wait = child.wait
                def wait(timeout=None):
                    real_wait(timeout=timeout)
                    return 1  # Inject report failure only after real tree retirement/reap.
                child.wait = wait
                return child
            with patch.object(qualifier.subprocess, "Popen", side_effect=spawn):
                with self.assertRaisesRegex(RuntimeError, "Python-control output budget exceeded") as raised:
                    qualifier.capture_command([sys.executable, "-B", emitter], root / "capture.log", budget)
            self.assertIn("Owned command tree retirement unconfirmed: status 1", raised.exception.__notes__)

    def test_mutant_restores_exact_bytes_on_exhaustion_and_preserves_primary(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            budget = qualifier.OutputBudget(root, 33792)
            source = root / "source.py"
            original = "# original source\n"
            source.write_text(original)
            with self.assertRaisesRegex(RuntimeError, "Python-control output budget exceeded"):
                with qualifier.mutated_source(source, original, "# mutant\n", budget):
                    budget.write(root / "overflow", b"x" * 2048)
            self.assertEqual(source.read_bytes(), original.encode())

            with self.assertRaisesRegex(AssertionError, "primary") as raised:
                with patch.object(budget, "write", wraps=budget.write) as writer:
                    with qualifier.mutated_source(source, original, "# mutant\n", budget):
                        writer.side_effect = OSError("restoration fault")
                        raise AssertionError("primary")
            self.assertIn("Source restoration failed: OSError", raised.exception.__notes__)
            source.write_text(original)  # Retire this test's intentionally failed disposable mutant.
            self.assertEqual(source.read_bytes(), original.encode())

    def test_exhaustion_during_input_freeze_retains_failed_receipt(self):
        output_parent = qualifier.ROOT / "build"
        output_parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="budget-negative-", dir=output_parent) as temporary:
            result = subprocess.run([sys.executable, "-B", RUNNER, "--output", temporary,
                                     "--output-budget-bytes", "32768"], capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b"Python-control output budget exceeded", result.stderr)
            receipts = list(Path(temporary).glob("run-*/receipt.json"))
            self.assertEqual(len(receipts), 1)
            receipt = json.loads(receipts[0].read_text())
            self.assertEqual(receipt["result"], "FAIL")
            self.assertEqual(receipt["failure"], "RuntimeError")
            self.assertEqual(receipt["commands"], [])
            self.assertLessEqual(sum(p.stat().st_size for p in Path(temporary).rglob("*") if p.is_file()), 32768)


class CommandTrees(unittest.TestCase):
    def exercise(self, mode):
        # This test process can have unrelated children. Scope adoption only for
        # fail-safe retirement when running the regression against rejected code.
        libc = ctypes.CDLL(None, use_errno=True)
        previous = ctypes.c_int()
        self.assertEqual(libc.prctl(37, ctypes.byref(previous), 0, 0, 0), 0)
        self.assertEqual(libc.prctl(36, 1, 0, 0, 0), 0)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            capture = root / "capture"
            capture.mkdir()
            pidfile = root / "observed.json"
            wrapper = root / "exercise.py"
            wrapper.write_text('''import importlib.util, json, os, signal, sys
from pathlib import Path
spec = importlib.util.spec_from_file_location("actual_tests", sys.argv[1])
tests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tests)
real = tests.REAL_POPEN

def identity(pid):
    return Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[19]

def observed_spawn(*args, **kwargs):
    child = real(*args, **kwargs)
    # Observe actual emit.py output, not elapsed time or only launcher creation.
    assert child.stdout.read(4096) == b"x" * 4096
    pending, identities = [child.pid], {}
    while pending:
        pid = pending.pop()
        identities[pid] = identity(pid)
        for task in Path(f"/proc/{pid}/task").iterdir():
            pending.extend(map(int, (task / "children").read_text().split()))
    Path(sys.argv[2]).write_text(json.dumps(identities))
    if sys.argv[3] == "leader-exit":
        os._exit(0)  # Deliberately bypass the actual test's inner cleanup.
    os.write(1, b"observed actual emitter output\\n" + b"x" * 8192)
    signal.pause()
    return child

tests.REAL_POPEN = observed_spawn
tests.Budgets("test_exhausted_capture_reaps_owned_emitter_and_does_not_return_success").run()
''')
            # An unrelated live child must neither be signalled nor reaped.
            idle = root / "unrelated.py"
            idle.write_text("import signal\nsignal.pause()\n")
            unrelated = REAL_POPEN([sys.executable, "-B", idle])
            direct = []
            def spawn(*args, **kwargs):
                child = REAL_POPEN(*args, **kwargs)
                direct.append(child)
                return child

            budget = qualifier.OutputBudget(capture, 36864)
            check = budget.check
            def observed_check(extra=0, **kwargs):
                if pidfile.exists():
                    if mode == "timeout":
                        raise subprocess.TimeoutExpired("observed command tree", 30)
                    if mode == "cancel":
                        raise KeyboardInterrupt("observed command tree cancellation")
                check(extra, **kwargs)

            identities = {}
            try:
                with patch.object(qualifier.subprocess, "Popen", side_effect=spawn), \
                     patch.object(budget, "check", side_effect=observed_check):
                    argv = [sys.executable, "-B", wrapper, Path(__file__).resolve(), pidfile, mode]
                    if mode == "leader-exit":
                        self.assertEqual(qualifier.capture_command(argv, capture / "outer.log", budget), 0)
                    else:
                        category, message = {
                            "exhaustion": (RuntimeError, "Python-control output budget exceeded"),
                            "timeout": (subprocess.TimeoutExpired, "observed command tree"),
                            "cancel": (KeyboardInterrupt, "observed command tree cancellation"),
                        }[mode]
                        with self.assertRaisesRegex(category, message):
                            qualifier.capture_command(argv, capture / "outer.log", budget)
                self.assertTrue(pidfile.exists(), "real emitter phase was not observed")
                identities = json.loads(pidfile.read_text())
                self.assertEqual(len(direct), 1)
                self.assertIsNotNone(direct[0].returncode, "outer immediate child was not reaped")
                self.assertTrue(direct[0].stdout.closed)
                for pid in identities:
                    self.assertFalse(Path(f"/proc/{pid}").exists(),
                                     f"owned descendant survived outer {mode}")
                self.assertIsNone(unrelated.poll(), "unrelated child was retired")
                self.assertLessEqual(budget.used(), budget.limit)
            finally:
                # Make intended RED/assertion unwind safe. Kill/reap only the
                # phase-recorded descendants, checking birth identity first.
                if pidfile.exists():
                    identities = json.loads(pidfile.read_text())
                for value, birth in identities.items():
                    pid = int(value)
                    try:
                        actual = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[19]
                        if actual == birth:
                            os.kill(pid, signal.SIGKILL)
                    except FileNotFoundError:
                        pass
                # A recorded nested owner may still be exiting. Its killed
                # children are adopted after that owner is reaped.
                for value in identities:
                    try:
                        os.waitpid(int(value), 0)
                    except ChildProcessError:
                        pass
                unrelated.kill()
                unrelated.wait(timeout=5)
                self.assertEqual(libc.prctl(36, previous.value, 0, 0, 0), 0)
                restored = ctypes.c_int()
                self.assertEqual(libc.prctl(37, ctypes.byref(restored), 0, 0, 0), 0)
                self.assertEqual(restored.value, previous.value)

    def test_outer_exhaustion_retires_actual_resource_test_tree(self):
        self.exercise("exhaustion")

    def test_outer_timeout_retires_actual_resource_test_tree(self):
        self.exercise("timeout")

    def test_outer_cancellation_retires_actual_resource_test_tree(self):
        self.exercise("cancel")

    def test_leader_exit_retires_actual_resource_test_tree(self):
        self.exercise("leader-exit")


if __name__ == "__main__":
    unittest.main()
