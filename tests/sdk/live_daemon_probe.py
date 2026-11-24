"""Isolated installed-SDK consumer of a real, explicitly supplied local mantisd.

Run with -I, under the existing Python-control command-tree supervisor. This is
Linux virtual-fixture qualification, not a launcher for an arbitrary plugin set.
Never emits the token, daemon output, project path or environment in its result.
"""
import argparse
from contextlib import contextmanager
import ctypes
import hashlib
import inspect
import json
import math
import os
from pathlib import Path
import selectors
import signal
import socket
import struct
import subprocess
import sys
import threading
import time


LOG_LIMIT = 65536


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def secondary(primary, error):
    # Python 3.10-compatible chaining; never overwrite errno/args/traceback.
    diagnostic = RuntimeError('live-daemon cleanup failed: ' + type(error).__name__)
    diagnostic.__cause__ = primary.__cause__
    primary.__cause__ = diagnostic
    primary.__suppress_context__ = True


class InjectedFailure(RuntimeError):
    """Qualification-only initiating failure after a successful real daemon Job."""


class ExportValidationError(AssertionError):
    """The virtual export violates an independently derived fixture invariant."""


def validate_export(path):
    # Bound the actual read, not only a racy stat or the declared vertex count.
    with path.open('rb') as stream:
        payload = stream.read(1024 * 1024)
    if not 0 < len(payload) < 1024 * 1024:
        raise ExportValidationError('unexpected export size')
    try:
        lines = payload.decode('ascii').splitlines()
    except UnicodeDecodeError as error:
        raise ExportValidationError('non-ASCII PLY') from error
    # This fixture targets ply-exporter.cpp, not every legal PLY dialect. Exact
    # format/property/units declarations prevent misinterpreting reordered data,
    # extra elements, binary bodies or a header count as actual point records.
    header = ['ply', 'format ascii 1.0', 'comment units millimeters',
              'element vertex 3072', 'property float x', 'property float y',
              'property float z', 'end_header']
    if lines[:len(header)] != header:
        raise ExportValidationError('invalid PLY header/properties')
    body = lines[len(header):]
    if len(body) != 3072:
        raise ExportValidationError('PLY vertex count mismatch')
    points = []
    for line in body:
        fields = line.split()
        if len(fields) != 3:
            raise ExportValidationError('invalid PLY XYZ record')
        try:
            point = tuple(float(field) for field in fields)
        except ValueError as error:
            raise ExportValidationError('invalid PLY XYZ number') from error
        if not all(math.isfinite(value) for value in point):
            raise ExportValidationError('nonfinite PLY XYZ')
        points.append(point)
    # recipes/example.json selects example-points.cpp: (x-32,y-24,pixel/16).
    # virtual-scanner.cpp supplies 64x48 pixels, (3*x+5*y+sequence)%256.
    # Sequence is capture-timing dependent; derive only its phase from the first
    # intensity and check independent spatial relationships at corners, adjacent
    # pixels, a row boundary and the centre. No observed export hash is an oracle.
    phase = points[0][2] * 16
    if not phase.is_integer() or not 0 <= phase <= 255:
        raise ExportValidationError('invalid virtual intensity phase')
    for index in (0, 1, 63, 64, 24 * 64 + 32, 3071):
        y, x = divmod(index, 64)
        expected = (x - 32, y - 24, (3 * x + 5 * y + phase) % 256 / 16)
        if not all(math.isclose(actual, wanted, rel_tol=0, abs_tol=1e-6)
                   for actual, wanted in zip(points[index], expected)):
            raise ExportValidationError('virtual PLY geometry mismatch')


