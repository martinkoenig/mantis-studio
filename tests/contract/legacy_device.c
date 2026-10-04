/* Compiled against the frozen v0.1 header, not the extended SDK header. */
#include "v1/plugin.h"
#include <stdlib.h>
#include <string.h>
typedef struct Device { const MantisHostV1 *host; uint64_t sequence; int streaming; } Device;
static int describe(MantisDeviceDescriptorV1 *d) {
    static const char *caps[] = {MANTIS_IMAGE_STREAM_V1};
    if (!d || d->struct_size < sizeof(*d) || d->abi_version != 1) return 1;
    *d = (MantisDeviceDescriptorV1){sizeof(*d), 1, "legacy-v1", "Frozen ABI v1 fixture", caps, 1}; return 0;
}
static int create(const MantisHostV1 *host, void **out) {
    if (!host || host->struct_size < sizeof(*host) || host->abi_version != 1 || !out) return 1;
    Device *d = calloc(1, sizeof(*d)); if (!d) return 1;
    d->host = host; *out = d; return 0;
}
static void destroy(void *p) { free(p); }
static int start(void *p) { ((Device *)p)->streaming = 1; return 0; }
static int stop(void *p) { ((Device *)p)->streaming = 0; return 0; }
static int next(void *p, MantisEmitV1 emit, void *context) {
    Device *d = p;
    void *bytes = NULL; uint64_t size = 0;
    if (!d->streaming) return 1;
    MantisBuffer *b = d->host->allocate(4, 64); if (!b) return 1;
    if (d->host->write_map(b, &bytes, &size) || size != 4) { d->host->release(b); return 1; }
    memset(bytes, (int)d->sequence, 4);
    if (d->host->publish(b)) { d->host->release(b); return 1; }
    MantisAttributeV1 a = {sizeof(a), 1, "org.mantis.pixels", "intensity", 1, 2, {2,2}, {2,1}, b, 0, 4};
    MantisPacketV1 packet = {sizeof(packet), 1, MANTIS_IMAGE, 1, d->sequence++, 0, "legacy.clock", "", 0, "legacy.optical", &a, 1};
    int result = emit(context, &packet); d->host->release(b); return result;
}
static const MantisDeviceV1 device = {sizeof(device), 1, describe, create, destroy, start, next, stop};
static int initialize(const MantisHostV1 *host) { return !host || host->struct_size < sizeof(*host) || host->abi_version != 1; }
static void shutdown(void) {}
static const void *query(const char *id) { return id && !strcmp(id, MANTIS_DEVICE_V1) ? &device : NULL; }
static const MantisPluginV1 plugin = {sizeof(plugin), 1, "org.mantis.test.legacy-v1", "0.1.0", initialize, shutdown, query};
MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t version) { return version == 1 ? &plugin : NULL; }
