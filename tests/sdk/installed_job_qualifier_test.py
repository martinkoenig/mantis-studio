"""Synthetic failure-unwind controls for the installed mutation qualifier.

These do not substitute for the real installed peer/mutant executions.
"""
import importlib.util
from pathlib import Path
import tempfile
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

    def test_primary_and_secondary_failures_survive_exact_restoration(self):
        initiating = PermissionError('primary command failure')
        with patch.object(artifacts, 'command', side_effect=[initiating, OSError('restore probe failure')]):
            with self.assertRaises(PermissionError) as caught:
                self.qualifier.installed_job_sensitivity(self.python, self.wheel)
        self.assertIs(caught.exception, initiating)
        self.assertIn('restore probe failure', str(caught.exception))
        self.assertEqual(self.module.read_bytes(), self.original)
        self.assertEqual(self.wheel.read_bytes(), b'synthetic-artifact-identity')

    def test_receipt_failure_does_not_replace_primary(self):
        initiating = RuntimeError('primary negative failure')
        with patch.object(artifacts, 'command', side_effect=[initiating, 'restored GREEN']), \
                patch.object(Path, 'write_text', side_effect=OSError('receipt failure')):
            with self.assertRaises(RuntimeError) as caught:
                self.qualifier.installed_job_sensitivity(self.python, self.wheel)
        self.assertIs(caught.exception, initiating)
        self.assertIn('receipt failure', str(caught.exception))
        self.assertEqual(self.module.read_bytes(), self.original)

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
