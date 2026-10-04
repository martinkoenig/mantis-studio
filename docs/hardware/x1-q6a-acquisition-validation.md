# Mantis X1 / Q6A acquisition validation

The user has already reported **PASS** for Q6A ARM64 build/tests, loading
org.mantis.x1, discovery of real Qualcomm CAMSS and both OV9281 sensors, stable
LEFT/RIGHT assignment and dynamic capture-node resolution. The first capture
failed **EPIPE / Broken pipe**: GREY 1280×800 capture nodes disagreed with upstream
Y10_1X10 1280×720. Discovery success is not evidence of successful acquisition.

The validated development reference is `/usr/local/sbin/mantis-camera-setup`,
SHA-256 `ae03e77305f1342eb2227cf3a62041c542e8ce94c309db6ad56e898e487e66e4`.
It configures **1280×720 Y10_1X10** on the sensor/bridge pads, **Y10P** capture,
stride 1600, sizeimage 1,152,000 and sensor **VBLANK=196**. Requested target is
120 FPS; VBLANK does not mathematically prove 120.000 FPS. Crop bounds 1280×800
are not a validated acquisition mode. RAW8 remains optional/debug support.

**PENDING USER EXECUTION:** plugin-owned links/pads/timing setup, Y10P STREAMON,
dual FrameSets, measured receive FPS, ten-second lossless recording, NVMe/storage
throughput, replay and crash recovery of real Y10P data, physical synchronization
and optical exposure skew. Codex has no Q6A attached. Fixtures do not certify these.

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

These tools are for inspection only. Mantis runtime does not execute them or
require v4l-utils. Kernel/OV9281 driver, DTBO, boot-time enablement, permissions and
physical camera presence must already be provisioned. Save the output and verify
bus `platform:acb3000.isp` plus these entity routes:

| Role | Sensor → CSIPHY → CSID → VFE RDI → video entity |
| --- | --- |
| LEFT | ov9281 18-0060 → msm_csiphy2 → msm_csid2 → msm_vfe2_rdi0 → msm_vfe2_video0 |
| RIGHT | ov9281 20-0060 → msm_csiphy3 → msm_csid3 → msm_vfe3_rdi0 → msm_vfe3_video0 |

The observed /dev/media0, /dev/video8, /dev/video11 and subdevice numbers are
transient diagnostics. Neither profile nor plugin hardcodes them. Copy and review
the version-2 profile:

```bash
cp profiles/x1-q6a.json /tmp/x1-q6a-validation.json
cat /tmp/x1-q6a-validation.json
sha256sum /usr/local/sbin/mantis-camera-setup
systemctl list-unit-files --type=service | grep -Ei 'mantis|camera'
```

If a setup service invokes the reference script, inspect its unit using
`systemctl cat UNIT_NAME` and stop that actual unit with `sudo systemctl stop
UNIT_NAME` before plugin-owned setup. Do not guess a unit name. Do not run other
capture/setup processes concurrently on these selected routes. The reference
script is historical evidence, not a runtime dependency. The plugin never performs
its global `media-ctl -r` reset.

The profile explicitly names the physical LEFT/RIGHT sensors, media bus, route
entities, Y10_1X10, Y10P, 1280×720 and VBLANK=196. Review physical placement and
actual topology. No /dev/video or /dev/v4l-subdev path belongs in the profile.
Version-1 profiles remain externally configured; they do not silently adopt
plugin-owned setup. Only assert `hardware_sync_configured` if physical trigger
configuration has actually been established; this plugin does not program it.
An empty calibration ID means unavailable.

For an explicit disabled-link discovery check after stopping other users, resolve
the controller by bus identity and disable **only** the four measurement links:

```bash
MEDIA=''
for candidate in /dev/media*; do
  if media-ctl -d "$candidate" -p | grep -Fq 'platform:acb3000.isp'; then
    if [ -n "$MEDIA" ]; then echo 'Ambiguous media bus'; exit 1; fi
    MEDIA="$candidate"
  fi
done
if [ -z "$MEDIA" ]; then echo 'Reference media bus not found'; exit 1; fi
media-ctl -d "$MEDIA" -l '"msm_csiphy2":1 -> "msm_csid2":0 [0]'
media-ctl -d "$MEDIA" -l '"msm_csid2":1 -> "msm_vfe2_rdi0":0 [0]'
media-ctl -d "$MEDIA" -l '"msm_csiphy3":1 -> "msm_csid3":0 [0]'
media-ctl -d "$MEDIA" -l '"msm_csid3":1 -> "msm_vfe3_rdi0":0 [0]'
media-ctl -d "$MEDIA" -p
```

