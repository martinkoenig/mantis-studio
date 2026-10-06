# Client SDKs

Clients control `mantisd` through the same versioned protocol. They do not instantiate device, pipeline or storage runtimes.

## Python

```python
import mantis

studio = mantis.connect()  # MANTIS_TOKEN, optional MANTIS_PORT
scanner = studio.devices.list()[0]
capture = studio.capture.start(scanner)
try:
    artifact = studio.pipeline.run(capture=capture, recipe="example").wait()
finally:
    capture.stop()
studio.export(artifact, "output.ply")

# Reprocess the recorded reference frame without a connected device.
second = studio.pipeline.run(raw_artifact=capture.raw_artifact).wait()
```

`Job.wait(timeout=30)` returns an immutable artifact reference. A wait timeout does not cancel runtime work. `job.cancel()` requests cancellation; `export(..., wait=False)` returns a Job. Callbacks and long-running operations do not occur in the Python interpreter inside the daemon.

`studio.snapshot()`, `studio.artifacts.list()`, `studio.events(after=sequence)`, `studio.enable_plugin(id, enabled)`, `studio.artifacts.recover(id)`, and `studio.project(path, create=True)` expose the other service operations.

## C++

Link `mantis-client` and include `<mantis/client.hpp>`:

```cpp
mantis::client::Client client;
auto devices = client.devices();
auto capture = client.start_capture({devices.at(0).id()});
auto completed = client.wait(client.run_pipeline(capture));
client.stop_capture(capture);
auto cloud = client.data(completed.result_artifact());
client.wait(client.export_artifact(completed.result_artifact(), "output.ply"));
```

Use RAII/finally handling in applications to stop capture only when desired. Disconnecting the client deliberately does not stop it. The higher-level methods are conveniences over `Client::call(Request)` and generated wire DTOs. Qt is not part of the Client SDK.

Client calls are synchronous; put them on an application worker thread. The Studio bridge demonstrates this with QtConcurrent. Job waits poll bounded control snapshots and check the optional CancellationToken; cancelling a wait token does not implicitly cancel the daemon job.

Data retrieval requests a `DataReference`, then maps the immutable local packet file using the canonical codec. There is no direct connection to the store implementation. Remote clients will need another data-plane transport implementation.

## Dual-camera acquisition and replay

Select the acquisition parent explicitly; child descriptors represent component
capabilities and cannot independently open the synchronized stream. The original
example recipe accepts the Virtual Scanner's single image; future algorithms
consume FrameSets through the same live/replay ImageStream contract.

```python
import mantis
c = mantis.connect()
x1 = next(d for d in c.devices.list() if d.plugin_id == "org.mantis.x1" and not d.parent)
capture = c.capture.start(x1.id)
print(capture.status())
ref = c.preview(capture)
if ref:
    with ref:
        print(ref.locator)  # map/read before releasing the lease
capture.stop()             # waits for daemon-owned finalization
report = c.replay.verify(capture.raw_artifact)
assert report["raw_integrity"] == report["replay"] == "PASS"
c.replay.start(capture.raw_artifact, real_time=True).wait(timeout=3600)
```

C++ adds `device_info`, `capture_status`, `captures`, `preview`, `replay` and
`recover_artifact`. `preview` maps and releases automatically; its immutable
Packet retains mapping ownership. Python exposes locator leases without moving
high-rate pixels through Protobuf. `artifacts.recover(..., timeout=3600)` and C++
recovery use a daemon job and return the finalized descriptor. Stop, recovery and
verification wait up to an hour by default; timeouts do not terminate acquisition
or the underlying job. CLI exposes the same controls (see `mantis-cli --help`).

`Capture.stop()` raises `MantisError(component="capture")` immediately when the
returned capture contains an error, including stop/cleanup failures. Successful
stops continue waiting for daemon-owned asynchronous RawCapture finalization;
finalization job failures retain their own diagnostics. Do not attempt replay of
a failed/recoverable capture before explicit recovery.

Capture diagnostics distinguish `pairing_mode`, selected `left.native_sequence`
and `right.native_sequence`, signed LEFT−RIGHT `native_sequence_offset`,
`paired_v4l2_delta_ns` (RIGHT−LEFT), startup exclusions, terminal lookahead,
per-camera native gaps and raw recorder failures. Native counter equality is
reported as `native_counter_equality`; it is not exposure synchronization.

`receive_fps` is the received-observation count at the last valid dequeue divided
by host elapsed time since acquisition started. It includes startup buffer drain
and exclusions, so short-run rates can differ between cameras and from their
steady cadence. It is separate from driver intervals and exposure timing.

## Calibration (M6)

C++ adds `create_calibration_target`, `build_calibration_dataset`,
`solve_camera_calibration`, `solve_rig_calibration`, `calibrations`,
`calibration_info`, `active_calibration`, `activate_calibration` and `clear_calibration`.
Python exposes these stages through `client.calibration`, including typed target
and measurement dataclasses. `MeasurementProvenance()` preserves present empty
provenance; `None` preserves absence. Artifact inputs accept IDs or references.

Dataset/camera/rig operations return Job IDs (C++) or `Job` (Python), without
implicit waits. Camera solves operate on one role; rigs consume two exact camera
artifacts with explicit left/right slots. Checkerboard supports mono; rig solving
requires ChArUco and rejects Checkerboard as incompatible. Active query means
current configuration for future capture, never the calibration of historical
RawCapture/replay data. Calibration belongs to the open/free platform.

See the complete [C++/Python/CLI workflow and API](../architecture/calibration-api.md).
Studio calibration UI remains M7; hardware acceptance remains M8.
