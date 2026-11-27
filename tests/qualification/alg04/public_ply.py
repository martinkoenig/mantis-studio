"""Export a real recorded ALG03 result through the existing loopback daemon/PLY plugin."""
from contextlib import contextmanager, suppress
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import select
import socket
import struct
import subprocess
import sys
import tempfile

from campaign import git


def f32_bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def f32_value(bits):
    return struct.unpack('<f', struct.pack('<I', bits))[0]


def check_ply(path, expected):
    with path.open(encoding='ascii') as ply:
        header = ('ply', 'format ascii 1.0', 'comment units millimeters',
                  f'element vertex {len(expected)}', 'property float x', 'property float y',
                  'property float z', 'end_header')
        for line in header:
            assert ply.readline().rstrip('\n') == line, f'{path.name}: expected {line}'
        max_error = 0.0
        for row, point in enumerate(expected):
            tokens = ply.readline().split()
            assert len(tokens) == 3, (path.name, row, tokens)
            for axis, (text, exact) in enumerate(zip(tokens, point)):
                value = float(text)
                assert math.isfinite(value), (path.name, row, axis, text)
                bits = f32_bits(value)
                assert bits == f32_bits(exact), f'{path.name}: f32 vertex order/bits differ at {row=} {axis=}'
                rounded = f32_value(bits)
                magnitude = bits & 0x7fffffff
                center = abs(rounded)
                lo = f32_value(magnitude - 1) if magnitude else -f32_value(1)
                hi = f32_value(magnitude + 1)
                error = abs(rounded - exact)
                assert error <= min(0.5 * max(center - lo, hi - center), 0.03125), (row, axis, error)
                max_error = max(max_error, error)
        assert ply.read() == '', f'{path.name}: extra vertices or data'
    return max_error


def plugin_directory(build, root):
    actual = build / 'plugins'
    manifest = actual / 'ply-exporter.json'
    library = json.loads(manifest.read_text())['library']
    plugins = root / 'plugins'
    plugins.mkdir()
    (plugins / manifest.name).write_bytes(manifest.read_bytes())
    (plugins / library).symlink_to(actual / library)
    return plugins


@contextmanager
def running_daemon(daemon, project, plugins, env, **options):
    with socket.socket() as reserve:
        reserve.bind(('127.0.0.1', 0))
        port = reserve.getsockname()[1]
    env = dict(env, MANTIS_PORT=str(port))
    with tempfile.TemporaryFile() as diagnostics:
        with subprocess.Popen([daemon, '--project', project, '--plugins', plugins],
                              env=env, stdout=subprocess.PIPE, stderr=diagnostics, **options) as proc:
            clean = False
            try:
                startup = proc.stdout
                assert startup and select.select([startup], [], [], 15)[0], 'mantisd startup deadline'
                assert startup.readline().startswith(b'mantisd ready on 127.0.0.1:'), 'mantisd not ready'
                client = mantis.Client(port, env['MANTIS_TOKEN'])
                yield client, env
                client.shutdown()
                assert proc.wait(timeout=10) == 0, 'mantisd exit failure'
                clean = True
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=5)
                if not clean:
                    diagnostics.seek(max(0, diagnostics.seek(0, os.SEEK_END) - 4096))
                    text = diagnostics.read().decode(errors='replace')
                    print(text.replace(env['MANTIS_TOKEN'], '[redacted]'), file=sys.stderr)


def assert_no_output(path):
    assert not path.exists(), path.name
    assert not list(path.parent.glob(path.name + '.*.part')), path.name


def assert_job(client, handle, state, *, source='', diagnostic=None):
    try:
        result = handle.wait(timeout=15)
        assert state == 'Completed', f'expected {state}: {diagnostic}; job completed'
        assert result and result.id == source, (source, result)
    except mantis.MantisError as error:
        assert state != 'Completed', f'positive export failed: {error}'
        assert diagnostic and diagnostic in str(error), (diagnostic, str(error))
    actual, = [j for j in client.snapshot().jobs if j.id == handle.id]
    assert actual.state == state and actual.result_artifact == source, actual
    if diagnostic:
        assert diagnostic in actual.diagnostics, actual