class OwnedDaemon:
    """Own one fresh process group, its output reader and adopted descendants.

    The outer dedicated supervisor additionally retires escaped sessions on
    cancellation. This helper never enumerates/signals unrelated process actors.
    """
    def __init__(self, argv, env, cwd, ready_line):
        self.argv, self.env, self.cwd = argv, env, cwd
        self.ready_line = ready_line
        self.process = self.reader = None
        self.ready, self.stop = threading.Event(), threading.Event()
        self.output = bytearray()
        self.read_error = None
        self.previous = None

    def read(self):
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(self.process.stdout, selectors.EVENT_READ)
                while not self.stop.is_set():
                    for key, _ in selector.select(0.05):
                        part = os.read(key.fileobj.fileno(), 4096)
                        if not part:
                            return
                        if len(self.output) + len(part) > LOG_LIMIT:
                            raise RuntimeError('daemon output bound exceeded')
                        self.output.extend(part)
                        if self.ready_line in self.output.splitlines():
                            self.ready.set()
        except BaseException as error:
            self.read_error = error
        finally:
            self.ready.set()  # Wake startup on EOF/error too; check exact marker.

    def check(self):
        if self.read_error is not None:
            raise self.read_error
        if self.process.poll() is not None:
            raise RuntimeError('daemon exited before expected shutdown')

    def __enter__(self):
        if sys.platform != 'linux':
            raise RuntimeError('live-daemon ownership requires Linux')
        self.libc = ctypes.CDLL(None, use_errno=True)
        previous = ctypes.c_int()
        if self.libc.prctl(37, ctypes.byref(previous), 0, 0, 0):
            raise OSError(ctypes.get_errno(), 'read subreaper state')
        self.previous = previous.value
        if self.libc.prctl(36, 1, 0, 0, 0):
            self.previous = None
            raise OSError(ctypes.get_errno(), 'enable subreaper')
        try:
            self.process = subprocess.Popen(self.argv, env=self.env, cwd=self.cwd,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                            start_new_session=True)
            self.reader = threading.Thread(target=self.read, name='live-daemon-output')
            self.reader.start()
            if not self.ready.wait(5):
                raise TimeoutError('daemon startup timeout')
            self.check()
            if self.ready_line not in self.output.splitlines():
                raise RuntimeError('daemon omitted readiness marker')
            return self
        except BaseException as primary:
            self.__exit__(type(primary), primary, primary.__traceback__)
            raise

    def __exit__(self, kind, primary, traceback):
        errors = []

        def attempt(action):
            # Each independently owned resource must get its release attempt even
            # when an earlier cleanup operation raises (including BaseException).
            try:
                action()
            except BaseException as error:
                errors.append(error)

        if self.process is not None:
            process = self.process

            def signal_group(number):
                try:
                    os.killpg(process.pid, number)
                except ProcessLookupError:
                    pass

            def ordinary_shutdown():
                if primary is None:
                    if process.poll() is None:
                        raise RuntimeError('daemon was not shut down by the consumer')
                    try:
                        os.killpg(process.pid, 0)
                    except ProcessLookupError:
                        pass
                    else:
                        raise RuntimeError('descendants outlived ordinary daemon shutdown')

            def terminate():
                if process.poll() is None:
                    signal_group(signal.SIGTERM)
                    try:
                        process.wait(timeout=1)
                    except subprocess.TimeoutExpired:
                        pass

            def reap_descendants():
                deadline = time.monotonic() + 5
                while True:
                    try:
                        pid, _ = os.waitpid(-process.pid, os.WNOHANG)
                    except ChildProcessError:
                        break
                    if not pid:
                        if time.monotonic() >= deadline:
                            raise RuntimeError('daemon descendant reaping timeout')
                        time.sleep(0.01)
                try:
                    os.killpg(process.pid, 0)
                except ProcessLookupError:
                    pass
                else:
                    raise RuntimeError('daemon group survived cleanup')

            attempt(ordinary_shutdown)
            attempt(terminate)
            # Neither a leader exit nor a failed TERM/wait proves retirement.
            attempt(lambda: signal_group(signal.SIGKILL))
            attempt(lambda: process.wait(timeout=5))
            attempt(reap_descendants)

        attempt(self.stop.set)

        def join_reader():
            # Thread construction is not thread acquisition. ident also covers
            # a start wrapper that starts the real thread and then raises.
            if self.reader is not None and self.reader.ident is not None:
                self.reader.join(timeout=1)
                if self.reader.is_alive():
                    raise RuntimeError('daemon reader survived cleanup')

        def restore_subreaper():
            if self.previous is not None and self.libc.prctl(36, self.previous, 0, 0, 0):
                raise OSError(ctypes.get_errno(), 'restore subreaper')

        attempt(join_reader)
        def close_stream():
            if self.process is not None and self.process.stdout is not None:
                self.process.stdout.close()

        attempt(close_stream)
        if self.read_error is not None:
            errors.append(self.read_error)
        attempt(restore_subreaper)
        if errors:
            first = errors[0] if primary is None else primary
            for error in errors[1:] if primary is None else errors:
                secondary(first, error)
            if primary is None:
                raise first
        return False


@contextmanager
def owned_session(argv, env, state, ready, client):
    owner = OwnedDaemon(argv, env, state, ready)
    try:
        with owner:
            yield owner
    except (InjectedFailure, ExportValidationError) as primary:
        # Only the dedicated isolated probe has no unrelated children. A cleanup
        # diagnostic disqualifies sensitivity, never an expected-negative PASS.
        if primary.__cause__ is None:
            try:
                assert not Path(f'/proc/self/task/{os.getpid()}/children').read_text().split()
                require_error(client.snapshot, ConnectionRefusedError)
                phase = ('installed-live-daemon-invalid-export' if isinstance(primary, ExportValidationError)
                         else 'installed-live-daemon-injected-failure')
                print(json.dumps({'phase': phase, 'successful': False, 'retired': True}), flush=True)
            except BaseException as error:
                secondary(primary, error)
        raise


