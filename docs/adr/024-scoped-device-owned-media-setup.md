# ADR-024: Profile-selected media runtime setup belongs in the device plugin

Status: Accepted for v0.2 hardware-gap continuation

User Q6A testing established that capture-only S_FMT is insufficient: GREY
1280×800 capture nodes against Y10_1X10 1280×720 upstream pads failed STREAMON
with EPIPE. The known-good development script uses Y10_1X10 through both complete
routes, Y10P 1280×720 capture nodes and sensor VBLANK=196. Kernel, device tree,
boot enablement and permissions remain provisioning responsibilities.

Normal route selection, links, ACTIVE subdevice pad formats, sensor timing and
capture-node negotiation belong in org.mantis.x1. No shell utilities are runtime
dependencies. The implementation uses native Linux ioctls behind the existing
private backend seam; no public C ABI change is required.

Profile version 2 explicitly opts into selected-route ownership and identifies
each route by entity names, sensor/bus identity and mode/timing parameters. Node
paths are resolved dynamically by kernel device major/minor, never profile node
numbers. Version 1 retains externally configured behavior and rejects new setup
fields rather than silently adopting them.

Discovery considers existing disabled links. Each named entity and consecutive
pad link must resolve uniquely. Automatic unconfigured-route discovery fails
on ambiguity. Configuration snapshots relevant state, refuses immutable link
conflicts, changes only necessary measurement-route links/pads/controls, verifies
read-back and attempts rollback on failure. No global media reset is allowed.
Unrelated RGB/other CAMSS paths remain untouched. Kernel EBUSY and rollback
failures must remain explicit; no userspace operation can guarantee atomicity
against unrelated processes changing the media graph.

Hardware setup, Y10P STREAMON, actual receive FPS, recording and synchronization
remain PENDING USER EXECUTION. Requested FPS and VBLANK are not measured exposure
timing or proof of exactly 120 FPS.
