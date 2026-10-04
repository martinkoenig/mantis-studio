# Mantis X1 / Q6A acquisition validation

Status: **PENDING USER EXECUTION**. No Q6A or OV9281 is attached to the development
workstation. Fixture tests are not hardware evidence.

## Inspect the target

```bash
uname -a
uname -m
cat /etc/os-release
sudo apt update
sudo apt install v4l-utils
v4l2-ctl --list-devices
for media in /dev/media*; do media-ctl -d "$media" -p; done
for video in /dev/video*; do
  echo "$video"
  v4l2-ctl -d "$video" --all
  v4l2-ctl -d "$video" --list-formats-ext
done
```

Save the output. Confirm both sensor identities and enabled paths to capture
nodes. Node numbers are diagnostic only. Configure the board's media links and
subdevice formats using its kernel/board documentation before acquisition.
The plugin follows enabled routes and refuses ambiguous routes; it does not
invent board-specific pad programming. Some QCS6490 drivers require explicit
media/subdevice setup. That behavior needs validation on the installed kernel.

Copy `profiles/x1-q6a.json` to a local configuration file. Review physical LEFT
and RIGHT placement and edit `sensor_identity` accordingly. If names repeat
across controllers, also set `bus_identity` from media-device information.
Select an enumerated native RAW8 fourcc, exact dimensions and target FPS. The
example uses GREY; it is not a claim that every Q6A driver exposes GREY. Packed
Y10P is enumerated diagnostically but is not accepted by this RAW8 milestone.
Only set `hardware_sync_configured` after configuring the physical trigger.
An empty calibration ID means unavailable, not an invented calibration.

