// Reuse the actual public-SDK virtual image generator; only delivery cadence differs.
#define mantis_plugin_entry unpaced_virtual_entry
#include "../../../plugins/first-party/devices/virtual-scanner.cpp"
#undef mantis_plugin_entry
#include <chrono>
#include <memory>
#include <thread>
namespace qualification {
using Clock = std::chrono::steady_clock;
struct Paced {
    void *source{};
    Clock::time_point due{};
    ~Paced() {
        device.destroy(source);
    }
};
int create(const MantisHostV1 *host, void **out) {
    return mantis::sdk::boundary([&] {
        if (!out)
            throw std::runtime_error("Missing instance destination");
        auto p = std::make_unique<Paced>();
        mantis::sdk::check(device.create(host, &p->source));
        *out = p.release();
    });
}
void destroy(void *p) {
    delete static_cast<Paced *>(p);
}
int start(void *p) {
    auto &s = *static_cast<Paced *>(p);
    s.due = Clock::now();
    return device.start(s.source);
}
int next(void *p, MantisEmitV1 emit, void *context) {
    return mantis::sdk::boundary([&] {
        auto &s = *static_cast<Paced *>(p);
        std::this_thread::sleep_until(s.due);
        // One frame/s, never catch up after a slow writer/runner. This fixture
        // needs three committed frames, rather than sustained 30fps throughput.
        s.due = Clock::now() + std::chrono::seconds(1);
        mantis::sdk::check(device.next(s.source, emit, context));
    });
}
int stop(void *p) {
    return device.stop(static_cast<Paced *>(p)->source);
}
const MantisDeviceV1 paced{sizeof(paced), 1, describe, create, destroy, start, next, stop};
const void *query(const char *id) {
    return id && std::strcmp(id, MANTIS_DEVICE_V1) == 0 ? &paced : nullptr;
}
const MantisPluginV1 plugin{sizeof(plugin), 1,    "org.mantis.virtual-scanner", "0.1.0", initialize,
                            shutdown,       query};
} // namespace qualification
extern "C" MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t v) {
    return v == 1 ? &qualification::plugin : nullptr;
}
