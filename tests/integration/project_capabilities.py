"""Audit actual mantisd through the public C++ Client in an isolated temp project."""
from contextlib import ExitStack, contextmanager, suppress
from pathlib import Path
import argparse
import errno
import json
import os
import re
import shutil
import signal
import sqlite3
import socket
import subprocess
import sys
import tempfile
import time

UNKNOWN = 'unavailable'
CHECKPOINTS = (0, 100, 250, 1000, 2000)
OBSERVATION_NS = 25_000_000
MAX_MS = 45_000
MARKER_BYTES = 8192
TCP_BYTES = 128 * 1024


def bounded_read(path, limit):
    with path.open('rb') as stream:
        data = stream.read(limit)
    if len(data) == limit or (data and not data.endswith(b'\n')):
        raise ValueError('incomplete sample')
    return data


def observed(probe, *args):
    # Unsupported procfs, races, caps and parser failures are not evidence of
    # absence and must not change the verdict of the real fixture.
    try:
        return probe(*args)
    except Exception:
        return UNKNOWN


def exists(path):
    try:
        path.stat()
    except FileNotFoundError:
        return False
    return True


def manifest_phase(project):
    if exists(project / 'manifest.json'):
        return 'published'
    if exists(project / 'manifest.part'):
        return 'partial'
    # Rename may have happened between the two observations.
    return 'published' if exists(project / 'manifest.json') else 'absent'


def runtime_marker(path):
    if not exists(path):
        return False
    return any(line.split(b'\t', 3)[1:3] == [b'runtime.ready', b'runtime']
               for line in bounded_read(path, MARKER_BYTES).splitlines())


def daemon_markers(path, port):
    lines = bounded_read(path, MARKER_BYTES).splitlines()
    return (f'mantisd ready on 127.0.0.1:{port}'.encode() in lines,
            any(line.startswith(b'mantisd: ') for line in lines))


def process_state(proc):
    if proc is None:
        return 'not-started'
    return 'live' if proc.poll() is None else 'exited'


def process_ticks(procfs):
    # The parenthesized comm field may include spaces or right parentheses.
    fields = bounded_read(procfs / 'stat', 4096).rsplit(b') ', 1)[1].split()
    state = {b'R': 'runnable', b'S': 'sleeping', b'D': 'io-wait', b'T': 'stopped',
             b't': 'stopped', b'Z': 'zombie', b'I': 'idle'}.get(fields[0], 'other')
    ms = (int(fields[11]) + int(fields[12])) * 1000 // os.sysconf('SC_CLK_TCK')
    return state, min(MAX_MS, max(0, ms))


def peer_fds(procfs, pid, project):
    sockets = set()
    lock = sqlite = False
    project_lock = project / 'runtime.lock'
    with os.scandir(procfs / 'fd') as entries:
        for i, fd in enumerate(entries):
            if i == 64:
                raise ValueError('fd cap')
            link = os.readlink(fd.path)
            if link.startswith('socket:[') and link.endswith(']'):
                sockets.add(int(link[8:-1]))
            elif link == str(project_lock):
                before = os.stat(fd.path)
                lines = bounded_read(procfs / 'fdinfo' / fd.name, 4096).splitlines()
                if not os.path.samestat(before, os.stat(fd.path)) or not os.path.samestat(before, project_lock.stat()):
                    raise ValueError('raced lock sample')
                signature = [b'FLOCK', b'ADVISORY', b'WRITE', str(pid).encode()]
                lock |= any(line.split()[2:6] == signature for line in lines if line.startswith(b'lock:'))
            elif link == str(project / 'project.sqlite'):
                if not os.path.samefile(fd.path, link):
                    raise ValueError('raced SQLite sample')
                sqlite = True
    return sockets, lock, sqlite


