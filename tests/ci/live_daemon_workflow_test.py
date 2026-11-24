"""LIGHT actual-shell wiring controls; no native product/tool installation runs."""
import copy

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]
NAME = 'Qualify installed SDK against real local mantisd'


def require_wiring(workflow):
    jobs = workflow['jobs']
    assert set(jobs) == {'linux', 'sanitizers'}, 'native job inventory changed'
    matrix = jobs['linux']['strategy']['matrix']
    assert matrix == {'runner': ['ubuntu-24.04', 'ubuntu-24.04-arm'], 'studio': ['ON', 'OFF']}
    for name, job in jobs.items():
        smoke, = [step for step in job['steps'] if step.get('name') == NAME]
        assert not any(key in smoke for key in ('if', 'continue-on-error')), 'daemon smoke is bypassable'
        runtime = 'build/ci' if name == 'linux' else 'build/sanitizers'
        assert smoke['run'] == ('build/live75-tools/bin/python -B tests/sdk/live_daemon.py '
                                 '--runtime ' + runtime + ' --work-dir build/live75-consumers'), 'wrong daemon command'
        if name == 'sanitizers':
            assert smoke['env'] == {'ASAN_OPTIONS': 'detect_leaks=1', 'UBSAN_OPTIONS': 'halt_on_error=1'}
        tests = [step for step in job['steps'] if step.get('run', '').startswith('ctest ')]
        assert len(tests) == 1 and tests[0]['run'] == f'ctest --test-dir {runtime} --output-on-failure'
        retain, = [step for step in job['steps'] if step.get('name') == 'Retain installed daemon results']
        assert retain['if'] == 'always()' and retain['with']['if-no-files-found'] == 'error'
        assert retain['with']['path'] == 'build/live75-consumers/results.json'


class Wiring(unittest.TestCase):
    def setUp(self):
        self.workflow = yaml.load((ROOT / '.github/workflows/build.yml').read_text(), Loader=yaml.BaseLoader)

    def test_all_five_existing_native_profiles_keep_full_tests_and_real_smoke(self):
        require_wiring(self.workflow)
        # Existing other12 jobs are preserved, not replaced by these native steps.
        expected = {'python-sdk.yml': 6, 'python-control.yml': 2,
                    'standalone-plugin-sdk.yml': 2, 'toolchain-provenance.yml': 2}
        for filename in expected:
            self.assertTrue((ROOT / '.github/workflows' / filename).is_file())

    def test_actual_shell_argv_and_failed_smoke_propagation(self):
        for name, job in self.workflow['jobs'].items():
            step, = [s for s in job['steps'] if s.get('name') == NAME]
            for status in (0, 19):
                with self.subTest(job=name, status=status), tempfile.TemporaryDirectory(prefix='live75-wiring-') as directory:
                    root = Path(directory)
                    python = root / 'build/live75-tools/bin/python'
                    python.parent.mkdir(parents=True)
                    python.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$TRACE"\nexit "$FIXTURE_EXIT"\n')
                    python.chmod(0o700)
                    trace = root / 'argv'
                    env = dict(os.environ, TRACE=str(trace), FIXTURE_EXIT=str(status))
                    completed = subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', step['run']],
                                               cwd=root, env=env, capture_output=True, timeout=5)
                    self.assertEqual(completed.returncode, status)
                    self.assertEqual(trace.read_text().splitlines(), ['-B', 'tests/sdk/live_daemon.py',
                        '--runtime', 'build/ci' if name == 'linux' else 'build/sanitizers',
                        '--work-dir', 'build/live75-consumers'])

    def test_missing_real_smoke_is_rejected_and_restoration_passes(self):
        broken = copy.deepcopy(self.workflow)
        broken['jobs']['linux']['steps'] = [s for s in broken['jobs']['linux']['steps'] if s.get('name') != NAME]
        with self.assertRaises(ValueError):
            require_wiring(broken)
        require_wiring(self.workflow)

    def test_swallowed_failure_is_rejected_and_restoration_passes(self):
        broken = copy.deepcopy(self.workflow)
        smoke, = [s for s in broken['jobs']['linux']['steps'] if s.get('name') == NAME]
        smoke['run'] += ' || true'
        with self.assertRaisesRegex(AssertionError, 'wrong daemon command'):
            require_wiring(broken)
        require_wiring(self.workflow)

    def test_sanitizer_disable_is_rejected_and_restoration_passes(self):
        broken = copy.deepcopy(self.workflow)
        smoke, = [s for s in broken['jobs']['sanitizers']['steps'] if s.get('name') == NAME]
        smoke['env']['ASAN_OPTIONS'] = 'detect_leaks=0'
        with self.assertRaises(AssertionError):
            require_wiring(broken)
        require_wiring(self.workflow)


if __name__ == '__main__':
    unittest.main(verbosity=2)
