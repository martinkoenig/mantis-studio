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
CI_RECORD_LIMIT = 16384  # Complete UTF-8 line, including prefix/envelope/newline.
CI_PREFIX = "ACCEPTANCE_FAILURE_EVIDENCE "
STATE_READ_LIMIT = 4 * 1024 * 1024  # Complete 32x16 schema-sized local history, not a log dump.


def exception_class(error):
    # Never export an exception's text or an untrusted subclass name.
    for kind in (FileNotFoundError, PermissionError, FileExistsError, UnicodeError,
                 TypeError, ValueError, OSError):
        if isinstance(error, kind):
            return kind.__name__
    return "Exception"


def ci_line(record):
    return CI_PREFIX + json.dumps(record, ensure_ascii=True, separators=(",", ":")) + "\n"


def redact_strings(value, secret):
    if isinstance(value, str):
        return "[REDACTED]" if secret and secret in value else value
    if isinstance(value, list):
        return [redact_strings(item, secret) for item in value]
    if isinstance(value, dict):
        return {key: redact_strings(item, secret) for key, item in value.items()}
    return value


def ci_state_summary(state, size, secret=""):
    """Select complete records; never slice serialized state at a byte offset.

    Capture/field omission counts refer to observation records, not unique IDs.
    Prioritize the latest relevant state and first/last observed terminal errors.
    Full local evidence is not modified. Missing optional diagnostics stay absent.
    """
    observations = state["observations"]
    if not isinstance(observations, list) or len(observations) > MAX_OBSERVATIONS:
        raise ValueError("Invalid observation count")
    rows = []
    for observation in observations:
        if not isinstance(observation, list) or len(observation) > MAX_CAPTURES:
            raise ValueError("Invalid capture count")
        row = []
        for capture in observation:
            clean = exported_capture(capture)
            for key in ("id", "raw_artifact", "finalization_job_id"):
                value = capture[key]
                if re.fullmatch(r"sha256:[0-9a-f]{64}", value):
                    clean[key] = value  # Keep local pseudonyms stable in CI.
            for key in CAPTURE_FIELDS:
                if type(clean[key]) is int and not 0 <= clean[key] < 2**64:
                    raise ValueError("Invalid capture scalar")
            clean["unavailable_diagnostics"] = len(RECORDER_FIELDS) - len(clean["diagnostics"])
            row.append(clean)
        rows.append(row)
    exits = {}
    for key in ("daemon_exits", "studio_exits"):
        value = state.get(key)
        if value is not None and (not isinstance(value, list) or len(value) > 8 or
                                  any(code is not None and (type(code) is not int or
                                      not -2**31 <= code < 2**31) for code in value)):
            raise ValueError("Invalid exit evidence")
        exits[key] = value
    all_records = [(i, j) for i, row in enumerate(rows) for j in range(len(row))]
    errors = [(i, j) for i, j in all_records if rows[i][j]["error"]]
    latest = len(rows) - 1 if rows else None
    recent = sorted(((latest, j) for j in range(len(rows[-1]))) if rows else (),
                    key=lambda key: (not bool(rows[key[0]][key[1]]["error"]),
                                     not rows[key[0]][key[1]]["active"]))
    priority = recent[:1] + errors[:1] + errors[-1:] + recent[1:] + list(reversed(all_records))
    selected = []

    def record(keys):
        included = {i for i, _ in keys}
        if rows and not rows[-1]:
            included.add(latest)  # An observed empty latest row is not unavailable.
        count_fields = lambda capture: len(CAPTURE_FIELDS) + len(capture["diagnostics"])
        omitted_fields = sum(count_fields(rows[i][j]) for i, j in all_records if (i, j) not in keys)
        summary = dict(version=1, latest_observation=latest,
                       observations=[dict(index=i, captures=[rows[i][j] for k, j in keys if k == i],
                                          omitted_captures=len(rows[i]) - sum(k == i for k, _ in keys))
                                     for i in sorted(included)],
                       total_observations=len(rows), omitted_observations=len(rows) - len(included),
                       total_captures=len(all_records), omitted_captures=len(all_records) - len(keys),
                       omitted_fields=omitted_fields, terminal_errors=len(errors),
                       omitted_terminal_errors=sum(key not in keys for key in errors),
                       truncated=len(keys) != len(all_records) or len(included) != len(rows), **exits)
        return redact_strings(dict(file="capture-state.json", size_bytes=size, summary=summary), secret)

    for key in priority:
        if key not in selected:
            candidate = record(selected + [key])
            if len(ci_line(candidate).encode("utf-8")) <= CI_RECORD_LIMIT:
                selected.append(key)
    return record(selected)


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
            summary = {"read_error": exception_class(error)}
        (evidence / name).write_text(json.dumps(summary) + "\n")


