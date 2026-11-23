#!/usr/bin/env python3
"""Internal product install v1: real component staging and relocated execution.

Linux execution qualification, not an OS-package install or release acceptance.
Outputs are retained; a repeat gets a fresh directory. No host installation,
service, hardware, user-state migration or global environment changes occur.
"""
import argparse
import contextlib
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('sdk_custody', REPO / 'tests/sdk/qualify.py')
if spec is None or spec.loader is None:
    raise RuntimeError('Cannot load the existing SDK qualification custody helpers')
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


def require(value, diagnostic):
    if not value:
        raise RuntimeError(diagnostic)


def tool_path(name):
    result = shutil.which(name)
    if result is None:
        raise RuntimeError('Required qualification tool unavailable: ' + name)
    return Path(result).resolve()


def inventory(root):
    return {str(p.relative_to(root)): sdk.digest(p) for p in sorted(root.rglob('*')) if p.is_file()}


def failure_evidence(root, primary):
    try:
        (root / 'first-failure.json').write_text(json.dumps({
            'type': type(primary).__name__, 'diagnosis': str(primary)}, indent=2))
    except BaseException as secondary:
        primary.add_note('Failure evidence unavailable: ' + type(secondary).__name__)


class Evidence:
    def __init__(self, root, build):
        self.root, self.build = root, build
        self.records, self.children = [], []
        self.env = os.environ.copy()
        for key in list(self.env):
            if key.startswith(('MANTIS_', 'CMAKE_', 'QT_', 'QML_', 'LD_')) or key in {
                'CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'DESTDIR'}:
                self.env.pop(key)
        self.env.update(LC_ALL='C', CMAKE_BUILD_PARALLEL_LEVEL='1', CTEST_PARALLEL_LEVEL='1')
        self.libc = ctypes.CDLL(None, use_errno=True)
        self.previous = ctypes.c_int()
        require(self.libc.prctl(37, ctypes.byref(self.previous), 0, 0, 0) == 0, 'GET_CHILD_SUBREAPER failed')
        require(self.libc.prctl(36, 1, 0, 0, 0) == 0, 'SET_CHILD_SUBREAPER failed')
        self.identity = {'source': subprocess.check_output(['git', '-C', REPO, 'rev-parse', 'HEAD'], text=True).strip(),
                         'source_inputs': {p: sdk.digest(REPO / p) for p in subprocess.check_output(
                             ['git', '-C', REPO, 'ls-files', '--cached', '--others', '--exclude-standard', '-z'],
                             text=True).split('\0') if p and (REPO / p).is_file()},
                         'tools': {name: {'path': str(tool_path(name)), 'sha256': sdk.digest(tool_path(name))}
                                   for name in ('cmake', 'ctest', 'cc', 'c++', 'readelf', 'ldd')}}
        # Include new untracked candidate implementation/tests in dirty-source receipts.
        self.identity['candidate_inputs'] = {str(p.relative_to(REPO)): sdk.digest(p)
            for directory in ('cmake', 'tests/install', 'apps/mantisd') for p in (REPO / directory).rglob('*')
            if p.is_file() and '__pycache__' not in p.parts}
        self.identity['build_inputs'] = {name: sdk.digest(build / name) for name in
            ('CMakeCache.txt', 'compile_commands.json', 'generated/product_paths.hpp', 'generated/ProductInstallGuard.cmake',
             'generated/install-layout-v1.json')
            if (build / name).is_file()}
        self.identity['execution_environment'] = {name: self.env.get(name) for name in
            ('TMPDIR', 'ASAN_OPTIONS', 'UBSAN_OPTIONS', 'CTEST_PARALLEL_LEVEL', 'CMAKE_BUILD_PARALLEL_LEVEL')}
        self.identity.update(machine=os.uname().machine, system=sys.platform)
        self.save()

    def save(self):
        (self.root / 'receipts.json').write_text(json.dumps({'identity': self.identity, 'commands': self.records}, indent=2) + '\n')

    def start(self, label, args, *, env=None, executable=None):
        record = {'label': label, 'argv': list(map(str, args)), 'cwd': str(self.root), 'log': label + '.log'}
        self.records.append(record)
        with (self.root / record['log']).open('wb') as log:
            process = subprocess.Popen(record['argv'], executable=executable, cwd=self.root, env=env or self.env,
                                       stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        self.children.append((process, record))  # Own before any observation/assertion.
        record.update(pid=process.pid, birth=[p for p in sdk.processes() if p['pid'] == process.pid])
        image = Path(executable or record['argv'][0])
        if image.is_file():
            record['executable_sha256'] = sdk.digest(image)
            record['executable'] = str(image.resolve())
        self.save()
        return process, record

    def retire(self, process, record, primary=None):
        if record.get('retired'):
            return  # Released process-group IDs are not a continuing cleanup authority.
        try:
            remaining = [p for p in sdk.processes() if p['group'] == process.pid]
            unexpected_survivors = process.poll() is not None and bool(remaining)
            record['before_retirement'] = remaining
            if process.poll() is None or remaining:
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    with contextlib.suppress(ProcessLookupError):
                        os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=2)
                if [p for p in sdk.processes() if p['group'] == process.pid]:
                    with contextlib.suppress(ProcessLookupError):
                        os.killpg(process.pid, signal.SIGKILL)
            require(not sdk.Campaign.reap_group(process.pid), 'Owned descendants did not retire')
            record['exit'] = process.wait(timeout=2)
            record['after_retirement'] = [p for p in sdk.processes() if p['group'] == process.pid]
            record['retired'] = True
            self.save()
            if unexpected_survivors and primary is None:
                raise RuntimeError('Unexpected surviving owned subprocess group after leader exit')
        except BaseException as secondary:
            record['cleanup_error'] = type(secondary).__name__
            with contextlib.suppress(Exception):
                self.save()
            if primary is None:
                raise
            primary.add_note('Cleanup failure: ' + type(secondary).__name__)

    def run(self, label, args, *, expected=None, env=None, timeout=30):
        process, record = self.start(label, args, env=env)
        record['expected_diagnosis'], record['timeout_seconds'] = expected, timeout
        primary = None
        try:
            code = process.wait(timeout=timeout)
            text = (self.root / record['log']).read_text(errors='replace')
            if expected is None:
                require(code == 0, f'{label}: exit {code}; see {record["log"]}')
            else:
                require(code > 0 and re.search(expected, re.sub(r'\s+', ' ', text)),
                        f'{label}: intended failure not observed (exit {code}); see {record["log"]}')
            return text
        except BaseException as exc:
            primary = exc
            record['first_cause'] = type(exc).__name__ + ': ' + str(exc)
            raise
        finally:
            self.retire(process, record, primary)

    def close(self, primary=None):
        for process, record in reversed(self.children):
            self.retire(process, record, primary)
        if self.libc.prctl(36, self.previous.value, 0, 0, 0) != 0:
            if primary is None:
                raise RuntimeError('Restore subreaper failed')
            primary.add_note('Restore subreaper failed')

    def install(self, label, destination, component=None, destdir=None, expected=None, build=None):
        producer = build or self.build
        args = ['cmake', '--install', producer, '--prefix', destination]
        if component:
            args += ['--component', component]
        env = self.env.copy()
        if destdir:
            env['DESTDIR'] = str(destdir)
        result = self.run(label, args, env=env, expected=expected)
        record = self.records[-1]
        record['install_prefix'], record['destdir'], record['component'] = str(destination), str(destdir), component
        record['completed'] = record['exit'] == 0
        manifest = producer / ('install_manifest' + ('_' + component if component else '') + '.txt')
        if record['completed'] and manifest.exists():
            (self.root / (label + '-cmake-manifest.txt')).write_bytes(manifest.read_bytes())
        self.save()
        return result


def cache(build):
    values = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if '=' in line and not line.startswith(('#', '//')):
            key, value = line.split('=', 1)
            values[key.split(':')[0]] = value
    layout = json.loads((build / 'generated/install-layout-v1.json').read_text())
    require(layout['contract_version'] == 1 and layout['application_version'] == '0.2.0',
            'Unsupported internal install layout contract')
    values.update(layout['destinations'])
    return values


def checked_rpaths(dynamic, image, prefix):
    paths = []
    for value in re.findall(r'\((?:RPATH|RUNPATH)\).*\[(.*?)\]', dynamic):
        if not value:
            continue
        for entry in value.split(':'):
            origin = re.fullmatch(r'\$(?:ORIGIN|\{ORIGIN\})(/.*)?', entry)
            if origin is None:
                raise RuntimeError('Installed ELF has a nonrelocatable loader search path')
            destination = (image.parent / (origin.group(1) or '').lstrip('/')).resolve()
            require(destination.is_relative_to(prefix.resolve()), 'Installed ELF loader search path escapes its prefix')
            paths.append(entry)
    return paths


def elf_manifest(evidence, roles):
    owners, result = {}, {}
    for role, root in roles.items():
        entries = {}
        for relative, digest in inventory(root).items():
            require(relative not in owners, 'Cross-role file ownership collision: ' + relative)
            owners[relative] = role
            path = root / relative
            entry = {'sha256': digest, 'mode': oct(path.stat().st_mode & 0o777)}
            if path.read_bytes()[:4] == b'\x7fELF':
                label = role + '-' + path.name
                dynamic = evidence.run(label + '-dynamic', ['readelf', '-d', path])
                entry['rpaths'] = checked_rpaths(dynamic, path, root)
                closure = evidence.run(label + '-closure', ['ldd', path])
                require('not found' not in closure, 'Unresolved installed dependency: ' + relative)
                require(role == 'Studio' or not re.search(r'libQt[56]', closure), 'Qt dependency in headless/client role')
                entry['needed'] = re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic)
                entry['closure'] = {str(Path(p).resolve()): sdk.digest(Path(p).resolve())
                                    for p in re.findall(r'(?:=>\s+|^\s*)(/[^\s]+)', closure, re.M)}
                require(not any(str(evidence.build) in p or str(REPO) in p for p in entry['closure']),
                        'Installed closure resolved into source/build')
            entries[relative] = entry
        result[role] = entries
    (evidence.root / 'role-file-dependency-manifest-v1.json').write_text(json.dumps(
        {'contract_version': 1, 'application_version': '0.2.0', 'roles': result,
         'role_dependencies': {role: [] for role in roles},
         'scope': 'actual staged immutable files and resolved Linux ELF closure; not Debian solver metadata'}, indent=2) + '\n')
    return owners


