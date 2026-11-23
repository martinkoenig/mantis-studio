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


if __name__ == "__main__":
    unittest.main()
