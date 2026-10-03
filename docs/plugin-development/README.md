# Developing a native plugin

The stable boundary is [`sdk/c/include/mantis/plugin.h`](../../sdk/c/include/mantis/plugin.h). The header is compiled by a C-only contract test. [`sdk/cpp/include/mantis/sdk.hpp`](../../sdk/cpp/include/mantis/sdk.hpp) provides RAII buffer ownership and exception containment without exporting C++ objects.

## Entry and interfaces

Export `mantis_plugin_entry(requested_abi)`. Return `nullptr` for an unsupported ABI. The root table includes its byte size, ABI version, UTF-8 identity/version, initialize/shutdown functions, and `query_interface`.

| Identifier | Interface |
| --- | --- |
| `org.mantis.device.v1` | Descriptor, create/destroy, start/next/stop |
| `org.mantis.processor.v1` | Typed descriptor and process/emit |
| `org.mantis.exporter.v1` | Accepted data type/schema and export |
| `org.mantis.camera.image-stream.v1` | Discoverable device capability |

Pointers returned by interface queries are borrowed and valid until shutdown. Device instances are created/destroyed by the same plugin. Do not allocate in one runtime and free in another.

## Buffer and packet lifetime

Ask the host to allocate a buffer. Map it writable while unpublished, fill it, discard writable aliases, publish it, then emit an attribute descriptor referencing that buffer. The emit callback is synchronous and the packet/strings/descriptors are borrowed for that call only. The receiver retains shared immutable storage if needed. Release the producer's buffer reference after emit returns. Input buffers are already published; write mapping fails. Do not retain packet descriptor pointers after callbacks.

C++ plugins can use `mantis::sdk::Buffer` and `mantis::sdk::boundary`. Every potentially throwing callback must catch exceptions before returning across C. Untrusted plugins cannot be made safe solely by checking function pointers; isolate them.

Namespaced attributes include scalar type, rank, shape, byte stride, unit, offset and byte extent. `org.mantis.position` is `float32[N,3]` in millimeters. Domain objects retain explicit timestamps, coordinate frames and calibration references. In-process adaptation shares buffer lifetime without copying payload bytes.

## Manifest

Build a module and keep its generated JSON manifest beside it. Example:

```json
{
  "manifest_version": 1,
  "id": "org.example.points",
  "version": "0.1.0",
  "abi_version": 1,
  "library": "example-points.so",
  "kind": "processor",
  "execution": "isolated",
  "permissions": ["project_data"]
}
```

`library` is a sibling filename, never a parent traversal. The runtime checks manifest and binary identity, ABI and interface sizes. Missing or incompatible binaries appear as failed entries with diagnostics. Unapproved IDs are always hosted out of process regardless of their requested execution mode. Shipped approved IDs are part of the trusted installation and must not be spoofed.

On Windows the filename ends in `.dll`; on macOS CMake supplies the module suffix. Do not hard-code platform extensions in your build.

## Examples and tests

- [Virtual Scanner](../../plugins/first-party/devices/virtual-scanner.cpp): public device API only.
- [Example geometry](../../plugins/first-party/algorithms/example-points.cpp): deterministic uint8 image to float32 point cloud.
- [PLY exporter](../../plugins/first-party/exporters/ply-exporter.cpp): public exporter interface only.
- [Crash fixture](../../plugins/examples/crash-test.cpp): intentionally aborts its host.

First-party plugins link only to the public SDK target. The contract test rejects includes from runtime modules.

The host supports `probe`, `process`, and `export` operations. Control arguments refer to separate mapped packet files; payloads are not command arguments. Isolated jobs have a 30-second host timeout. This is crash containment, not a security sandbox.