This optional debugging step exercises pre-enabled-route independence; production
uses native ioctls. Discovery must still expose the parent/children with these
links disabled. Capture start must enable the selected links, set and verify both
pads of each bridge/VFE and sensor pad 0, set/read VBLANK, then negotiate capture
nodes and MMAP/STREAMON. Incoming conflicts require explicit profile permission;
unrelated paths stay untouched. Errors name role, sensor, entity/pad/node and
operation/errno. Setup failure attempts selected upstream rollback, reporting
rollback failure separately. Capture-node S_FMT is not restored to its previous
mode. Successful stop leaves selected media configuration in place. Kernel EBUSY
or concurrent graph mutation is a failure, not a fallback to another route.

The implementation preserves timestamp flags described by the kernel
[buffer API](https://docs.kernel.org/userspace-api/media/v4l/buffer.html).
Clock type and timestamp source vary by driver. Software sequence agreement and
V4L2 timestamp delta do not measure optical exposure skew. Formats are queried
through [ENUM_FMT](https://docs.kernel.org/userspace-api/media/v4l/vidioc-enum-fmt.html).

## Build and discover

Follow BUILDING.md on the Q6A, using the feature branch, then:

```bash
MANTIS_BUILD_DIR="$PWD/build/debug"
MANTIS_BIN="$MANTIS_BUILD_DIR/bin"
MANTIS_PYTHONPATH="$MANTIS_BUILD_DIR/python"
MANTIS_CAPTURE_ROOT="$PWD" # select the actual capture filesystem after storage validation
export MANTIS_X1_PROFILE="/tmp/x1-q6a-validation.json"
unset MANTIS_X1_FAKE
export MANTIS_TOKEN="$(python3 -c 'import secrets; print(secrets.token_hex(24))')"
export MANTIS_PORT=47321
wait_mantisd() {
  PYTHONPATH="$MANTIS_PYTHONPATH" /usr/bin/python3 - <<'PYWAIT'
import time
import mantis
client = mantis.connect()
deadline = time.monotonic() + 20
while True:
    try:
        client.snapshot()
        break
    except (OSError, mantis.MantisError):
        if time.monotonic() >= deadline:
            raise RuntimeError('Daemon startup timed out; inspect /tmp/mantis-x1*.log')
        time.sleep(0.1)
PYWAIT
}
"$MANTIS_BIN"/mantisd --project "$MANTIS_CAPTURE_ROOT/X1-validation.mantis" > /tmp/mantis-x1.log 2>&1 &
DAEMON_PID=$!
wait_mantisd
"$MANTIS_BIN"/mantis-cli plugins list
"$MANTIS_BIN"/mantis-cli devices list
```

The plugin is `org.mantis.x1`; the acquisition parent has FrameSet capability
and two children with role, stable identity and current video path. No profile
means no assumed X1 assignment. Review `/tmp/mantis-x1.log` if discovery fails.
The fake backend requires explicit `MANTIS_X1_FAKE=normal` and a fixture profile
with `bus_identity: fixture`;
never enable it for hardware acceptance.


## Ten-second acquisition acceptance

Build **Release** for sustained throughput measurements (Debug is for diagnosis).
Use BUILDING.md's Release command. Packed Y10P at 1280×720×1.25×2×120 requires
**276.48 MB/s / 263.671875 MiB/s** payload and approximately **2.765 GB** for ten
seconds before overhead. Y10P 1280×800 would require 307.20 MB/s; that mode is not
validated. The user's current `/dev/mmcblk1p3` ext4 microSD measured approximately
**32.04 MB/s append payload**. That is a storage observation, not a RawCapture
implementation ceiling. No NVMe is installed yet. Do not expect a successful
full-rate ten-second capture on this evidence; sustained acceptance is pending
NVMe or equivalent high-throughput storage testing. The bounded raw queue must
fail explicitly if storage cannot keep up; preview does not hide that failure.

Place the `.mantis` project on the actual target capture filesystem, with enough
space. Run `mantis-acquisition-benchmark CAPTURE_DIRECTORY 64 Y10P` there first;
a short cached result is still not proof of sustained speed. Avoid interpreting
Debug throughput or the existing microSD result as hardware acceptance. A brief
initial capture can inspect setup/STREAMON diagnostics, but writer saturation
must remain an explicit failed capture, not a recording PASS.

Use the same shell for these steps. After building Release, restart using matching
Release executables, plugins and Python package before throughput acceptance:

```bash
"$MANTIS_BIN"/mantis-cli shutdown
wait "$DAEMON_PID"
MANTIS_BUILD_DIR="$PWD/build/release"
MANTIS_BIN="$MANTIS_BUILD_DIR/bin"
MANTIS_PYTHONPATH="$MANTIS_BUILD_DIR/python"
"$MANTIS_BIN"/mantisd --project "$MANTIS_CAPTURE_ROOT/X1-validation.mantis" > /tmp/mantis-x1.log 2>&1 &
DAEMON_PID=$!
wait_mantisd
```

Run Studio from a Studio-enabled build if desired; it remains a protocol client.
Then record through the matching Python Client SDK:

```bash
PYTHONPATH="$MANTIS_PYTHONPATH" /usr/bin/python3 - <<'PY'
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
sync mode. Also record `left_requested_vblank`, `right_requested_vblank`, both
`*_readback_vblank`, requested target FPS, optional `*_sensor_driver_interval_*`
and capture driver interval, and measured per-camera receive FPS. Missing driver
intervals/controls are unavailable, not fabricated. VBLANK is set on graph-resolved
sensor subdevices; capture S_PARM does not override that explicit sensor timing.
Driver intervals are reported without claiming a measured rate.
Writer throughput is committed raw **payload** divided by duration;
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
"$MANTIS_BIN"/mantis-cli shutdown
wait "$DAEMON_PID"
"$MANTIS_BIN"/mantisd --project "$MANTIS_CAPTURE_ROOT/X1-validation.mantis" > /tmp/mantis-x1-replay.log 2>&1 &
DAEMON_PID=$!
wait_mantisd
"$MANTIS_BIN"/mantis-cli captures list
RAW=$(python3 -c 'import json; print(json.load(open("/tmp/x1-capture.json"))["raw"])')
"$MANTIS_BIN"/mantis-cli replay verify "$RAW" > /tmp/x1-verify-first.json
"$MANTIS_BIN"/mantis-cli replay realtime "$RAW"
"$MANTIS_BIN"/mantis-cli replay asap "$RAW"
"$MANTIS_BIN"/mantis-cli replay verify "$RAW" > /tmp/x1-verify-second.json
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
PYTHONPATH="$MANTIS_PYTHONPATH" /usr/bin/python3 - <<'PY'
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
"$MANTIS_BIN"/mantisd --project "$MANTIS_CAPTURE_ROOT/X1-validation.mantis" > /tmp/mantis-x1-recovery.log 2>&1 &
DAEMON_PID=$!
wait_mantisd
"$MANTIS_BIN"/mantis-cli captures list
RAW=$(python3 -c 'import json; print(json.load(open("/tmp/x1-crash-capture.json"))["raw"])')
"$MANTIS_BIN"/mantis-cli artifact recover "$RAW"
"$MANTIS_BIN"/mantis-cli replay verify "$RAW"
"$MANTIS_BIN"/mantis-cli replay realtime "$RAW"
```

Before recovery require RECOVERABLE. After recovery require FINALIZED, verified
complete records, retained earlier segments and PASS on replay. An incomplete
trailing record is truncated; a corrupt complete record causes an explicit error
and retains the verified prefix on disk for diagnosis. Never accept fabricated
frames to fill a discontinuity. Keep logs and the project for inspection.
A process kill validates process recovery, not device power-loss certification.

## Result classification

- **USER-REPORTED HARDWARE PASS:** Q6A ARM64 build/tests, X1 loading, real CAMSS
  traversal, dual sensor discovery, stable LEFT/RIGHT and capture-node resolution.
- **OBSERVED FAILURE:** initial GREY 1280×800 STREAMON against Y10_1X10 1280×720
  upstream graph returned EPIPE. The known-good external setup is documented above.
- **IMPLEMENTED AND AUTOMATICALLY TESTED:** versioned route profiles, discovery
  with disabled links, scoped conflicts and unrelated-route preservation,
  transactional rollback/read-back failures, VBLANK fixtures, RAW8/Y10P acquisition,
  known-vector unpack/display mapping, exact packed storage/replay, bounded QoS,
  leased preview, real Studio dual preview, clients, restart/recovery, frozen ABI,
  old capture readability, cleanup error propagation and quiet idle listener.
- **IMPLEMENTED BUT REQUIRES Q6A VALIDATION — PENDING USER EXECUTION:** plugin-owned
  media setup/Y10P STREAMON, actual FrameSets and receive FPS, sustained recording
  on suitable storage, real Y10P replay/crash recovery, timing source semantics,
  physical synchronization and optical exposure skew. No hardware PASS is claimed.
- **DEFERRED:** DMABUF/external-buffer zero-copy, 1280×800 mode investigation,
  physical trigger programming, true optical timing instrumentation and scanner
  algorithms beyond acquisition. No optical exposure-skew measurement exists.

Native memory path is MMAP plus one acquisition copy before QBUF. Raw bytes remain
packed; consumer-only preview maps sample intensity to its upper eight bits.
RawCapture schema 2/container and SQLite project schema 1 are unchanged. Old
RAW8/schema-1 data and the plugin ABI v1 remain usable.