def rpath_control(e, studio_root, paths):
    image = studio_root / paths['bin'] / 'mantis-studio'
    dynamic = (e.root / 'Studio-mantis-studio-dynamic.log').read_text()
    values = re.findall(r'\((?:RPATH|RUNPATH)\).*\[(.*?)\]', dynamic)
    if not values or not values[0]:
        return  # No runtime search path to mutate in this available Qt closure.
    original = image.read_bytes()
    anchor = values[0].encode()
    require(original.count(anchor) == 1, 'ELF dynamic-string mutation anchor ambiguous')
    directory = e.root / 'disposable absolute rpath'
    directory.mkdir()
    candidate = directory / image.name
    shutil.copy2(image, candidate)
    try:
        candidate.write_bytes(original.replace(anchor, b'/' + b'x' * (len(anchor) - 1)))
        wrong = e.run('absolute-rpath-actual-elf', ['readelf', '-d', candidate])
        try:
            checked_rpaths(wrong, candidate, directory)
        except RuntimeError as exc:
            require(str(exc) == 'Installed ELF has a nonrelocatable loader search path', 'Wrong RPATH failure cause')
        else:
            raise RuntimeError('Absolute installed ELF RPATH escaped the origin oracle')
    finally:
        candidate.write_bytes(original)
    require(sdk.digest(candidate) == sdk.digest(image), 'ELF RPATH mutation restoration mismatch')
    restored = e.run('restored-rpath-actual-elf', ['readelf', '-d', candidate])
    # The restored file is in a disposable flat directory; validate against its
    # original role prefix and location, not an invented directory interpretation.
    checked_rpaths(restored, image, studio_root)
    (e.root / 'rpath-restoration.json').write_text(json.dumps({
        'original': sdk.digest(image), 'restored': sdk.digest(candidate),
        'absolute_rpath_rejected': True}, indent=2))


