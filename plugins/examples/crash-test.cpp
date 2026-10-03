#include <cstdlib>
#include <cstring>
#include <mantis/sdk.hpp>
namespace {
int init(const MantisHostV1 *h) {
    return mantis::sdk::compatible(h) ? 0 : 1;
}
void shutdown() {}
int describe(MantisNodeDescriptorV1 *d) {
    if (!d || d->struct_size < sizeof(*d))
        return 1;
    *d = {sizeof(*d), 1, "org.mantis.crash-test", MANTIS_IMAGE, MANTIS_POINTS, 1, 1, 0, "cpu"};
    return 0;
}
int process(const MantisHostV1 *, const MantisPacketV1 *, MantisEmitV1, void *) {
    std::abort();
}
const MantisProcessorV1 processor{sizeof(processor), 1, describe, process};
const void *query(const char *id) {
    return id && std::strcmp(id, MANTIS_PROCESSOR_V1) == 0 ? &processor : nullptr;
}
const MantisPluginV1 plugin{sizeof(plugin), 1, "org.mantis.crash-test", "0.1.0", init, shutdown, query};
} // namespace
extern "C" MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t v) {
    return v == 1 ? &plugin : nullptr;
}
