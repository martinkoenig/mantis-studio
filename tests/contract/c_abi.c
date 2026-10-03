#include <mantis/plugin.h>
#include <stddef.h>
_Static_assert(offsetof(MantisPluginV1, struct_size) == 0, "size prefix");
_Static_assert(offsetof(MantisHostV1, abi_version) == 4, "version prefix");
int main(void) {
    MantisPacketV1 packet = {0};
    packet.struct_size = sizeof(packet);
    packet.abi_version = MANTIS_ABI_V1;
    return packet.abi_version == 1 ? 0 : 1;
}
