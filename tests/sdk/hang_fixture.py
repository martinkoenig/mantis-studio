"""Disposable hang fixture with an observable child and graceful reaping."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading

if sys.argv[1] == '--child':
    Path(sys.argv[2]).write_text(str(os.getpid()))
    print('child-ready', flush=True)
    threading.Event().wait()
else:
    child = subprocess.Popen([sys.executable, __file__, '--child', sys.argv[1]],
                             stdout=subprocess.PIPE, text=True)
    def stop(signum, frame):
        child.terminate()
        child.wait(timeout=5)
        sys.exit(143)
    signal.signal(signal.SIGTERM, stop)
    assert child.stdout.readline().strip() == 'child-ready'
    print('parent-and-child-ready', flush=True)
    child.wait()
