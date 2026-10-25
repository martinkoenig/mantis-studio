"""Actual daemon/client F2 SimulationOnly path, with controller-free exact replay."""
from pathlib import Path
import os, socket, subprocess, sys, tempfile, time
build=Path(sys.argv[1]).resolve()
profile=Path(sys.argv[2]).resolve()
config=Path(sys.argv[3]).resolve()
pty=len(sys.argv)==5 and sys.argv[4]=='--pty'
plugin='x1-f2-pty-plugins' if pty else 'x1-f2-plugins'
client='mantis-x1-f2-pty-client-tests' if pty else 'mantis-x1-f2-client-tests'
fixture='libmantis-x1-f2-pty-fixture.so' if pty else 'libmantis-x1-f2-fixture.so'
symbols=subprocess.check_output(['nm','-D','-C',str(build/plugin/fixture)],text=True)
assert 'x1::linux_backend' not in symbols and ' ioctl' not in symbols
if not pty:assert ' tcsetattr' not in symbols
else:assert 'FdTransport::' in symbols and 'configure_serial' in symbols
modes=('normal','off','lost-ack','lost-event','prepare','configure','arm','lease','reboot','cleanup','cancel','stop','timeout','recorder','stop-ack')
if pty:modes+=('corrupt-run','fragmented','congestion','partial-run','queued-claim','disconnect','shutdown')
with tempfile.TemporaryDirectory(prefix='x1-f2-',dir=build) as directory:
    root=Path(directory)
    for mode in modes:
        with socket.socket() as sock:
            sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        env=os.environ.copy()
        env.update(MANTIS_PORT=str(port),MANTIS_TOKEN='x1-f2-simulation-only-token',
                   MANTIS_X1_F2_SIMULATION='1',MANTIS_X1_FAKE='normal',MANTIS_X1_FAKE_PACE='1',
                   MANTIS_X1_PROFILE=str(profile),MANTIS_X1_F2_CONFIG=str(config),
                   MANTIS_X1_F2_RELEASE_FILE=str(root/(mode+'.release')))
        if pty:env['MANTIS_X1_F2_PTY_FIXTURE']='1'
        env['MANTIS_X1_F2_FAULT']={'normal':'hold','off':'','cancel':'blocked','stop':'blocked','timeout':'blocked','stop-ack':'stop','shutdown':'blocked'}.get(mode,mode)
        with open(root/(mode+'.log'),'w+') as log:
            daemon=subprocess.Popen([str(build/'bin/mantisd'),'--plugins',str(build/plugin),'--project',str(root/mode)],env=env,stdout=log,stderr=log)
            try:
                for _ in range(100):
                    if daemon.poll() is not None:
                        log.seek(0);raise AssertionError(log.read())
                    ready=subprocess.run([str(build/'bin/mantis-cli'),'projected','devices'],env=env,capture_output=True,timeout=5)
                    if ready.returncode==0:break
                    time.sleep(.02)
                else:raise AssertionError('F2 mantisd startup deadline')
                scenario_started=time.monotonic() if pty else 0
                subprocess.run([str(build/'bin'/client),mode,str(root/mode)],env=env,check=True,timeout=20)
                assert daemon.wait(timeout=5)==0
                if pty and mode=='normal':print(f'PTY normal daemon/client validation+capture+recording+shutdown wall_ms={(time.monotonic()-scenario_started)*1000:.3f}')
                for key in tuple(env):
                    if key.startswith('MANTIS_X1_'):del env[key]
                subprocess.run([str(build/'bin'/client),'--verify',str(root/mode),mode],env=env,check=True,timeout=10)
            except BaseException:
                log.seek(0);print(log.read(),file=sys.stderr);raise
            finally:
                if daemon.poll() is None:daemon.terminate();daemon.wait(timeout=5)
print('F2 mantisd/L5/L3/L2 framed simulation, failure artifacts and controller-free replay passed')
