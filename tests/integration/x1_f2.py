"""Actual daemon/client F2 SimulationOnly path, with controller-free exact replay."""
from pathlib import Path
import os, socket, subprocess, sys, tempfile, time
build=Path(sys.argv[1]).resolve()
profile=Path(sys.argv[2]).resolve()
config=Path(sys.argv[3]).resolve()
symbols=subprocess.check_output(['nm','-D','-C',str(build/'x1-f2-plugins/libmantis-x1-f2-fixture.so')],text=True)
assert 'x1::linux_backend' not in symbols and ' ioctl' not in symbols and ' tcsetattr' not in symbols
with tempfile.TemporaryDirectory(prefix='x1-f2-',dir=build) as directory:
    root=Path(directory)
    for mode in ('normal','off','lost-ack','lost-event','prepare','configure','arm','lease','reboot','cleanup','cancel','stop','timeout','recorder','stop-ack'):
        with socket.socket() as sock:
            sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        env=os.environ.copy()
        env.update(MANTIS_PORT=str(port),MANTIS_TOKEN='x1-f2-simulation-only-token',
                   MANTIS_X1_F2_SIMULATION='1',MANTIS_X1_FAKE='normal',MANTIS_X1_FAKE_PACE='1',
                   MANTIS_X1_PROFILE=str(profile),MANTIS_X1_F2_CONFIG=str(config),
                   MANTIS_X1_F2_RELEASE_FILE=str(root/(mode+'.release')))
        env['MANTIS_X1_F2_FAULT']={'normal':'hold','off':'','cancel':'blocked','stop':'blocked','timeout':'blocked','stop-ack':'stop'}.get(mode,mode)
        with open(root/(mode+'.log'),'w+') as log:
            daemon=subprocess.Popen([str(build/'bin/mantisd'),'--plugins',str(build/'x1-f2-plugins'),'--project',str(root/mode)],env=env,stdout=log,stderr=log)
            try:
                for _ in range(100):
                    if daemon.poll() is not None:
                        log.seek(0);raise AssertionError(log.read())
                    ready=subprocess.run([str(build/'bin/mantis-cli'),'projected','devices'],env=env,capture_output=True,timeout=5)
                    if ready.returncode==0:break
                    time.sleep(.02)
                else:raise AssertionError('F2 mantisd startup deadline')
                subprocess.run([str(build/'bin/mantis-x1-f2-client-tests'),mode,str(root/mode)],env=env,check=True,timeout=20)
                assert daemon.wait(timeout=5)==0
                for key in tuple(env):
                    if key.startswith('MANTIS_X1_'):del env[key]
                subprocess.run([str(build/'bin/mantis-x1-f2-client-tests'),'--verify',str(root/mode),mode],env=env,check=True,timeout=10)
            except BaseException:
                log.seek(0);print(log.read(),file=sys.stderr);raise
            finally:
                if daemon.poll() is None:daemon.terminate();daemon.wait(timeout=5)
print('F2 mantisd/L5/L3/L2 framed simulation, failure artifacts and controller-free replay passed')
