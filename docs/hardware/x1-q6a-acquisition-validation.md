# Mantis X1 / Q6A acquisition validation

**USER-VALIDATED PASS:** Q6A ARM64 build/tests, X1 discovery with all four mutable
measurement links disabled, stable LEFT/RIGHT mapping and dynamic nodes,
plugin-owned CAMSS setup, Y10_1X10 pads, VBLANK=196 set/read-back, Y10P 1280×720
dual STREAMON, real packed RAW10 acquisition and ~119.223 receive FPS per camera.
A short capture finalized and passed deterministic replay. The original GREY
1280×800/upstream Y10_1X10 1280×720 EPIPE is historical; the validated native setup
must be preserved. Software correspondence passed favorable 4 ms starts, including
the new smoke harness, but a later cold full run failed its 4 ms criterion.
**4 ms is not robustly validated for arbitrary free-running startup phase.**
The candidate 5 ms policy and counted steady-state re-alignment await a real run.

The validated development reference is `/usr/local/sbin/mantis-camera-setup`,
SHA-256 `ae03e77305f1342eb2227cf3a62041c542e8ce94c309db6ad56e898e487e66e4`.
It configures **1280×720 Y10_1X10** on the sensor/bridge pads, **Y10P** capture,
stride 1600, sizeimage 1,152,000 and sensor **VBLANK=196**. Requested target is
120 FPS; VBLANK does not mathematically prove 120.000 FPS. Crop bounds 1280×800
are not a validated acquisition mode. RAW8 remains optional/debug support.

The earlier historical user capture used a **temporary 100 ms** tolerance for diagnosis:

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

The reference software profile now uses **5,000,000 ns**: half the requested
120 FPS period plus 20% headroom. At measured ~119.27 FPS, half-period is ~4.192 ms,
already beyond 4 ms. Software correspondence selects nearest unused comparable
V4L2 timestamps with bounded lookahead and explicitly counts startup and
steady-state exclusions. Observed native periods and half-periods are diagnostics;
only actual cross-camera candidate timestamps establish correspondence failure.
See [ADR-026](../adr/026-bounded-software-observation-pairing.md) for its derivation,
limits and interpretation.
Hardware-configured mode keeps stricter native-counter checks. Host arrival is
diagnostic only; SyncQuality stays software and exposure skew is unavailable.

**PENDING USER EXECUTION:** sustained ten-second/full-rate recording on NVMe or
equivalent storage, real process-crash
recovery after corrected pairing, hardware trigger/synchronization, optical
exposure skew and 1280×800 mode. The developer has no Q6A attached and does not claim the
new 5 ms policy has been executed on hardware. The existing harness itself has
now passed real smoke and full software/discovery stages, as recorded below.

## Preferred one-command validation

```bash
./scripts/validate-x1-q6a.sh --smoke
```

Run from this feature branch after provisioning the kernel/cameras and device
permissions. Install `v4l-utils` for the harness's independent read-backs. Smoke
configures/builds `build/release` (Release, Studio OFF) and performs discovery,
plugin-owned setup, capture, finalization and two deterministic verification calls
through `tools/validate_x1_pairing.py`. The one-second default uses `/dev/shm`
after checking free capacity. It rejects a nonempty `MANTIS_X1_FAKE` and validates
the source profile without overwriting it, including 5 ms tolerance and requested
120 FPS. Old explicit 4 ms profiles fail pre-flight and require an intentional
operator update; the harness never rewrites their tolerance. `--profile PATH` overrides
`MANTIS_X1_PROFILE`, which otherwise overrides `profiles/x1-q6a.json`.

```bash
./scripts/validate-x1-q6a.sh --full
./scripts/validate-x1-q6a.sh --smoke --disable-measurement-links
./scripts/validate-x1-q6a.sh --full --storage /mnt/mantis-nvme --duration 10
```

Full configures/builds Debug with Studio ON and Release with Studio OFF and runs
all CTest suites in each with `TMPDIR=/dev/shm`. CTest exit status and individual
results determine PASS, with skipped tests rejected. The previous sixteen suites
are retained; harness regression tests bring the Linux total to seventeen.
The generic acceptance baseline isolates X1 configuration, with deliberately
injected fixture variables to guard against regression. Build trees can be
selected with `--build-dir` (Release) and `--debug-build-dir`. `--jobs` defaults
to four. `MANTIS_VALIDATION_PYTHON` selects the interpreter (default `/usr/bin/python3`),
which must have Protobuf. Both modes configure/build Release so its daemon,
plugins and generated Python SDK match.

