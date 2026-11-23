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

`Job.wait(timeout=30)` returns an immutable artifact reference when the completed
Job has a result artifact, otherwise `None`. `Job.wait_report(timeout=30)` waits
for completion and retrieves/parses the Job's JSON status report. Each public
wait uses **one monotonic outer budget**, including all snapshot exchanges,
connect/send/header/body I/O, polling sleeps and (for `wait_report`) the subsequent
report exchange and JSON handling. No nested call renews that budget; every
exchange is capped at the smaller of five seconds and the remaining outer time.
Late completion is rejected, not returned as success. An absent Job ID keeps
waiting; Failed/Cancelled Jobs retain `MantisError(component="job")` diagnostics.
Malformed report JSON still raises `json.JSONDecodeError`.

Timeouts are finite numeric seconds. NaN/infinity raise `ValueError`; nonnumeric
values raise `TypeError`. Zero and negative finite values raise `TimeoutError`
before any connection. Expiry raises `TimeoutError`, **does not cancel runtime
work**, and can leave the daemon outcome **UNKNOWN**. There is no retry or
implicit cancellation. `job.cancel()` explicitly requests cancellation;
`export(..., wait=False)` returns a Job. Custom/duck-typed clients and overridden
no-argument `snapshot()`/`_call()` retain their existing call shapes; custom
transports must bound their own blocking I/O, and the wait rejects late returns.
These are software deadline checks, not a hard real-time scheduling guarantee
or preemption of CPU work. Callbacks and long-running operations do not occur in
the Python interpreter inside the daemon.

`studio.snapshot()`, `studio.artifacts.list()`, `studio.events(after=sequence)`, `studio.enable_plugin(id, enabled)`, `studio.artifacts.recover(id)`, and `studio.project(path, create=True)` expose the other service operations.

### Python control exchange failures

Importing the SDK or constructing `Client`/`connect()` does not open a connection.
Credentials still come from the explicit token or `MANTIS_TOKEN`; the optional
port comes from the argument or `MANTIS_PORT`. Calls connect only to `127.0.0.1`.
There is no ambient remote-host selection or remote TLS transport.

Each synchronous control exchange has **one monotonic five-second I/O budget**
shared by connect, request send, response header and response body. Each blocking
operation uses the remaining budget; fragmented/trickling progress does not
restart it. Expired completion is rejected, including the final response read.
Ordinary control callers retain that five-second policy; nested Job snapshots
shorten it to the remaining outer wait budget.

Transport failures retain their Python `TimeoutError`, `ConnectionError` or other
`OSError` categories. Invalid frame lengths and version/request-correlation
mismatches remain `MantisError`; malformed Protobuf is contained as
`MantisError(component="protocol")` with the fixed diagnosis
`Malformed control response protobuf`, not raw peer bytes or decoder diagnostics.
A valid daemon error retains its original code, component and message. An
initiating exchange error takes precedence over a secondary socket-close error;
a close error after an otherwise successful exchange is not hidden.

**A client timeout does not cancel daemon work.** The command may already have
executed and its outcome can be **UNKNOWN**. The SDK never automatically retries
or replays commands. Reconcile through authoritative daemon state where possible
before deciding what to do next; a later independent call uses a fresh connection
and request ID.

The standalone source-level regression command is:

```bash
/usr/bin/python3 -B tests/qualification/python-control/run.py \
  --output build/python-control --negative-controls
```

It requires installed `protoc`, Python `protobuf` and `PyYAML` (Ubuntu packages
`protobuf-compiler`, `python3-protobuf`, `python3-yaml`). It generates temporary
bindings from the canonical schema under the ignored build directory, checks exact
source/descriptor and independent golden request bytes, runs real owned loopback
protocol peers plus existing capture/calibration/protocol/boundary tests, and
proves existing two-argument receive-caller compatibility, deadline-guard,
blocking-budget and outer Job-budget sensitivity in disposable SDK copies. The compatibility regression
executes the unchanged acceptance test's `raw_request` function with real sockets,
not its surrounding product scenario. The private `_receive(sock, count)` form
gets one five-second budget for that standalone read; normal `_call` exchanges
always pass their single explicit shared deadline across header and body.
Logs, exact input/tool hashes, commands, exits and source restorations are retained
per run; insufficient disk reserve or an unexpected timeout fails qualification.
On these native Linux runners the default cumulative output allowance is 128 MiB
with a hard 1 GiB free-space reserve. A caller may explicitly select
`--output-budget-bytes 524288` (integer range 1–134217728); the default and reserve
are not reduced. Admission reserves the selected allowance; input copies,
generated files, command captures and the receipt count toward it. An allowance
too small for complete qualification fails closed, never omits a gate or converts
a truncated failure into PASS. Captures are streamed/bounded. A dedicated Linux
cleanup owner establishes subreaping **before** admitting each fixed command;
it retires/reaps that command's actual descendants (including nested budget-test
runners/emitters) on exhaustion, timeout/cancellation and leader exit. Only that
dedicated process reaps its adopted children; unrelated caller children and the
caller's subreaper state are untouched. The existing 30-second command and
five-second failure-cleanup bounds are retained; incomplete retirement is an
explicit failure, not PASS. Phase-observed real-emitter composition regressions
and a direct-launch bypass mutant check this boundary. The fixed two-file
canonical protoc generation also has a kernel file-size bound. Receipt,
restoration and cleanup failures do not mask an initiating error. This contains
the fixed cooperative qualification workload, not arbitrary hostile processes,
OS-enforced isolation, or external killing of the cleanup authority.
No generated module is committed. The dedicated `python-control` workflow runs
this command on native Ubuntu x86_64 and ARM64 with read-only repository permissions
and no stored checkout credentials. A protocol peer is **not `mantisd`**: these
checks do not qualify live daemon integration, built wheel/sdist integration,
hardware, non-Linux support, release publishing or whole Issue #75.

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
