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

Further recording/replay and crash acceptance commands are added with the
service/storage checkpoint. Until then this procedure does not establish a
completed v0.2 acceptance result.
