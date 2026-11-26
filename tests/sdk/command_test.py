"""Linux command-runner regressions, independent of artifact builds."""
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import signal
import select
import subprocess
import sys
import tempfile
import time
import unittest
from typing import Any
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('artifacts', Path(__file__).with_name('artifacts_test.py'))
assert spec is not None and spec.loader is not None
runner: Any = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class Commands(unittest.TestCase):
    def setUp(self):
        evidence = os.environ.get('MANTIS_COMMAND_EVIDENCE')
        if evidence:
            runner.WORK = Path(evidence) / self._testMethodName
            runner.WORK.mkdir(parents=True, exist_ok=False)
        else:
            self.directory = tempfile.TemporaryDirectory(prefix='sdk75-command-')
            self.addCleanup(self.directory.cleanup)
            runner.WORK = Path(self.directory.name)
        (runner.WORK / 'tmp').mkdir()
        self.libc = ctypes.CDLL(None, use_errno=True)
        self.previous = ctypes.c_int()
        self.assertEqual(self.libc.prctl(37, ctypes.byref(self.previous), 0, 0, 0), 0)
        self.addCleanup(self.restore_subreaper)

    def restore_subreaper(self):
        self.assertEqual(self.libc.prctl(36, self.previous.value, 0, 0, 0), 0)

    def receipt(self):
        return json.loads((runner.WORK / 'commands.jsonl').read_text().splitlines()[-1])

    def test_timeout_retires_resistant_child_after_parent_exits(self):
        self.check_timeout_fixture(resistant=True)

    def test_timeout_retires_cooperative_child(self):
        self.check_timeout_fixture(resistant=False)

    def check_timeout_fixture(self, resistant):
        # The test is an outer safety net for the intentionally broken runner.
        # Reap/signals are restricted to the newly created fixture group.
        self.assertEqual(self.libc.prctl(36, 1, 0, 0, 0), 0)
        pidfile = runner.WORK / 'resistant.pid'
        original_wait = subprocess.Popen.wait
        processes = []
        ready = False

        def gated_wait(process, timeout=None):
            nonlocal ready
            if not ready:
                processes.append(process)
                deadline = time.monotonic() + 5
                while not pidfile.exists():
                    if time.monotonic() >= deadline:
                        raise AssertionError('resistant fixture did not become ready')
                    time.sleep(0.01)
                ready = True
            return original_wait(process, timeout=timeout)

        try:
            with patch.object(subprocess.Popen, 'wait', gated_wait):
                try:
                    runner.command([sys.executable, Path(__file__).with_name('hang_fixture.py'),
                                    pidfile] + (['--resistant'] if resistant else []),
                                   timeout=0.2, expected=1)
                except AssertionError as error:
                    # Ordinary negative commands must reject timeout (F2), too.
                    self.assertIn('timeout', str(error))
            self.assertTrue(ready)
            self.assertTrue(self.receipt()['timed_out'])
            output = (runner.WORK / self.receipt()['log']).read_text()
            self.assertIn('parent-and-child-ready', output)
            child = int(pidfile.read_text())
            self.assertFalse(Path(f'/proc/{child}').exists(),
                             'timed-out command retained a live or zombie descendant')
            self.assertEqual(self.receipt()['cleanup_errors'], [])
            if resistant:
                self.assertIn(child, [entry['pid'] for entry in self.receipt()['reaped_descendants']])
            else:
                self.assertEqual(self.receipt()['exit'], 143, 'cooperative parent did not finish its handler')
                self.assertEqual(self.receipt()['reaped_descendants'], [], 'parent did not reap its child')
            self.assertNotIn('Traceback', output)
            with self.assertRaises(ProcessLookupError):
                os.kill(processes[0].pid, 0)
        finally:
            if processes:
                process = processes[0]
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                original_wait(process, timeout=5)
                deadline = time.monotonic() + 5
                while True:
                    try:
                        pid, _ = os.waitpid(-process.pid, os.WNOHANG)
                    except ChildProcessError:
                        break
                    if time.monotonic() >= deadline:
                        self.fail('test safety-net could not reap its fixture')
                    if pid == 0:
                        time.sleep(0.01)
            self.restore_subreaper()

    def tearDown(self):
        state = ctypes.c_int()
        self.assertEqual(self.libc.prctl(37, ctypes.byref(state), 0, 0, 0), 0)
        self.assertEqual(state.value, self.previous.value, 'runner leaked subreaper state')

    def test_hanging_negative_is_not_a_rejection(self):
        fixture = runner.WORK / 'hang.py'
        fixture.write_text('import threading\nthreading.Event().wait()\n')
        for expected in (0, 1):
            with self.subTest(expected=expected), self.assertRaisesRegex(AssertionError, 'unexpected command timeout'):
                runner.command([sys.executable, fixture], expected=expected, timeout=0.2)
            self.assertTrue(self.receipt()['timed_out'])

    def test_explicit_expected_timeout_and_required_occurrence(self):
        fixture = runner.WORK / 'explicit.py'
        fixture.write_text('import threading\nprint("ready", flush=True)\nthreading.Event().wait()\n')
        output = runner.command([sys.executable, fixture], expect_timeout=True, timeout=0.2)
        self.assertIn('ready', output)
        self.assertTrue(self.receipt()['expected_timeout'])
        self.assertIsNone(self.receipt()['failure'])
        fixture.write_text('print("not hanging")\n')
        with self.assertRaisesRegex(AssertionError, 'expected command timeout did not occur'):
            runner.command([sys.executable, fixture], expect_timeout=True, timeout=0.2)
        self.assertFalse(self.receipt()['timed_out'])

    def test_real_rejection_is_not_a_timeout(self):
        fixture = runner.WORK / 'reject.py'
        fixture.write_text('raise RuntimeError("schema rejection control")\n')
        output = runner.command([sys.executable, fixture], expected=1)
        self.assertIn('RuntimeError: schema rejection control', output)
        self.assertEqual(self.receipt()['exit'], 1)
        self.assertFalse(self.receipt()['timed_out'])

    def test_primary_timeout_survives_cleanup_error(self):
        fixture = runner.WORK / 'hang.py'
        fixture.write_text('import threading\nthreading.Event().wait()\n')
        retire = runner._retire_group

        def retire_then_fail(process, reaped):
            retire(process, reaped)
            raise OSError('injected cleanup reporting failure')

        with patch.object(runner, '_retire_group', retire_then_fail):
            with self.assertRaisesRegex(AssertionError, 'unexpected command timeout') as failed:
                runner.command([sys.executable, fixture], expected=1, timeout=0.2)
        self.assertIn('injected cleanup reporting failure', str(failed.exception))
        receipt = self.receipt()
        self.assertIn('unexpected command timeout', receipt['failure'])
        self.assertIn('injected cleanup reporting failure', receipt['cleanup_errors'][0])
        self.assertFalse(Path(f"/proc/{receipt['pid']}").exists())

    def test_primary_timeout_survives_receipt_write_error(self):
        fixture = runner.WORK / 'hang.py'
        fixture.write_text('import threading\nprint("timeout diagnostic", flush=True)\nthreading.Event().wait()\n')
        original_open = Path.open

        def fail_receipt(path, *args, **kwargs):
            if path == runner.WORK / 'commands.jsonl':
                raise OSError('injected receipt failure')
            return original_open(path, *args, **kwargs)

        with patch.object(Path, 'open', fail_receipt):
            with self.assertRaisesRegex(AssertionError, 'unexpected command timeout') as failed:
                runner.command([sys.executable, fixture], expected=1, timeout=0.2)
        self.assertIn('receipt write failed', str(failed.exception))
        self.assertIn('timeout diagnostic', str(failed.exception))
        self.assertIn('timeout diagnostic', next(runner.WORK.glob('*.log')).read_text())

    def test_unrelated_child_and_process_local_settings_are_preserved(self):
        fixture = runner.WORK / 'unrelated.py'
        fixture.write_text('import threading\nthreading.Event().wait()\n')
        unrelated = subprocess.Popen([sys.executable, fixture], start_new_session=True)
        try:
            noop = runner.WORK / 'noop.py'
            noop.write_text('print("success")\n')
            self.assertEqual(runner.command([sys.executable, noop]).strip(), 'success')
            self.assertIsNone(unrelated.poll(), 'runner retired an unrelated child')
        finally:
            unrelated.kill()
            unrelated.wait(timeout=5)
        self.assertFalse(Path(f'/proc/{unrelated.pid}').exists())

    def test_unrelated_zombie_is_not_reaped(self):
        fixture = runner.WORK / 'unrelated-exit.py'
        fixture.write_text('raise SystemExit(7)\n')
        unrelated = subprocess.Popen([sys.executable, fixture], start_new_session=True)
        try:
            fd = os.pidfd_open(unrelated.pid)
            try:
                self.assertTrue(select.select([fd], [], [], 5)[0], 'unrelated child did not exit')
            finally:
                os.close(fd)
            noop = runner.WORK / 'noop.py'
            noop.write_text('pass\n')
            runner.command([sys.executable, noop])
            # WNOWAIT observes ownership without reaping. A broad waitpid(-1)
            # would consume this status and make the exact-child oracle fail.
            status = os.waitid(os.P_PID, unrelated.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
            assert status is not None, 'runner consumed unrelated child status'
            self.assertEqual(status.si_status, 7)
            self.assertEqual(unrelated.wait(timeout=5), 7)
        finally:
            if unrelated.poll() is None:
                unrelated.kill()
            unrelated.wait(timeout=5)


if __name__ == '__main__':
    unittest.main(verbosity=2)
