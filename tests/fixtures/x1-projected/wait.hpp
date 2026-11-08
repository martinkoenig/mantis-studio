#pragma once
#include <cstdint>

namespace x1_fixture {
// Private synthetic-fixture observer, never a released Plugin SDK interface.
inline constexpr char wait_interface[] = "org.mantis.test.x1-projected-wait";
enum class Wait : uint32_t { none, blocked_next, camera_stall };
struct WaitObserver {
    uint32_t struct_size, abi_version;
    Wait (*blocked)(uint32_t timeout_ms) noexcept;
};
} // namespace x1_fixture