Pre-flight reports branch, exact HEAD, clean/dirty state, architecture, kernel,
build trees, source profile and hash, storage/free capacity and fake state.
Dirty state is recorded, without an invented clean PASS. Capacity reserve is
1.25 × 276.48 MB/s × (duration + one startup second) plus 64 MiB; this is a space
estimate, not a storage performance measurement. Duration must be in (0,60]
seconds, consistent with the public-client smoke validator. The chosen storage
directory must already exist. Each run creates its own project and retains it.

The harness detects `mantis-cameras.service`. An absent, inactive or failed
service stays in that state. An active service is visibly stopped and restarted
on cleanup using only the necessary `sudo systemctl` calls; it is never disabled,
masked or deleted. Unreadable/transitional service states fail. Service state is
checked again before capture and after capture. Other camera users must be stopped
by their owner before validation.

The controller is selected uniquely by `platform:acb3000.isp`. Full mode (or the
explicit smoke flag) records the graph and disables only the four mutable links
listed in the manual procedure below. Discovery must expose one real X1 parent
and both children while those links remain disabled. Their original states are
restored at cleanup, including partial setup failures. Other graph links are
compared before/after. A saved, printed `validation-profile.json` snapshot sets
`runtime_setup.disable_conflicting_links=false` for this run so the plugin cannot
disable unrelated incoming/fan-out routes on conflict. The user's source profile
is unchanged. An EBUSY fails with diagnostics. Successful setup leaves selected
pad/capture formats in place, consistent with the plugin's existing contract;
the harness restores the temporary link changes it made. It never resets the graph.

The harness creates a temporary token, picks a free loopback port by default
(or checks `--port PORT`), starts its matching Release daemon and waits for an
actual authenticated snapshot. It records the daemon PID and stdout/stderr.
EXIT/INT/TERM traps invoke the runner's bounded cleanup: stop only that daemon
and its owned children, restore temporary measurement links, then restore the
previously active service. Cleanup failures turn a successful validation into
FAIL and are recorded alongside an earlier failure. Signals and failed stages
retain all diagnostics and capture data; a forced daemon termination may leave
its recording RECOVERABLE. No unrelated daemon is killed.

Post-capture checks resolve the sensor and video nodes through media entities,
compare them to discovery, and read back Y10P 1280×720, stride 1600, sizeimage
1,152,000, both VBLANK=196 and Y10_1X10/1280×720 on the selected upstream pads.
Correspondence checks accept any native startup offset and explicitly account for
startup exclusions, counted steady-state exclusions and shutdown lookahead. Zero
steady-state exclusions is not required; their deterministic per-pair bound is two.
Per-camera observed periods, half-periods and maxima are reported, with no
cadence-derived PASS/FAIL threshold or automatic widening. Requested FPS is a
nominal target rather than exact sensor timing. The selected timestamp delta must
stay within 5 ms, with zero pairing failures, pending saturation, timestamp
discontinuities, native sequence gaps/capture errors and raw loss/saturation.
Produced/committed counts must agree and exceed zero; RawCapture must be
FINALIZED and deterministic replay/raw integrity must PASS.

Reports default to `validation-output/x1-q6a-YYYYMMDD-HHMMSS-microseconds/` in UTC
(select `--output-root PATH` to change this). They include `summary.txt`,
`summary.json`, `daemon.log`, `devices.json`, `pairing.json`, `media-before.txt`,
`media-after.txt`, `validation-profile.json`, command/service/build logs and
per-camera read-back logs. Full adds `ctest-debug.log`, `ctest-release.log` and
JUnit XML; disabled-link runs add disabled/discovery/cleanup graphs. On early
failure, required artifacts contain explicit NOT REACHED markers. The console
ends with RESULT, and a failing stage, reason and log directory on failure.

A ten-second run remains a hardware smoke validation. It **does not establish
sustained-storage acceptance**, even when the writer reports high throughput.
NVMe/equivalent capacity, sustained duration and throughput criteria below must
be evaluated separately. No new harness run on Q6A is claimed by the developer;
the favorable 4 ms successes and later cold failure below were supplied by the user.

## Historical favorable 4 ms real Q6A starts — user-validated successes

The user validated the unchanged reference: profile v2, software synchronization,
4,000,000 ns tolerance, Y10P 1280×720 / Y10_1X10 and VBLANK=196.

