"""Qualifier resource controls; tiny owned emitters, not SDK behavior doubles."""

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


if __name__ == "__main__":
    unittest.main()
