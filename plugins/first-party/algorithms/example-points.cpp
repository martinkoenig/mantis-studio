#include <cstring>
#include <mantis/sdk.hpp>
namespace {
int describe(MantisNodeDescriptorV1 *o) {
    if (!o || o->struct_size < sizeof(*o))
        return 1;
    *o = {sizeof(*o), 1, "org.mantis.example-points", MANTIS_IMAGE, MANTIS_POINTS, 1, 1, 1, "cpu"};
    return 0;
}
int process(const MantisHostV1 *host, const MantisPacketV1 *input, MantisEmitV1 emit, void *ctx) {
    return mantis::sdk::boundary([&] {
        if (!input || input->struct_size < sizeof(*input) || std::strcmp(input->type_id, MANTIS_IMAGE) ||
            input->attribute_count != 1)
            throw std::runtime_error("Expected image");
        const auto &a = input->attributes[0];
        if (a.scalar_type != 1 || a.rank != 2)
            throw std::runtime_error("Expected uint8 image");
        auto pixels = mantis::sdk::read(host, a);
        auto height = a.shape[0], width = a.shape[1];
        if (width == 0 || height == 0 || width > 4096 || height > 4096)
            throw std::runtime_error("Image too large");
        mantis::sdk::Buffer buffer(host, width * height * 3 * sizeof(float));
        auto out = buffer.writable();
        for (uint64_t y = 0; y < height; ++y)
            for (uint64_t x = 0; x < width; ++x) {
                auto index = y * a.stride[0] + x * a.stride[1];
                if (index >= pixels.size())
                    throw std::runtime_error("Stride");
                float xyz[]{static_cast<float>(x) - static_cast<float>(width) / 2,
                            static_cast<float>(y) - static_cast<float>(height) / 2,
                            static_cast<float>(std::to_integer<uint8_t>(pixels[index])) / 16.0F};
                std::memcpy(out.data() + (y * width + x) * 12, xyz, 12);
            }
        buffer.publish();
        MantisAttributeV1 position{
            sizeof(position), 1, "org.mantis.position", "mm", 3, 2, {width * height, 3}, {12, 4},
            buffer.get(),     0, width * height * 12};
        auto packet = *input;
        packet.type_id = MANTIS_POINTS;
        packet.attributes = &position;
        packet.attribute_count = 1;
        mantis::sdk::check(emit(ctx, &packet));
    });
}
const MantisProcessorV1 processor{sizeof(processor), 1, describe, process};
int init(const MantisHostV1 *h) {
    return mantis::sdk::compatible(h) ? 0 : 1;
}
void shutdown() {}
const void *query(const char *id) {
    return id && std::strcmp(id, MANTIS_PROCESSOR_V1) == 0 ? &processor : nullptr;
}
const MantisPluginV1 plugin{sizeof(plugin), 1, "org.mantis.example-points", "0.1.0", init, shutdown, query};
} // namespace
extern "C" MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t v) {
    return v == 1 ? &plugin : nullptr;
}
