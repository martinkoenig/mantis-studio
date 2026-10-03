#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <mantis/sdk.hpp>
namespace {
int export_file(const MantisHostV1 *h, const MantisPacketV1 *p, const char *path) {
    return mantis::sdk::boundary([&] {
        if (!p || std::strcmp(p->type_id, MANTIS_POINTS))
            throw std::runtime_error("Expected point cloud");
        const MantisAttributeV1 *position = nullptr;
        for (uint32_t i = 0; i < p->attribute_count; ++i)
            if (std::strcmp(p->attributes[i].name, "org.mantis.position") == 0)
                position = &p->attributes[i];
        if (!position || position->scalar_type != 3 || position->rank != 2 || position->shape[1] != 3 ||
            std::strcmp(position->unit, "mm"))
            throw std::runtime_error("Expected float32 millimeter positions");
        auto bytes = mantis::sdk::read(h, *position);
        std::ofstream o(std::filesystem::path(reinterpret_cast<const char8_t *>(path)));
        o.imbue(std::locale::classic());
        o << "ply\nformat ascii 1.0\ncomment units millimeters\nelement vertex " << position->shape[0]
          << "\nproperty float x\nproperty float y\nproperty float z\nend_header\n"
          << std::setprecision(9);
        for (uint64_t i = 0; i < position->shape[0]; ++i) {
            for (uint64_t j = 0; j < 3; ++j) {
                auto offset = i * position->stride[0] + j * position->stride[1];
                if (offset + 4 > bytes.size())
                    throw std::runtime_error("Position out of bounds");
                float v;
                std::memcpy(&v, bytes.data() + offset, 4);
                o << v << (j == 2 ? '\n' : ' ');
            }
        }
        o.flush();
        if (!o)
            throw std::runtime_error("PLY write failed");
    });
}
int init(const MantisHostV1 *h) {
    return mantis::sdk::compatible(h) ? 0 : 1;
}
void shutdown() {}
const MantisExporterV1 exporter{sizeof(exporter), 1, MANTIS_POINTS, 1, export_file};
const void *query(const char *id) {
    return id && std::strcmp(id, MANTIS_EXPORTER_V1) == 0 ? &exporter : nullptr;
}
const MantisPluginV1 plugin{sizeof(plugin), 1, "org.mantis.ply", "0.1.0", init, shutdown, query};
} // namespace
extern "C" MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t v) {
    return v == 1 ? &plugin : nullptr;
}