The implementation preserves timestamp flags described by the kernel
[buffer API](https://docs.kernel.org/userspace-api/media/v4l/buffer.html).
Clock type and timestamp source vary by driver. Software sequence agreement and
V4L2 timestamp delta do not measure optical exposure skew. Formats are queried
through [ENUM_FMT](https://docs.kernel.org/userspace-api/media/v4l/vidioc-enum-fmt.html).

## Build and discover

Follow BUILDING.md on the Q6A, using the feature branch, then:

```bash
export MANTIS_X1_PROFILE="$PWD/profiles/x1-q6a.json"
unset MANTIS_X1_FAKE
export MANTIS_TOKEN="$(python3 -c 'import secrets; print(secrets.token_hex(24))')"
export MANTIS_PORT=47321
./build/debug/bin/mantisd --project "$PWD/X1-validation.mantis" > /tmp/mantis-x1.log 2>&1 &
DAEMON_PID=$!
./build/debug/bin/mantis-cli plugins list
./build/debug/bin/mantis-cli devices list
```

The plugin is `org.mantis.x1`; the acquisition parent has FrameSet capability
and two children with role, stable identity and current video path. No profile
means no assumed X1 assignment. Review `/tmp/mantis-x1.log` if discovery fails.
The fake backend requires explicit `MANTIS_X1_FAKE=normal` and an explicit profile;
never enable it for hardware acceptance.


## Ten-second acquisition acceptance

Build **Release** for sustained throughput measurements (Debug is for diagnosis).
Use BUILDING.md's Release command. RAW8 at 1280×800×2×120 requires 245.76 MB/s
(234.375 MiB/s) payload and approximately 2.458 GB for ten seconds, before overhead.
Measure the actual target filesystem; a short cache-backed benchmark does not
certify sustained media speed. An SD card may be insufficient. NVMe is a performance
consideration to evaluate, not an unmeasured mandatory hardware requirement.

Run Studio if desired; it uses the same daemon. Then record through the Python
Client SDK (change `build/debug` to the actual matching Release build path):

```bash
PYTHONPATH=build/debug/python /usr/bin/python3 - <<'PY'
import json, time
from pathlib import Path
from google.protobuf.json_format import MessageToDict
import mantis
c = mantis.connect()
parents = [d for d in c.devices.list() if d.plugin_id == 'org.mantis.x1' and not d.parent]
assert len(parents) == 1, 'Inspect plugin diagnostics and profile before capture'
parent = parents[0]
for d in c.devices.list():
    if d.parent == parent.id:
        print(d.metadata['role'], d.metadata['identity'], d.metadata['video_node'])
capture = c.capture.start(parent.id)
Path('/tmp/x1-capture.json').write_text(json.dumps({'capture': capture.id, 'raw': capture.raw_artifact}))
start = time.monotonic()
while time.monotonic() - start < 10:
    status = capture.status()
    print(status.framesets_committed, status.queue_depth, status.dropped, dict(status.diagnostics))
    assert not status.error, status.error
    time.sleep(0.5)
capture.stop()  # waits for daemon-owned finalization; disconnecting UI does not stop acquisition
status = capture.status()
print(json.dumps(MessageToDict(status, preserving_proto_field_name=True), indent=2))
assert status.dropped == 0 and not status.error, 'Zero observed raw-loss events required'
assert status.queue_saturation == 0
PY
```

Save duration, FrameSets produced/committed, camera frame counts and sequence gaps,
queue depth/capacity/high water, total container bytes, writer MB/s and MiB/s,
copy/buffer mode, sequence agreement, V4L2 delta, host-arrival delta and configured
sync mode. Writer throughput is committed raw **payload** divided by duration;
`total_bytes` includes container overhead. Preview delivery FPS is separate from
camera receive FPS. A capture failure means acceptance failed, even if a count
cannot identify all exposures never delivered by a disconnected camera.

Target receive rates should be near the requested mode, but **PENDING USER EXECUTION**:
120 FPS is not an automatic success criterion proven by software fixtures.
`hardware_sync_configured` records an operator assertion; no trigger controller
is programmed in v0.2. Ordinary V4L2/host timestamps do not measure optical skew.

## Replay without cameras

Stop/disable camera access physically, and optionally restart the daemon:

```bash
./build/debug/bin/mantis-cli shutdown
./build/debug/bin/mantisd --project "$PWD/X1-validation.mantis" > /tmp/mantis-x1-replay.log 2>&1 &
DAEMON_PID=$!
./build/debug/bin/mantis-cli captures list
RAW=$(python3 -c 'import json; print(json.load(open("/tmp/x1-capture.json"))["raw"])')
./build/debug/bin/mantis-cli replay verify "$RAW" > /tmp/x1-verify-first.json
./build/debug/bin/mantis-cli replay realtime "$RAW"
./build/debug/bin/mantis-cli replay asap "$RAW"
./build/debug/bin/mantis-cli replay verify "$RAW" > /tmp/x1-verify-second.json
cmp /tmp/x1-verify-first.json /tmp/x1-verify-second.json
```

ASAP/realtime commands return daemon-owned job IDs. Use `mantis-cli job wait JOB`
or inspect snapshots. Verification validates segment/record integrity, continuous
FrameSet sequences, role frame counts, and two canonical replay digests including
all persisted metadata and pixels. Require matching digests and PASS for integrity
and replay. A known-good original capture plus corruption checks establishes byte
preservation; the digest is not a cryptographic security signature.

## Process-crash recovery acceptance

Reconnect cameras and use the same reviewed profile. With the daemon running,
start a second capture and let multiple segments become complete:

```bash
PYTHONPATH=build/debug/python /usr/bin/python3 - <<'PY'
import json, time
from pathlib import Path
import mantis
c = mantis.connect()
d = next(d for d in c.devices.list() if d.plugin_id == 'org.mantis.x1' and not d.parent)
capture = c.capture.start(d.id)
Path('/tmp/x1-crash-capture.json').write_text(json.dumps({'capture': capture.id, 'raw': capture.raw_artifact}))
while True:
    s = capture.status()
    assert not s.error, s.error
    if s.framesets_committed >= 100:
        print('Capture remains daemon-owned:', capture.id, capture.raw_artifact)
        break
    time.sleep(0.1)
PY
kill -KILL "$DAEMON_PID"
wait "$DAEMON_PID"
./build/debug/bin/mantisd --project "$PWD/X1-validation.mantis" > /tmp/mantis-x1-recovery.log 2>&1 &
DAEMON_PID=$!
./build/debug/bin/mantis-cli captures list
RAW=$(python3 -c 'import json; print(json.load(open("/tmp/x1-crash-capture.json"))["raw"])')
./build/debug/bin/mantis-cli artifact recover "$RAW"
./build/debug/bin/mantis-cli replay verify "$RAW"
./build/debug/bin/mantis-cli replay realtime "$RAW"
```

Before recovery require RECOVERABLE. After recovery require FINALIZED, verified
complete records, retained earlier segments and PASS on replay. An incomplete
trailing record is truncated; a corrupt complete record causes an explicit error
and retains the verified prefix on disk for diagnosis. Never accept fabricated
frames to fill a discontinuity. Keep logs and the project for inspection.
A process kill validates process recovery, not device power-loss certification.

## Result classification

- **IMPLEMENTED AND AUTOMATICALLY TESTED:** profile mapping/renumbering, fake dual
  acquisition, sequence/timestamp/stall/disconnect failures, bounded QoS,
  immutable preview references, Studio dual preview, segmented storage,
  process-crash recovery, deterministic replay and clients.
- **IMPLEMENTED BUT REQUIRES Q6A VALIDATION:** native media/V4L2 discovery and
  MMAP streaming, driver RAW8 negotiation, target receive rate, real sustained
  recording, timestamp semantics and physically configured hardware sync.
- **DEFERRED:** DMABUF/external-buffer zero-copy, physical trigger programming,
  true optical exposure-skew measurement, RAW10 capture, board-specific automatic
  media-pad setup, and all scanner algorithms beyond raw acquisition.
