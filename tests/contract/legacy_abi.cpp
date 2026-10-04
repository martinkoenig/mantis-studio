#include <iostream>
#include <mantis/plugin_runtime.hpp>
int main(int argc, char **argv) {
    try {
        if (argc != 2) return 1;
        mantis::plugins::Loaded loaded(argv[1]);
        auto *device = loaded.query<MantisDeviceV1>(MANTIS_DEVICE_V1);
        auto *host = mantis::plugins::host_api();
        void *instance{};
        if (device->create(host, &instance) || !instance) return 1;
        struct Cleanup { const MantisDeviceV1 *api; void *instance; ~Cleanup() { api->stop(instance); api->destroy(instance); } } cleanup{device, instance};
        if (device->start(instance)) return 1;
        struct State { const MantisHostV1 *host; uint64_t sequence{}; MantisBuffer *retained{}; } state{host};
        auto emit = [](void *ctx, const MantisPacketV1 *p) {
            auto &s = *static_cast<State *>(ctx);
            if (!mantis::sdk::compatible_table(p) || p->sequence != s.sequence++ || p->attribute_count != 1) return 1;
            const auto &a = p->attributes[0]; auto bytes = mantis::sdk::read(s.host, a);
            if (bytes.size() != 4 || bytes[0] != static_cast<std::byte>(p->sequence)) return 1;
            if (!s.retained) { s.host->retain(a.buffer); s.retained = a.buffer; } return 0;
        };
        if (device->next(instance, emit, &state) || device->next(instance, emit, &state)) return 1;
        const void *bytes{}; uint64_t size{};
        int status = host->read_map(state.retained, &bytes, &size);
        bool unchanged = !status && size == 4 && static_cast<const std::byte *>(bytes)[0] == std::byte{0};
        host->release(state.retained);
        if (!unchanged || loaded.api()->query_interface(MANTIS_ACQUISITION_V1)) return 1;
        std::cout << "Frozen-header C ABI v1 DSO, callbacks and retained buffers passed\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
