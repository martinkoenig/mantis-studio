"""Audit actual mantisd through the public C++ Client in an isolated temp project."""
from pathlib import Path
import os,socket,subprocess,sys,tempfile,time
build=Path(sys.argv[1]).resolve();sys.path.insert(0,str(build/'python'))
import mantis
out=build/'ui-m2a/capabilities';out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='mantis-project-capabilities-') as tmp:
    root=Path(tmp);(root/'file').write_text('not a directory')
    s=socket.socket();s.bind(('127.0.0.1',0));port=s.getsockname()[1];s.close()
    env=dict(os.environ,MANTIS_PORT=str(port),MANTIS_TOKEN='projects-capabilities-token-0123456789');env.pop('MANTIS_X1_PROFILE',None);env.pop('MANTIS_X1_FAKE',None)
    c=mantis.Client(port,env['MANTIS_TOKEN'])
    with (out/'daemon.log').open('w+') as log:
        daemon=subprocess.Popen([str(build/'bin/mantisd'),'--project',str(root/'A.mantis')],env=env,stdout=log,stderr=log)
        locked=None
        try:
            ls=socket.socket();ls.bind(('127.0.0.1',0));locked_port=ls.getsockname()[1];ls.close()
            locked_env=dict(env,MANTIS_PORT=str(locked_port))
            locked=subprocess.Popen([str(build/'bin/mantisd'),'--project',str(root/'Locked.mantis')],env=locked_env,stdout=log,stderr=log)
            lc=mantis.Client(locked_port,env['MANTIS_TOKEN'])
            for _ in range(100):
                try:lc.snapshot();break
                except (OSError,mantis.MantisError):time.sleep(.03)
            else:raise AssertionError('Locked peer startup deadline')
            for _ in range(100):
                if daemon.poll() is not None:raise AssertionError('Daemon failed startup')
                try:c.snapshot();break
                except (OSError,mantis.MantisError):time.sleep(.03)
            else:raise AssertionError('Startup deadline')
            proc=subprocess.run([str(build/'bin/mantis-project-capabilities-tests'),str(root)],env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
            (out/'capabilities.log').write_text(proc.stdout);print(proc.stdout);assert proc.returncode==0,proc.stdout
            assert daemon.wait(timeout=8)==0
            assert not (root/'denied.mantis').exists() and not (root/'missing.mantis').exists() and not (root/'busy.mantis').exists()
        finally:
            if locked and locked.poll() is None:locked.kill();locked.wait()
            if daemon.poll() is None:daemon.kill();daemon.wait()
