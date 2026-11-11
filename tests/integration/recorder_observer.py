"""Strict independent decoder/retention for the opt-in Issue #12 daemon.

Private integer trace, not a wire format or SDK. Never infer historical cause,
durable records, I/O versus descheduling, or observer qualification from it.
"""
from pathlib import Path
import hashlib
import json
import struct

MAGIC = b"MREC0001"
WORD = struct.Struct("<Q")
RECORD = struct.Struct("<11Q")
NONE = 2**64 - 1
LIMITS = [4096, 8192, 32768, 1024]
MAX_BYTES = 8 * 1024 * 1024
KINDS = {1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15,
         20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
         40, 41, 42, 43, 44, 45, 46, 50}
STORE_STAGES = tuple(range(21, 35))


class InvalidTrace(ValueError):
    """Trusted diagnostic only; never echo paths, raw records or exceptions."""


def require(predicate, reason):
    if not predicate:
        raise InvalidTrace(reason)


def _integer(value):
    return type(value) is int and 0 <= value <= NONE


def _uuid(value):
    import re
    return isinstance(value, str) and re.fullmatch(
        r"[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}", value) is not None


def _object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate header key")
        result[key] = value
    return result


def decode(data, *, capture_id, raw_id, workload="actual-acceptance"):
    require(isinstance(data, bytes) and 16 <= len(data) <= MAX_BYTES, "trace byte bound")
    require(data[:8] == MAGIC, "trace magic")
    size, = WORD.unpack_from(data, 8)
    require(0 < size <= 8192 and 16 + size <= len(data), "header bound")
    try:
        header = json.loads(data[16:16 + size], object_pairs_hook=_object)
    except (ValueError, UnicodeError):
        raise InvalidTrace("invalid header") from None
    require(isinstance(header, dict), "header object")
    for key in ("version", "record_bytes", "origin_ns", "snapshot_ns", "capture_ordinal",
                "schema", "streams", "source_paced", "queue_capacity", "enqueue_budget_ns"):
        require(_integer(header.get(key)), "invalid typed header scalar")
    require(workload in ("actual-acceptance", "observer-self-test") and
            header.get("workload") == workload, "workload/control identity")
    require(header.get("version") == 1 and header.get("record_bytes") == RECORD.size,
            "trace version/record size")
    require(header.get("clock") == "steady_ns_since_observer_start" and
            header.get("thread_cpu_clock") == "thread_duration_only", "clock identity")
    require(_uuid(capture_id) and _uuid(raw_id) and header.get("capture_id") == capture_id and
            header.get("raw_id") == raw_id, "capture/raw identity mismatch")
    require(header.get("capture_selected") is True and header.get("capture_ordinal") == 3 and
            header.get("schema") == 1 and header.get("streams") == 1 and
            header.get("source_paced") == 0, "legacy acceptance workload mismatch")
    require(header.get("queue_capacity") == 32 and header.get("enqueue_budget_ns") == 50000000,
            "queue contract mismatch")
    require(header.get("queue_sample_scope") == "separate_metric_lock_interval_not_linearization",
            "queue sample semantics")
    require(header.get("wait_attribution") == "off_cpu_or_io_not_discriminated",
            "unjustified wait attribution")
    require(header.get("observer_qualification") == "UNQUALIFIED_REQUIRES_MATCHED_NATIVE_CONTROL",
            "unsupported observer qualification claim")
    for key in ("counts", "exported_counts", "dropped", "limits", "observer_emission_cpu_ns"):
        values = header.get(key)
        require(isinstance(values, list) and len(values) == 4 and all(map(_integer, values)),
                "invalid lane counters")
    require(header["limits"] == LIMITS and
            all(count <= limit for count, limit in zip(header["counts"], LIMITS)), "lane bound")
    for key in ("clock_error", "identity_error", "observer_error", "expired", "first_failure_pending", "output_rejected"):
        require(type(header.get(key)) is bool, "missing evidence status")
    require(not header["clock_error"] and not header["identity_error"] and not header["observer_error"],
            "observer clock/identity/sampling failure")
    running = header.get("workers_running")
    require(isinstance(running, list) and len(running) == 2 and
            all(type(value) is bool for value in running), "worker retirement status")
    require(_integer(header.get("snapshot_ns")) and _integer(header.get("origin_ns")),
            "invalid clock bounds")
    expected = [0] * 4 if header["output_rejected"] else header["counts"]
    require(header["exported_counts"] == expected, "unreported record loss")
    payload = data[16 + size:]
    require(len(payload) == sum(expected) * RECORD.size, "truncated/extra records")
    events = []
    counters = [0] * 4
    incomplete = header["expired"] or header["output_rejected"] or header["first_failure_pending"]
    incomplete = incomplete or any(header["dropped"]) or any(running)
    previous_begin = [0] * 4
    for words in RECORD.iter_unpack(payload):
        lane, ordinal, kind, item, begin, end, cpu_begin, cpu_end, a, b, flags = words
        require(lane < 4 and ordinal == counters[lane], "duplicate/missing/lane order")
        require(counters[lane] < expected[lane] and kind in KINDS, "record type/lane bound")
        counters[lane] += 1
        require(previous_begin[lane] <= begin <= header["snapshot_ns"], "nonmonotonic event clock")
        previous_begin[lane] = begin
        require(flags in (0, 1, 2), "record flags")
        if flags == 2:
            require(end == NONE and cpu_end == NONE, "invalid pending span")
            incomplete = True
        else:
            require(begin <= end <= header["snapshot_ns"] and cpu_begin <= cpu_end,
                    "span clock/order")
        require((kind not in range(20, 35) and kind not in (11, 12, 13, 14, 15)) or lane == 2,
                "writer/Store lane misattribution")
        require(kind not in (2, 3, 4, 5, 6, 7, 8) or lane == 1, "producer lane misattribution")
        require(kind != 50 or lane == 3, "observer lane misattribution")
        events.append(dict(lane=lane, ordinal=ordinal, kind=kind, item=item, begin=begin,
                           end=end, cpu_begin=cpu_begin, cpu_end=cpu_end, a=a, b=b, flags=flags))
    require(counters == expected, "missing lane records")
    if workload == "actual-acceptance" and not header["output_rejected"]:
        states = [e for e in events if e["kind"] == 1]
        require(len(states) == 1 and states[0]["a"] == 32 and states[0]["b"] == 0,
                "missing/invalid capture-state provenance")
        if any(not running[i] and not any(e["kind"] == (42 + i) for e in events)
               for i in range(2)):
            incomplete = True  # A retirement sample can race the earlier record-count cut.
    first = header.get("first_failure")
    if first is not None:
        require(isinstance(first, list) and len(first) == 11 and all(map(_integer, first)),
                "invalid first failure")
        require(first[0] < 3 and first[2] == 40 and first[4] == first[5] <= header["snapshot_ns"]
                and first[9] <= first[8] and first[10] in (0, 4), "first failure semantics")
        if not incomplete:
            require(any(e["kind"] == 40 and (e["item"], e["a"], e["b"]) ==
                        (first[3], first[8], first[9]) for e in events), "missing first failure event")
    require(not header["first_failure_pending"] or first is None, "inconsistent first failure")
    # Missing coverage is not waived by one incomplete row. Validate every fully
    # observed callback independently, including all post-return service stages.
    by_item = {}
    for event in events:
        if event["lane"] == 2 and event["item"] != NONE:
            by_item.setdefault(event["item"], {}).setdefault(event["kind"], []).append(event)
    committed = 0
    accounted_items = set()
    for item, groups in by_item.items():
        for kind, records in groups.items():
            require(len(records) == 1, "duplicate writer item/stage")
        callback = groups.get(12, [None])[0]
        if callback is None or callback["flags"] != 0:
            continue
        service = groups.get(11, [None])[0]
        require(service is not None, "omitted writer service cycle")
        # An ongoing service can legitimately be after callback return. It is
        # incomplete, never promoted to callback-accounted or complete coverage.
        if service["flags"] == 2:
            continue
        needed = (13, 14, 15, 20) + STORE_STAGES
        if header["dropped"][2] and not all(k in groups for k in needed):
            continue  # Declared exhausted tail is INCOMPLETE, never coverage-qualified.
        require(all(k in groups for k in needed), "omitted writer/Store span")
        require(all(groups[k][0]["flags"] == 0 for k in needed), "incomplete successful writer cycle")
        store = groups[20][0]
        require(service["begin"] <= callback["begin"] <= store["begin"] <= store["end"]
                <= callback["end"] <= groups[13][0]["begin"], "writer/Store nesting")
        ordered = [groups[k][0] for k in STORE_STAGES]
        require(store["begin"] <= ordered[0]["begin"] and ordered[-1]["end"] <= store["end"],
                "Store parent misattribution")
        require(all(left["end"] <= right["begin"] for left, right in zip(ordered, ordered[1:])),
                "overlapping exclusive Store stages")
        tail = [groups[k][0] for k in (13, 14, 15)]
        require(all(left["end"] <= right["begin"] for left, right in zip(tail, tail[1:])) and
                tail[-1]["end"] <= service["end"], "missing/misattributed post-callback service")
        committed += 1
        accounted_items.add(item)
    admissions = [e for e in events if e["kind"] == 7 and e["flags"] == 0]
    for i, event in enumerate(admissions):
        require(event["item"] == i and event["b"] == i and event["a"] in (0, 1),
                "source/admission identity/order")
        source = [e for e in events if e["lane"] == 1 and e["item"] == i]
        for kind in (2, 3, 4, 5, 6):
            matching = [e for e in source if e["kind"] == kind]
            require(len(matching) == 1 and matching[0]["flags"] == 0,
                    "omitted source/admission span")
        ordered = [next(e for e in source if e["kind"] == kind) for kind in (2, 3, 4, 5, 6)]
        require(all(left["end"] <= right["begin"] for left, right in zip(ordered, ordered[1:])),
                "source/admission overlap")
    if not incomplete and not any(e["kind"] == 12 and e["flags"] == 1 for e in events):
        require(accounted_items == {e["item"] for e in admissions if e["a"] == 1},
                "unmatched accepted item/writer accounting")
    if not incomplete and first is None:
        for event in admissions:
            if event["a"]:
                sleeps = [e for e in events if e["kind"] == 8 and e["item"] == event["item"]]
                require(len(sleeps) == 1 and sleeps[0]["flags"] == 0 and
                        event["end"] <= sleeps[0]["begin"], "missing source pacing span")
    samples = [e for e in events if e["kind"] == 50 and e["flags"] == 0]
    require(all(left["a"] < right["a"] and left["b"] <= right["b"]
                for left, right in zip(samples, samples[1:])), "observer resource order")
    for e in events:
        if e["kind"] == 44 and e["flags"] == 0:
            require(e["a"] <= e["b"] <= 32, "queue sample bound")
            following = next((x for x in events if x["lane"] == e["lane"] and
                              x["ordinal"] == e["ordinal"] + 1), None)
            if following is None and incomplete:
                continue
            require(following is not None and following["kind"] == 45 and following["item"] == e["item"]
                    and following["a"] >= following["b"] and
                    following["a"] - following["b"] == e["a"], "queue snapshot counter mismatch")
        if e["kind"] == 50 and e["flags"] == 0:
            require(e["a"] <= e["begin"], "shifted observer resource window")
    result = dict(version=1, status="INCOMPLETE" if incomplete else "COVERAGE_CHECKED",
                  observer="UNQUALIFIED", historical_cause="UNKNOWN", recovered_records=None,
                  capture=hashlib.sha256(capture_id.encode()).hexdigest(),
                  raw=hashlib.sha256(raw_id.encode()).hexdigest(),
                  records=len(events), fully_accounted_callbacks=committed,
                  failed_admissions=sum(e["a"] == 0 for e in admissions),
                  first_failure_retained=first is not None, dropped=header["dropped"],
                  pending_spans=sum(e["flags"] == 2 for e in events),
                  wait_attribution="UNAVAILABLE: I/O, waits and descheduling are not exclusive")
    return result


