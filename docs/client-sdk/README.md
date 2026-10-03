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
