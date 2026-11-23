/* External installed-header consumer, never a shipping product plugin. */
#include <mantis/plugin.h>
#ifndef TEST_ABI
#define TEST_ABI 1
#endif
#ifndef TEST_SIZE
#define TEST_SIZE sizeof(MantisPluginV1)
#endif
static int initialize(const MantisHostV1 *host) {
    return !host || host->struct_size < sizeof(*host) || host->abi_version != 1;
}
static void shutdown_plugin(void) {}
static const void *query(const char *id) {
    (void)id;
    return 0;
}
static const MantisPluginV1 plugin = {
    TEST_SIZE, TEST_ABI, "org.example.installed-c", "0.1.0", initialize, shutdown_plugin, query};
MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t abi) {
    return abi == 1 ? &plugin : 0;
}
