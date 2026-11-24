"""Selected canonical Job/control cases against an isolated installed SDK.

Fixtures are loaded by explicit filename, never by adding checkout/build paths.
Run only as ``venv/bin/python -I -B installed_job_probe.py``.
"""
import argparse
import hashlib
import importlib.util
import inspect
import json
from pathlib import Path
import sys
import threading
import unittest


JOBS = (
    'test_completed_and_pending_missing_transitions',
    'test_report_uses_remaining_budget_and_preserves_json_categories',
    'test_header_and_body_trickles_expire_without_budget_renewal',
    'test_late_decoded_terminal_success_is_rejected_without_report_rpc',
    'test_expiry_in_report_io_and_json_handling',
    'test_sleep_is_clipped_and_no_poll_after_expiry',
    'test_failed_cancelled_diagnostics_and_no_report_fetch',
    'test_success_after_job_transport_failure_has_no_global_deadline',
    'test_custom_snapshot_and_call_shapes_and_late_completion',
    'test_invalid_and_nonpositive_timeouts_are_inert',
)
CONTROLS = (
    'test_daemon_application_error_retains_all_fields',
    'test_peer_exception_is_propagated',
    'test_assertion_unwind_releases_peer_before_join',
    'test_transport_failure_survives_socket_close_failure',
    'test_close_failure_is_not_hidden_after_success_or_protocol_failure',
)
SENSITIVITY = 'test_expiry_in_report_io_and_json_handling'


def load_fixture(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ImportError(f'cannot load fixture: {path}')
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def installed_origins(mantis, wire):
    prefix = Path(sys.prefix).resolve()
    paths = {}
    for name, filename in (
        ('mantis', mantis.__file__), ('wire', wire.__file__),
        ('Client', inspect.getfile(mantis.Client)),
        ('Job', inspect.getfile(mantis.Job)),
        ('Job.wait', mantis.Job.wait.__code__.co_filename),
        ('Job.wait_report', mantis.Job.wait_report.__code__.co_filename),
    ):
        path = Path(filename).resolve()
        if not path.is_relative_to(prefix) or 'site-packages' not in path.parts:
            raise AssertionError(f'not an installed SDK origin: {name}: {path}')
        paths[name] = {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    if mantis.wire is not wire or sys.modules['mantis'] is not mantis:
        raise AssertionError('fixtures do not share the installed SDK/wire objects')
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sensitivity', action='store_true', help='run the report-budget oracle only')
    args = parser.parse_args()
    if not sys.flags.isolated or sys.prefix == sys.base_prefix:
        raise RuntimeError('installed Job probe requires -I and a private venv')
    before_path = list(sys.path)
    before_threads = set(threading.enumerate())
    fixtures = Path(__file__).resolve().parents[1] / 'unit'
    # The fixture consumes this explicit mode instead of a build-root argument.
    sys.argv = [sys.argv[0], '--installed-sdk']
    control = load_fixture('python_control', fixtures / 'python_control.py')
    if sys.path != before_path:
        raise AssertionError('installed fixture must not insert a build/python SDK path')
    job = load_fixture('python_job', fixtures / 'python_job.py')
    origins = installed_origins(job.mantis, job.wire)
    if control.mantis is not job.mantis or control.wire is not job.wire:
        raise AssertionError('Job/control fixtures resolved different SDK objects')
    print(json.dumps({'phase': 'installed-job-origins', 'python': sys.version,
                      'prefix': sys.prefix, 'origins': origins,
                      'fixtures': {m.__name__: {'path': m.__file__,
                          'sha256': hashlib.sha256(Path(inspect.getfile(m)).read_bytes()).hexdigest()}
                          for m in (control, job)}}, sort_keys=True), flush=True)
    suite = unittest.TestSuite(job.Jobs(name) for name in ((SENSITIVITY,) if args.sensitivity else JOBS))
    if not args.sensitivity:
        suite.addTests(control.Control(name) for name in CONTROLS)

        class UnknownOutcome(unittest.TestCase):
            def runTest(self):
                with job.exchanges(self, [job.snapshot()], budget=0.01) as run:
                    with self.assertRaisesRegex(TimeoutError, 'outcome may be unknown'):
                        run.job.wait(0.01)
                    # Canonical peer rejects non-snapshot commands and duplicate
                    # connections, including implicit cancel/retry on expiry.
                    self.assertEqual(len(run.connects), 1)
        suite.addTest(UnknownOutcome())
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    # Existing cases also assert owned peer joins and socket closure. Do not let
    # unittest's captured primary failures hide an additional leaked peer thread.
    leaked = [t.name for t in threading.enumerate() if t not in before_threads]
    if sys.path != before_path or installed_origins(job.mantis, job.wire) != origins or leaked:
        raise AssertionError(f'installed probe state not restored; leaked threads: {leaked}')
    print(json.dumps({'phase': 'installed-job-result', 'tests': result.testsRun,
                      'successful': result.wasSuccessful(), 'leaked_threads': leaked}), flush=True)
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
