"""Dedicated Python control CI stays native, read-only and unconditionally tested."""
from pathlib import Path
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[2]


class Workflow(unittest.TestCase):
    def test_native_read_only_control_jobs(self):
        workflow = yaml.load((ROOT / ".github/workflows/python-control.yml").read_text(),
                             Loader=yaml.BaseLoader)
        self.assertEqual(workflow["permissions"], {"contents": "read"})
        self.assertEqual(set(workflow["on"]), {"push", "pull_request", "workflow_dispatch"})
        self.assertIsNone(workflow["on"]["pull_request"] or None)
        jobs = workflow["jobs"]
        self.assertEqual(set(jobs), {"control"})
        job = jobs["control"]
        self.assertNotIn("permissions", job)
        self.assertNotIn("if", job)
        self.assertNotIn("continue-on-error", job)
        self.assertEqual(job["strategy"]["matrix"]["runner"],
                         ["ubuntu-24.04", "ubuntu-24.04-arm"])
        self.assertEqual(job["runs-on"], "${{ matrix.runner }}")
        steps = job["steps"]
        self.assertEqual(len(steps), 3)
        self.assertEqual(steps[0]["uses"], "actions/checkout@v4")
        self.assertEqual(steps[0]["with"], {"persist-credentials": "false"})
        self.assertEqual(steps[2]["run"],
                         "/usr/bin/python3 -B tests/qualification/python-control/run.py "
                         "--output build/python-control --negative-controls")
        for step in steps:
            self.assertNotIn("if", step)
            self.assertNotIn("continue-on-error", step)
        self.assertNotIn("pull_request_target", workflow["on"])


    def test_artifact_matrix_and_installed_evidence_are_preserved(self):
        workflow = yaml.load((ROOT / '.github/workflows/python-sdk.yml').read_text(),
                             Loader=yaml.BaseLoader)
        self.assertEqual(workflow['permissions'], {'contents': 'read'})
        self.assertEqual(set(workflow['on']), {'push', 'pull_request', 'workflow_dispatch'})
        self.assertNotIn('pull_request_target', workflow['on'])
        job = workflow['jobs']['artifacts']
        self.assertNotIn('if', job)
        self.assertNotIn('continue-on-error', job)
        self.assertEqual(job['strategy']['fail-fast'], 'false')
        self.assertEqual(job['strategy']['matrix'], {
            'python': ['3.10', '3.12', '3.14'], 'runner': ['ubuntu-24.04'],
            'machine': ['x86_64'], 'include': [{'python': '3.12',
                'runner': 'ubuntu-24.04-arm', 'machine': 'aarch64', 'suffix': ' (native ARM64)'}]})
        steps = job['steps']
        self.assertEqual(steps[0]['with'], {'persist-credentials': 'false'})
        qualify = next(step for step in steps if 'tests/sdk/artifacts_test.py' in step.get('run', ''))
        self.assertNotIn('if', qualify)
        self.assertNotIn('continue-on-error', qualify)
        self.assertEqual(qualify['run'],
                         'python -B tests/sdk/artifacts_test.py --work-dir "$RUNNER_TEMP/python-sdk-evidence" '
                         '--expected-machine ${{ matrix.machine }} --expected-python ${{ matrix.python }}')
        retained = steps[-1]
        self.assertEqual(retained['if'], 'always()')
        self.assertEqual(retained['uses'], 'actions/upload-artifact@v4')
        self.assertEqual(retained['with'].get('include-hidden-files', 'false'), 'false')
        self.assertEqual(retained['with']['if-no-files-found'], 'error')
        patterns = ('*.log', '*.json', '*.jsonl', 'command-regressions/', 'dist-a/*',
                    'dist-b/*', 'rebuilt/*', 'pip-sdist-wheel/*', 'control-before353/*',
                    'control-restored/*', 'frozen-inputs.zip')
        self.assertEqual(retained['with']['path'].splitlines(),
                         ['${{ runner.temp }}/python-sdk-evidence/' + pattern for pattern in patterns])


if __name__ == "__main__":
    unittest.main()
