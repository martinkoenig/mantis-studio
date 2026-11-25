"""Linux cleanup owner for the fixed Python-control qualification commands.

Each instance owns only its own child/adopted descendants. Establish subreaping
before command admission; unlike the capturing parent, this process has no
unrelated children. Nested runners may themselves own children or new sessions.
This is cooperative workload containment, not a hostile-process sandbox.
"""
import ctypes
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def retire_children():
    """Kill and reap only this dedicated owner's children, including adoptees."""
    deadline = time.monotonic() + 4
    while True:
        # Killing a nested owner reparents its descendants to this subreaper.
        # Never waitpid(-1) in the caller, where unrelated work may exist.
        children = Path(f"/proc/self/task/{os.getpid()}/children").read_text().split()
        for value in children:
            try:
                os.kill(int(value), signal.SIGKILL)
            except ProcessLookupError:
                pass
        while True:
            try:
                pid, _ = os.waitpid(-1, os.WNOHANG)
            except ChildProcessError:
                return
            if pid == 0:
                break
        if time.monotonic() >= deadline:
            raise RuntimeError("Python-control owned command tree cleanup timed out")
        # Scheduling/hang bound only; no ordering assertion depends on this wait.
        time.sleep(0.01)


def main(argv):
    if sys.platform != "linux":
        raise RuntimeError("Python-control command ownership requires Linux")
    libc = ctypes.CDLL(None, use_errno=True)
    previous = ctypes.c_int()
    if libc.prctl(37, ctypes.byref(previous), 0, 0, 0) != 0:
        raise OSError(ctypes.get_errno(), "Cannot read child-subreaper state")
    if libc.prctl(36, 1, 0, 0, 0) != 0:
        raise OSError(ctypes.get_errno(), "Cannot establish command-tree ownership")
    cancelled = False

    def cancel(signum, frame):
        nonlocal cancelled
        cancelled = True

    # Handlers exist before Popen can admit a nested command. Do not raise in
    # Popen's fork/exec boundary: record cancellation and unwind after admission.
    signal.signal(signal.SIGTERM, cancel)
    signal.signal(signal.SIGINT, cancel)
    child = None
    try:
        child = subprocess.Popen(argv)
        while not cancelled:
            try:
                result = child.wait(timeout=0.1)
                break
            except subprocess.TimeoutExpired:
                continue
        else:
            result = -signal.SIGKILL
    finally:
        initiating = sys.exc_info()[1]
        try:
            retire_children()
            if child is not None and child.returncode is None:
                # retire_children reaped it; avoid Popen's destructor treating it
                # as live. Cancellation's externally visible status is SIGKILL.
                child.returncode = -signal.SIGKILL
        except BaseException as cleanup:
            if initiating is None:
                raise
            initiating.add_note(f"Owned command tree cleanup failed: {type(cleanup).__name__}")
        finally:
            restoring = sys.exc_info()[1]
            if libc.prctl(36, previous.value, 0, 0, 0) != 0:
                error = OSError(ctypes.get_errno(), "Cannot restore child-subreaper state")
                if restoring is None:
                    raise error
                restoring.add_note(str(error))
    if result < 0:
        # Preserve the command's signal status only after all owned work is reaped.
        if -result not in (signal.SIGKILL, signal.SIGSTOP):
            signal.signal(-result, signal.SIG_DFL)
        os.kill(os.getpid(), -result)
    return result


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
