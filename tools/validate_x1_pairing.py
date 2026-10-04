"""Public-client Q6A pairing check; a short run is not sustained-storage acceptance."""
import argparse
import json
import math
import time
import mantis
from google.protobuf.json_format import MessageToDict


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=float, default=1.0)
    parser.add_argument("--allow-fixture", action="store_true", help="Automated tests only")
    args = parser.parse_args()
    require(math.isfinite(args.duration) and 0 < args.duration <= 60, "Duration must be in (0, 60] seconds")
    client = mantis.connect()
    parents = [d for d in client.devices.list() if d.plugin_id == "org.mantis.x1" and not d.parent]
    require(len(parents) == 1, "Expected one configured X1 parent; inspect device diagnostics")
    parent = parents[0]
    require(args.allow_fixture or parent.metadata["backend"] == "Linux V4L2", "Hardware validation rejects the fake backend")
    profile = json.loads(parent.metadata["profile"])
    require(profile.get("max_v4l2_delta_ns", 4000000) == 4000000, "Restore 4 ms tolerance and restart mantisd")
    require(not profile.get("hardware_sync_configured", False), "This check requires software pairing")
    if not args.allow_fixture:
        mode = profile["mode"]
        require((mode["width"], mode["height"], mode["fourcc"], mode.get("vertical_blanking")) ==
                (1280, 720, "Y10P", 196), "Use the validated 720-line Q6A RAW10 profile")
    capture = client.capture.start(parent.id)
    try:
        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline:
            status = capture.status()
            require(not status.error, status.error)
            if status.framesets_produced:
                require(abs(int(status.diagnostics["paired_v4l2_delta_ns"])) <= 4000000, "Selected pair exceeded 4 ms")
            time.sleep(0.05)
    finally:
        capture.stop()  # surfaces capture errors, otherwise waits for finalization
    status = capture.status()
    metrics = status.diagnostics
    require(not status.error, status.error)
    require(not status.active, "Capture remained active after stop")
    require(status.framesets_produced > 0 and status.framesets_produced == status.framesets_committed, "Incomplete paired recording")
    require(status.dropped == 0 and status.queue_saturation == 0, "Raw recorder loss/saturation")
    require(metrics["pairing_mode"] == "timestamp-nearest" and metrics["max_v4l2_delta_ns"] == "4000000", "Unexpected pairing configuration")
    require(metrics["pairing_failures"] == "0" and metrics["pairing_pending_saturation"] == "0", "Pairing failure/saturation")
    require(abs(int(metrics["paired_v4l2_delta_ns"])) <= 4000000, "Final selected pair exceeded 4 ms")
    require(metrics["timestamp_discontinuities"] == "0", "Timestamp discontinuity")
    require(metrics["exposure_skew"] == "unavailable" and metrics["sync_quality"] == "software", "Unexpected timing claim")
    for role in ("left", "right"):
        require(metrics[f"{role}.sequence_gaps"] == "0" and metrics[f"{role}.capture_errors"] == "0", f"{role} acquisition failed")
        require(int(metrics[f"pending_high_water_{role}"]) <= 2, "Pending queue exceeded its bound")
        accounted = status.framesets_produced + int(metrics[f"startup_unmatched_{role}"]) + int(metrics[f"shutdown_unmatched_{role}"])
        require(int(metrics[f"{role}.frames"]) == accounted, f"Unaccounted {role} observation")
        if not args.allow_fixture:
            require((metrics[f"{role}.width"], metrics[f"{role}.height"], metrics[f"{role}.fourcc"],
                     metrics[f"{role}.stride"], metrics[f"{role}.buffer_size"]) ==
                    ("1280", "720", "Y10P", "1600", "1152000"), f"Unexpected {role} capture layout")
            require(metrics[f"{role}_readback_vblank"] == "196", f"Unexpected {role} VBLANK read-back")
    require(metrics["left.timestamp_clock"] == metrics["right.timestamp_clock"] != "linux.v4l2.unknown", "Incomparable clocks")
    artifact = next(a for a in client.artifacts.list() if a.id == capture.raw_artifact)
    require(artifact.state == "FINALIZED", "RawCapture is not FINALIZED")
    if not args.allow_fixture:
        require(metrics.get("media_setup") == "plugin-owned selected routes verified", "Plugin-owned setup was not verified")
    first = client.replay.verify(capture.raw_artifact)
    second = client.replay.verify(capture.raw_artifact)
    require(first == second and first["raw_integrity"] == first["replay"] == "PASS", "Deterministic replay verification failed")
    print(json.dumps({"backend": parent.metadata["backend"],
                      "raw_artifact_state": artifact.state,
                      "capture": MessageToDict(status, preserving_proto_field_name=True),
                      "verification": first, "short_pairing_check": "PASS",
                      "sustained_storage_acceptance": "NOT ESTABLISHED"}, indent=2))


if __name__ == "__main__":
    main()