def decode_file(path, *, capture_id, raw_id, workload="actual-acceptance"):
    with Path(path).open("rb") as source:
        data = source.read(MAX_BYTES + 1)
    return decode(data, capture_id=capture_id, raw_id=raw_id, workload=workload)


class AcceptanceObserver:
    """Opt-in evidence for the existing fixture; owns no process or runtime."""
    def __init__(self, build, root):
        self.build, self.root = Path(build), Path(root)
        self.output = self.build / "test-diagnostics" / "recorder-observer" / self.root.name
        self.output.mkdir(parents=True, exist_ok=False)
        self.runs = []
        self.identity = None
        self.errors = []
        self.workload = {"version": 1, "kind": "actual-acceptance", "capture_ordinal": 3,
                         "schema": 1, "source": "legacy-virtual-scanner", "source_paced": False,
                         "identity_scope": "selected inputs, not compiled-input attestation",
                         "studio": (self.build / "bin/mantis-studio").exists(), "inputs": {}}
        files = ["bin/mantisd", "bin/mantis-recorder-observer-mantisd", "bin/mantis-plugin-host",
                 "bin/mantis-cli", "plugins/virtual-scanner.so", "plugins/crash-test.so",
                 "plugins/example-points.so", "plugins/ply-exporter.so",
                 "python/mantis/__init__.py", "python/mantis/mantis_pb2.py"]
        if self.workload["studio"]:
            files.append("bin/mantis-studio")
        for relative in files:
            self.workload["inputs"][relative] = self.hash(self.build / relative)
        repo = Path(__file__).resolve().parents[2]
        for relative in ("tests/integration/acceptance.py", "tests/integration/recorder_observer.py",
                         "tests/storage/recorder_observer.cpp", "tests/storage/recorder_observer.hpp",
                         "tests/storage/recorder_daemon.cpp", "src/device-runtime/device-runtime.cpp",
                         "src/artifact-store/artifact-store.cpp", "src/services/services.cpp",
                         "src/pipeline-api/include/mantis/pipeline_api.hpp"):
            self.workload["inputs"][relative] = self.hash(repo / relative)
        (self.output / "workload.json").write_text(json.dumps(self.workload, indent=2))

    @staticmethod
    def hash(path):
        digest = hashlib.sha256()
        with Path(path).open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    def daemon_args(self):
        output = self.output / ("daemon-" + str(len(self.runs)))
        self.runs.append(output)
        return [str(self.build / "bin/mantis-recorder-observer-mantisd"),
                "--observer-output", str(output), "--observer-capture", "3"]

    def link(self, capture):
        require(_uuid(capture.id) and _uuid(capture.raw_artifact), "wire capture/raw identity")
        self.identity = dict(capture_id=capture.id, raw_id=capture.raw_artifact)

    def flush(self, process, *, now=None, wait=None):
        import signal
        import time
        now = now or time.monotonic
        wait = wait or time.sleep
        require(self.identity is not None and self.runs, "missing active workload linkage")
        # The first daemon owns the chosen third capture. A later recovery
        # daemon is a separate runtime; never attribute its new ordinal to it.
        root = self.runs[0]
        before = set(root.glob("*.bin"))
        process.send_signal(signal.SIGUSR1)
        end = now() + 2
        while now() < end:
            fresh = sorted(set(root.glob("snapshot-*.bin")) - before)
            if fresh:
                result = decode_file(fresh[-1], **self.identity)
                (self.output / "latest-decoded.json").write_text(json.dumps(result, indent=2))
                return result
            require(process.poll() is None, "observer daemon retired before export acknowledgement")
            wait(0.01)  # Bound the ACK wait; file publication proves completion.
        raise InvalidTrace("observer export acknowledgement unavailable")

    def retain(self, processes, primary=None):
        """Called AFTER the original process owner has retired its children."""
        import shutil
        try:
            # Failed project bytes are evidence only. This copy is made after
            # process retirement, not a live SQLite/WAL snapshot or replay proof.
            if primary is not None:
                project = self.root / "Project.mantis"
                count, size = 0, 0
                require(not project.is_symlink(), "owned project link bound")
                for path in project.rglob("*") if project.exists() else ():
                    count += 1
                    require(count <= 2048 and not path.is_symlink(), "owned project file bound")
                    if path.is_file():
                        size += path.stat().st_size
                        require(size <= 32 * 1024 * 1024, "owned project byte bound")
                if project.exists():
                    shutil.copytree(project, self.output / "failed-project")
            summary = dict(process_exits=[p.poll() for p in processes], errors=self.errors,
                           original_failure=primary is not None,
                           observer_qualification="UNQUALIFIED", historical_cause="UNKNOWN")
            (self.output / "retirement.json").write_text(json.dumps(summary, indent=2))
        except BaseException:
            try:
                (self.output / "retention-unavailable.json").write_text(
                    '{"status":"UNQUALIFIED","retention":"unavailable"}\n')
            except BaseException:
                pass
            if primary is None:
                raise InvalidTrace("observer retention unavailable") from None


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--capture-id", required=True)
    parser.add_argument("--raw-id", required=True)
    parser.add_argument("--observer-self-test", action="store_true")
    args = parser.parse_args()
    try:
        result = decode_file(args.trace, capture_id=args.capture_id, raw_id=args.raw_id,
                             workload="observer-self-test" if args.observer_self_test else "actual-acceptance")
    except (InvalidTrace, OSError):
        raise SystemExit("UNQUALIFIED: invalid or unavailable Recorder observer evidence") from None
    print(json.dumps(result, sort_keys=True))
