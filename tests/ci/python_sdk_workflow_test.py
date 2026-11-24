"""Artifact workflow wiring, not hosted-interpreter qualification.

Uses the same YAML BaseLoader convention as compiler_cache_test.py. The shell
fixture executes the actual qualifier command but substitutes only the python
launcher: it proves argv/work-directory and failure propagation, not SDK behavior.
The unchanged artifacts_test.py must still execute on every hosted interpreter.
"""
import copy
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / '.github/workflows/python-sdk.yml'
RUNTIMES = ['3.10', '3.11', '3.12', '3.13', '3.14']
TOOLS = {'pip==25.1.1', 'build==1.2.2.post1', 'setuptools==75.8.2',
         'wheel==0.45.1', 'protoc-wheel-0==3.20.1', 'protobuf==3.20.0',
         'PyYAML==6.0.3'}
EVIDENCE = ['*.log', '*.json', '*.jsonl', 'command-regressions/',
            'dist-a/*', 'dist-b/*', 'rebuilt/*']


def require(predicate, diagnostic):
    if not predicate:
        raise AssertionError(diagnostic)


def validate(workflow):
    """Fail closed on selection, overrides, shortened gates and lost evidence."""
    require(workflow['permissions'] == {'contents': 'read'}, 'read-only permissions')
    require(set(workflow['on']) == {'pull_request', 'push', 'workflow_dispatch'}, 'events')
    require(workflow['on']['pull_request'] == '', 'unfiltered PR event')
    require(workflow['on']['push'] == {'branches': ['develop', 'main', 'integration/**']},
            'push branches')
    require(set(workflow['jobs']) == {'artifacts'}, 'single artifact job')
    job = workflow['jobs']['artifacts']
    require(job['strategy'] == {'fail-fast': 'false', 'matrix': {'python': RUNTIMES}},
            'complete runtime matrix without exclusions')
    require(job['runs-on'] == 'ubuntu-24.04', 'isolated hosted Linux x64 runner')
    require(job['name'] == 'python-artifacts (${{ matrix.python }})', 'runtime job identity')
    # No alternate interpreter, source path, shell defaults or failure suppression.
    for scope in (workflow, job):
        require(not ({'if', 'env', 'defaults', 'container', 'continue-on-error'} & set(scope)),
                'unconditional unmodified job context')
    steps = job['steps']
    require(len(steps) == 6, 'checkout/setup/tools/contract/qualifier/evidence sequence')
    checkout, setup, tools, contract, qualify, upload = steps
    require(checkout == {'uses': 'actions/checkout@v4',
                         'with': {'persist-credentials': 'false'}}, 'credential-free checkout')
    require(setup == {'uses': 'actions/setup-python@v5',
                      'with': {'python-version': '${{ matrix.python }}'}},
            'matrix-selected interpreter')
    for step in (tools, contract, qualify):
        require(set(step) <= {'name', 'env', 'run'}, 'unconditional fail-closed execution')
    require(tools.get('env') == {'PIP_ONLY_BINARY': ':all:'}, 'binary-only tools')
    argv = shlex.split(tools['run'])
    require(argv[:4] == ['python', '-m', 'pip', 'install'] and
            len(argv[4:]) == len(TOOLS) and set(argv[4:]) == TOOLS, 'exact pinned tools')
    require(contract.get('env', {}) == {} and contract['run'] ==
            'python -B tests/ci/python_sdk_workflow_test.py', 'unconditional workflow contract')
    require(qualify.get('env', {}) == {} and qualify['run'] ==
            'python -B tests/sdk/artifacts_test.py --work-dir "$RUNNER_TEMP/python-sdk-evidence"',
            'full qualifier without test selection or failure suppression')
    require(set(upload) == {'name', 'if', 'uses', 'with'} and
            upload['if'] == 'always()' and upload['uses'] == 'actions/upload-artifact@v4',
            'evidence on success and failure')
    require(upload['with'] == {
        'name': 'python-sdk-${{ matrix.python }}',
        'path': ''.join('${{ runner.temp }}/python-sdk-evidence/' + path + '\n'
                        for path in EVIDENCE),
        'if-no-files-found': 'error'}, 'complete runtime-separated evidence with missing-file error')
    return job


class WorkflowContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = WORKFLOW.read_bytes()
        cls.workflow = yaml.load(cls.original, Loader=yaml.BaseLoader)

    def test_current_workflow(self):
        validate(self.workflow)

    def test_real_shell_dispatch_preserves_full_argv_and_failure(self):
        job = validate(self.workflow)
        for runtime in job['strategy']['matrix']['python']:
            # Setup's matrix expression is checked above; this launcher is a
            # dispatch fixture, deliberately NOT an installed-runtime substitute.
            with self.subTest(runtime=runtime), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                launcher = root / 'python'
                receipt = root / 'argv'
                launcher.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$ARGV_RECEIPT"\n'
                                    'exit "$FIXTURE_EXIT"\n')
                launcher.chmod(0o755)
                runner_temp = root / ('runner space ' + runtime)
                for code in (0, 29):
                    env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                               RUNNER_TEMP=str(runner_temp), ARGV_RECEIPT=str(receipt),
                               FIXTURE_EXIT=str(code))
                    result = subprocess.run(['bash', '--noprofile', '--norc', '-e', '-o',
                                             'pipefail', '-c', job['steps'][4]['run']],
                                            env=env, cwd=ROOT, capture_output=True, timeout=10)
                    self.assertEqual(result.returncode, code, result.stderr)
                    self.assertEqual(receipt.read_text().splitlines(),
                                     ['-B', 'tests/sdk/artifacts_test.py', '--work-dir',
                                      str(runner_temp / 'python-sdk-evidence')])

    def test_wrong_wiring_is_rejected_and_original_is_unchanged(self):
        # Independently selected realistic regressions, not expected token counts.
        mutations = [
            ('omit 3.11', 'complete runtime matrix',
             lambda w: w['jobs']['artifacts']['strategy']['matrix']['python'].remove('3.11')),
            ('omit 3.13', 'complete runtime matrix',
             lambda w: w['jobs']['artifacts']['strategy']['matrix']['python'].remove('3.13')),
            ('fixed interpreter', 'matrix-selected interpreter',
             lambda w: w['jobs']['artifacts']['steps'][1]['with'].update({'python-version': '3.12'})),
            ('conditional qualifier', 'unconditional fail-closed',
             lambda w: w['jobs']['artifacts']['steps'][4].update({'if': "matrix.python != '3.13'"})),
            ('shortened qualifier', 'full qualifier',
             lambda w: w['jobs']['artifacts']['steps'][4].update({'run':
                 'python -B tests/sdk/artifacts_test.py --work-dir "$RUNNER_TEMP/python-sdk-evidence" -k archives'})),
            ('swallowed failure', 'unconditional fail-closed',
             lambda w: w['jobs']['artifacts']['steps'][4].update({'continue-on-error': 'true'})),
            ('source import override', 'unconditional unmodified job',
             lambda w: w['jobs']['artifacts'].update({'env': {'PYTHONPATH': 'sdk/python'}})),
            ('source builds', 'binary-only tools',
             lambda w: w['jobs']['artifacts']['steps'][2]['env'].update({'PIP_ONLY_BINARY': ''})),
            ('unpinned tools', 'exact pinned tools',
             lambda w: w['jobs']['artifacts']['steps'][2].update({'run':
                 w['jobs']['artifacts']['steps'][2]['run'].replace('protobuf==3.20.0', 'protobuf')})),
            ('credential persistence', 'credential-free checkout',
             lambda w: w['jobs']['artifacts']['steps'][0]['with'].update({'persist-credentials': 'true'})),
            ('write authority', 'read-only permissions',
             lambda w: w['permissions'].update({'contents': 'write'})),
            ('success-only upload', 'evidence on success and failure',
             lambda w: w['jobs']['artifacts']['steps'][5].update({'if': 'success()'})),
            ('missing cleanup receipts', 'complete runtime-separated evidence',
             lambda w: w['jobs']['artifacts']['steps'][5]['with'].update({'path':
                 w['jobs']['artifacts']['steps'][5]['with']['path'].replace(
                     '${{ runner.temp }}/python-sdk-evidence/command-regressions/\n', '')})),
            ('missing evidence ignored', 'complete runtime-separated evidence',
             lambda w: w['jobs']['artifacts']['steps'][5]['with'].update({'if-no-files-found': 'warn'})),
        ]
        validate(self.workflow)
        for name, diagnostic, mutate in mutations:
            with self.subTest(mutation=name):
                wrong = copy.deepcopy(self.workflow)
                mutate(wrong)
                with self.assertRaisesRegex(AssertionError, diagnostic) as rejected:
                    validate(wrong)
                print(f'REJECTED {name}: {rejected.exception}', flush=True)
                validate(self.workflow)
        self.assertEqual(WORKFLOW.read_bytes(), self.original)
        print('Original workflow bytes unchanged; restored contract GREEN', flush=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