class Daemon:
    def __init__(self, evidence, prefix, paths, label, extra=(), spoof_argv=False):
        self.e, self.prefix, self.paths, self.label = evidence, prefix, paths, label
        self.env = evidence.env.copy()
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        self.env.update(MANTIS_TOKEN=secrets.token_hex(24), MANTIS_PORT=str(port))
        self.image = prefix / paths['bin'] / 'mantisd'
        self.project = evidence.root / (label + '.mantis')
        self.args = [self.image, '--project', self.project, '--port', str(port), *extra]
        self.process = None
        self.spoof = spoof_argv

    def cli(self, label, *args, expected=None, env=None):
        return self.e.run(self.label + '-' + label, [self.prefix / self.paths['bin'] / 'mantis-cli', *args],
                          env=env or self.env, expected=expected)

    def __enter__(self):
        args = list(self.args)
        if self.spoof:
            args[0] = '/unrelated argv0/bin/not-the-daemon'
        self.process, self.record = self.e.start(self.label + '-daemon', args, env=self.env,
                                               executable=str(self.image) if self.spoof else None)
        try:
            deadline = time.monotonic() + 10
            log = self.e.root / self.record['log']
            while time.monotonic() < deadline:
                require(self.process.poll() is None, 'Daemon exited before readiness: ' + log.read_text())
                if 'mantisd ready on 127.0.0.1:' in log.read_text():
                    break
                time.sleep(0.01)  # Bounded readiness observation, never a favorable test retry.
            else:
                raise RuntimeError('Daemon readiness deadline')
            actual = Path(f'/proc/{self.process.pid}/exe').resolve()
            require(actual == self.image.resolve(), 'Daemon executable origin mismatch')
            self.record['observed_executable'] = str(actual)
            self.record['maps'] = Path(f'/proc/{self.process.pid}/maps').read_text()
            self.e.save()
            return self
        except BaseException as exc:
            self.e.retire(self.process, self.record, exc)
            raise

    def __exit__(self, kind, primary, trace):
        if self.process is not None:
            if primary is None:
                try:
                    self.cli('shutdown', 'shutdown')
                    require(self.process.wait(timeout=10) == 0, 'Explicit daemon shutdown failed')
                except BaseException as exc:
                    self.e.retire(self.process, self.record, exc)
                    raise
            self.e.retire(self.process, self.record, primary)


def workflow(daemon, label, expected=None):
    target = daemon.e.root / (daemon.label + '-' + label + '.ply')
    result = daemon.cli(label, 'workflow', str(target), expected=expected)
    if expected:
        require(not target.exists(), 'Failed workflow published an export')
    else:
        text = target.read_text()
        require(text.startswith('ply\n') and 'element vertex 3072\n' in text, 'Installed workflow PLY oracle failed')
    return result


