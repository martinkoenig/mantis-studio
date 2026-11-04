"""Identity-preserving observations for the real-process acceptance test."""
import json
import time
import subprocess
import hashlib
import re


# Version-1 Session/Store scalar names, not a prefix match on plugin metadata.
# Session timestamps are ns since Session initialization; append/full/wait fields
# are durations. Live observations are not globally atomic or durability proof.
RECORDER_FIELDS = tuple("recorder_" + name for name in (
    "diagnostic_version", "snapshot_ns", "writer_started_ns", "producer_started_ns",
    "first_acquired_ns", "last_acquired_ns", "first_enqueued_ns", "first_callback_ns",
    "first_commit_ns", "first_append_ns", "first_error_ns", "secondary_error_ns",
    "max_append_ns", "produced", "committed", "saturation", "queue_occupancy",
    "queue_high_water", "queue_capacity", "queue_full_ns", "failed_enqueue_wait_ns",
    "writer_running", "producer_running", "source_paced", "cleanup_started_ns",
    "cleanup_finished_ns", "cleanup_errors", "diagnostic_errors",
    "observed_frames_per_second", "secondary_error_truncated", "error_truncated")) + (
    "raw_append_diagnostic_version", "raw_append_available", "raw_first_append_ns",
    "raw_first_append_complete", "raw_first_append_failed_stage", "raw_first_provenance_ns",
    "raw_first_writer_init_ns", "raw_first_segment_open_ns", "raw_first_directory_sync_ns",
    "raw_first_record_write_ns", "raw_first_segment_seal_ns")
CAPTURE_FIELDS = ("id", "raw_artifact", "active", "frames", "error",
                  "framesets_produced", "framesets_committed", "queue_depth",
                  "queue_capacity", "queue_high_water", "queue_saturation", "finalization_job_id")
LOG_READ_LIMIT = 65536
MAX_OBSERVATIONS = 32
MAX_CAPTURES = 16


def scalar_diagnostics(metadata):
    result = {}
    for key in RECORDER_FIELDS:
        if key in metadata:
            value = metadata[key]
            # Reject credentials/paths even when supplied under an allowed key.
            pattern = r"-?\d{1,20}(?:\.\d{1,6})?" if key.endswith("frames_per_second") else r"-?\d{1,20}"
            result[key] = value if isinstance(value, str) and re.fullmatch(pattern, value, re.ASCII) else "[INVALID]"
    return result


def error_summary(value):
    if not value:
        return ""
    if "LOSSLESS writer queue saturated; capture failed explicitly" in value:
        return "LOSSLESS writer queue saturated; capture failed explicitly"
    return "capture error (text withheld)"


def identity_summary(value):
    return "sha256:" + hashlib.sha256(value.encode()).hexdigest() if value else ""


def exported_capture(record):
    # Stable pseudonyms preserve identity comparisons without exporting arbitrary
    # strings supplied in IDs, errors, plugin metadata or finalization job IDs.
    out = {key: record[key] for key in CAPTURE_FIELDS}
    for key, value in out.items():
        expected = str if key in ("id", "raw_artifact", "error", "finalization_job_id") else bool if key == "active" else int
        if type(value) is not expected:
            raise ValueError(f"Invalid exported capture field: {key}")
    for key in ("id", "raw_artifact", "finalization_job_id"):
        out[key] = identity_summary(out[key])
    out["error"] = error_summary(out["error"])
    out["diagnostics"] = scalar_diagnostics(record["diagnostics"])
    return out


