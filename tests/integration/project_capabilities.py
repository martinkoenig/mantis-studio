"""Audit actual mantisd through the public C++ Client in an isolated temp project."""
from pathlib import Path
import os
import socket
import subprocess
import sys
import tempfile
import time

build = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(build / 'python'))
import mantis

out = build / 'ui-m2a/capabilities'
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
        (out / name).write_text(text)
        print(text)

    with (out / 'daemon.log').open('w+') as log:
        daemon = subprocess.Popen([str(build / 'bin/mantisd'), '--project', str(root / 'A.mantis')],
                                  env=env, stdout=log, stderr=log)
        locked = None
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
            proc = subprocess.run([str(build / 'bin/mantis-project-capabilities-tests'), str(root)],
                                  env=env, text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=30)
            retain('capabilities.log', proc.stdout)
            assert proc.returncode == 0, 'Public capabilities client failed; see retained state above'
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
            for process in (locked, daemon):
                if process and process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
