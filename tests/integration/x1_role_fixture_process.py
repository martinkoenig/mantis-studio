"""Bounded ownership for the software-only role fixture's exact daemon child."""
import subprocess


class FixtureDaemon:
    def __init__(self, terminate_timeout=2, kill_timeout=2):
        self.process = None
        self.terminate_timeout = terminate_timeout
        self.kill_timeout = kill_timeout

    def start(self, args, wait_ready, **kwargs):
        self.retire()  # A restart cannot lose the previous owned child.
        self.process = subprocess.Popen(args, **kwargs)
        try:
            wait_ready(self.process)
        except BaseException as primary:
            self.retire(primary=primary)
            raise
        return self.process

    def retire(self, primary=None):
        process = self.process
        if process is None:
            return
        try:
            terminate_error = None
            if process.poll() is None:
                try:
                    process.terminate()
                except ProcessLookupError:
                    pass
                except OSError as error:
                    terminate_error = error
                try:
                    process.wait(timeout=self.terminate_timeout)
                except subprocess.TimeoutExpired:
                    try:
                        process.kill()
                    except ProcessLookupError:
                        pass
                    process.wait(timeout=self.kill_timeout)
            else:
                process.wait(timeout=self.kill_timeout)
            self.process = None  # Only actual wait/reap releases ownership.
            if terminate_error is not None:
                raise terminate_error
        except BaseException as secondary:
            if primary is None:
                raise
            primary.add_note("Role fixture child cleanup: " + type(secondary).__name__ +
                             ("; ownership retained" if self.process is not None else "; child reaped"))
