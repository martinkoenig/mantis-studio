# ADR-021: Enumerated acquisition and FrameSet transport

Status: Accepted for v0.2 development

The static v1 device table cannot enumerate composite hardware or preserve
per-camera time/sync metadata. Introduce `org.mantis.acquisition.v1`, queried
from the unchanged MantisPluginV1 root. Enumeration uses synchronous callbacks
and borrowed descriptors; selected stable device IDs open owned instances.
Parent IDs express the generic device graph. Only acquisition-capable parents
are opened; children describe components.

The interface emits a versioned FrameSet containing a header and image headers.
Each image references published host buffers. Receiver retention uses the
existing ownership rules. Metadata is a bounded JSON object of string values.
The new Packet FrameSet representation retains immutable child packets and
does not flatten observations into unrelated images. New encoding is explicit;
v1 packets remain readable. ABI v1 structures are unchanged.

Rejected alternatives: in-place ABI changes, product-specific core types,
implicit pairing of independent packets, and introducing external driver-buffer
ownership before its backpressure consequences have been measured.
