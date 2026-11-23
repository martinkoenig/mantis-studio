"""Test-only forked child for exact group-retirement controls, never installed."""
import os
from pathlib import Path
import signal
import sys

child = os.fork()
if child:
    Path(sys.argv[1]).write_text(str(child))
    if '--leader-exit' in sys.argv:
        os._exit(0)  # Exact orphan-group failure injection; child remains owned.
while True:
    signal.pause()
