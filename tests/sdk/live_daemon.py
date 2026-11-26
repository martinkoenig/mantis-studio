"""Qualify installed wheel and independently rebuilt sdist against local mantisd.

Requires an explicitly admitted, matching native runtime (bin/plugins/recipes).
This does not build native code, grant TEST custody or qualify the runtime's source.
Use existing artifact tooling and the existing bounded command-tree supervisor.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import sys
import zipfile

import artifacts_test as artifacts

ROOT = Path(__file__).resolve().parents[2]


def load_capture():
    path = ROOT / 'tests/qualification/python-control/run.py'
    spec = importlib.util.spec_from_file_location('control_capture', path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def virtual_fixture(runtime, work):
    """Copy only the existing acceptance workflow's three non-hardware plugins."""
    selected = {'org.mantis.virtual-scanner', 'org.mantis.example-points', 'org.mantis.ply'}
    plugins = work / 'plugins'
    plugins.mkdir()
    records = {}
    for manifest in sorted((runtime / 'plugins').glob('*.json')):
        entry = json.loads(manifest.read_text())
        if entry['id'] not in selected:
            continue
        assert entry['id'] not in records, 'duplicate virtual fixture manifest'
        library = runtime / 'plugins' / entry['library']
        assert (not manifest.is_symlink() and library.parent == runtime / 'plugins'
                and not library.is_symlink() and library.is_file()), 'unsafe/missing plugin library'
        shutil.copyfile(manifest, plugins / manifest.name)
        shutil.copyfile(library, plugins / library.name)
        records[entry['id']] = {'manifest': artifacts.sha(manifest.read_bytes()),
                                'filename': library.name,
                                'library': artifacts.sha(library.read_bytes())}
    assert set(records) == selected, 'missing virtual acceptance plugin'
    recipes = work / 'recipes'
    recipes.mkdir()
    source = runtime / 'recipes/example.json'
    assert source.read_bytes() == (ROOT / 'recipes/example.json').read_bytes(), 'runtime recipe mismatch'
    shutil.copyfile(source, recipes / source.name)
    return plugins, recipes, records


def prepare(work):
    """Reuse the artifact qualifier's checked build/install functions, not suites."""
    artifacts.WORK = work
    work.mkdir()
    (work / 'tmp').mkdir()
    identity = artifacts.execution_identity(platform.machine(), '.'.join(map(str, sys.version_info[:2])))
    (work / 'execution.json').write_text(json.dumps(identity, indent=2) + '\n')
    from protoc import PROTOC_EXE
    artifacts.command([PROTOC_EXE, '-I' + str(artifacts.SCHEMA.parent),
                       '--descriptor_set_out=' + str(work / 'canonical.pb'), artifacts.SCHEMA])
    source = artifacts.copy_source('source')
    artifacts.build(source, work / 'dist', isolated=True)
    wheel, = (work / 'dist').glob('*.whl')
    sdist, = (work / 'dist').glob('*.tar.gz')
    rebuilt_source = artifacts.unpack(sdist, 'independent-sdist')
    source.parents[1].rename(work / 'original-source-unavailable')
    artifacts.build(rebuilt_source, work / 'rebuilt', kind='wheel', isolated=True)
    rebuilt, = (work / 'rebuilt').glob('*.whl')
    artifacts.verify_wheel(wheel)
    artifacts.verify_wheel(rebuilt)
    assert wheel.read_bytes() == rebuilt.read_bytes(), 'sdist rebuilt wheel differs'
    return [('wheel', wheel), ('rebuilt-sdist', rebuilt)]


def require_injected_failure(status, text):
    assert status == 1, 'injected daemon failure did not exit exactly 1'
    records = [json.loads(line) for line in text.splitlines() if line.startswith('{')]
    failure, = [r for r in records if r.get('phase') == 'installed-live-daemon-injected-failure']
    assert failure == {'phase': 'installed-live-daemon-injected-failure',
                       'successful': False, 'retired': True}, 'unconfirmed injected retirement'
    assert 'InjectedFailure: Injected installed-daemon post-Job failure' in text
    assert 'live-daemon cleanup failed' not in text, 'cleanup failed during sensitivity control'
    return failure


def require_invalid_export(status, text):
    assert status == 1, 'invalid export did not exit exactly 1'
    records = [json.loads(line) for line in text.splitlines() if line.startswith('{')]
    failure, = [r for r in records if r.get('phase') == 'installed-live-daemon-invalid-export']
    assert failure == {'phase': 'installed-live-daemon-invalid-export',
                       'successful': False, 'retired': True}, 'unconfirmed invalid-export retirement'
    assert 'ExportValidationError: PLY vertex count mismatch' in text, 'wrong invalid-export error'
    assert 'live-daemon cleanup failed' not in text, 'cleanup failed during invalid-export control'
    return failure