def inventory(client):
    return {a.id: a.SerializeToString(deterministic=True) for a in client.snapshot().artifacts}


def stored_files(project):
    out = {}
    for p in sorted((project / 'objects').rglob('*')):
        if p.is_file():
            with p.open('rb') as stored:
                out[str(p.relative_to(project))] = hashlib.file_digest(stored, 'sha256').hexdigest()
    return out


def run_exports(root, client, cli, env, project, fixture):
    s = client.snapshot()
    assert Path(s.project_path) == project
    plugin, = s.plugins
    assert (plugin.id, plugin.kind, plugin.execution, plugin.state) == ('org.mantis.ply', 'exporter',
                                                                        'in_process', 'registered'), plugin
    before = inventory(client)
    by_id = {a.id: a for a in s.artifacts}
    for label in ('sensor', 'mixed', 'empty_input', 'all_rejected'):
        ref = fixture[label]
        stored = by_id[ref['id']]
        assert (stored.type, stored.schema_version, stored.state) == ('org.mantis.Alg03Result', 1, 'FINALIZED')
        assert stored.hash == ref['hash'] and stored.bytes == ref['bytes'] and stored.chunks == 1
        assert list(stored.inputs) == ref['inputs'] and stored.inputs[0] == ref['observation']
        obs = by_id[ref['observation']]
        assert obs.type == 'org.mantis.LaserObservation' and obs.state == 'FINALIZED' and ref['raw'] in obs.inputs
        assert by_id[ref['raw']].type == 'org.mantis.RawCapture' and by_id[ref['raw']].schema_version == 3
    for label in ('mixed', 'sensor'):
        ref = fixture[label]
        path = root / (label + '.ply')
        handle = client.export(ref['id'], path, wait=False)
        assert_job(client, handle, 'Completed', source=ref['id'])
        rounded_error = check_ply(path, ref['points_f64'])
        print(f'ALG03_PLY case={label} rows={ref["samples"]} vertices={len(ref["points_f64"])} '
              f'rejected_reason_bits={ref["rejected_reason"]} f64_oracle_error_mm={ref["max_geometry_error_mm"]:.17g} '
              f'f32_conversion_error_mm={rounded_error:.17g}', flush=True)
        assert not list(root.glob(path.name + '.*.part'))
        assert inventory(client) == before

    mixed = fixture['mixed']
    path = root / 'mixed-repeat.ply'
    prior_jobs = {j.id for j in client.snapshot().jobs}
    cli_job = json.loads(subprocess.check_output([cli, 'export', mixed['id'], path], env=env, timeout=15))
    repeated, = [j for j in client.snapshot().jobs if j.id not in prior_jobs]
    assert repeated.id == cli_job['id'] and repeated.state == 'Completed'
    assert repeated.result_artifact == cli_job['result_artifact'] == mixed['id']
    assert path.read_bytes() == (root / 'mixed.ply').read_bytes(), 'matched repeated export must be byte-stable'
    assert not list(root.glob(path.name + '.*.part'))

    duplicate = root / 'duplicate.ply'
    source = fixture['duplicate_id']
    assert_job(client, client.export(source, duplicate, wait=False), 'Completed', source=source)
    check_ply(duplicate, fixture['duplicate_points_f64'])
    assert not list(root.glob(duplicate.name + '.*.part'))

    for label, message in (('empty_input', 'empty_input'), ('all_rejected', 'all_rejected'),
                           ('tensor', 'Exporter input type mismatch'),
                           ('wrong_version', 'ALG03 export: unsupported result schema'),
                           ('wrong_envelope', 'Expected flat Tensor1 document'),
                           ('wrong_build', 'Attribute byte mismatch'), ('corrupt', 'Object integrity check failed')):
        ref = fixture[label]
        path = root / (label + '.ply')
        handle = client.export(ref['id'] if isinstance(ref, dict) else ref, path, wait=False)
        assert_job(client, handle, 'Failed', diagnostic=message)
        assert_no_output(path)
        assert inventory(client) == before

    existing = root / 'existing.ply'
    existing.write_bytes(b'keep destination intact')
    prior_jobs = {j.id for j in client.snapshot().jobs}
    for artifact, path, code, message in (
            (fixture['unfinalized'], root / 'unfinalized.ply', 6, 'Export requires finalized artifact'),
            (mixed['id'], project / 'in-store.ply', 1, 'Export outside the project store'),
            (mixed['id'], existing, 1, 'Export destination already exists')):
        try:
            client.export(artifact, path, wait=False)
        except mantis.MantisError as error:
            assert error.code == code and message in str(error), (code, message, error.code, str(error))
        else:
            raise AssertionError(f'admission accepted {path.name}')
        if path != existing:
            assert_no_output(path)
    assert existing.read_bytes() == b'keep destination intact'
    assert {j.id for j in client.snapshot().jobs} == prior_jobs

    bad = root / 'absent-parent' / 'obstructed.ply'
    assert_job(client, client.export(mixed['id'], bad, wait=False), 'Failed', diagnostic='Export failed')
    assert_no_output(bad)
    assert not bad.parent.exists()

    legacy = root / 'legacy.ply'
    assert_job(client, client.export(fixture['legacy'], legacy, wait=False), 'Completed', source=fixture['legacy'])
    check_ply(legacy, [[2.0, 4.0, 8.0], [-16.0, -32.0, -64.0]])
    assert inventory(client) == before, 'read-side export changed the public artifact inventory'
    return before


