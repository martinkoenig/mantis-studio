#pragma once
// Plugin-private development clock control; not part of the Mantis SDK ABI.
// Public acquisition remains MantisAcquisitionV1. Select virtual time before start,
// advance it independently of next(), and never overlap normal acquisition calls.
#include <cstdint>
namespace virtual_rig {
inline constexpr const char *clock_interface = "org.mantis.virtual-rig.clock.v1";
struct ClockV1 {
    uint32_t struct_size, abi_version;
    int (*use_virtual_time)(void *);
    int (*advance)(void *, int64_t elapsed_ns);
    // Development cancellation/state observables for the real wait path.
    int (*wait_for_waiter)(void *, uint32_t timeout_ms);
    int (*cancel_wait)(void *);
};
} // namespace virtual_rig