def qualify(runtime, work):
    work.mkdir(mode=0o700, parents=False, exist_ok=False)
    capture = load_capture()
    budget = capture.OutputBudget(work, capture.MAX_OUTPUT)
    daemon, host = runtime / 'bin/mantisd', runtime / 'bin/mantis-plugin-host'
    binaries = {name: capture.digest(path) for name, path in (('daemon', daemon), ('host', host))}
    plugins, recipes, plugin_identity = virtual_fixture(runtime, work)
    wheels = prepare(work / 'package')
    results = []
    sensitivity = invalid_export = None
    for label, wheel in wheels:
        for protobuf in ('3.20.0', '6.33.5'):
            name = label + '-' + protobuf
            python = artifacts.install(wheel, name, protobuf)
            with zipfile.ZipFile(wheel) as archive:
                expected = {'wheel': artifacts.sha(wheel.read_bytes()),
                            'client': artifacts.sha(archive.read('mantis/__init__.py')),
                            'wire': artifacts.sha(archive.read('mantis/mantis_pb2.py')),
                            'schema': artifacts.sha(artifacts.SCHEMA.read_bytes()),
                            'plugins': {r['filename']: r['library'] for r in plugin_identity.values()}}
            identity = work / (name + '-expected.json')
            identity.write_text(json.dumps(expected) + '\n')
            assert capture.digest(daemon) == binaries['daemon'] and capture.digest(host) == binaries['host']
            argv = [str(python), '-I', '-B', str(Path(__file__).with_name('live_daemon_probe.py')),
                    '--daemon', str(daemon), '--plugin-host', str(host), '--plugins', str(plugins),
                    '--recipes', str(recipes), '--state', str(work / (name + '-state')),
                    '--identity', str(identity)]
            log = work / (name + '.log')
            if not results:
                # Same installed consumer and actual daemon; fail only after a
                # completed Job, prove exception cleanup, then restore normal run.
                negative_argv = list(argv)
                negative_argv[negative_argv.index('--state') + 1] = str(work / (name + '-negative-state'))
                negative_log = work / (name + '-negative.log')
                status = capture.capture_command(negative_argv + ['--fail-after-job'], negative_log, budget, cwd=work)
                sensitivity = {'exit': status, 'result': require_injected_failure(status, negative_log.read_text()),
                               'log_sha256': capture.digest(negative_log)}
                negative_argv[negative_argv.index('--state') + 1] = str(work / (name + '-invalid-export-state'))
                export_log = work / (name + '-invalid-export.log')
                status = capture.capture_command(negative_argv + ['--truncate-export'], export_log, budget, cwd=work)
                invalid_export = {'exit': status, 'result': require_invalid_export(status, export_log.read_text()),
                                  'log_sha256': capture.digest(export_log)}
            exit_code = capture.capture_command(argv, log, budget, cwd=work)
            assert exit_code == 0, 'installed real-daemon consumer failed; see private bounded log'
            records = [json.loads(line) for line in log.read_text().splitlines() if line.startswith('{')]
            result, = [r for r in records if r.get('phase') == 'installed-live-daemon']
            assert result['successful'] and result['retired'] and result['wheel_sha256'] == expected['wheel']
            assert result['daemon_sha256'] == binaries['daemon']
            results.append({'consumer': name, 'exit': exit_code, 'result': result,
                            'log_sha256': capture.digest(log)})
            (work / 'results.json').write_text(json.dumps({'runtime_binaries': binaries,
                'plugins': plugin_identity, 'injected_failure': sensitivity,
                'invalid_export': invalid_export, 'consumers': results}, indent=2) + '\n')
            budget.check()
    assert capture.digest(daemon) == binaries['daemon'] and capture.digest(host) == binaries['host']
    print('PASS: four installed local-mantisd virtual consumers; not hardware/platform/release acceptance')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', required=True, type=Path)
    parser.add_argument('--work-dir', required=True, type=Path)
    args = parser.parse_args()
    previous = os.environ.get('PIP_NO_CACHE_DIR')
    os.environ['PIP_NO_CACHE_DIR'] = '1'
    try:
        qualify(args.runtime.resolve(strict=True), args.work_dir.resolve())
    finally:
        if previous is None:
            os.environ.pop('PIP_NO_CACHE_DIR', None)
        else:
            os.environ['PIP_NO_CACHE_DIR'] = previous


if __name__ == '__main__':
    main()
