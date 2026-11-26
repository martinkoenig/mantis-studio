"""Disposable cooperative/resistant child fixtures with readiness handshakes."""
from pathlib import Path
import signal
import subprocess
import sys
import threading

if sys.argv[1] == '--child':
    if '--resistant' in sys.argv:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
    print('child-ready', flush=True)
    threading.Event().wait()
else:
    resistant = ['--resistant'] if '--resistant' in sys.argv else []
    child = subprocess.Popen([sys.executable, __file__, '--child'] + resistant,
                             stdout=subprocess.PIPE, text=True)
    def stop(signum, frame):
        child.terminate()
        child.wait(timeout=5)
        sys.exit(143)
    if not resistant:
        signal.signal(signal.SIGTERM, stop)
    assert child.stdout.readline().strip() == 'child-ready'
    print('parent-and-child-ready', flush=True)
    # Publish readiness only after the child's TERM disposition is installed.
    Path(sys.argv[1]).write_text(str(child.pid))
    # Do not reenter Popen.wait from the SIGTERM handler while its waitpid lock
    # is already held. The cooperative handler exclusively owns child reaping.
    threading.Event().wait()