| Observation | Real Q6A result |
| --- | --- |
| Backend / setup | Linux V4L2 / plugin-owned selected routes verified |
| FrameSets produced / committed | 140 / 140 |
| Raw bytes / writer | 323,488,370 / 240.798 MB/s |
| Queue high water / capacity | 11 / 32 |
| Raw drops / queue saturation | 0 / 0 |
| Pairing mode / native sequence offset | timestamp-nearest / +8 |
| Startup unmatched LEFT / RIGHT | 8 / 0 |
| Shutdown unmatched LEFT / RIGHT | 0 / 1 |
| Selected V4L2 delta / absolute delta | -2,861,000 ns / 2.861 ms < 4 ms |
| Pairing failures / pending saturation / timestamp discontinuities | 0 / 0 / 0 |
| LEFT / RIGHT native gaps and capture errors | All zero |
| LEFT / RIGHT VBLANK / stride / buffer size | 196 / 1600 / 1,152,000 each |
| Replay FrameSets / LEFT / RIGHT / passes | 140 / 140 / 140 / 2 |
| Raw integrity / replay / sequences / short pairing check | PASS / PASS / continuous / PASS |

This is a successful 4 ms start, not robust validation for arbitrary phase.
Startup timing can change the native offset; +8 is an observation, not an
acceptance requirement. The historical 100 ms result remains separate evidence.
Sustained storage acceptance is pending.

## Real harness follow-up: favorable smoke PASS, cold full pairing FAIL

| Stage | User-provided Q6A evidence |
| --- | --- |
| Smoke | PASS; native offset +7; startup unmatched 7/0; selected delta +1.686 ms |
| Smoke capture/replay | 123/123 FrameSets; zero pairing/raw failures; FINALIZED; deterministic replay/integrity PASS |
| Full software | Debug 17/17 PASS; Release 17/17 PASS |
| Full discovery/setup | Disabled-link discovery PASS; plugin-owned cold setup reached acquisition |
| Full capture | FAIL: `Camera timestamp delta exceeds profile pairing limit` |

Both runs used the former 4 ms criterion. At ~119.27 FPS, period ≈ 8.384 ms and
half-period ≈ 4.192 ms. Thus even healthy free-running cameras can exceed 4 ms
under arbitrary phase; no retry or favorable-phase wait establishes robustness.

The candidate 5 ms bound leaves ~0.808 ms above measured half-period for timing
variation. Slow independent-oscillator drift can also cross a neighbor boundary;
steady-state exclusions are therefore allowed and explicitly counted within
fixed queue/read/time/per-pair limits. Acquisition still detects native gaps
before correspondence. Recorder losses remain separate. RawCapture stores the
exact published associations and exclusions; replay never re-pairs. SyncQuality
stays software; optical skew is unavailable. Hardware mode remains strict.

**PENDING USER EXECUTION:** the new 5 ms candidate and counted steady-state
re-alignment, especially cold disabled-link acquisition and longer drift runs.
The existing harness is user-validated operationally; sustained storage is pending.

## Real 5 ms full follow-up: observed-period guard blocked acquisition

The user ran `./scripts/validate-x1-q6a.sh --full` on
`4ec71d9d1553216b6a69f942802760e299150567` with 1280×720 Y10P, requested
120 FPS, VBLANK=196, hardware sync false and **5,000,000 ns** correspondence bound.

| Stage | User-provided Q6A evidence |
| --- | --- |
| Software | Debug 17/17 PASS; Release 17/17 PASS |
| Discovery/setup | Disabled-link discovery PASS; plugin-owned setup reached real acquisition |
| Resolved devices in this run | LEFT `/dev/video8`; RIGHT `/dev/video11` (observations, never hard-coded) |
| Capture start | FAIL: `left.observed half-period exceeds software pairing tolerance` |

The observed-period veto rejected acquisition before actual cross-camera
correspondence could be validated. An individual native interval above 10 ms
with a 5 ms bound does not prove that no opposite-camera timestamp is within
5 ms. This is a separate failure from the former 4 ms nearest-pair rejection:
**the 5 ms pairing policy itself has not failed on Q6A**. Its nominal half-period
plus margin derivation remains defensible; observed periods are now diagnostics.
The bound remains fixed at 5 ms and still does not represent optical exposure skew.

**PENDING USER EXECUTION:** real Q6A acquisition after removal of the guard, using:

```bash
./scripts/validate-x1-q6a.sh --full
```

On a genuine nearest-pair failure, `pairing.json` retains the configured bound,
LEFT/RIGHT front timestamps, available lookahead timestamps and distances,
nearest available distance, latest/max native periods and failure reason.
The JSON `capture.diagnostics` event in the retained project's `diagnostics.log`
also survives a failure before `capture.start()` returns a capture handle.
Native continuity, bounded exclusions, recorder integrity and replay checks remain
strict. No retry or tolerance increase is used; sustained storage remains pending.

## Manual diagnostics and reference procedure