def require_error(call, error_type, *, code=None, message=None):
    try:
        call()
    except error_type as error:
        if code is not None:
            assert error.code == code, 'wrong structured error code'
        if message is not None:
            assert str(error) == message, 'wrong contract error diagnostic'
    else:
        raise AssertionError('negative operation unexpectedly succeeded')


def installed_identity(mantis, wire, expected):
    prefix = Path(sys.prefix).resolve()
    paths = {'Client': inspect.getfile(mantis.Client), 'Job': inspect.getfile(mantis.Job),
             'Client._call': mantis.Client._call.__code__.co_filename,
             'Job.wait': mantis.Job.wait.__code__.co_filename,
             'Job.wait_report': mantis.Job.wait_report.__code__.co_filename,
             'wire': wire.__file__}
    result = {}
    for name, value in paths.items():
        path = Path(value).resolve()
        assert path.is_relative_to(prefix) and 'site-packages' in path.parts, 'non-installed SDK origin'
        sha = digest(path)
        assert sha == expected['wire' if name == 'wire' else 'client'], 'installed payload mismatch'
        result[name] = sha
    provenance = json.loads(Path(wire.__file__).with_name('_provenance.json').read_text())
    assert provenance['schema_sha256'] == expected['schema'], 'installed schema mismatch'
    return result


def raw_request(wire, port, request):
    payload = request.SerializeToString()
    # The installed receive helper keeps a single exchange deadline here too.
    import mantis
    deadline = time.monotonic() + 5
    with socket.create_connection(('127.0.0.1', port), timeout=5) as connection:
        connection.sendall(struct.pack('!I', len(payload)) + payload)
        size, = struct.unpack('!I', mantis.Client._receive(connection, 4, deadline))
        assert 0 < size <= 4 * 1024 * 1024, 'invalid daemon response size'
        response = wire.Response.FromString(mantis.Client._receive(connection, size, deadline))
    assert response.protocol_version == 1 and response.request_id == request.request_id
    return response


def loaded_identity(pid, executable, plugins=None):
    """Bind actual system/daemon and exact approved in-process fixture mappings."""
    plugins = {} if plugins is None else plugins
    records, seen_plugins = {}, set()
    for line in Path(f'/proc/{pid}/maps').read_text().splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) != 6 or 'x' not in fields[1] or not fields[5].startswith('/'):
            continue
        path = Path(fields[5]).resolve(strict=True)
        assert (path == executable or path in plugins or path.is_relative_to('/usr/lib')), \
            'unexpected executable mapping origin'
        sha = digest(path)
        if path in plugins:
            assert sha == plugins[path], 'fixture executable hash mismatch'
            seen_plugins.add(path)
        assert path.name not in records or records[path.name] == sha, 'ambiguous executable mapping name'
        records[path.name] = sha
    assert seen_plugins == set(plugins), 'missing fixture executable mapping'
    assert records.get(executable.name) == digest(executable), 'daemon executable mapping mismatch'
    return records


