"""Real mantisd + L5 C++ client, fixture-only DSO, deterministic hardware-free capture."""
from pathlib import Path
import os, socket, subprocess, sys, tempfile, time
build=Path(sys.argv[1]).resolve()
profile=Path(sys.argv[2]).resolve()
# Prove the dedicated DSO has no Linux camera/control entry points linked.
symbols=subprocess.run(['nm','-D','-C',str(build/'x1-fixture-plugins/libmantis-x1-projected-fixture.so')],check=True,text=True,capture_output=True).stdout
assert 'x1::linux_backend' not in symbols and ' ioctl' not in symbols
with tempfile.TemporaryDirectory(prefix='x1-projected-',dir=build) as directory:
    root=Path(directory)
    for mode in ('normal','prepare','start','disconnect','timeout','stop','abort','recorder','shutdown'):
        with socket.socket() as sock:
            sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
        project=root/mode
        env=os.environ.copy()
        env.update(MANTIS_PORT=str(port),MANTIS_TOKEN='x1-projected-fixture-token',
                   MANTIS_X1_PROJECTED_FIXTURE='1',MANTIS_X1_FAKE='disconnect' if mode=='disconnect' else 'normal',
                   MANTIS_X1_FAKE_PACE='1',MANTIS_X1_PROFILE=str(profile),
                   MANTIS_X1_PROJECTED_RELEASE_FILE=str(root/(mode+'.release')))
        env['MANTIS_X1_PROJECTED_FAULT']={'timeout':'block','abort':'block','recorder':'delay','shutdown':'block'}.get(mode,mode if mode in ('prepare','start') else '')
        with open(root/(mode+'.log'),'w+') as log:
            daemon=subprocess.Popen([str(build/'bin/mantisd'),'--plugins',str(build/'x1-fixture-plugins'),'--project',str(project)],env=env,stdout=log,stderr=log)
            try:
                for _ in range(100):
                    if daemon.poll() is not None:
                        log.seek(0); raise AssertionError(log.read())
                    ready=subprocess.run([str(build/'bin/mantis-cli'),'projected','devices'],env=env,capture_output=True,timeout=5)
                    if ready.returncode==0: break
                    time.sleep(.02)
                else: raise AssertionError('X1 mantisd startup timeout')
                subprocess.run([str(build/'bin/mantis-x1-projected-client-tests'),mode,str(project)],env=env,check=True,timeout=20)
                assert daemon.wait(timeout=5)==0
                # Replay proof cannot open any fixture or hardware: clear all source switches.
                for key in tuple(env):
                    if key.startswith('MANTIS_X1_'): del env[key]
                subprocess.run([str(build/'bin/mantis-x1-projected-client-tests'),'--verify',str(project),mode],env=env,check=True,timeout=10)
            except BaseException:
                log.seek(0); print(log.read(),file=sys.stderr); raise
            finally:
                if daemon.poll() is None: daemon.terminate(); daemon.wait(timeout=5)
print('X1 projected real daemon controls, ownership, failures, RawCapture-3 and exact replay passed')