def custody_controls(e, prefix, paths):
    # A separate owner is deliberately kept alive across both unwind controls.
    unrelated = subprocess.Popen(['sleep', '60'], start_new_session=True)
    try:
        daemon = Daemon(e, prefix, paths, 'initiating-error')
        try:
            with daemon:
                state = json.loads(daemon.cli('live-state', 'snapshot'))
                require(state.get('project_path'), 'Daemon ownership phase was not observed')
                raise RuntimeError('intended initiating error after daemon readiness')
        except RuntimeError as exc:
            require(str(exc) == 'intended initiating error after daemon readiness', 'Initiating error replaced during unwind')
            if daemon.process is None or daemon.process.poll() is None:
                raise RuntimeError('Ready daemon survived failure unwind')
        else:
            raise RuntimeError('Initiating-error control did not fail')
        marker = e.root / 'owned-fork-phase.txt'
        process, record = e.start('fork-retirement-control', [sys.executable, '-B', REPO / 'tests/install/owned_child.py', marker])
        primary = None
        child = None
        try:
            deadline = time.monotonic() + 5
            while not marker.exists() and time.monotonic() < deadline:
                require(process.poll() is None, 'Fork fixture exited before phase')
                time.sleep(0.01)
            require(marker.exists(), 'Fork fixture phase deadline')
            child = int(marker.read_text())
            record['observed_group'] = [p for p in sdk.processes() if p['group'] == process.pid]
            require(child in {p['pid'] for p in record['observed_group']}, 'Forked owned child was not observed')
            raise RuntimeError('intended initiating error after fork phase')
        except RuntimeError as exc:
            primary = exc
            require(str(exc) == 'intended initiating error after fork phase', 'Fork control initiating error changed')
        finally:
            e.retire(process, record, primary)
        require(not Path(f'/proc/{child}').exists(), 'Fork child was not reaped')
        orphan_marker = e.root / 'orphan-phase.txt'
        try:
            e.run('leader-exit-control', [sys.executable, '-B', REPO / 'tests/install/owned_child.py',
                                         orphan_marker, '--leader-exit'])
        except RuntimeError as exc:
            require(str(exc) == 'Unexpected surviving owned subprocess group after leader exit',
                    'Leader-exit control failed for a different cause')
            e.records[-1]['expected_retirement_failure'] = str(exc)
            e.save()
        else:
            raise RuntimeError('Exited leader incorrectly proved process-group retirement')
        require(orphan_marker.exists() and not Path('/proc/' + orphan_marker.read_text()).exists(),
                'Orphan-group cleanup was not independently observed')
        require(unrelated.poll() is None, 'Cleanup retired an unrelated child')
        unavailable = e.root / 'new read-only evidence control'
        unavailable.mkdir(mode=0o500)
        primary = RuntimeError('intended initiating error with unwritable evidence')
        failure_evidence(unavailable, primary)
        require(str(primary) == 'intended initiating error with unwritable evidence' and
                'Failure evidence unavailable: PermissionError' in getattr(primary, '__notes__', ()),
                'Evidence I/O failure replaced the initiating error')
        (e.root / 'custody-controls.json').write_text(json.dumps({
            'actual_daemon_ready_then_failed': True, 'initiating_errors_preserved': True,
            'observed_owned_fork_retired': True, 'leader_exit_orphan_rejected_and_reaped': True,
            'unrelated_child_survived': True}, indent=2))
    finally:
        unrelated.terminate()
        unrelated.wait(timeout=2)


def observe(daemon, label, predicate):
    deadline = time.monotonic() + 10
    observations = []
    while time.monotonic() < deadline:
        state = json.loads(daemon.cli(label + '-' + str(len(observations)), 'snapshot'))
        observations.append(state)
        if predicate(state):
            (daemon.e.root / (label + '-phases.json')).write_text(json.dumps(observations, indent=2))
            return state
        time.sleep(0.01)  # Only bounded phase observation; no retry of failed actions.
    raise RuntimeError('Installed ownership phase not reached: ' + label)


def job_ownership(daemon):
    capture = json.loads(daemon.cli('capture-start', 'capture', 'start', 'virtual-scanner'))['result_id']
    state = observe(daemon, 'capture-owned', lambda state: any(
        c['id'] == capture and c['active'] and int(c['frames']) >= 32 for c in state['captures']))
    raw = next(c['raw_artifact'] for c in state['captures'] if c['id'] == capture)
    daemon.cli('capture-finalize', 'capture', 'stop', capture)
    response = json.loads(daemon.cli('replay-start', 'replay', 'realtime', raw))
    job = response['result_id']
    observe(daemon, 'replay-owned', lambda state: any(
        j['id'] == job and j['state'] == 'Running' for j in state['jobs']))
    daemon.cli('replay-cancel', 'job', 'cancel', job)
    observe(daemon, 'replay-retired', lambda state: any(
        j['id'] == job and j['state'] == 'Cancelled' for j in state['jobs']))
    daemon.cli('after-job-retirement', 'snapshot')