The remaining steps are lower-level diagnostics and separate storage/crash
acceptance procedures; use the harness above for routine hardware smoke checks.

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


## Manual 5 ms software correspondence check — candidate awaiting hardware validation

Build the current feature branch, copy/review the unchanged reference profile
again and restart `mantisd` using that profile. Keep
`hardware_sync_configured=false` and `max_v4l2_delta_ns=5000000`. Stop other users
of the selected measurement routes. With the matching daemon/SDK environment
from above, run a brief check before attempting sustained recording:

```bash
PYTHONPATH="$MANTIS_PYTHONPATH" /usr/bin/python3 tools/validate_x1_pairing.py --duration 1 > /tmp/x1-pairing-5ms.json
cat /tmp/x1-pairing-5ms.json
```

The tool uses only the public Python client API. It rejects a fake backend,
a widened tolerance or a different reference mode, records briefly, stops and
waits for finalization, then runs verification twice and compares the reports.
A capture error is raised at stop instead of proceeding to a misleading replay
failure. Save the result and daemon log. The previous 4 ms version passed the
favorable starts above; this 5 ms policy still requires real Q6A execution.
A short buffered/cache-backed
capture does not establish sustained microSD/NVMe throughput.

Require timestamp-nearest pairing, both clocks comparable, the actually selected
RIGHT−LEFT V4L2 delta within ±5 ms, zero native gaps/capture errors and pairing
failures, zero recorder drops/saturation and equal produced/committed FrameSets.
Review selected native LEFT/RIGHT counters and signed LEFT−RIGHT offset; an offset
near six is plausible from the earlier result but not required on every start.
Startup and steady-state exclusions are counted separately from native sequence
loss. Received counts must equal paired FrameSets plus startup exclusions plus
steady-state exclusions plus shutdown lookahead for each camera. Never describe
intentional exclusions as a
lossless recording of all sensor observations.

Pending memory is two owned observations per camera: front plus one successor.
Nearest decisions use monotonically increasing comparable V4L2 timestamps;
ties choose the earlier observation. Startup permits 32 exclusions total within
the profile stall timeout (1000 ms in the reference). Once aligned, at most two
explicitly counted exclusions are allowed between published pairs; publication
must resume within the same stall timeout. The budget persists across calls.
Hardware-configured mode additionally
requires equal native counters; neither path measures optical exposure skew.
Parent metadata stores decisions, tolerance, counters/offset, deltas and lookahead
for exact replay. Camera-native counters still detect gaps, repeats and reversals.
Native media setup, packed bytes and RawCapture schema are unchanged.

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
offset, paired V4L2 delta, host-arrival delta, startup and steady-state unmatched
counts, per-pair exclusion identities, observed periods, terminal
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
  real replay, plus favorable 4 ms timestamp-nearest starts. A later cold full
  acquisition failed the 4 ms criterion, so it is not robust for arbitrary phase. The historical
  141-FrameSet results used 100 ms; the new 140-FrameSet result used the reference
  4 ms and selected -2.861 ms with native offset +8.
- **OBSERVED PAIRING FAILURE:** equal native counters had ~51.8 ms timestamp
  offset, so the old 4 ms equality-based pairing failed. Independent sequence
  origins require software timestamp correspondence. The later cold-start nearest
  failure is a separate half-period/phase issue; the 5 ms candidate is derived in
  ADR-026 and is not the historical 100 ms workaround.
- **IMPLEMENTED AND AUTOMATICALLY TESTED:** timestamp-nearest software pairing,
  two-slot pending queues, startup offsets, bounded counted steady-state
  re-alignment with exact exclusion identities and a period-derived 5 ms reference,
  gap/repeat/reversal/clock/tolerance failures after alignment, strict hardware
  mode, explicit accounting and packed-byte replay. Existing native route setup,
  RAW8, Y10P, preview/QoS, recovery, clients and frozen ABI remain tested.
- **PENDING USER EXECUTION:** sustained ten-second/full-rate NVMe recording,
  real 5 ms/counted re-alignment validation, process-crash recovery, physical hardware
  synchronization, optical exposure skew and 1280×800 mode. No fixture result
  changes these to PASS.
- **DEFERRED:** DMABUF/external-buffer zero-copy, physical trigger programming,
  optical timing instrumentation and scanner algorithms beyond acquisition.

Native memory path is MMAP plus one acquisition copy before QBUF. Raw bytes remain
packed; consumer-only preview maps sample intensity to its upper eight bits.
RawCapture schema 2/container and SQLite project schema 1 are unchanged. Old
RAW8/schema-1 data and the plugin ABI v1 remain usable.
