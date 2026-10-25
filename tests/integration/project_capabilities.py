"""Audit actual mantisd through the public C++ Client in an isolated temp project."""
from pathlib import Path
import argparse
import json
import os
import re
import signal
import sqlite3
import socket
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('build', type=Path)
parser.add_argument('--gdb-client', action='store_true')
parser.add_argument('--negative-writer', action='store_true')
args = parser.parse_args()
build = args.build.resolve()
sys.path.insert(0, str(build / 'python'))
import mantis
from mantis import mantis_pb2 as w

out = build / ('ui-m2a/capabilities-negative' if args.negative_writer else 'ui-m2a/capabilities')
out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='mantis-project-capabilities-') as tmp:
    root = Path(tmp)
    (root / 'file').write_text('not a directory')
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    env = {k: v for k, v in os.environ.items() if not k.startswith('MANTIS_X1_')}
    env.update(MANTIS_PORT=str(port), MANTIS_TOKEN=os.urandom(24).hex())
    c = mantis.Client(port, env['MANTIS_TOKEN'])

    def redact(text):
        if isinstance(text, bytes):
            text = text.decode('utf-8', errors='replace')
        return (text or '').replace(env['MANTIS_TOKEN'], '[redacted]')

    def retain(name, text):
        text = redact(text)
        if len(text) > 65536:
            text = '[earlier output truncated]\n' + text[-65536:]
        (out / name).write_text(text)
        print(text)

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
    with (out / 'daemon.log').open('w+') as log:
        daemon = subprocess.Popen(daemon_command, pass_fds=daemon_fds,
                                  env=env, stdout=log, stderr=log)
        if gate_daemon and not args.negative_writer:
            gate_daemon.close()
        locked = None
        proc = None
        writer_lock = None
        try:
            with socket.socket() as sock:
                sock.bind(('127.0.0.1', 0))
                locked_port = sock.getsockname()[1]
            locked_env = dict(env, MANTIS_PORT=str(locked_port))
            locked = subprocess.Popen([str(build / 'bin/mantisd'), '--project', str(root / 'Locked.mantis')],
                                      env=locked_env, stdout=log, stderr=log)
            lc = mantis.Client(locked_port, env['MANTIS_TOKEN'])
            for _ in range(100):
                if locked.poll() is not None:
                    raise AssertionError('Locked peer failed startup')
                try:
                    lc.snapshot()
                    break
                except (OSError, mantis.MantisError):
                    time.sleep(.03)
            else:
                raise AssertionError('Locked peer startup deadline')
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
                writer_lock.rollback()
                writer_lock.close()
                writer_lock = None
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
            retain('capabilities.log', trace)
            stages = re.findall(r'^STAGE (begin|end) (.+)$', trace, re.MULTILINE)
            number = -proc.returncode if proc.returncode < 0 else None
            result = dict(returncode=proc.returncode, signal=number,
                          signal_name=(signal.Signals(number).name if number in signal.valid_signals() else str(number)) if number else None,
                          timed_out=timed_out, debugger=args.gdb_client,
                          last_attempted_stage=next((v for phase, v in reversed(stages) if phase == 'begin'), None),
                          last_completed_stage=next((v for phase, v in reversed(stages) if phase == 'end'), None),
                          last_api_status=next((line for line in reversed(trace.splitlines()) if line.startswith('API ')), None),
                          daemon_returncode=daemon.poll(), locked_daemon_returncode=locked.poll())
            retain('client-result.json', json.dumps(result, indent=2) + '\n')
            if args.negative_writer:
                assert not timed_out and proc.returncode == 1, result
                assert result['last_attempted_stage'] == 'capture-ready', result
                assert 'EXCEPTION: status.error().empty()' in trace, trace
                assert 'LOSSLESS writer queue saturated' in trace, trace
                assert 'BLOCKER VERIFIED' not in trace
                captures = c.snapshot().captures
                assert len(captures) == 1
                stopped = c._call(capture_stop=w.Id(id=captures[0].id)).captures[0]
                assert stopped.error and stopped.queue_saturation == 1, stopped
                assert stopped.framesets_produced > stopped.framesets_committed, stopped
                failed = next(a for a in c.artifacts.list() if a.id == stopped.raw_artifact)
                assert failed.state == 'RECOVERABLE', failed
                try:
                    c.replay.start(failed.id)
                except mantis.MantisError as error:
                    assert 'Replay requires a finalized RawCapture' in str(error), error
                else:
                    raise AssertionError('Failed recording incorrectly accepted for replay')
                retain('negative-result.log', f'{stopped}\n{failed}\nPASS: real writer saturation '
                       'rejected by the C++ readiness check; failed artifact not replayable\n')
                c.shutdown()
            else:
                assert not timed_out and proc.returncode == 0, 'Public capabilities client failed; see retained termination and stage evidence'
            assert daemon.wait(timeout=8) == 0
            assert not any((root / name).exists() for name in ('denied.mantis', 'missing.mantis', 'busy.mantis'))
        except BaseException as error:
            if isinstance(error, subprocess.TimeoutExpired):
                retain('capabilities.log', error.stdout)
            # Bound retained diagnostics, including the real daemon's durable
            # capture errors. Never dump Requests, environment or access tokens.
            for name, path in [('daemon', out / 'daemon.log'),
                               ('project-diagnostics', root / 'A.mantis/diagnostics.log')]:
                if path.exists():
                    with path.open('rb') as diagnostic:
                        diagnostic.seek(0, os.SEEK_END)
                        diagnostic.seek(max(0, diagnostic.tell() - 65536))
                        text = redact(diagnostic.read(65536))
                    (out / (name + '-failure.log')).write_text(text)
                    print(name + ':\n' + text, file=sys.stderr)
            raise
        finally:
            if writer_lock:
                writer_lock.rollback()
                writer_lock.close()
            if gate_daemon:
                gate_daemon.close()
            if proc and proc.poll() is None:
                if os.name == 'posix':
                    os.killpg(proc.pid, signal.SIGKILL)
                else:
                    proc.kill()
                proc.wait(timeout=5)
            if gate_client:
                gate_client.close()
            for process in (locked, daemon):
                if process and process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