def probe_exports(root, project, plugins, env, daemon, fixture, inventory_before):
    sensor = fixture['sensor']
    daemon_gate, client_gate = socket.socketpair()
    with daemon_gate, client_gate:
        client_gate.settimeout(15)
        with running_daemon(daemon, project, plugins, dict(env, MANTIS_PLY_PROBE_FD=str(daemon_gate.fileno())),
                            pass_fds=(daemon_gate.fileno(),)) as (client, _), client_gate.makefile('r') as reports:
            daemon_gate.close()
            assert inventory(client) == inventory_before

            def read_stage(stage):
                parts = reports.readline().split()
                assert parts and parts[0] == stage, (stage, parts)
                numbers = parts[1:]
                return {key: int(value) for key, value in (item.split('=') for item in numbers)}

            stock = (root / 'sensor.ply').read_bytes()
            for scenario in ('matched', 'cancel', 'collision'):
                path = root / (scenario + '.ply')
                handle = client.export(sensor['id'], path, wait=False)
                try:
                    validate = read_stage('validation')
                    projected = read_stage('projection')
                    read_stage('part_synced')
                    assert validate['shared_columns'] == validate['total_columns'] == 17, validate
                    assert validate['mapped_bytes'] == sensor['bytes'], validate
                    assert validate['aligned_bytes'] == validate['payload_bytes'] > 256 * 121, validate
                    assert validate['aligned_allocations'] == 2, validate  # replay rows + manifest
                    assert projected == dict(vertices=256, backing_bytes=3072, payload_bytes=3072,
                                             aligned_allocations=1, aligned_bytes=3072, bulk_copies=0,
                                             bulk_copy_bytes=0, envelope_ok=1, frame_ok=1, calibration_ok=1,
                                             same_address=1), projected
                    current, = [job for job in client.snapshot().jobs if job.id == handle.id]
                    assert current.state == 'Running' and not current.result_artifact and not path.exists(), current
                    part, = root.glob(path.name + '.*.part')
                    assert part.read_bytes() == stock, 'real exporter must have written its part before the gate'
                    if scenario == 'cancel':
                        handle.cancel()  # Public RPC completed before letting the real worker reach rename.
                    elif scenario == 'collision':
                        path.write_bytes(b'late destination')
                except BaseException:
                    with suppress(OSError):
                        client_gate.sendall(b'G')
                    raise
                client_gate.sendall(b'G')
                if scenario == 'matched':
                    assert_job(client, handle, 'Completed', source=sensor['id'])
                    assert path.read_bytes() == stock, 'probe daemon output differs from stock daemon'
                    print('ALG03_PLY_BACKINGS ' + json.dumps(dict(validation=validate, projection=projected),
                                                            sort_keys=True), flush=True)
                elif scenario == 'cancel':
                    assert_job(client, handle, 'Cancelled', diagnostic='Operation cancelled')
                    assert_no_output(path)
                else:
                    assert_job(client, handle, 'Failed', diagnostic='Export destination appeared while writing')
                    assert path.read_bytes() == b'late destination'
                assert not list(root.glob(path.name + '.*.part'))
                assert inventory(client) == inventory_before
            print('ALG03_PLY prepublication: cancelled synced part removed; late destination preserved; no new artifacts',
                  flush=True)


