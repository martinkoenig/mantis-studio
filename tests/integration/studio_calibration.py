"""Studio controller across the public SDK/wire and a real hardware-free mantisd."""
from pathlib import Path
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

build = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(build / 'python'))
import mantis
with tempfile.TemporaryDirectory(prefix='mantis-m7-') as directory:
    root = Path(directory)
    project = root / 'Project.mantis'
    seed = json.loads(subprocess.check_output([str(build / 'bin/mantis-calibration-service-tests'), 'seed', str(project)], text=True))
    env = os.environ.copy()
    env.pop('MANTIS_X1_PROFILE', None)
    env.pop('MANTIS_X1_FAKE', None)
    with socket.socket() as socket_:
        socket_.bind(('127.0.0.1', 0))
        port = socket_.getsockname()[1]
    env.update(MANTIS_PORT=str(port), MANTIS_TOKEN='studio-calibration-' + os.urandom(16).hex(),
               MANTIS_X1_PROFILE=str(Path(__file__).resolve().parents[1] / 'fixtures/x1-acceptance-env.json'),
               MANTIS_X1_FAKE='normal')
    client = mantis.connect(port=port, token=env['MANTIS_TOKEN'])
    with open(root / 'daemon.log', 'w+') as log:
        daemon = subprocess.Popen([str(build / 'bin/mantisd'), '--project', str(project)], env=env, stdout=log, stderr=log)
        try:
            for _ in range(200):
                if daemon.poll() is not None:
                    log.seek(0)
                    raise AssertionError(log.read())
                try:
                    client.calibration.list()
                    break
                except (OSError, mantis.MantisError):
                    time.sleep(.025)
            else:
                raise AssertionError('Daemon startup timeout')
            subprocess.run([str(build / 'bin/mantis-studio-calibration-integration'), seed['dataset'], *seed['raw']], env=env, check=True, timeout=150)
            assert client.calibration.active('studio.offline') is None
            client.shutdown()
            assert daemon.wait(timeout=10) == 0
        finally:
            if daemon.poll() is None:
                daemon.kill()
                daemon.wait(timeout=5)