def smoke(args):
    import mantis
    from mantis import mantis_pb2 as wire
    expected = json.loads(args.identity.read_text())
    origins = installed_identity(mantis, wire, expected)
    manifests = [json.loads(path.read_text()) for path in args.plugins.glob('*.json')]
    assert {entry['id'] for entry in manifests} == {
        'org.mantis.virtual-scanner', 'org.mantis.example-points', 'org.mantis.ply'
    } and len(manifests) == 3, 'fixture must contain only the three virtual-workflow plugins'
    assert set(expected['plugins']) == {entry['library'] for entry in manifests}, 'fixture identity mismatch'
    plugin_hashes = {args.plugins / name: sha for name, sha in expected['plugins'].items()}
    for entry in manifests:
        library = args.plugins / entry['library']
        assert library.parent == args.plugins and library.is_file() and not library.is_symlink()
        assert digest(library) == plugin_hashes[library], 'fixture payload mismatch'
    args.state.mkdir(mode=0o700, parents=False, exist_ok=False)
    assert args.state.stat().st_mode & 0o777 == 0o700
    # No inherited device/role/fault settings and no X1 plugin in the supplied
    # allowlisted fixture. Default daemon loopback token enforcement is untouched.
    env = {k: v for k, v in os.environ.items()
           if not k.startswith('MANTIS_') and k not in ('PYTHONPATH', 'PYTHONHOME')}
    for key in ('HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME', 'XDG_STATE_HOME', 'TMPDIR'):
        private = args.state / key.lower()
        private.mkdir(mode=0o700)
        env[key] = str(private)
    token = os.urandom(24).hex()
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    env.update(MANTIS_TOKEN=token, MANTIS_PORT=str(port))
    client = mantis.connect(port=port, token=token)
    argv = [str(args.daemon), '--project', str(args.state / 'Project.mantis'),
            '--plugins', str(args.plugins), '--plugin-host', str(args.plugin_host),
            '--recipes', str(args.recipes), '--port', str(port)]
    with owned_session(argv, env, args.state, f'mantisd ready on 127.0.0.1:{port}'.encode(), client) as daemon:
        assert daemon.process is not None
        devices = client.devices.list()
        loaded = loaded_identity(daemon.process.pid, args.daemon, plugin_hashes)
        assert len(devices) == 1 and devices[0].id == 'virtual-scanner', 'virtual discovery mismatch'
        assert 'org.mantis.camera.image-stream.v1' in devices[0].capabilities
        assert client.devices.info('virtual-scanner').id == 'virtual-scanner'
        assert not client.snapshot().captures, 'private project is not empty'
        require_error(lambda: mantis.connect(port=port, token='incorrect').snapshot(),
                      mantis.MantisError, code=1, message='Invalid local access token')
        # Missing SDK credentials reject before connecting. An explicit empty wire
        # token independently exercises the daemon-side missing-token check.
        saved = os.environ.pop('MANTIS_TOKEN', None)
        try:
            require_error(lambda: mantis.connect(port=port), ValueError)
        finally:
            if saved is not None:
                os.environ['MANTIS_TOKEN'] = saved
        for version, credential, code, message in (
                (1, '', 1, 'Invalid local access token'),
                (99, token, 3, 'Unsupported control protocol version')):
            response = raw_request(wire, port, wire.Request(protocol_version=version,
                                   request_id='live-negative', token=credential, snapshot=wire.Empty()))
            assert response.HasField('error') and response.error.code == code
            assert response.error.message == message
        require_error(lambda: client.pipeline.run(recipe='../invalid'), mantis.MantisError,
                      code=1, message='Invalid recipe name')
        assert client.devices.list()[0].id == 'virtual-scanner', 'auth/schema negative poisoned daemon'
        capture = client.capture.start(devices[0])
        assert capture.status().id == capture.id
        job = client.pipeline.run(capture=capture, recipe='example')
        assert type(job) is mantis.Job and job.client is client and job.id
        artifact = job.wait(timeout=5)
        assert artifact is not None and artifact.id
        state = next(j for j in client.snapshot().jobs if j.id == job.id)
        assert state.state == 'Completed' and state.result_artifact == artifact.id
        if args.fail_after_job:
            raise InjectedFailure('Injected installed-daemon post-Job failure')
        capture.stop()
        export = client.export(artifact, args.state / 'points.ply', wait=False)
        assert type(export) is mantis.Job and export.client is client
        export.wait(timeout=5)
        output = args.state / 'points.ply'
        validate_export(output)
        if args.truncate_export:
            # Only our completed private export is modified, never production,
            # schema, the stored artifact or a retained prior qualification file.
            # A real validator rejection (not an unconditional throw) must retire
            # the daemon; the driver then restores an ordinary installed run.
            payload = output.read_bytes()
            output.write_bytes(payload[:payload.rstrip(b'\n').rfind(b'\n') + 1])
            validate_export(output)
            raise AssertionError('truncated export unexpectedly accepted')
        daemon.check()
        client.shutdown()
        assert daemon.process.wait(timeout=5) == 0, 'daemon shutdown failed'
    # This -I probe is a dedicated owner with no unrelated children. A daemon
    # descendant that escaped its group must fail qualification; the existing
    # outer supervisor, not a broad signal here, performs exception retirement.
    assert not Path(f'/proc/self/task/{os.getpid()}/children').read_text().split(), 'escaped daemon descendant'
    # Endpoint refusal after verified process/thread/group retirement, not a
    # random unused port, and no implicit retry/cancel by the installed client.
    require_error(client.snapshot, ConnectionRefusedError)
    print(json.dumps({'phase': 'installed-live-daemon', 'origins': origins,
                      'wheel_sha256': expected['wheel'], 'daemon_sha256': digest(args.daemon),
                      'daemon_loaded': loaded,
                      'successful': True, 'retired': True, 'scope': 'Linux virtual local protocol only'}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    controls = parser.add_mutually_exclusive_group()
    controls.add_argument('--fail-after-job', action='store_true',
                          help='qualifier-only exception/owned retirement sensitivity control')
    controls.add_argument('--truncate-export', action='store_true',
                          help='qualifier-only malformed real export/retirement sensitivity control')
    for name in ('daemon', 'plugins', 'plugin-host', 'recipes', 'state', 'identity'):
        parser.add_argument('--' + name, required=True, type=lambda value: Path(value).resolve())
    smoke(parser.parse_args())


if __name__ == '__main__':
    main()
