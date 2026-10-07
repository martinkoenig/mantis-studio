#include <mantis/projected_light.h>
#include <stddef.h>
_Static_assert(offsetof(MantisPluginV1, struct_size) == 0, "size prefix");
_Static_assert(offsetof(MantisHostV1, abi_version) == 4, "version prefix");
_Static_assert(offsetof(MantisAcquisitionV1, abi_version) == 4, "acquisition version prefix");
_Static_assert(offsetof(MantisFrameSetV1, struct_size) == 0, "frameset size prefix");
_Static_assert(offsetof(MantisDiscoveredDeviceV1, struct_size) == 0, "discovery size prefix");
_Static_assert(MANTIS_ABI_V1 == 1u, "root ABI remains 1");
_Static_assert(offsetof(MantisProjectedLightV1, struct_size) == 0, "projected size prefix");
_Static_assert(offsetof(MantisProjectedLightV1, abi_version) == sizeof(uint32_t), "projected version prefix");
_Static_assert(offsetof(MantisProcessorV2, abi_version) == sizeof(uint32_t), "processor v2 root ABI prefix");
_Static_assert(offsetof(MantisAcquisitionProgramV1, struct_size) == 0, "program prefix");
_Static_assert(offsetof(MantisAcquisitionBundleV1, struct_size) == 0, "bundle prefix");
_Static_assert(offsetof(MantisSemanticPacketV1, abi_version) == sizeof(uint32_t), "semantic prefix");
_Static_assert(offsetof(MantisProjectedComponentV1, abi_version) == sizeof(uint32_t), "component prefix");
_Static_assert(offsetof(MantisAbortOutcomeV1, abi_version) == sizeof(uint32_t), "abort prefix");
_Static_assert(MANTIS_MAX_BUNDLE_MEMBERS == 64u, "bounded bundle");
int main(void) {
    MantisPacketV1 packet = {0};
    packet.struct_size = sizeof(packet);
    packet.abi_version = MANTIS_ABI_V1;
    return packet.abi_version == 1 ? 0 : 1;
}