def endpoint_listeners(procfs, port, remaining):
    loopback = f'{int.from_bytes(socket.inet_aton("127.0.0.1"), sys.byteorder):08X}'.encode()
    suffix = f':{port:04X}'.encode()
    inodes = set()
    for table in ('tcp', 'tcp6'):
        # Reserve the bytes before reading: a capped/failed read may also
        # have consumed the remainder of the budget.
        limit, remaining[0] = remaining[0], 0
        data = bounded_read(procfs / 'net' / table, limit)
        remaining[0] = limit - len(data)
        rows = data.splitlines()
        if not rows or rows[0].split()[1:2] != [b'local_address']:
            raise ValueError('unknown listener table')
        for row in rows[1:]:
            fields = row.split()
            if len(fields) < 10:
                raise ValueError('unknown listener row')
            if fields[3] != b'0A' or not fields[1].endswith(suffix):
                continue
            address = fields[1][:-len(suffix)]
            if table == 'tcp6' or address == b'00000000':
                # Wildcard/dual-stack relevance cannot be inferred here.
                raise ValueError('ambiguous listener')
            if address == loopback:
                inodes.add(int(fields[9]))
    if len(inodes) > 1 or 0 in inodes:
        raise ValueError('conflicting listener')
    return inodes


def socket_owner(first, procfs, port, remaining, fds):
    if first == UNKNOWN or fds == UNKNOWN:
        return UNKNOWN
    if not first:
        return 'absent'
    if first & fds[0]:
        return 'owned'
    # Only report another listener if it was stable around our fd sample.
    return 'other' if endpoint_listeners(procfs, port, remaining) == first else UNKNOWN


def peer_checkpoint(peer, binary, project, port, stream, main, main_log, main_port, *, sample_listener):
    life = process_state(peer)
    procfs = Path('/proc') / str(peer.pid) if life == 'live' and sys.platform.startswith('linux') else None
    exe = task = fds = listener = UNKNOWN
    if procfs is not None:
        exe = observed(lambda: 'expected' if (procfs / 'exe').samefile(binary) else 'other')
        task = observed(process_ticks, procfs)
        # Table first: a listener created before our fd scan can be matched;
        # a listener created later cannot be misattributed to another owner.
        remaining = [TCP_BYTES]
        first = observed(endpoint_listeners, procfs, port, remaining) if sample_listener else UNKNOWN
        fds = observed(peer_fds, procfs, peer.pid, project)
        if sample_listener:
            listener = observed(socket_owner, first, procfs, port, remaining, fds)
        if peer.poll() is not None:
            life, exe, task, fds, listener = 'exited', UNKNOWN, UNKNOWN, UNKNOWN, UNKNOWN
    marks = observed(daemon_markers, stream, port)
    overlap = observed(daemon_markers, main_log, main_port)
    return dict(peer=life, exec=exe, scheduler=task[0] if task != UNKNOWN else UNKNOWN,
                cpu_ms=task[1] if task != UNKNOWN else None,
                directory=observed(exists, project), lock=fds[1] if fds != UNKNOWN else UNKNOWN,
                manifest=observed(manifest_phase, project), sqlite_file=observed(exists, project / 'project.sqlite'),
                sqlite_fd=fds[2] if fds != UNKNOWN else UNKNOWN,
                diagnostics=observed(exists, project / 'diagnostics.log'),
                runtime_ready=observed(runtime_marker, project / 'diagnostics.log'),
                announced=marks[0] if marks != UNKNOWN else UNKNOWN,
                error_marker=marks[1] if marks != UNKNOWN else UNKNOWN, listener=listener,
                listener_sampled=procfs is not None and sample_listener,
                primary=process_state(main), primary_announced=overlap[0] if overlap != UNKNOWN else UNKNOWN)


