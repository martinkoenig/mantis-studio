"""Studio role controller + real authenticated daemon with hardware-free X1 seam."""
from pathlib import Path
import os
import socket
import subprocess
import sys
import tempfile
import time
from x1_role_fixture_process import FixtureDaemon


def run(build, fixture):
    sys.path.insert(0, str(build / 'python'))
    import mantis
    from mantis import mantis_pb2 as wire
    with tempfile.TemporaryDirectory(prefix='studio-x1-roles-') as directory:
        root = Path(directory)
        state = root / 'state'
        state.mkdir(mode=0o700)
        profile = root / 'profile.json'
        original = fixture.read_bytes()
        profile.write_bytes(original)
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        token = 'studio-role-fixture-' + os.urandom(16).hex()
        env = dict(os.environ, MANTIS_TOKEN=token, MANTIS_PORT=str(port),
                   MANTIS_X1_PROFILE=str(profile), MANTIS_X1_PROJECTED_FIXTURE='1',
                   MANTIS_X1_FAKE='normal', MANTIS_X1_FAKE_PACE='1',
                   QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
        env.pop('MANTIS_X1_ROLE_STATE_DIR', None)
        client = mantis.connect(port=port, token=token)
        owner = FixtureDaemon()
        evidence_root = build / 'cal03-role-ui'
        evidence_root.mkdir(exist_ok=True)
        evidence = Path(tempfile.mkdtemp(prefix='daemon-', dir=evidence_root))
        with (evidence / 'stdout.log').open('w') as stdout, (evidence / 'stderr.log').open('w') as stderr:
            def start(opt_in):
                args = [str(build / 'bin/mantisd'), '--project', str(root / 'project'),
                        '--plugins', str(build / 'x1-fixture-plugins')]
                if opt_in:
                    args += ['--x1-role-state-dir', str(state)]
                def ready(process):
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline:
                        assert process.poll() is None, 'Software fixture exited before readiness'
                        try:
                            client.x1_roles.query()
                            return
                        except (OSError, mantis.MantisError):
                            time.sleep(.01)
                    raise AssertionError('Software fixture readiness deadline')
                return owner.start(args, ready, env=env, stdout=stdout, stderr=stderr)
            def studio(mode=None, wrong_token=False):
                args = [str(build / 'bin/mantis-studio-x1-role-daemon-tests')]
                if mode:
                    args.append(mode)
                child_env = dict(env)
                if wrong_token:
                    child_env['MANTIS_TOKEN'] = 'invalid-role-fixture-token'
                subprocess.run(args, env=child_env, check=True, timeout=75)
            try:
                daemon = start(False)
                studio('unsupported')
                client._call(shutdown=wire.Empty())
                assert daemon.wait(timeout=20) == 0
                daemon = start(True)
                studio('bad-auth', wrong_token=True)
                studio()
                after = client.x1_roles.query()
                assert after.generation == 1 and after.swapped and not after.busy
                assert [c.identity for c in after.cameras] == [
                    'fixture/ov9281 20-0060', 'fixture/ov9281 18-0060']
                assert profile.read_bytes() == original, 'Studio must never edit the input profile'
                client._call(shutdown=wire.Empty())
                assert daemon.wait(timeout=20) == 0
            finally:
                owner.retire(primary=sys.exc_info()[1])
    print('Software-only Studio opt-in/authentication, public Jobs and preview composition passed')


if __name__ == '__main__':
    run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
