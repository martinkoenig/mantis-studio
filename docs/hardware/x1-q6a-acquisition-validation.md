# Mantis X1 / Q6A acquisition validation

**USER-VALIDATED PASS:** Q6A ARM64 build/tests, X1 discovery with all four mutable
measurement links disabled, stable LEFT/RIGHT mapping and dynamic nodes,
plugin-owned CAMSS setup, Y10_1X10 pads, VBLANK=196 set/read-back, Y10P 1280×720
dual STREAMON, real packed RAW10 acquisition and ~119.223 receive FPS per camera.
A short capture finalized and passed deterministic replay. The original GREY
1280×800/upstream Y10_1X10 1280×720 EPIPE is historical; the validated native setup
must be preserved.

The validated development reference is `/usr/local/sbin/mantis-camera-setup`,
SHA-256 `ae03e77305f1342eb2227cf3a62041c542e8ce94c309db6ad56e898e487e66e4`.
It configures **1280×720 Y10_1X10** on the sensor/bridge pads, **Y10P** capture,
stride 1600, sizeimage 1,152,000 and sensor **VBLANK=196**. Requested target is
120 FPS; VBLANK does not mathematically prove 120.000 FPS. Crop bounds 1280×800
are not a validated acquisition mode. RAW8 remains optional/debug support.

The successful user capture used a **temporary 100 ms** tolerance for diagnosis:

| Real hardware observation | User result |
| --- | --- |
| FrameSets produced / committed; LEFT / RIGHT frames | 141 / 141; 141 / 141 |
| LEFT / RIGHT receive FPS | 119.223228 / 119.223228 |
| Native sequence gaps / capture errors | 0 / 0 on both cameras |
| Queue high water / capacity | 12 / 32 |
| Raw recorder drops / saturation | 0 / 0 |
| Artifact state / bytes / chunks | FINALIZED / 325,625,318 / 5 |
| Artifact hash | fnv1a64:26a3ea204d51863c |
| Two-pass replay | 141 FrameSets, 141 LEFT, 141 RIGHT; continuous; integrity PASS; replay PASS |

This is evidence of native acquisition, recorder integrity and deterministic
replay, not correct cross-camera exposure association. Equal native counters had
~51.5–51.9 ms V4L2 delta (final 51,786,000 ns), with 5–8 ms host-arrival delta,
zero gaps, and both clocks `linux.monotonic` with flags 8193. At ~119.22 FPS this
is approximately six frame periods. Independently started V4L2 counters are
camera-local identifiers, not cross-camera exposure identifiers. The old
`sequence_agreement=aligned` was counter equality, not synchronization.

The profile is restored to **4,000,000 ns** and must remain there. Software
pairing now selects nearest comparable V4L2 timestamps with bounded lookahead,
counts startup exclusions and fails on steady-state unmatched observations.
Hardware-configured mode keeps stricter native-counter checks. Host arrival is
diagnostic only; SyncQuality stays software and exposure skew is unavailable.

**PENDING USER EXECUTION:** corrected 4 ms software pairing on Q6A, sustained
ten-second/full-rate recording on NVMe or equivalent storage, real process-crash
recovery after corrected pairing, hardware trigger/synchronization, optical
exposure skew and 1280×800 mode. Codex has no Q6A attached; it does not claim the
new pairing implementation is hardware-validated.

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
Clock type and timestamp source vary by driver. Camera-local counter equality and
selected V4L2 timestamp delta do not measure optical exposure skew. Formats are queried
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


## Corrected 4 ms software pairing acceptance — PENDING USER EXECUTION

Build the current feature branch, copy/review the unchanged reference profile
again and restart `mantisd` using that profile. Keep
`hardware_sync_configured=false` and `max_v4l2_delta_ns=4000000`. Stop other users
of the selected measurement routes. With the matching daemon/SDK environment
from above, run a brief check before attempting sustained recording:

```bash
PYTHONPATH="$MANTIS_PYTHONPATH" /usr/bin/python3 tools/validate_x1_pairing.py --duration 1 > /tmp/x1-pairing-4ms.json
cat /tmp/x1-pairing-4ms.json
```

The tool uses only the public Python client API. It rejects a fake backend,
a widened tolerance or a different reference mode, records briefly, stops and
waits for finalization, then runs verification twice and compares the reports.
A capture error is raised at stop instead of proceeding to a misleading replay
failure. Save the result and daemon log; do not classify the corrected pairing as
hardware PASS until this command passes on Q6A. A short buffered/cache-backed
capture does not establish sustained microSD/NVMe throughput.