def qt_origins(e, text):
    modules = sorted(set(re.findall(r'^qt\.core\.library: "([^"]+)" loaded library$', text, re.M)))
    require(modules, 'Installed Studio has no observed Qt module origins')
    result = {}
    for number, name in enumerate(modules):
        path = Path(name).resolve()
        require(path.is_file() and not path.is_relative_to(REPO), 'Qt module resolved into source/build')
        closure = e.run('qt-module-' + str(number) + '-closure', ['ldd', path])
        require('not found' not in closure and 'libQt5' not in closure, 'Unresolved or mixed Qt module closure')
        result[str(path)] = {'sha256': sdk.digest(path), 'closure': {
            str(Path(p).resolve()): sdk.digest(Path(p).resolve())
            for p in re.findall(r'(?:=>\s+|^\s*)(/[^\s]+)', closure, re.M)}}
    (e.root / 'observed-qt-module-origins.json').write_text(json.dumps({
        'scope': 'available system Qt modules actually loaded by relocated offscreen Studio; not Issue39 clearance',
        'modules': result}, indent=2))


def exercise(e, prefix, paths):
    cli = prefix / paths['bin'] / 'mantis-cli'
    e.run('cli-help', [cli, '--help'])
    e.run('cli-missing-token', [cli, 'snapshot'], expected='Set MANTIS_TOKEN')
    e.run('daemon-missing-token', [prefix / paths['bin'] / 'mantisd', '--project', e.root / 'must-not-create.mantis'],
          expected='MANTIS_TOKEN must contain at least 16')
    require(not (e.root / 'must-not-create.mantis').exists(), 'Missing-token startup created state')
    with socket.socket() as unavailable:
        unavailable.bind(('127.0.0.1', 0))  # Reserved, deliberately not listening.
        env = e.env | {'MANTIS_TOKEN': secrets.token_hex(24), 'MANTIS_PORT': str(unavailable.getsockname()[1])}
        e.run('cli-unavailable', [cli, 'snapshot'], env=env, expected='Cannot connect')
    plugins, host, recipes = prefix / paths['plugins'], prefix / paths['host'] / 'mantis-plugin-host', prefix / paths['recipes']
    library = next(plugins.glob('example-points.*so'))
    e.run('installed-host-probe', [host, 'probe', library, e.root / 'probe.json'])
    require(json.loads((e.root / 'probe.json').read_text())['id'] == 'org.mantis.example-points', 'Installed host identity mismatch')
    with Daemon(e, prefix, paths, 'relocated', spoof_argv=True) as daemon:
        statuses = json.loads(daemon.cli('plugins', 'plugins', 'list'))['plugins']
        require(all(p['state'] == 'registered' for p in statuses), 'Installed plugin registration failed')
        require({p['id'] for p in statuses} == {'org.mantis.virtual-scanner', 'org.mantis.virtual-rig',
                'org.mantis.example-points', 'org.mantis.ply', 'org.mantis.x1'},
                'Installed plugin composition differs from approved Linux product set')
        require(str(library.resolve()) in daemon.record['maps'], 'Daemon did not map installed processor')
        workflow(daemon, 'workflow')
        job_ownership(daemon)
        e.run('project-owner-conflict', [daemon.image, '--project', daemon.project], env=daemon.env, expected='lock|locked|already')
        daemon.cli('after-conflict', 'snapshot')
    original = inventory(prefix)
    # Each negative exercises production discovery/recipe handling on exact installed
    # bytes. Byte-identical restoration is checked before another full positive.
    mutations = [('abi', plugins / 'example-points.json', 'Manifest/ABI version unsupported'),
                 ('identity', plugins / 'example-points.json', 'Manifest/binary identity mismatch'),
                 ('missing-plugin', library, 'Cannot load plugin'),
                 ('recipe', recipes / 'example.json', 'Recipe not found'),
                 ('recipe-version', recipes / 'example.json', 'Unsupported recipe schema')]
    for label, target, diagnosis in mutations:
        before = target.read_bytes()
        if label in ('missing-plugin', 'recipe'):
            target.rename(target.with_suffix(target.suffix + '.unavailable'))
        else:
            value = json.loads(before)
            value[{'abi': 'abi_version', 'identity': 'version', 'recipe-version': 'schema_version'}[label]] = 99
            if label == 'identity':
                value['version'] = '99.0.0'
            target.write_text(json.dumps(value))
        try:
            with Daemon(e, prefix, paths, label) as daemon:
                if label.startswith('recipe'):
                    workflow(daemon, 'rejected', diagnosis)
                else:
                    text = daemon.cli('statuses', 'plugins', 'list')
                    require('failed' in text and diagnosis in text, 'Plugin failure diagnostic absent: ' + label)
                    workflow(daemon, 'rejected', 'Unknown plugin: org.mantis.example-points' if label == 'abi'
                             else 'ProcessorV1 unavailable: org.mantis.example-points')
                daemon.cli('still-alive', 'snapshot')
        finally:
            backup = target.with_suffix(target.suffix + '.unavailable')
            if backup.exists():
                backup.rename(target)
            else:
                target.write_bytes(before)
        require(inventory(prefix) == original, 'Installed negative restoration mismatch: ' + label)
    # Force the existing real processor through the production isolated-host path.
    manifest = plugins / 'example-points.json'
    before = manifest.read_bytes()
    value = json.loads(before)
    value['execution'] = 'isolated'
    manifest.write_text(json.dumps(value))
    try:
        with Daemon(e, prefix, paths, 'isolated') as daemon:
            workflow(daemon, 'host-workflow')
        with Daemon(e, prefix, paths, 'wrong-host', extra=['--plugin-host', cli]) as daemon:
            text = daemon.cli('status', 'plugins', 'list')
            require('Plugin probe host exited 2' in text, 'Wrong executable accepted as plugin host')
            workflow(daemon, 'rejected', 'ProcessorV1 unavailable')
        hidden = host.with_name(host.name + '.unavailable')
        host.rename(hidden)
        try:
            with Daemon(e, prefix, paths, 'missing-host') as daemon:
                text = daemon.cli('status', 'plugins', 'list')
                require('failed' in text and ('spawn' in text or 'host' in text), 'Missing installed host was hidden')
                workflow(daemon, 'rejected', 'ProcessorV1 unavailable')
        finally:
            hidden.rename(host)
    finally:
        manifest.write_bytes(before)
    require(inventory(prefix) == original, 'Isolated host restoration mismatch')
    with Daemon(e, prefix, paths, 'restored') as daemon:
        workflow(daemon, 'green')
    custody_controls(e, prefix, paths)
    studio = prefix / paths['bin'] / 'mantis-studio'
    if studio.exists():
        env = e.env | {'QT_QPA_PLATFORM': 'offscreen', 'QT_QUICK_BACKEND': 'software',
                           'QT_DEBUG_PLUGINS': '1', 'QML_IMPORT_TRACE': '1'}
        text = e.run('studio-client-only', [studio, '--ui-mode=mock', '--quit-after', '600', '--screenshot', e.root / 'installed-studio.png'], env=env)
        require(not re.search(r'failed to load|ReferenceError|TypeError', text, re.I), 'Installed Studio QML error')
        require((e.root / 'installed-studio.png').is_file(), 'Installed Studio screenshot missing')
        qt_origins(e, text)


