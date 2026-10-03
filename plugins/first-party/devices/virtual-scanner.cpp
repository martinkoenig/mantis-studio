#include <cstring>
#include <mantis/sdk.hpp>
namespace {
struct Device {
    const MantisHostV1 *host;
    uint64_t sequence{};
    bool streaming{};
};
int describe(MantisDeviceDescriptorV1 *out) {
    if (!out || out->struct_size < sizeof(*out))
        return 1;
    static const char *caps[]{MANTIS_IMAGE_STREAM_V1};
    *out = {sizeof(*out), 1, "virtual-scanner", "Virtual Scanner", caps, 1};
    return 0;
}
int create(const MantisHostV1 *host, void **out) {
    return mantis::sdk::boundary([&] {
        if (!mantis::sdk::compatible(host) || !out)
            throw std::runtime_error("ABI");
        *out = new Device{host};
    });
}
void destroy(void *p) {
    delete static_cast<Device *>(p);
}
int start(void *p) {
    auto &d = *static_cast<Device *>(p);
    d.sequence = 0;
    d.streaming = true;
    return 0;
}
int stop(void *p) {
    static_cast<Device *>(p)->streaming = false;
    return 0;
}
int next(void *p, MantisEmitV1 emit, void *ctx) {
    return mantis::sdk::boundary([&] {
        auto &d = *static_cast<Device *>(p);
        if (!d.streaming)
            throw std::runtime_error("Stopped");
        mantis::sdk::Buffer buffer(d.host, 64 * 48);
        auto bytes = buffer.writable();
        for (size_t y = 0; y < 48; ++y)
            for (size_t x = 0; x < 64; ++x)
                bytes[y * 64 + x] = static_cast<std::byte>((x * 3 + y * 5 + d.sequence) % 256);
        buffer.publish();
        MantisAttributeV1 a{sizeof(a), 1,       "org.mantis.pixels", "intensity", 1,      2,
                            {48, 64},  {64, 1}, buffer.get(),        0,           64 * 48};
        MantisPacketV1 f{sizeof(f),
                         1,
                         MANTIS_IMAGE,
                         1,
                         d.sequence,
                         static_cast<int64_t>(d.sequence * 33333333),
                         "org.mantis.virtual.clock",
                         "org.mantis.virtual.calibration",
                         1,
                         "org.mantis.world",
                         &a,
                         1};
        mantis::sdk::check(emit(ctx, &f));
        ++d.sequence;
    });
}
const MantisDeviceV1 device{sizeof(device), 1, describe, create, destroy, start, next, stop};
int initialize(const MantisHostV1 *h) {
    return mantis::sdk::compatible(h) ? 0 : 1;
}
void shutdown() {}
const void *query(const char *id) {
    return id && std::strcmp(id, MANTIS_DEVICE_V1) == 0 ? &device : nullptr;
}
const MantisPluginV1 plugin{sizeof(plugin), 1,    "org.mantis.virtual-scanner", "0.1.0", initialize,
                            shutdown,       query};
} // namespace
extern "C" MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t v) {
    return v == 1 ? &plugin : nullptr;
}