def log_summary(path, *, project=False):
    """Read a bounded tail; export recognized events/scalars, never raw text."""
    with path.open("rb") as source:
        size = source.seek(0, 2)
        start = max(0, size - LOG_READ_LIMIT)
        source.seek(start)
        data = source.read(LOG_READ_LIMIT)
    if start:
        data = data.partition(b"\n")[2]  # Discard a possibly partial record.
    events = []
    pairs = {("runtime.ready", "runtime"), ("capture.started", "capture"),
             ("capture.stopped", "capture"), ("error", "capture"),
             ("error", "capture.diagnostics"), ("capture.storage.diagnostics", "capture"),
             ("capture.cleanup.failed", "capture")}
    for line in data.decode("utf-8", errors="replace").splitlines():
        if project:
            fields = line.split("\t", 3)
            if len(fields) != 4 or not re.fullmatch(r"[0-9]{1,20}", fields[0]):
                continue
            sequence, kind, component, message = fields
            if (kind, component) not in pairs:
                continue
            event = dict(sequence=int(sequence), kind=kind, component=component)
            if component == "capture.diagnostics" or kind == "capture.storage.diagnostics":
                try:
                    metadata = json.loads(message) if len(message) <= 16384 else None
                except (ValueError, RecursionError):
                    metadata = None
                event["diagnostics_valid"] = isinstance(metadata, dict)
                event["diagnostics"] = scalar_diagnostics(metadata) if isinstance(metadata, dict) else {}
                if isinstance(metadata, dict):
                    for key in ("recorder_capture_id", "raw_append_artifact_id"):
                        if isinstance(metadata.get(key), str):
                            event[key] = identity_summary(metadata[key])
                    event["error"] = error_summary(metadata.get("recorder_error", "")) if isinstance(metadata.get("recorder_error", ""), str) else "invalid error diagnostic"
            elif kind in ("error", "capture.cleanup.failed"):
                event["error"] = error_summary(message)
            events.append(event)
        elif re.fullmatch(r"mantisd ready on 127\.0\.0\.1:[0-9]{1,5}", line):
            events.append({"event": "mantisd ready"})
        elif line.startswith("STUDIO_ACCEPTANCE_READY "):
            events.append({"event": "STUDIO_ACCEPTANCE_READY"})
    return dict(size_bytes=size, omitted_prefix_bytes=start, events=events[-32:])


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
    """Persist bounded sanitized evidence outside the temporary project.

    Callers must preserve the primary failure if retention itself fails. Raw log
    messages/paths never enter these files, including locally retained files.
    """
    evidence.mkdir(parents=True, exist_ok=True)
    records = list(observations)
    if len(records) > MAX_OBSERVATIONS or any(len(row) > MAX_CAPTURES for row in records):
        raise ValueError("Capture evidence observation bound exceeded")
    for exits in (daemon_exits, studio_exits):
        if len(exits) > 8 or any(code is not None and type(code) is not int for code in exits):
            raise ValueError("Invalid process exit evidence")
    (evidence / "capture-state.json").write_text(json.dumps({
        "observations": [[exported_capture(capture) for capture in row] for row in records], "daemon_exits": daemon_exits,
        "studio_exits": studio_exits,
    }, indent=2) + "\n")
    for name, path, project in (("daemon.log", root / "daemon.log", False),
                                ("studio.log", root / "studio.log", False),
                                ("project-diagnostics.json", root / "Project.mantis" / "diagnostics.log", True)):
        try:
            summary = log_summary(path, project=project)
        except FileNotFoundError:
            continue
        except OSError as error:
            summary = {"read_error": type(error).__name__}
        (evidence / name).write_text(json.dumps(summary) + "\n")


def emit_failure(evidence, stream, *, secret="", limit=16384):
    """Export bounded log/state tails to CI output without exposing the fixture token.

    Each record is one escaped JSON line, so log control characters and runner
    commands cannot become output directives. Retained files contain only the
    allowlisted summaries produced by retain_failure, not original project logs.
    """
    if not 0 < limit <= 16384:
        raise ValueError("Evidence output limit must be in 1..16384")
    token = secret.encode("utf-8")
    for name in ("daemon.log", "studio.log", "capture-state.json", "project-diagnostics.json"):
        record = {"file": name}
        try:
            with (evidence / name).open("rb") as source:
                size = source.seek(0, 2)
                start = max(0, size - limit)
                # Include overlap so a token crossing the tail boundary can be
                # redacted in full rather than leaking its suffix.
                read_start = max(0, start - max(0, len(token) - 1))
                source.seek(read_start)
                data = source.read(size - read_start)
            cut = start - read_start
            if token:
                position = data.find(token)
                while position != -1 and position < cut:
                    if position + len(token) > cut:
                        cut = position
                        break
                    position = data.find(token, position + 1)
            tail = data[cut:]
            if token:
                tail = tail.replace(token, b"[REDACTED]")
            record.update(size_bytes=size, omitted_prefix_bytes=read_start + cut,
                          tail=tail.decode("utf-8", errors="backslashreplace"))
        except OSError as error:
            record["read_error"] = type(error).__name__
        print("ACCEPTANCE_FAILURE_EVIDENCE " + json.dumps(record), file=stream, flush=True)


def report_failure(root, evidence, observations, daemons, studios, stream, *, secret=""):
    """Best-effort evidence: never replace the exception handled by the caller."""
    try:
        retain_failure(root, evidence, observations, [process.poll() for process in daemons],
                       [process.poll() for process in studios])
        emit_failure(evidence, stream, secret=secret)
    except Exception as error:
        try:
            print(f"Acceptance evidence could not be retained: {type(error).__name__}", file=stream)
        except Exception:
            pass  # Broken evidence output must not replace the initiating error.


def capture_evidence(snapshot):
    # Access required schema fields directly. Proto3 defaults (including zero)
    # are valid; an absent/renamed field must never become a successful null.
    records = []
    if len(snapshot.captures) > MAX_CAPTURES:
        raise AssertionError("Capture evidence capture bound exceeded")
    for capture in snapshot.captures:
        record = {}
        for key in (*CAPTURE_FIELDS, "diagnostics"):
            try:
                value = getattr(capture, key)
            except AttributeError:
                raise AssertionError(f"Capture schema missing required field: {key}") from None
            if key == "diagnostics":
                record[key] = scalar_diagnostics(value)
            else:
                expected = str if key in ("id", "raw_artifact", "error", "finalization_job_id") else bool if key == "active" else int
                if type(value) is not expected:
                    raise AssertionError(f"Capture schema invalid required field: {key}")
                record[key] = value
        records.append(record)
    return records


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