def qualify(e, values):
    invalid_project = e.root / 'configure path controls'
    invalid_project.mkdir()
    (invalid_project / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.24)\n'
        'project(InstallPathControl VERSION 0.2.0 LANGUAGES NONE)\n'
        f'include("{REPO / "cmake/ProductInstallPaths.cmake"}")\n')
    for label, value in [('absolute', '/outside-prefix'), ('traversal', '../outside-prefix'), ('empty', '')]:
        e.run('reject-configure-' + label, ['cmake', '-S', invalid_project, '-B', e.root / ('invalid path ' + label),
              '-DMANTIS_INSTALL_PLUGINSDIR=' + value],
              expected='MANTIS_INSTALL_PLUGINSDIR must be a nonempty prefix-relative path without parent traversal')
    paths = {'bin': values['CMAKE_INSTALL_BINDIR'], 'plugins': values['MANTIS_INSTALL_PLUGINSDIR'],
             'host': values['MANTIS_INSTALL_HOSTDIR'], 'recipes': values['MANTIS_INSTALL_RECIPESDIR']}
    roles = {}
    for role in ['Daemon', 'CLI', 'PluginSDK'] + (['Studio'] if values['MANTIS_BUILD_STUDIO'] == 'ON' else []):
        root = e.root / ('component ' + role)
        e.install('install-' + role, root, role)
        roles[role] = root
        require(inventory(root), 'Empty component: ' + role)
        if role != 'Daemon':
            require(not list(root.rglob('mantisd')) and not list(root.rglob('*.so')), 'Runtime files leaked into client/SDK role')
        if role in ('CLI', 'Studio'):
            binary = 'mantis-cli' if role == 'CLI' else 'mantis-studio'
            expected = {f'{paths["bin"]}/{binary}',
                        f'{values["CMAKE_INSTALL_DATADIR"]}/licenses/MantisStudio-0.2.0/{role}/LICENSE'}
            require(set(inventory(root)) == expected, 'Unexpected files owned by client-only role: ' + role)
        if role == 'PluginSDK':
            sdk.check_install(root, REPO / 'sdk', values['CMAKE_INSTALL_LIBDIR'], values['CMAKE_INSTALL_INCLUDEDIR'],
                              values['CMAKE_INSTALL_DATADIR'], values['MantisPluginSDK_INSTALL_CMAKEDIR'], [REPO, e.build])
        if role == 'CLI':
            e.run('cli-only-help', [root / paths['bin'] / 'mantis-cli', '--help'])
            e.run('cli-only-no-service', [root / paths['bin'] / 'mantis-cli', 'snapshot'], expected='Set MANTIS_TOKEN')
        if role == 'Studio':
            env = e.env | {'QT_QPA_PLATFORM': 'offscreen', 'QT_QUICK_BACKEND': 'software',
                           'QT_DEBUG_PLUGINS': '1', 'QML_IMPORT_TRACE': '1'}
            text = e.run('studio-only-offline', [root / paths['bin'] / 'mantis-studio', '--ui-mode=mock',
                         '--quit-after', '600', '--screenshot', e.root / 'studio-only.png'], env=env)
            require(not re.search(r'failed to load|ReferenceError|TypeError', text, re.I), 'Studio-only QML failed')
            require((e.root / 'studio-only.png').exists(), 'Studio-only screenshot missing')
    owners = elf_manifest(e, roles)
    if 'Studio' in roles:
        rpath_control(e, roles['Studio'], paths)
    # The full authoritative CMake stage must be exactly the component composition.
    stage, target = e.root / 'DESTDIR with spaces', Path('/opt/Mantis internal candidate')
    e.install('install-full-destdir', target, destdir=stage)
    installed = stage / target.relative_to('/')
    require(inventory(installed) == {p: inventory(roles[role])[p] for p, role in owners.items()}, 'Full/component composition differs')
    require(not any('crash-test' in p or 'mantis-worker' in p for p in inventory(installed)), 'Nonproduct target installed')
    relocated = Path(tempfile.mkdtemp(prefix='mantis-relocated-')) / 'product with spaces'
    require(not relocated.is_relative_to(REPO) and not relocated.is_relative_to(e.build),
            'Relocation evidence requires TMPDIR outside the original source/build trees')
    shutil.copytree(installed, relocated)
    require(inventory(relocated) == inventory(installed), 'Relocation changed bytes')
    (e.root / 'relocation.json').write_text(json.dumps({'stage': str(installed), 'relocated': str(relocated), 'paths': paths}, indent=2))
    # Hide original install outputs. Installed lookup cannot use them for success.
    installed.rename(installed.with_name(installed.name + '.offline'))
    exercise(e, relocated, paths)
    # Public headers are installed by the existing SDK implementation. Exercise its
    # existing external C/C++ contracts, plus load actual SDK-built plugin below.
    fixture = sdk.copy_fixture(e.root)
    shutil.copy2(REPO / 'tests/install/plugin.c', fixture / 'plugin.c')
    shutil.copy2(REPO / 'plugins/first-party/algorithms/example-points.cpp', fixture / 'example-points.cpp')
    with (fixture / 'CMakeLists.txt').open('a') as out:
        out.write('''
foreach(target installed-c-plugin incompatible-plugin small-table-plugin)
 add_library(${target} MODULE plugin.c)
 target_link_libraries(${target} PRIVATE MantisPluginSDK::mantis-sdk-c)
 set_target_properties(${target} PROPERTIES PREFIX "")
endforeach()
target_compile_definitions(incompatible-plugin PRIVATE TEST_ABI=99)
target_compile_definitions(small-table-plugin PRIVATE TEST_SIZE=8)
add_library(installed-cpp-plugin MODULE example-points.cpp)
target_link_libraries(installed-cpp-plugin PRIVATE MantisPluginSDK::mantis-sdk-cpp)
set_target_properties(installed-cpp-plugin PROPERTIES PREFIX "")
''')
    consumer = e.root / 'external sdk build'
    hint = relocated / values['MantisPluginSDK_INSTALL_CMAKEDIR']
    e.run('sdk-consumer-configure', ['cmake', '-S', fixture, '-B', consumer, '-G', 'Ninja',
          f'-DMantisPluginSDK_DIR={hint}', f'-DSDK_FORBIDDEN_ROOT={REPO / "sdk"}'])
    e.run('sdk-consumer-build', ['cmake', '--build', consumer, '--parallel', '1'], timeout=60)
    e.run('sdk-consumer-test', ['ctest', '--test-dir', consumer, '--parallel', '1', '--output-on-failure', '--verbose'])
    dependencies = e.run('sdk-consumer-dependencies', ['ninja', '-C', consumer, '-t', 'deps'])
    for header in sdk.HEADERS.values():
        matches = [line.strip() for line in dependencies.splitlines() if line.strip().endswith(header)]
        require(matches and all(str(relocated) in line for line in matches), 'SDK compiler header origin mismatch: ' + header)
    require(str(relocated) in (consumer / 'origins.txt').read_text(), 'SDK origin not relocated')
    host = relocated / paths['host'] / 'mantis-plugin-host'
    for target in ('installed-c-plugin', 'installed-cpp-plugin', 'incompatible-plugin', 'small-table-plugin'):
        e.run(target + '-host-load', [host, 'probe', consumer / (target + '.so'), e.root / (target + '.json')],
              expected='Plugin ABI incompatible' if target in ('incompatible-plugin', 'small-table-plugin') else None)
    # Same real processor source, independently compiled against installed SDK only,
    # discovered through explicit overrides and executed through the installed host.
    external = e.root / 'external plugin bundle'
    shutil.copytree(relocated / paths['plugins'], external)
    old = next(external.glob('example-points.*so'))
    shutil.copy2(consumer / 'installed-cpp-plugin.so', old)
    manifest = external / 'example-points.json'
    value = json.loads(manifest.read_text())
    value['execution'] = 'isolated'
    manifest.write_text(json.dumps(value))
    with Daemon(e, relocated, paths, 'external-sdk', extra=['--plugins', external, '--plugin-host', host,
                                                          '--recipes', relocated / paths['recipes']]) as daemon:
        workflow(daemon, 'real-processor')
    # Nonprivileged install failure and symlink containment. No chmod/global policy.
    blocked = e.root / 'not a directory'
    blocked.write_text('owned output obstruction\n')
    e.install('reject-unwritable-output', blocked, 'CLI', expected='file INSTALL cannot|Not a directory|file cannot create')
    read_only = e.root / 'new read-only destination'
    read_only.mkdir(mode=0o500)  # New owned failure fixture, never chmod shared/installed state.
    e.install('reject-permission-denied', read_only, 'CLI', expected='file INSTALL cannot make directory|Permission denied|file cannot create')
    require(not list(read_only.iterdir()), 'Unwritable staging unexpectedly acquired files')
    escaped = e.root / 'outside destination'
    escaped.mkdir()
    bad = e.root / 'symlink stage'
    bad.mkdir()
    (bad / paths['bin']).parent.mkdir(parents=True, exist_ok=True)
    (bad / paths['bin']).symlink_to(escaped, target_is_directory=True)
    e.install('reject-stage-symlink', bad, 'CLI', expected='refuses symlink destination ancestry')
    require(not list(escaped.iterdir()), 'Stage containment failed')
    # Real bounded installer-guard bypass. The same negative oracle must go RED,
    # not merely accept an arbitrary nonzero child exit. All writes remain in our
    # owned disposable tree even while the symlink guard is intentionally wrong.
    original_guard = e.build / 'generated/ProductInstallGuard.cmake'
    guard_bytes = original_guard.read_bytes()
    mutant = e.root / 'disposable installer mutation'
    mutant.mkdir()
    guard = mutant / 'ProductInstallGuard.cmake'
    installer = (e.build / 'cmake_install.cmake').read_text()
    require(installer.count(str(original_guard)) == 1, 'Installer guard include anchor changed')
    (mutant / 'cmake_install.cmake').write_text(installer.replace(str(original_guard), str(guard)))
    anchor = b'message(FATAL_ERROR "Product staging refuses symlink destination ancestry:'
    require(guard_bytes.count(anchor) == 1, 'Installer mutation anchor changed')
    try:
        guard.write_bytes(guard_bytes.replace(anchor, b'message(STATUS "Product staging refuses symlink destination ancestry:'))
        try:
            e.install('guard-bypass-red', bad, 'CLI', expected='refuses symlink destination ancestry', build=mutant)
        except RuntimeError as exc:
            require('intended failure not observed (exit 0)' in str(exc), 'Mutation did not fail the intended installer oracle')
        else:
            raise RuntimeError('Installer guard bypass escaped the negative oracle')
        require(list(escaped.iterdir()), 'Guard bypass did not exercise the real symlink destination')
    finally:
        guard.write_bytes(guard_bytes)
    require(sdk.digest(guard) == hashlib.sha256(guard_bytes).hexdigest(), 'Installer guard restoration mismatch')
    require(original_guard.read_bytes() == guard_bytes, 'Producer guard was modified')
    e.install('guard-restored-green', bad, 'CLI', expected='refuses symlink destination ancestry', build=mutant)
    (e.root / 'mutation-restoration.json').write_text(json.dumps({
        'guard_before': hashlib.sha256(guard_bytes).hexdigest(), 'guard_restored': sdk.digest(guard),
        'red': 'real install exit 0 rejected by intended-failure oracle', 'restored_green': 'symlink rejected'}, indent=2))
    required = e.build / 'plugins/example-points.json'
    hidden = required.with_suffix('.json.install-negative')
    required.rename(hidden)
    try:
        e.install('reject-missing-required-output', e.root / 'incomplete stage', 'Daemon',
                  expected='file INSTALL cannot find.*example-points.json')
    finally:
        hidden.rename(required)
    # Owned overwrite/remove simulation, deliberately not apt/deb upgrade/remove.
    home = e.root / 'user state with spaces'
    home.mkdir()
    for name in ('project.sentinel', 'config.sentinel', 'key.sentinel'):
        (home / name).write_bytes(secrets.token_bytes(32))
    sentinels = inventory(home)
    owned = roles['CLI']
    before = inventory(owned)
    e.install('owned-staging-replacement', owned, 'CLI')
    require(inventory(owned) == before and inventory(home) == sentinels, 'Replacement modified state')
    for path in before:
        target_file = owned / path
        require(target_file.resolve().is_relative_to(owned.resolve()), 'Owned removal escaped stage')
        target_file.unlink()
    require(not inventory(owned) and inventory(home) == sentinels, 'Owned removal modified user state')
    (e.root / 'state-preservation.json').write_text(json.dumps({'before': sentinels, 'after': inventory(home), 'scope': 'owned staging overwrite/remove simulation only'}, indent=2))
    return relocated


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    e = Evidence(root, args.build.resolve())
    primary = None
    closed = False
    try:
        relocated = qualify(e, cache(args.build))
        e.close()
        closed = True
        (root / 'result.json').write_text(json.dumps({'result': 'PASS', 'scope': 'internal Linux staged product v1', 'relocated': str(relocated)}, indent=2))
        print('Installed product qualification PASS; evidence: ' + str(root))
    except BaseException as exc:
        primary = exc
        failure_evidence(root, exc)
        raise
    finally:
        if not closed:
            e.close(primary)


if __name__ == '__main__':
    main()
