/* Compiled against the frozen pre-L2 camera acquisition SDK. */
#include "v2/plugin.h"
#include <stdlib.h>
#include <string.h>
typedef struct Device {
    const MantisHostV1 *host;
    uint64_t sequence;
    int streaming;
} Device;
static int describe(MantisDeviceDescriptorV1 *d) {
    static const char *caps[] = {MANTIS_IMAGE_STREAM_V1};
    if (!d || d->struct_size < sizeof(*d) || d->abi_version != 1)
        return 1;
    *d = (MantisDeviceDescriptorV1){sizeof(*d), 1, "legacy-v1", "Frozen ABI v1 fixture", caps, 1};
    return 0;
}
static int create(const MantisHostV1 *host, void **out) {
    if (!host || host->struct_size < sizeof(*host) || host->abi_version != 1 || !out)
        return 1;
    Device *d = calloc(1, sizeof(*d));
    if (!d)
        return 1;
    d->host = host;
    *out = d;
    return 0;
}
static void destroy(void *p) {
    free(p);
}
static int start(void *p) {
    ((Device *)p)->streaming = 1;
    return 0;
}
static int stop(void *p) {
    ((Device *)p)->streaming = 0;
    return 0;
}
static int next(void *p, MantisEmitV1 emit, void *context) {
    Device *d = p;
    void *bytes = NULL;
    uint64_t size = 0;
    if (!d->streaming)
        return 1;
    MantisBuffer *b = d->host->allocate(4, 64);
    if (!b)
        return 1;
    if (d->host->write_map(b, &bytes, &size) || size != 4) {
        d->host->release(b);
        return 1;
    }
    memset(bytes, (int)d->sequence, 4);
    if (d->host->publish(b)) {
        d->host->release(b);
        return 1;
    }
    MantisAttributeV1 a = {sizeof(a), 1, "org.mantis.pixels", "intensity", 1, 2, {2, 2}, {2, 1}, b, 0, 4};
    MantisPacketV1 packet = {
        sizeof(packet), 1, MANTIS_IMAGE, 1, d->sequence++, 0, "legacy.clock", "", 0, "legacy.optical", &a, 1};
    int result = emit(context, &packet);
    d->host->release(b);
    return result;
}
static const MantisDeviceV1 device = {sizeof(device), 1, describe, create, destroy, start, next, stop};
static int initialize(const MantisHostV1 *host) {
    return !host || host->struct_size < sizeof(*host) || host->abi_version != 1;
}
static void shutdown(void) {}
static int enumerate(MantisDiscoverEmitV1 emit, void *ctx) {
    static const char *caps[] = {MANTIS_FRAMESET_STREAM_V1};
    MantisDiscoveredDeviceV1 d = {sizeof(d), 1, "legacy-v2", "", "Frozen camera acquisition", caps, 1, "{}"};
    return emit(ctx, &d);
}
static int open_camera(const MantisHostV1 *h, const char *id, void **p) {
    if (!id || strcmp(id, "legacy-v2"))
        return 1;
    return create(h, p);
}
typedef struct SetEmit {
    MantisFrameSetEmitV1 emit;
    void *ctx;
} SetEmit;
static int emit_set(void *ctx, const MantisPacketV1 *packet) {
    SetEmit *e = ctx;
    MantisObservationV1 image = {sizeof(image), 1, *packet, 0, "legacy.sync", 0, 1, "{}"};
    MantisFrameSetV1 set = {sizeof(set), 1, image, &image, 1};
    set.observation.packet.type_id = MANTIS_FRAMESET;
    set.observation.packet.attributes = NULL;
    set.observation.packet.attribute_count = 0;
    return e->emit(e->ctx, &set);
}
static int camera_next(void *p, uint32_t timeout_ms, MantisFrameSetEmitV1 emit, void *ctx) {
    SetEmit e = {emit, ctx};
    if (!timeout_ms)
        return 2;
    return next(p, emit_set, &e);
}
static int diagnostics(void *p, MantisTextEmitV1 emit, void *ctx) {
    (void)p;
    return emit(ctx, "{}");
}
static const MantisAcquisitionV1 acquisition = {
    sizeof(acquisition), 1, enumerate, open_camera, destroy, start, camera_next, stop, diagnostics};
static const void *query(const char *id) {
    if (!id)
        return NULL;
    if (!strcmp(id, MANTIS_DEVICE_V1))
        return &device;
    if (!strcmp(id, MANTIS_ACQUISITION_V1))
        return &acquisition;
    return NULL;
}
static const MantisPluginV1 plugin = {sizeof(plugin), 1,    "org.mantis.test.legacy-v1", "0.1.0", initialize,
                                      shutdown,       query};
MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t version) {
    return version == 1 ? &plugin : NULL;
}
