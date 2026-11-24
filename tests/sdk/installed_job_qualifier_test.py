"""Synthetic failure-unwind controls for the installed mutation qualifier.

These do not substitute for the real installed peer/mutant executions.
"""
import errno
import importlib.util
from pathlib import Path
import tempfile
import traceback
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('artifacts', Path(__file__).with_name('artifacts_test.py'))
if spec is None or spec.loader is None:
    raise ImportError('cannot load artifact qualifier')
artifacts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(artifacts)


class SensitivityFailures(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='installed-job-unwind-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.python = self.root / 'venv/bin/python'
        self.package = self.root / 'venv/lib/python-test/site-packages/mantis'
        self.package.mkdir(parents=True)
        self.module = self.package / '__init__.py'
        self.original = (artifacts.REPO / 'sdk/python/mantis/__init__.py').read_bytes()
        self.module.write_bytes(self.original)
        self.wheel = self.root / 'disposable.whl'
        self.wheel.write_bytes(b'synthetic-artifact-identity')
        self.work = patch.object(artifacts, 'WORK', self.root, create=True)
        self.work.start()
        self.addCleanup(self.work.stop)
        self.qualifier = artifacts.Artifacts()
        self.qualifier.wheel = self.wheel

    def test_primary_and_secondary_failures_survive_exact_restoration(self):
        initiating = PermissionError('primary command failure')
        with patch.object(artifacts, 'command', side_effect=[initiating, OSError('restore probe failure')]):
            with self.assertRaises(PermissionError) as caught:
                self.qualifier.installed_job_sensitivity(self.python, self.wheel)
        self.assertIs(caught.exception, initiating)
        self.assertIn('restore probe failure', ''.join(traceback.format_exception(
            type(caught.exception), caught.exception, caught.exception.__traceback__)))
        self.assertEqual(self.module.read_bytes(), self.original)
        self.assertEqual(self.wheel.read_bytes(), b'synthetic-artifact-identity')

    def test_receipt_failure_does_not_replace_primary(self):
        initiating = RuntimeError('primary negative failure')
        with patch.object(artifacts, 'command', side_effect=[initiating, 'restored GREEN']), \
                patch.object(Path, 'write_text', side_effect=OSError('receipt failure')):
            with self.assertRaises(RuntimeError) as caught:
                self.qualifier.installed_job_sensitivity(self.python, self.wheel)
        self.assertIs(caught.exception, initiating)
        self.assertIn('receipt failure', ''.join(traceback.format_exception(
            type(caught.exception), caught.exception, caught.exception.__traceback__)))
        self.assertEqual(self.module.read_bytes(), self.original)

    def test_errno_primary_restore_and_receipt_failures_are_rendered(self):
        initiating = PermissionError(errno.EACCES, 'primary executable access failure')
        initiating.__cause__ = ValueError('preceding root cause')
        original_args = initiating.args
        calls = []
        def fail_command(*args, **kwargs):
            calls.append(args)
            if len(calls) == 1:
                raise initiating
            raise OSError(errno.EIO, 'restore probe failure')
        with patch.object(artifacts, 'command', side_effect=fail_command), \
                patch.object(Path, 'write_text', side_effect=OSError(errno.ENOSPC, 'receipt failure')):
            try:
                self.qualifier.installed_job_sensitivity(self.python, self.wheel)
            except PermissionError as caught:
                self.assertIs(caught, initiating)
                self.assertEqual(caught.errno, errno.EACCES)
                self.assertEqual(caught.args, original_args)
                rendered = ''.join(traceback.format_exception(type(caught), caught, caught.__traceback__))
                self.assertIn('preceding root cause', rendered)
                self.assertIn('fail_command', rendered)  # initiating traceback survives
                self.assertIn('installed Job restoration also failed', rendered)
                self.assertIn('restore probe failure', rendered)
                self.assertIn('receipt failure', rendered)
                self.assertIn('primary executable access failure', rendered)
            else:
                self.fail('primary exception was lost')
        self.assertEqual(self.module.read_bytes(), self.original)
        self.assertFalse((self.root / 'installed-job-sensitivity.json').exists())
        self.assertEqual(self.wheel.read_bytes(), b'synthetic-artifact-identity')

    def test_errno_missing_wire_restoration_failure_is_rendered(self):
        wire = self.package / 'mantis_pb2.py'
        wire.write_bytes(b'original wire')
        initiating = PermissionError(errno.EACCES, 'primary installed probe failure')
        restore = OSError(errno.EIO, 'wire restore failure')
        write_bytes = Path.write_bytes
        def fail_restore(path, data):
            if path == wire:
                raise restore
            return write_bytes(path, data)
        with patch.object(artifacts, 'install', return_value=self.python), \
                patch.object(artifacts, 'command', side_effect=[
                    'installed Job probe requires -I and a private venv',
                    'FileNotFoundError python_control.py', initiating]), \
                patch.object(Path, 'write_bytes', fail_restore):
            try:
                self.qualifier.test_installed_job_adapter_rejects_unisolated_and_absent_fixtures()
            except PermissionError as caught:
                self.assertIs(caught, initiating)
                self.assertEqual(caught.errno, errno.EACCES)
                rendered = ''.join(traceback.format_exception(type(caught), caught, caught.__traceback__))
                self.assertIn('installed wire restoration also failed', rendered)
                self.assertIn('wire restore failure', rendered)
                self.assertIn('primary installed probe failure', rendered)
            else:
                self.fail('primary exception was lost')
        self.assertFalse(wire.exists())  # Failed restoration is not relabeled GREEN.

    def test_unrelated_nonzero_is_not_the_intended_negative(self):
        with patch.object(artifacts, 'command', side_effect=['ImportError: absent fixture', 'restored GREEN']):
            with self.assertRaisesRegex(AssertionError, 'not the intended late-report assertion RED'):
                self.qualifier.installed_job_sensitivity(self.python, self.wheel)
        self.assertEqual(self.module.read_bytes(), self.original)

    def test_successful_negative_command_fails_closed(self):
        # The real command runner rejects a zero exit when nonzero was required.
        failure = AssertionError('command exit 0, expected 1')
        with patch.object(artifacts, 'command', side_effect=[failure, 'restored GREEN']):
            with self.assertRaises(AssertionError) as caught:
                self.qualifier.installed_job_sensitivity(self.python, self.wheel)
        self.assertIs(caught.exception, failure)
        self.assertEqual(self.module.read_bytes(), self.original)


if __name__ == '__main__':
    unittest.main(verbosity=2)
