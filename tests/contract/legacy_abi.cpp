#include <iostream>
#include <mantis/plugin_runtime.hpp>
extern "C" size_t mantis_frozen_layout(size_t *);
void layouts() {
    size_t frozen[256]{}, current[256]{};
    size_t n = 0;
#define LAYOUT_TYPE(T)                                                                                       \
    current[n++] = sizeof(T);                                                                                \
    current[n++] = alignof(T);
#define LAYOUT_FIELD(T, F) current[n++] = offsetof(T, F);
#include "legacy_layout_fields.inc"
#undef LAYOUT_TYPE
#undef LAYOUT_FIELD
    if (mantis_frozen_layout(frozen) != n || !std::equal(current, current + n, frozen))
        throw std::runtime_error("Frozen ABI layout changed");
}
void camera_legacy(const char *path) {
    mantis::plugins::Loaded loaded(path);
    if (loaded.api()->abi_version != 1 || loaded.api()->query_interface(MANTIS_PROJECTED_LIGHT_V1) ||
        loaded.api()->query_interface(MANTIS_PROCESSOR_V2))
        throw std::runtime_error("Legacy optional query changed");
    auto *api = loaded.query<MantisAcquisitionV1>(MANTIS_ACQUISITION_V1);
    mantis::sdk::Acquisition stream(api, mantis::plugins::host_api(), "legacy-v2");
    stream.start();
    struct State {
        const MantisHostV1 *host;
        MantisBuffer *retained{};
        int calls{};
    } state{mantis::plugins::host_api()};
    auto emit = [](void *ctx, const MantisFrameSetV1 *set) noexcept {
        auto &s = *static_cast<State *>(ctx);
        if (!mantis::sdk::compatible_table(set) || set->frame_count != 1 ||
            set->observation.sync_quality != 1 || set->frames[0].packet.sequence != uint64_t(s.calls++))
            return 1;
        if (!s.retained) {
            s.retained = set->frames[0].packet.attributes[0].buffer;
            s.host->retain(s.retained);
        }
        return 0;
    };
    if (stream.next(0, emit, &state) != 2 || state.calls || stream.next(10, emit, &state) ||
        stream.next(10, emit, &state) || state.calls != 2)
        throw std::runtime_error("Legacy acquisition next changed");
    const void *bytes{};
    uint64_t size{};
    bool ok = !state.host->read_map(state.retained, &bytes, &size) && size == 4 &&
              static_cast<const std::byte *>(bytes)[0] == std::byte{0};
    state.host->release(state.retained);
    if (!ok)
        throw std::runtime_error("Legacy retained FrameSet buffer changed");
}
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            return 1;
        layouts();
        camera_legacy(argv[2]);
        mantis::plugins::Loaded loaded(argv[1]);
        auto *device = loaded.query<MantisDeviceV1>(MANTIS_DEVICE_V1);
        auto *host = mantis::plugins::host_api();
        void *instance{};
        if (device->create(host, &instance) || !instance)
            return 1;
        struct Cleanup {
            const MantisDeviceV1 *api;
            void *instance;
            ~Cleanup() {
                api->stop(instance);
                api->destroy(instance);
            }
        } cleanup{device, instance};
        if (device->start(instance))
            return 1;
        struct State {
            const MantisHostV1 *host;
            uint64_t sequence{};
            MantisBuffer *retained{};
        } state{host};
        auto emit = [](void *ctx, const MantisPacketV1 *p) {
            auto &s = *static_cast<State *>(ctx);
            if (!mantis::sdk::compatible_table(p) || p->sequence != s.sequence++ || p->attribute_count != 1)
                return 1;
            const auto &a = p->attributes[0];
            auto bytes = mantis::sdk::read(s.host, a);
            if (bytes.size() != 4 || bytes[0] != static_cast<std::byte>(p->sequence))
                return 1;
            if (!s.retained) {
                s.host->retain(a.buffer);
                s.retained = a.buffer;
            }
            return 0;
        };
        if (device->next(instance, emit, &state) || device->next(instance, emit, &state))
            return 1;
        const void *bytes{};
        uint64_t size{};
        int status = host->read_map(state.retained, &bytes, &size);
        bool unchanged = !status && size == 4 && static_cast<const std::byte *>(bytes)[0] == std::byte{0};
        host->release(state.retained);
        if (!unchanged || loaded.api()->abi_version != 1 ||
            loaded.api()->query_interface(MANTIS_ACQUISITION_V1) ||
            loaded.api()->query_interface(MANTIS_PROJECTED_LIGHT_V1) ||
            loaded.api()->query_interface(MANTIS_PROCESSOR_V2))
            return 1;
        std::cout << "Frozen-header C ABI v1 DSO, callbacks and retained buffers passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
