#include "x1/acquisition.hpp"
namespace {
using namespace x1::acquisition;
int initialize(const MantisHostV1 *host) {
    return mantis::sdk::compatible(host) ? 0 : 1;
}
void shutdown() {}
const MantisAcquisitionV1 acquisition{
    sizeof(acquisition), 1, enumerate, open_device, destroy, start, next, stop, diagnostics};
const void *query(const char *id) {
    return id && !std::strcmp(id, MANTIS_ACQUISITION_V1) ? &acquisition : nullptr;
}
const MantisPluginV1 plugin{sizeof(plugin), 1, "org.mantis.x1", "0.2.0", initialize, shutdown, query};
} // namespace
extern "C" MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t abi) {
    return abi == 1 ? &plugin : nullptr;
}