Require timestamp-nearest pairing, both clocks comparable, the actually selected
RIGHT−LEFT V4L2 delta within ±4 ms, zero native gaps/capture errors and pairing
failures, zero recorder drops/saturation and equal produced/committed FrameSets.
Review selected native LEFT/RIGHT counters and signed LEFT−RIGHT offset; an offset
near six is plausible from the earlier result but not required on every start.
Startup exclusions are counted separately and are not steady-state sequence
loss. Received counts must equal paired FrameSets plus startup exclusions plus
shutdown lookahead for each camera. Never describe intentional exclusions as a
lossless recording of all sensor observations.

Pending memory is two owned observations per camera: front plus one successor.
Nearest decisions use monotonically increasing comparable V4L2 timestamps;
ties choose the earlier observation. Startup permits 32 exclusions total within
the profile stall timeout (1000 ms in the reference). Once aligned, any required
unmatched advancement fails capture. Hardware-configured mode additionally
requires equal native counters; neither path measures optical exposure skew.
Parent metadata stores decisions, tolerance, counters/offset, deltas and lookahead
for exact replay. Camera-native counters still detect gaps, repeats and reversals.
The profile, media setup, packed bytes and RawCapture schema are unchanged.

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
copy/buffer mode, pairing mode, selected native counters and signed LEFT−RIGHT
offset, paired V4L2 delta, host-arrival delta, startup unmatched counts, terminal
lookahead counts, pairing failures and configured sync mode. Also record `left_requested_vblank`, `right_requested_vblank`, both
`*_readback_vblank`, requested target FPS, optional `*_sensor_driver_interval_*`
and capture driver interval, and measured per-camera receive FPS. Missing driver
intervals/controls are unavailable, not fabricated. VBLANK is set on graph-resolved
sensor subdevices; capture S_PARM does not override that explicit sensor timing.
Driver intervals are reported without claiming a measured rate.
Writer throughput is committed raw **payload** divided by duration;
`total_bytes` includes container overhead. Preview delivery FPS is separate from
camera receive FPS. A capture failure means acceptance failed, even if a count
cannot identify all exposures never delivered by a disconnected camera.

The short user capture measured ~119.223 receive FPS per camera. Sustained rates
with corrected pairing remain **PENDING USER EXECUTION**; fixtures do not prove
physical timing or storage capacity.
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

- **USER-VALIDATED HARDWARE PASS:** native ARM64 build/tests, disabled-link
  discovery, stable roles/nodes, plugin-owned CAMSS setup, Y10_1X10 pads,
  VBLANK=196 read-back, 720-line Y10P dual STREAMON and ~119.22 receive FPS, zero
  native gaps/capture errors, short lossless recording/finalization and two-pass
  real replay. The 141-FrameSet results above used the diagnostic 100 ms tolerance.
- **OBSERVED PAIRING FAILURE:** equal native counters had ~51.8 ms timestamp
  offset, so the old 4 ms equality-based pairing failed. Independent sequence
  origins require software timestamp correspondence; the tolerance is not widened.
- **IMPLEMENTED AND AUTOMATICALLY TESTED:** timestamp-nearest software pairing,
  two-slot pending queues, startup offsets in both directions, startup budget,
  gap/repeat/reversal/clock/tolerance failures after alignment, strict hardware
  mode, explicit accounting and packed-byte replay. Existing native route setup,
  RAW8, Y10P, preview/QoS, recovery, clients and frozen ABI remain tested.
- **PENDING USER EXECUTION:** corrected 4 ms pairing, sustained ten-second/full-rate
  NVMe recording, corrected-pairing process-crash recovery, physical hardware
  synchronization, optical exposure skew and 1280×800 mode. No fixture result
  changes these to PASS.
- **DEFERRED:** DMABUF/external-buffer zero-copy, physical trigger programming,
  optical timing instrumentation and scanner algorithms beyond acquisition.

Native memory path is MMAP plus one acquisition copy before QBUF. Raw bytes remain
packed; consumer-only preview maps sample intensity to its upper eight bits.
RawCapture schema 2/container and SQLite project schema 1 are unchanged. Old
RAW8/schema-1 data and the plugin ABI v1 remain usable.