class Startup:
    def __init__(self, data):
        self.data = data
        self.started = time.monotonic_ns()
        self.index = 0
        self.cost = 0

    def ms(self, now):
        return min(MAX_MS, (now - self.started) // 1_000_000)

    @contextmanager
    def observe(self):
        start = time.monotonic_ns()
        try:
            yield
        except Exception:
            self.data['observer_error'] = True
        finally:
            self.cost += time.monotonic_ns() - start
            self.data['observer_us'] = min(MAX_MS * 1000, self.cost // 1000)
            if self.cost >= OBSERVATION_NS:
                self.data['budget_exhausted'] = True

    def checkpoint(self, args, terminal=False):
        if self.data['budget_exhausted']:
            return
        ms = self.ms(time.monotonic_ns())
        index = self.index
        while self.index < len(CHECKPOINTS) and CHECKPOINTS[self.index] <= ms:
            self.index += 1
        if not terminal and self.index == index:
            return
        # Do not catch up on missed intervals. A sample is not atomic.
        label = 'terminal' if terminal else str(CHECKPOINTS[self.index - 1])
        # Walking both kernel TCP tables dominates cost. Keep inexpensive
        # project/process probes on schedule but reserve half for terminal.
        sample_listener = terminal or self.cost < OBSERVATION_NS // 2
        with self.observe():
            sample = peer_checkpoint(*args, sample_listener=sample_listener)
            self.data['checkpoints'][label] = dict(ms=ms, attempts=self.data['attempts'],
                                                   errors=self.data['errors'].copy(), **sample)

    def error(self, error, mantis):
        if self.data['budget_exhausted']:
            return
        with self.observe():
            label = 'unknown'
            if isinstance(error, mantis.MantisError):
                label = 'response'
            else:
                # Structural classification only; no frame/exception data is retained.
                tb = error.__traceback__
                for _ in range(12):
                    if tb is None:
                        break
                    code = tb.tb_frame.f_code
                    if code is socket.create_connection.__code__:
                        label = 'refused' if error.errno == errno.ECONNREFUSED else 'connect'
                        break
                    if code is mantis.Client._receive.__code__:
                        label = 'response'
                        break
                    tb = tb.tb_next
            self.data['errors'][label] += 1

    def snapshot(self, reply, project):
        if not self.data['budget_exhausted']:
            with self.observe():
                if reply.project_path:
                    self.data['identity'] = 'expected' if reply.project_path == str(project) else 'other'


def receipt(args):
    return dict(version=1, case='negative-writer' if args.negative_writer else 'public',
                main='gated' if not args.negative_writer and sys.platform.startswith('linux') else 'stock',
                result='pending', phase='setup', locked=dict(
                    outcome='not-started', identity=UNKNOWN, spawn_return_ms=None, terminal_ms=None,
                    attempts=0, observer_us=0, budget_exhausted=False, observer_error=False,
                    errors=dict(refused=0, connect=0, response=0, unknown=0),
                    checkpoints={k: None for k in ('0', '100', '250', '1000', '2000', 'terminal')}),
                client=dict(exit='not-started', attempted='unobserved', completed='unobserved'),
                controls=dict(locked_busy=False, gate_entered=False, gate_released=False,
                              replay_completed=False, writer_saturated=False, recoverable=False,
                              replay_refused=False))


def publish(out, build, report):
    try:
        data = (json.dumps(report, separators=(',', ':'), sort_keys=True) + '\n').encode('ascii')
        if len(data) > 8192:
            raise ValueError('receipt cap')
        # Keep partial files out of the recursive upload as well.
        with tempfile.NamedTemporaryFile(dir=build, prefix='.capabilities-', delete=False) as file:
            pending = Path(file.name)
            try:
                file.write(data)
                file.close()
                os.replace(pending, out / 'client-result.json')
            finally:
                pending.unlink(missing_ok=True)
    except Exception:
        return False
    return True


def prepare_output(build, negative):
    ui = build / 'ui-m2a'
    ui.mkdir(exist_ok=True)
    if ui.is_symlink():
        raise ValueError('unexpected artifact root')
    out = ui / ('capabilities-negative' if negative else 'capabilities')
    # These two directories belong exclusively to their respective tests.
    # Retire legacy raw/stale evidence even when the new launch fails early.
    if out.is_symlink() or (out.exists() and not out.is_dir()):
        out.unlink()
    elif out.exists():
        shutil.rmtree(out)
    out.mkdir()
    return out


STAGES = ('capture-ready', 'finalized-artifact', 'replay-submit', 'replay-job-inspect',
          'job-busy-refusal', 'replay-completion')


def record_client(report, trace, stages, attempted, returncode, timed_out):
    completed = next((v for phase, v in reversed(stages) if phase == 'end'), None)
    report['client'].update(exit=('timeout' if timed_out else 'zero' if returncode == 0 else
                                  'one' if returncode == 1 else 'signal' if returncode < 0 else 'other'),
                            attempted=attempted if attempted in STAGES else 'unobserved',
                            completed=completed if completed in STAGES else 'unobserved')
    report['controls']['locked_busy'] = 'Refusal: Project already open by another runtime' in trace.splitlines()
    report['controls']['replay_completed'] = ('end', 'replay-completion') in stages


@contextmanager
def fixture_resources():
    resources = ExitStack()
    try:
        yield resources
    except BaseException:
        # Retire all owned resources without replacing the initiating error.
        with suppress(Exception):
            resources.close()
        raise
    else:
        resources.close()


def stop_process(process, group=False):
    if process and process.poll() is None:
        if group and os.name == 'posix':
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
        process.wait(timeout=5)


def run_fixture(args, build, out, report, mantis, w):
    with fixture_resources() as fixture:
        # Keep fixture raw data off every upload path, including if TMPDIR is
        # configured inside this build's recursive UI artifact tree.
        tmp = Path(tempfile.gettempdir()).resolve()
        static_evidence = Path(__file__).resolve().parents[2] / 'docs/ui/evidence'
        if tmp.is_relative_to(build) or tmp.is_relative_to(static_evidence):
            tmp = build
        root = Path(fixture.enter_context(tempfile.TemporaryDirectory(
            prefix='mantis-project-capabilities-', dir=tmp)))
        (root / 'file').write_text('not a directory')
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        env = {k: v for k, v in os.environ.items() if not k.startswith('MANTIS_X1_')}
        env.update(MANTIS_PORT=str(port), MANTIS_TOKEN=os.urandom(24).hex())
        c = mantis.Client(port, env['MANTIS_TOKEN'])

        gate_daemon = gate_client = None
        daemon_command = [str(build / 'bin/mantisd')]
        daemon_fds = ()
        if sys.platform.startswith('linux'):
            gate_daemon, gate_client = socket.socketpair()
            if not args.negative_writer:
                daemon_command = [str(build / 'bin/mantis-project-capabilities-mantisd'),
                                  '--test-replay-gate', str(gate_daemon.fileno())]
                daemon_fds = (gate_daemon.fileno(),)
        daemon_command += ['--project', str(root / 'A.mantis'), '--plugins',
                           str(build / ('plugins' if args.negative_writer else 'project-capabilities-plugins'))]
        with fixture_resources() as resources:
            daemon = locked = proc = writer_lock = None

            def retire():
                # ExitStack runs in reverse order and continues if a cleanup fails.
                with ExitStack() as cleanup:
                    cleanup.callback(stop_process, daemon)
                    cleanup.callback(stop_process, locked)
                    if gate_client:
                        cleanup.callback(gate_client.close)
                    cleanup.callback(stop_process, proc, group=True)
                    if gate_daemon:
                        cleanup.callback(gate_daemon.close)
                    if writer_lock:
                        cleanup.callback(writer_lock.close)
                        cleanup.callback(writer_lock.rollback)

            main_log = root / 'daemon.log'
            log = resources.enter_context(main_log.open('w+'))
            resources.callback(retire)
            report['phase'] = 'primary-spawn'
            daemon = subprocess.Popen(daemon_command, pass_fds=daemon_fds,
                                      env=env, stdout=log, stderr=log)
            if gate_daemon and not args.negative_writer:
                gate_daemon.close()
            locked_stream = root / 'locked-peer.log'
            project = root / 'Locked.mantis'
            binary = build / 'bin/mantisd'
            report['phase'] = 'locked-spawn'
            data = report['locked']
            with socket.socket() as sock:
                sock.bind(('127.0.0.1', 0))
                locked_port = sock.getsockname()[1]
            locked_env = dict(env, MANTIS_PORT=str(locked_port))
            # The peer only holds a project lock; it needs no plugin catalogue.
            locked_plugins = root / 'locked-peer-plugins'
            locked_plugins.mkdir()
            locked_log = resources.enter_context(locked_stream.open('wb'))
            startup = Startup(data)
            data['outcome'] = 'spawn-incomplete'
            try:
                locked = subprocess.Popen([str(binary), '--project', str(project),
                                           '--plugins', str(locked_plugins)],
                                          env=locked_env, stdout=locked_log, stderr=locked_log)
                data['spawn_return_ms'] = startup.ms(time.monotonic_ns())
                report['phase'] = 'locked-snapshot'
                data['outcome'] = 'waiting'
                lc = mantis.Client(locked_port, env['MANTIS_TOKEN'])
                checkpoint = (locked, binary, project, locked_port, locked_stream, daemon, main_log, port)
                for _ in range(100):
                    startup.checkpoint(checkpoint)
                    if locked.poll() is not None:
                        data['outcome'] = 'peer-exited'
                        raise AssertionError('Locked peer failed startup')
                    data['attempts'] += 1
                    try:
                        reply = lc.snapshot()
                        startup.snapshot(reply, project)
                        data['outcome'] = 'snapshot-returned'
                        break
                    except (OSError, mantis.MantisError) as error:
                        startup.error(error, mantis)
                        time.sleep(.03)
                else:
                    data['outcome'] = 'deadline'
                    raise AssertionError('Locked peer startup deadline')
            except BaseException:
                if data['outcome'] == 'waiting':
                    data['outcome'] = 'snapshot-exception'
                elif data['outcome'] == 'spawn-incomplete':
                    data['outcome'] = 'spawn-failed'
                raise
            finally:
                # Take the terminal checkpoint before owned teardown. A timeout
                # at the outer CTest limit may leave the earlier pending receipt.
                with suppress(Exception):
                    data['terminal_ms'] = startup.ms(time.monotonic_ns())
                    startup.checkpoint((locked, binary, project, locked_port, locked_stream,
                                        daemon, main_log, port), terminal=True)
            # Persist completed Locked evidence outside the snapshot exchange.
            publish(out, build, report)
            report['phase'] = 'primary-snapshot'
            for _ in range(100):
                if daemon.poll() is not None:
                    raise AssertionError('Daemon failed startup')
                try:
                    c.snapshot()
                    break
                except (OSError, mantis.MantisError):
                    time.sleep(.03)
            else:
                raise AssertionError('Startup deadline')
            report['phase'] = 'capabilities-client'
            command = [str(build / 'bin/mantis-project-capabilities-tests'), str(root)]
            client_fds = ()
            if gate_client:
                command.append(str(gate_client.fileno()))
                client_fds = (gate_client.fileno(),)
            if args.negative_writer:
                command.append('--negative-writer')
            if args.gdb_client:
                command = ['gdb', '--batch', '--return-child-result',
                           '-ex', 'set pagination off', '-ex', 'set print frame-arguments none',
                           '-ex', 'set backtrace limit 32', '-ex', 'run',
                           '-ex', 'info program', '-ex', 'thread apply all bt', '--args', *command]
            proc = subprocess.Popen(command, env=env, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, start_new_session=(os.name == 'posix'),
                                    pass_fds=client_fds)
            if args.negative_writer:
                report['phase'] = 'writer-checkpoint'
                assert gate_daemon is not None, 'Negative writer fixture requires Linux socketpair'
                gate_daemon.settimeout(10)
                assert gate_daemon.recv(1) == b'W', 'Capture checkpoint not reached'
                writer_lock = sqlite3.connect(root / 'A.mantis/project.sqlite', timeout=1)
                writer_lock.execute('BEGIN IMMEDIATE')
                gate_daemon.sendall(b'L')
                deadline = time.monotonic() + 5
                diagnostic = root / 'A.mantis/diagnostics.log'
                while not (diagnostic.exists() and
                           'LOSSLESS writer queue saturated' in diagnostic.read_text()):
                    assert proc.poll() is None, 'Client ended before real queue failure'
                    assert time.monotonic() < deadline, 'Queue saturation checkpoint deadline'
                    time.sleep(.01)
                report['controls']['writer_saturated'] = True
                writer_lock.rollback()
                writer_lock.close()
                writer_lock = None
            report['phase'] = 'capabilities-client'
            timed_out = False
            try:
                trace, _ = proc.communicate(timeout=30)
            except subprocess.TimeoutExpired:
                timed_out = True
                if os.name == 'posix':
                    os.killpg(proc.pid, signal.SIGKILL)
                else:
                    proc.kill()
                trace, _ = proc.communicate(timeout=5)
            stages = re.findall(r'^STAGE (begin|end) (.+)$', trace, re.MULTILINE)
            attempted = next((v for phase, v in reversed(stages) if phase == 'begin'), None)
            with suppress(Exception):
                record_client(report, trace, stages, attempted, proc.returncode, timed_out)
            if report['main'] == 'gated':
                marks = observed(bounded_read, main_log, MARKER_BYTES)
                if marks != UNKNOWN:
                    lines = marks.splitlines()
                    report['controls']['gate_entered'] = b'REPLAY_GATE entered finalized packet read' in lines
                    report['controls']['gate_released'] = b'REPLAY_GATE released' in lines
            if args.negative_writer:
                assert not timed_out and proc.returncode == 1
                assert attempted == 'capture-ready'
                assert 'EXCEPTION: status.error().empty()' in trace, trace
                assert 'LOSSLESS writer queue saturated' in trace, trace
                assert 'BLOCKER VERIFIED' not in trace
                report['phase'] = 'writer-recovery'
                captures = c.snapshot().captures
                assert len(captures) == 1
                stopped = c._call(capture_stop=w.Id(id=captures[0].id)).captures[0]
                assert stopped.error and stopped.queue_saturation == 1, stopped
                assert stopped.framesets_produced > stopped.framesets_committed, stopped
                failed = next(a for a in c.artifacts.list() if a.id == stopped.raw_artifact)
                assert failed.state == 'RECOVERABLE', failed
                report['controls']['recoverable'] = True
                try:
                    c.replay.start(failed.id)
                except mantis.MantisError as error:
                    assert 'Replay requires a finalized RawCapture' in str(error), error
                else:
                    raise AssertionError('Failed recording incorrectly accepted for replay')
                report['controls']['replay_refused'] = True
                c.shutdown()
            else:
                assert not timed_out and proc.returncode == 0, 'Public capabilities client failed; see retained termination and stage evidence'
            report['phase'] = 'shutdown'
            assert daemon.wait(timeout=8) == 0
            report['phase'] = 'side-effects'
            assert not any((root / name).exists() for name in ('denied.mantis', 'missing.mantis', 'busy.mantis'))
            report['phase'] = 'retirement'


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise ValueError('fixture arguments')


def main():
    out = None
    report = None
    status = 1
    try:
        parser = Parser(prog='project_capabilities.py')
        parser.add_argument('build', type=Path)
        parser.add_argument('--gdb-client', action='store_true')
        parser.add_argument('--negative-writer', action='store_true')
        args = parser.parse_args()
        build = args.build.resolve()
        report = receipt(args)
        out = prepare_output(build, args.negative_writer)
        publish(out, build, report)
        sys.path.insert(0, str(build / 'python'))
        import mantis
        from mantis import mantis_pb2 as w
        run_fixture(args, build, out, report, mantis, w)
        report.update(result='pass', phase='complete')
        status = 0
    except SystemExit as error:
        if error.code == 0:  # Fixed --help text.
            return 0
    except KeyboardInterrupt:
        status = 130
    except Exception:
        pass
    finally:
        if report is not None and status != 0:
            report['result'] = 'failure'
        if out is not None and not publish(out, build, report):
            print('Project capabilities receipt unavailable', file=sys.stderr)
    # CTest and the recursive Studio upload also retain the fixture console.
    # Never propagate raw exception text, protobufs or child output to it.
    if status != 0:
        if report is not None and report['phase'] == 'locked-snapshot':
            phase = report['locked']['outcome']
        else:
            phase = report['phase'] if report is not None else 'setup'
        print('Project capabilities failed: ' + phase, file=sys.stderr)
    return status


if __name__ == '__main__':
    sys.exit(main())