def emit_failure(evidence, stream, *, secret="", limit=16384):
    """Emit four complete escaped records, each at most 16 KiB including envelope.

    `limit` retains its text-tail read bound; structured state is independently
    selected/serialized. Total output is at most 4*CI_RECORD_LIMIT UTF-8 bytes.
    Local complete evidence is never rewritten. Logs remain bounded text tails.
    """
    if not 0 < limit <= 16384:
        raise ValueError("Evidence output limit must be in 1..16384")
    token = secret.encode("utf-8")
    for name in ("daemon.log", "studio.log", "capture-state.json", "project-diagnostics.json"):
        record = {"file": name}
        try:
            if name == "capture-state.json":
                with (evidence / name).open("rb") as source:
                    size = source.seek(0, 2)
                    if size > STATE_READ_LIMIT:
                        raise ValueError("State read bound exceeded")
                    source.seek(0)
                    data = source.read(STATE_READ_LIMIT + 1)
                record = ci_state_summary(json.loads(data), size, secret)
            else:
                record = text_evidence(evidence / name, name, token, limit)
        except OSError as error:
            record = dict(file=name, read_error=exception_class(error))
        except Exception as error:
            record = dict(file=name, summary_error=exception_class(error))
        # Escaping can expand text. Trim only text at character boundaries;
        # state above is selected at structure boundaries, never tail-sliced.
        removed = 0
        while len(ci_line(record).encode("utf-8")) > CI_RECORD_LIMIT:
            tail = record["tail"]
            cut = max(1, len(tail) // 2)
            removed += cut
            record["tail"] = tail[cut:]
            record["additional_output_prefix_characters_omitted"] = removed
        stream.write(ci_line(record))
        stream.flush()


def text_evidence(path, name, token, limit):
    with path.open("rb") as source:
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
    return dict(file=name, size_bytes=size, omitted_prefix_bytes=read_start + cut,
                tail=tail.decode("utf-8", errors="backslashreplace"))


def report_failure(root, evidence, observations, daemons, studios, stream, *, secret=""):
    """Best-effort evidence: never replace the exception handled by the caller."""
    try:
        retain_failure(root, evidence, observations, [process.poll() for process in daemons],
                       [process.poll() for process in studios])
        emit_failure(evidence, stream, secret=secret)
    except BaseException as error:
        try:
            print(f"Acceptance evidence could not be retained: {exception_class(error)}", file=stream)
        except BaseException:
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


CAPTURE_PHASES = frozenset((
    "Studio acceptance ready", "headless capture started", "after isolated plugin crash",
    "before Studio termination", "after Studio termination", "after Studio restart/exit",
    "after UI termination", "disconnect", "restart", "after crash", "UI exited", "Studio ready"))
CAPTURE_INVARIANTS = frozenset((
    "expected exactly one original capture", "original RawCapture identity changed",
    "capture reported an error", "original capture became inactive",
    "expected exactly one daemon-owned active capture", "frame counter reversed", "frame progress deadline"))


def capture_assertion(reason, phase, records, expected=None):
    """Only diagnostics change: callers retain exact raw identity predicates.

    Even a failing sanitizer/formatter must produce a safe primary assertion,
    without exposing its own exception or an untrusted chained exception.
    """
    reason = reason if type(reason) is str and reason in CAPTURE_INVARIANTS else "capture invariant"
    try:
        details = dict(reason=reason, phase=phase if phase in CAPTURE_PHASES else "unclassified",
                       phase_identity=identity_summary(phase))
        if expected is not None:
            details.update(expected_capture=identity_summary(expected.id),
                           expected_raw=identity_summary(expected.raw_artifact))
            if type(expected.frames) is int and 0 <= expected.frames < 2**64:
                details["expected_frames"] = expected.frames
        relevant = sorted(records, key=lambda record: expected is None or record["id"] != expected.id)
        captures = []
        for record in relevant[:4]:
            clean = exported_capture(record)
            captures.append({key: clean[key] for key in
                             ("id", "raw_artifact", "active", "frames", "error", "queue_saturation")})
            for key in ("frames", "queue_saturation"):
                if type(captures[-1][key]) is not int or not 0 <= captures[-1][key] < 2**64:
                    raise ValueError("Invalid assertion scalar")
        details.update(captures=captures, omitted_captures=max(0, len(records) - len(captures)))
        message = json.dumps(details, sort_keys=True)
        if len(message.encode("utf-8")) > 4096:
            raise ValueError("Assertion output bound")
    except BaseException:
        # reason is a caller-owned invariant literal, never capture/user text.
        message = ('{"reason":"' + reason +
                   '","phase":"unavailable","diagnostic":"unavailable"}')
    return AssertionError(message)


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
        raise capture_assertion(reason, phase, records, expected) from None
    return matches[0]


def initial_capture(snapshot, phase):
    active = [capture for capture in snapshot.captures if capture.active]
    if len(active) != 1:
        raise capture_assertion("expected exactly one daemon-owned active capture", phase,
                                capture_evidence(snapshot)) from None
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
        observed = snapshot()
        current = require_capture(observed, expected, phase)
        if current.frames < expected.frames:
            raise capture_assertion("frame counter reversed", phase,
                                    capture_evidence(observed),
                                    expected) from None
        if current.frames > expected.frames:
            return current
        remaining = deadline - now()
        if remaining <= 0:
            raise capture_assertion("frame progress deadline", phase,
                                    capture_evidence(observed),
                                    expected) from None
        wait(min(.05, remaining))