def checkout_receipt(daemon, probe, fit):
    root = Path(__file__).resolve().parents[3]
    head, tree = git(root, 'rev-parse', 'HEAD'), git(root, 'rev-parse', 'HEAD^{tree}')
    clean = git(root, 'status', '--porcelain') == ''
    native = platform.machine()
    machines = []
    for exe in (daemon, probe):
        with exe.open('rb') as binary:
            elf = binary.read(20)
        assert elf[:6] == b'\x7fELF\x02\x01'
        machines.append(int.from_bytes(elf[18:20], 'little'))
    event = {}
    if os.environ.get('GITHUB_ACTIONS') == 'true':
        event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
        assert head == os.environ['GITHUB_SHA'] and tree and clean
        assert (os.environ['RUNNER_ARCH'], native, *machines) in {
            ('X64', 'x86_64', 62, 62), ('ARM64', 'aarch64', 183, 183)}
    print('ALG03_PLY_CONTEXT ' + json.dumps({
        'checkout': head, 'tree': tree, 'worktree_clean': clean,
        'pr_head': event.get('pull_request', {}).get('head', {}).get('sha'),
        'run_id': os.getenv('GITHUB_RUN_ID'), 'run_attempt': os.getenv('GITHUB_RUN_ATTEMPT'),
        'runner_arch': os.getenv('RUNNER_ARCH'), 'arch': native, 'elf_machine': machines,
        'fit_sha256': hashlib.sha256(fit).hexdigest(),
        'workload': '1280x960 Y10P stride=1603; 1280 profiles; inherited fixed M4 fit and 256-row stripe',
    }, sort_keys=True), flush=True)


def main():
    build, generator, daemon, cli, probe = map(Path, sys.argv[1:])
    sys.path.insert(0, str(build / 'python'))
    global mantis
    import mantis
    with tempfile.TemporaryDirectory(prefix='alg03-ply-', dir=Path.cwd()) as tmp:
        root = Path(tmp)
        project = root / 'PLY.mantis'
        fit = root / 'calibration.json'
        with gzip.open(Path(__file__).with_name('calibration.json.gz'), 'rb') as compressed:
            data = compressed.read((512 << 10) + 1)
        assert 0 < len(data) <= 512 << 10
        fit.write_bytes(data)
        checkout_receipt(daemon, probe, data)
        receipt = root / 'fixture.json'
        env = {k: v for k, v in os.environ.items() if not k.startswith(('MANTIS_X1_', 'MANTIS_PLY_PROBE_'))}
        env.update(MANTIS_TOKEN=os.urandom(24).hex(), TMPDIR=tmp)
        subprocess.run([generator, project, receipt, 'ply-fixture', fit], env=env, check=True, timeout=25)
        fixture = json.loads(receipt.read_text())
        files = stored_files(project)
        plugins = plugin_directory(build, root)
        with running_daemon(daemon, project, plugins, env) as (client, daemon_env):
            inventory_before = run_exports(root, client, cli, daemon_env, project, fixture)
        probe_exports(root, project, plugins, env, probe, fixture, inventory_before)
        assert stored_files(project) == files, 'export changed stored artifact bytes or files'


if __name__ == '__main__':
    main()
