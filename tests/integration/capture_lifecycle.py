"""Identity-preserving observations for the real-process acceptance test."""
import json
import time
import subprocess
import shutil


class OwnedProcesses:
    """Own each harness child before any readiness or completion wait."""
    def __init__(self, launch=subprocess.Popen):
        self.launch = launch
        self.processes = []

    def start(self, *args, **kwargs):
        process = self.launch(*args, **kwargs)
        self.processes.append(process)
        return process

    def start_ready(self, *args, ready, **kwargs):
        process = self.start(*args, **kwargs)
        ready(process)  # If this raises, the owner still holds the child.
        return process

    def __iter__(self):
        return iter(self.processes)

    def close(self):
        for process in self.processes:
            if process.poll() is None:
                process.kill()
                process.wait()


def retain_failure(root, evidence, observations, daemon_exits, studio_exits):
    """Preserve test logs before TemporaryDirectory cleanup; state is allowlisted."""
    evidence.mkdir(parents=True, exist_ok=True)
    for name in ("daemon.log", "studio.log"):
        if (root / name).is_file():
            shutil.copyfile(root / name, evidence / name)
    (evidence / "capture-state.json").write_text(json.dumps({
        "observations": list(observations), "daemon_exits": daemon_exits,
        "studio_exits": studio_exits,
    }, indent=2) + "\n")


def capture_evidence(snapshot):
    # Explicit allowlist: never serialize requests, credentials or environment.
    fields = ("id", "raw_artifact", "active", "frames", "error", "produced",
              "committed", "queue_saturation", "finalization_job_id")
    return [{key: getattr(capture, key, None) for key in fields}
            for capture in snapshot.captures]


def require_capture(snapshot, expected, phase):
    records = capture_evidence(snapshot)
    matches = [capture for capture in snapshot.captures if capture.id == expected.id]
    reason = None
    if len(matches) != 1:
        reason = "expected exactly one original capture"
    elif matches[0].raw_artifact != expected.raw_artifact:
        reason = "original RawCapture identity changed"
    elif matches[0].error:
        reason = "capture reported an error"
    elif not matches[0].active:
        reason = "original capture became inactive"
    if reason:
        raise AssertionError(json.dumps({"phase": phase, "reason": reason,
            "expected_capture": expected.id, "expected_raw": expected.raw_artifact,
            "captures": records}, sort_keys=True))
    return matches[0]


def initial_capture(snapshot, phase):
    active = [capture for capture in snapshot.captures if capture.active]
    if len(active) != 1:
        raise AssertionError(json.dumps({"phase": phase,
            "reason": "expected exactly one daemon-owned active capture",
            "captures": capture_evidence(snapshot)}, sort_keys=True))
    return require_capture(snapshot, active[0], phase)


def await_progress(snapshot, expected, phase, timeout=10, *,
                   now=time.monotonic, wait=time.sleep):
    """Poll the public snapshot until this exact healthy capture advances.

    The deadline bounds failure; neither another capture nor an errored/inactive
    observation is accepted as continuity. Clock/wait injection keeps unit tests
    deterministic without changing the integration deadline.
    """
    deadline = now() + timeout
    while True:
        current = require_capture(snapshot(), expected, phase)
        if current.frames < expected.frames:
            raise AssertionError(f"{phase}: {expected.id}: frame counter reversed "
                                 f"{expected.frames} -> {current.frames}")
        if current.frames > expected.frames:
            return current
        remaining = deadline - now()
        if remaining <= 0:
            raise AssertionError(json.dumps({"phase": phase, "reason": "frame progress deadline",
                "expected_capture": expected.id, "expected_raw": expected.raw_artifact,
                "frames": current.frames, "error": current.error}, sort_keys=True))
        wait(min(.05, remaining))
