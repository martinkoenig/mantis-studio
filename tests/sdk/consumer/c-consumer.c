#include <mantis/plugin.h>
#include <mantis/projected_light.h>
#include <mantis/semantic_views.h>
#include <stdio.h>
#include <string.h>

_Static_assert(offsetof(MantisPluginV1, struct_size) == 0, "size prefix");
_Static_assert(offsetof(MantisHostV1, abi_version) == sizeof(uint32_t), "version prefix");
_Static_assert(MANTIS_ABI_V1 == 1, "SDK package version is not the C ABI version");

static const MantisPluginV1 plugin = {
    sizeof(MantisPluginV1), 1, "org.example.sdk-consumer", "0.0.1", NULL, NULL, NULL};
MANTIS_EXPORT const MantisPluginV1 *mantis_plugin_entry(uint32_t requested_abi) {
    return requested_abi == 1 ? &plugin : NULL;
}
int main(void) {
    const MantisPluginV1 *root = mantis_plugin_entry(1);
    if (!root || root->struct_size != sizeof(*root) || root->abi_version != 1 ||
        strcmp(root->id, "org.example.sdk-consumer") || mantis_plugin_entry(2) != NULL) {
        fputs("C entry ABI rejection failed\n", stderr);
        return 1;
    }
    puts("C public headers and entry ABI rejection passed");
    return 0;
}
