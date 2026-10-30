#!/usr/bin/env python3
"""Synthetic negative controls for the qualification evidence policy."""
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools'))
import verify_studio_sanitizers as verifier


class PolicyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.files = {}
        self.prefix = '/isolated/qt/6.9.2/gcc_64'
        self.config = {'cache': 'cache.txt', 'compiler_version': 'compiler.txt',
                       'compile_commands': 'commands.json', 'qt_prefix': self.prefix,
                       'qt_version': '6.9.2', 'executables': [{'symbols': 'symbols.txt',
                       'sections': 'sections.txt', 'link_command': 'link.txt', 'linkage': 'ldd.txt'}]}
        cache = {'CMAKE_BUILD_TYPE:STRING': 'Debug', 'CMAKE_CXX_COMPILER:FILEPATH': '/usr/bin/g++',
                 'MANTIS_BUILD_STUDIO:BOOL': 'ON', 'MANTIS_SANITIZE:BOOL': 'ON',
                 'Python3_EXECUTABLE:FILEPATH': '/usr/bin/python3'}
        cache.update({f'Qt6{module}_DIR:PATH': f'{self.prefix}/lib/cmake/Qt6{module}'
                      for module in ('Core', 'Gui', 'Qml', 'Quick', 'QuickControls2', 'Concurrent', 'Test')})
        self.write('cache.txt', '\n'.join(f'{k}={v}' for k, v in cache.items())+'\n')
        self.write('compiler.txt', '13.3.0\n')
        self.write('commands.json', json.dumps([{'command': '/usr/bin/g++ -g -fsanitize=address,undefined -c apps/studio/bridge.cpp'},
                                               {'command': '/usr/bin/g++ -g -fsanitize=address,undefined -c tests/unit/studio_scan.cpp'}]))
        self.write('symbols.txt', 'U __asan_init\nU __ubsan_handle_add_overflow\n')
        self.write('sections.txt', '.debug_info\n')
        self.write('link.txt', '/usr/bin/g++ -fsanitize=address,undefined -o mantis-studio\n')
        self.write('ldd.txt', f'libQt6Quick.so.6 => {self.prefix}/lib/libQt6Quick.so.6.9.2 (0x0)\nlibasan.so.8 => /lib/libasan.so.8\nlibubsan.so.1 => /lib/libubsan.so.1\n')
        self.env = dict(verifier.ENVIRONMENT)
        self.gate = {'receipt': 'receipt.json', 'inventory': 'inventory.json', 'log': 'ctest.log', 'junit': 'junit.xml', 'full': True}
        self.write('receipt.json', json.dumps({'exit': 0, 'environment': self.env, 'command': ['ctest', '--parallel', '1']}))
        self.write('inventory.json', json.dumps({'tests': [{'name': 'a'}, {'name': 'b'}]}))
        self.write('ctest.log', '1/2 Test #1: a .... Passed\n2/2 Test #2: b .... Passed\n100% tests passed, 0 tests failed out of 2\n')
        self.write('junit.xml', '<testsuite><testcase name="a" status="run"/><testcase name="b" status="run"/></testsuite>')

    def write(self, name, text):
        path = self.root/name
        path.write_text(text)
        self.files[name] = verifier.digest(path)

    def evidence(self):
        self.write('manifest.json', json.dumps({'files': {k: v for k, v in self.files.items() if k != 'manifest.json'}}))
        return verifier.Evidence(self.root, verifier.digest(self.root/'manifest.json'))

    def configuration(self):
        verifier.check_configuration(self.evidence(), self.config, True, True)

    def gate_check(self):
        verifier.check_ctest(self.evidence(), self.gate, {'a', 'b'})

    def test_valid_synthetic_configuration(self):
        self.configuration()
        self.gate_check()

    def test_wrong_qt(self):
        self.config['qt_version'] = '6.4.2'
        self.assertRaises(verifier.InvalidEvidence, self.configuration)

    def test_wrong_compiler(self):
        self.write('compiler.txt', '15.2.0\n')
        self.assertRaises(verifier.InvalidEvidence, self.configuration)

    def test_studio_off(self):
        self.write('cache.txt', (self.root/'cache.txt').read_text().replace('MANTIS_BUILD_STUDIO:BOOL=ON', 'MANTIS_BUILD_STUDIO:BOOL=OFF'))
        self.assertRaises(verifier.InvalidEvidence, self.configuration)

    def test_missing_actual_instrumentation(self):
        for name in ('commands.json', 'symbols.txt', 'sections.txt', 'link.txt', 'ldd.txt'):
            with self.subTest(name=name):
                original = (self.root/name).read_text()
                self.write(name, '[]' if name.endswith('.json') else 'uninstrumented\n')
                self.assertRaises(verifier.InvalidEvidence, self.configuration)
                self.write(name, original)

    def test_leaks_disabled_and_overrides(self):
        for key, value in [('ASAN_OPTIONS', 'detect_leaks=0:halt_on_error=1'),
                           ('LSAN_OPTIONS', 'suppressions=hidden.txt'), ('LD_LIBRARY_PATH', '/system/qt')]:
            with self.subTest(key=key):
                env = dict(self.env, **{key: value})
                self.assertRaises(verifier.InvalidEvidence, verifier.check_environment, env)

    def test_filtered_full_suite(self):
        for flag in ('-R', '-E', '--tests-from-file', '--rerun-failed', '--stop-on-failure'):
            self.write('receipt.json', json.dumps({'exit': 0, 'environment': self.env,
                                                  'command': ['ctest', '--parallel', '1', flag, 'selection']}))
            self.assertRaises(verifier.InvalidEvidence, self.gate_check)

    def test_ctest_property_disables_leaks(self):
        for property_name in ('ENVIRONMENT', 'ENVIRONMENT_MODIFICATION'):
            value = 'ASAN_OPTIONS=detect_leaks=0' if property_name == 'ENVIRONMENT' else 'ASAN_OPTIONS=set:detect_leaks=0'
            self.write('inventory.json', json.dumps({'tests': [{'name': 'a', 'properties': [{'name': property_name, 'value': [value]}]}, {'name': 'b'}]}))
            self.assertRaises(verifier.InvalidEvidence, self.gate_check)

    def test_missing_results(self):
        self.write('junit.xml', '<testsuite/>')
        self.assertRaises(verifier.InvalidEvidence, self.gate_check)

    def test_missing_registered_test(self):
        self.write('inventory.json', json.dumps({'tests': [{'name': 'a'}]}))
        self.assertRaises(verifier.InvalidEvidence, self.gate_check)

    def test_skipped_or_failed_test(self):
        for tag in ('skipped', 'failure', 'error'):
            self.write('junit.xml', f'<testsuite><testcase name="a"><{tag}/></testcase><testcase name="b"/></testsuite>')
            self.assertRaises(verifier.InvalidEvidence, self.gate_check)

    def test_nonzero_test_exit(self):
        self.write('receipt.json', json.dumps({'exit': 8, 'environment': self.env, 'result': 'PASS'}))
        self.assertRaises(verifier.InvalidEvidence, self.gate_check)

    def test_missing_logs(self):
        self.write('ctest.log', '')
        self.assertRaises(verifier.InvalidEvidence, self.gate_check)

    def test_sanitizer_finding(self):
        self.write('ctest.log', 'ERROR: LeakSanitizer: detected memory leaks\n')
        self.assertRaises(verifier.InvalidEvidence, self.gate_check)

    def test_changed_or_manipulated_manifest(self):
        evidence = self.evidence()
        (self.root/'symbols.txt').write_text('tampered')
        self.assertRaises(verifier.InvalidEvidence, verifier.Evidence, self.root, verifier.digest(self.root/'manifest.json'))
        self.assertRaises(verifier.InvalidEvidence, verifier.Evidence, self.root, '0'*64)

    def test_unsafe_manifest_path(self):
        self.write('manifest.json', json.dumps({'files': {'../escape': '0'*64}}))
        self.assertRaises(verifier.InvalidEvidence, verifier.Evidence, self.root, verifier.digest(self.root/'manifest.json'))

    def test_missing_rendered_workload(self):
        self.write('workload.log', 'application exited cleanly\n')
        workload = {'receipt': 'receipt.json', 'log': 'workload.log', 'screenshot': 'missing.png'}
        self.assertRaises(verifier.InvalidEvidence, verifier.check_workload, self.evidence(), workload, self.prefix)

    def test_cli_invalid_evidence_returns_nonzero(self):
        self.write('qualification.json', json.dumps({'result': 'PASS'}))
        self.evidence()
        result = subprocess.run([sys.executable, str(Path(verifier.__file__)), str(self.root),
                                 '--expected-manifest-sha256', verifier.digest(self.root/'manifest.json'),
                                 '--expected-candidate-sha256', '0'*64], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Invalid qualification evidence:', result.stderr)

    def test_incomplete_qualification_rejects_pass_label(self):
        self.write('candidate-source.json', '{}')
        self.write('protected-before.json', json.dumps({'head': verifier.BASELINE, 'branch': 'ao/ui-m3d1-recovery/root', 'files': {}}))
        self.write('protected-after.json', (self.root/'protected-before.json').read_text())
        qualification = {'result': 'PASS', 'baseline_revision': verifier.BASELINE,
                         'candidate_source_manifest': 'candidate-source.json',
                         'baseline_source_manifest': 'candidate-source.json', 'protected_before': 'protected-before.json', 'protected_after': 'protected-after.json', 'gates': {}}
        self.write('qualification.json', json.dumps(qualification))
        self.evidence()
        self.assertRaises(verifier.InvalidEvidence, verifier.verify, self.root, verifier.digest(self.root/'manifest.json'), verifier.digest(self.root/'candidate-source.json'))
        self.write('protected-after.json', '{"changed":true}')
        self.evidence()
        self.assertRaises(verifier.InvalidEvidence, verifier.verify, self.root, verifier.digest(self.root/'manifest.json'), verifier.digest(self.root/'candidate-source.json'))
        self.assertRaises(verifier.InvalidEvidence, verifier.verify, self.root, verifier.digest(self.root/'manifest.json'), '0'*64)


class CompletePackageTests(unittest.TestCase):
    write = PolicyTests.write
    evidence = PolicyTests.evidence

    def setUp(self):
        PolicyTests.setUp(self)
        names = sorted(verifier.ROUTES | verifier.FOCUSED | {'studio-bridge-replay', 'boundaries'})
        self.write('inventory.json', json.dumps({'tests': [{'name': name} for name in names]}))
        self.write('ctest.log', ''.join(f'{i}/{len(names)} Test #{i}: {name} .... Passed\n' for i, name in enumerate(names, 1)) + f'100% tests passed, 0 tests failed out of {len(names)}\n')
        self.write('junit.xml', '<testsuite>' + ''.join(f'<testcase name="{name}" status="run"/>' for name in names) + '</testsuite>')
        self.write('candidate-source.json', json.dumps({'source.cpp': 'a' * 64}))
        self.write('protected-before.json', json.dumps({'head': verifier.BASELINE, 'branch': 'ao/ui-m3d1-recovery/root', 'files': {'source.cpp': {'sha256': 'a' * 64}}}))
        self.write('protected-after.json', (self.root/'protected-before.json').read_text())
        self.write('baseline-source.json', '{}')
        self.addCleanup(patch.stopall)
        patch.object(verifier, 'BASELINE_SOURCE_MANIFEST', verifier.digest(self.root/'baseline-source.json')).start()
        cache = (self.root/'cache.txt').read_text()
        self.write('normal-studio-cache.txt', cache.replace('MANTIS_SANITIZE:BOOL=ON', 'MANTIS_SANITIZE:BOOL=OFF'))
        self.write('normal-headless-cache.txt', (self.root/'normal-studio-cache.txt').read_text().replace('MANTIS_BUILD_STUDIO:BOOL=ON', 'MANTIS_BUILD_STUDIO:BOOL=OFF'))
        self.write('headless-sanitizers-cache.txt', cache.replace('MANTIS_BUILD_STUDIO:BOOL=ON', 'MANTIS_BUILD_STUDIO:BOOL=OFF'))
        self.write('headless-ldd.txt', 'libasan.so.8 => /lib/libasan.so.8\nlibubsan.so.1 => /lib/libubsan.so.1\n')
        self.write('cache.txt', cache + 'CMAKE_HOME_DIRECTORY:INTERNAL=/synthetic/baseline-source\n')
        self.write('candidate-cache.txt', cache + 'CMAKE_HOME_DIRECTORY:INTERNAL=/synthetic/candidate-source\n')
        configurations = {'baseline': dict(self.config), 'candidate': dict(self.config, cache='candidate-cache.txt')}
        git_fixtures = {}
        full_receipts = {}
        for variant in ('baseline', 'candidate'):
            git = {'head': verifier.BASELINE, 'git_dir': '/synthetic/' + variant + '-git',
                   'source': '/synthetic/' + variant + '-source'}
            git_fixtures[variant] = variant + '-git.json'
            self.write(git_fixtures[variant], json.dumps(git))
            receipt = json.loads((self.root/'receipt.json').read_text())
            receipt['environment'].update(GIT_DIR=git['git_dir'], GIT_WORK_TREE=git['source'], GIT_OPTIONAL_LOCKS='0')
            full_receipts[variant] = variant + '-full-receipt.json'
            self.write(full_receipts[variant], json.dumps(receipt))
        for name in ('normal-studio', 'normal-headless', 'headless-sanitizers'):
            configurations[name] = dict(self.config, cache=name+'-cache.txt')
        configurations['headless-sanitizers']['executables'] = [dict(self.config['executables'][0], linkage='headless-ldd.txt')]
        self.write('workload.log', f"Loading backend software\nFrame rendered with 'software' renderloop\ncalling init: {self.prefix}/lib/libQt6Core.so.6.9.2\n{self.prefix}/plugins/platforms/libqoffscreen.so\n{self.prefix}/qml/QtQuick/Controls\n")
        (self.root/'frame.png').write_bytes(b'\x89PNG\r\n\x1a\nsynthetic policy fixture')
        self.files['frame.png'] = verifier.digest(self.root/'frame.png')
        self.write('workload-receipt.json', json.dumps({'exit': 0, 'environment': self.env, 'command': ['/synthetic/bin/mantis-studio']}))
        self.workload = {'receipt': 'workload-receipt.json', 'log': 'workload.log', 'screenshot': 'frame.png'}
        gates = {name: dict(self.gate) for name in verifier.GATES}
        for name in ('baseline-repeat', 'candidate-repeat'):
            gates[name] = []
            for i in range(3):
                receipt = f'{name}-{i}-receipt.json'
                self.write(receipt, (self.root/'receipt.json').read_text())
                gates[name].append(dict(self.gate, receipt=receipt))
        for variant in ('baseline', 'candidate'):
            gates[variant+'-full']['receipt'] = full_receipts[variant]
        self.qualification = {'git_fixtures': git_fixtures, 'baseline_revision': verifier.BASELINE,
                              'baseline_source_manifest': 'baseline-source.json',
                              'candidate_source_manifest': 'candidate-source.json',
                              'protected_before': 'protected-before.json', 'protected_after': 'protected-after.json',
                              'gates': gates, 'configurations': configurations,
                              'workloads': {'baseline': self.workload, 'candidate': self.workload}}

    def package_check(self):
        self.write('qualification.json', json.dumps(self.qualification))
        self.evidence()
        return verifier.verify(self.root, verifier.digest(self.root/'manifest.json'), verifier.digest(self.root/'candidate-source.json'))

    def test_complete_synthetic_package(self):
        self.assertEqual(self.package_check()['result'], 'QUALIFIED')

    def test_reused_repetition_receipt(self):
        self.qualification['gates']['candidate-repeat'][1] = self.qualification['gates']['candidate-repeat'][0]
        self.assertRaises(verifier.InvalidEvidence, self.package_check)

    def test_summary_count_not_inventory(self):
        lines = (self.root/'ctest.log').read_text().splitlines()
        lines[-1] = '100% tests passed, 0 tests failed out of 2'
        self.write('ctest.log', '\n'.join(lines) + '\n')
        self.assertRaises(verifier.InvalidEvidence, self.package_check)

    def test_wrong_git_source_head(self):
        git = json.loads((self.root/'baseline-git.json').read_text())
        git['head'] = 'wrong'
        self.write('baseline-git.json', json.dumps(git))
        self.assertRaises(verifier.InvalidEvidence, self.package_check)

    def test_git_worktree_differs_from_build_source(self):
        git = json.loads((self.root/'candidate-git.json').read_text())
        git['source'] = '/different/source'
        self.write('candidate-git.json', json.dumps(git))
        self.assertRaises(verifier.InvalidEvidence, self.package_check)

    def test_missing_gate_with_rehashed_manifest(self):
        del self.qualification['gates']['candidate-full']
        self.assertRaises(verifier.InvalidEvidence, self.package_check)

    def test_snapshot_inconsistent_with_protected_hashes(self):
        self.write('candidate-source.json', json.dumps({'source.cpp': 'b' * 64}))
        self.assertRaises(verifier.InvalidEvidence, self.package_check)

    def test_mismatched_environments(self):
        self.qualification['configurations']['candidate'] = dict(self.config, qt_prefix='/different/qt')
        self.assertRaises(verifier.InvalidEvidence, self.package_check)

    def test_different_runtime_environments(self):
        receipt = json.loads((self.root/'candidate-full-receipt.json').read_text())
        receipt['environment']['QT_LOGGING_RULES'] = 'qt.quick.*=true'
        self.write('candidate-receipt.json', json.dumps(receipt))
        self.qualification['gates']['candidate-full']['receipt'] = 'candidate-receipt.json'
        self.assertRaises(verifier.InvalidEvidence, self.package_check)

    def test_missing_mandatory_test_with_rehashed_manifest(self):
        text = (self.root/'junit.xml').read_text().replace('<testcase name="studio-scan-qml" status="run"/>', '')
        self.write('junit.xml', text)
        self.assertRaises(verifier.InvalidEvidence, self.package_check)

    def test_changed_protected_state_with_rehashed_manifest(self):
        self.write('protected-after.json', '{}')
        self.assertRaises(verifier.InvalidEvidence, self.package_check)

    def test_workload_wrapper_disables_leaks(self):
        receipt = json.loads((self.root/'workload-receipt.json').read_text())
        receipt['command'] = ['env', 'ASAN_OPTIONS=detect_leaks=0', '/synthetic/bin/mantis-studio']
        self.write('workload-receipt.json', json.dumps(receipt))
        self.assertRaises(verifier.InvalidEvidence, self.package_check)

    def test_missing_qt_workload_with_rehashed_manifest(self):
        self.write('workload.log', 'Clean exit without rendering')
        self.assertRaises(verifier.InvalidEvidence, self.package_check)


if __name__ == '__main__':
    unittest.main()
