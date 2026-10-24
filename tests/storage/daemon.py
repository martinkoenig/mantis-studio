"""Actual mantisd main + production services/recorder, isolated storage seam and internal PTY."""
from pathlib import Path
import os
import socket
import subprocess
import sys
import tempfile
import time

build, source = map(lambda p: Path(p).resolve(), sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='storage-daemon-', dir=build) as temporary:
    root = Path(temporary)
    project = root / 'Project.mantis'
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    env = {k:v for k,v in os.environ.items() if not k.startswith('MANTIS_X1_')}
    env.update(MANTIS_PORT=str(port), MANTIS_TOKEN='storage-durability-'+os.urandom(16).hex(),
               MANTIS_X1_F2_SIMULATION='1', MANTIS_X1_F2_PTY_FIXTURE='1',
               MANTIS_X1_FAKE='normal', MANTIS_X1_PROFILE=str(source/'tests/fixtures/x1-projected/profile.json'),
               MANTIS_X1_F2_CONFIG=str(source/'tests/fixtures/x1-f2/config.json'))
    with open(root/'daemon.log', 'w+') as log:
        daemon = subprocess.Popen([str(build/'bin/mantis-storage-fault-mantisd'), '--plugins',
                                   str(build/'x1-f2-pty-plugins'), '--project', str(project)],
                                  env=env, stdout=log, stderr=log)
        try:
            deadline = time.monotonic()+10
            while True:
                assert daemon.poll() is None, 'Daemon terminated during startup'
                ready = subprocess.run([str(build/'bin/mantis-cli'), 'projected', 'devices'],
                                       env=env, capture_output=True, timeout=3)
                if ready.returncode == 0:
                    break
                assert time.monotonic() < deadline, 'Storage daemon startup deadline'
                time.sleep(.02)
            subprocess.run([str(build/'bin/mantis-storage-fault-client'), '--capture', str(project)],
                           env=env, check=True, timeout=15)
            assert daemon.wait(timeout=5) == 0
            clean = {k:v for k,v in env.items() if not k.startswith('MANTIS_X1_')}
            subprocess.run([str(build/'bin/mantis-storage-fault-client'), '--verify', str(project)],
                           env=clean, check=True, timeout=10)
        except BaseException:
            log.seek(0)
            print(log.read(), file=sys.stderr)
            raise
        finally:
            if daemon.poll() is None:
                daemon.kill()
                daemon.wait(timeout=5)
print('PTY daemon execution COMPLETED, failed SQLite finalization RECOVERABLE, exact offline recovery/replay PASS')
