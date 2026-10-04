#include <mantis/plugin.h>
#include <stddef.h>
_Static_assert(offsetof(MantisPluginV1, struct_size) == 0, "size prefix");
_Static_assert(offsetof(MantisHostV1, abi_version) == 4, "version prefix");
_Static_assert(offsetof(MantisAcquisitionV1, abi_version) == 4, "acquisition version prefix");
_Static_assert(offsetof(MantisFrameSetV1, struct_size) == 0, "frameset size prefix");
_Static_assert(offsetof(MantisDiscoveredDeviceV1, struct_size) == 0, "discovery size prefix");
int main(void) {
    MantisPacketV1 packet = {0};
    packet.struct_size = sizeof(packet);
    packet.abi_version = MANTIS_ABI_V1;
    return packet.abi_version == 1 ? 0 : 1;
}
