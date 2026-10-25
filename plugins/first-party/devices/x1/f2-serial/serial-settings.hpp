#pragma once
#include <cstdint>

namespace x1::f2::posix {
// Private Linux-only caller-side configuration. Borrowed FD; never opened,
// closed, duplicated or retained. Caller exclusively owns it until transfer to
// FdTransport, and must serialize configuration against all I/O/close operations.
struct SerialProfile {
    uint32_t baud = 115200;
    uint8_t data_bits = 8, stop_bits = 1;
    bool parity{}, software_flow{}, hardware_flow{};
    bool supported() const noexcept;
};
enum class SerialStatus { configured, unsupported, inspect_failed, apply_failed, verify_failed };
struct SerialResult {
    SerialStatus status{};
    int error{}, rollback_error{};
    bool rollback_attempted{};
    explicit operator bool() const noexcept {
        return status == SerialStatus::configured;
    }
};
// Applies immediately (TCSANOW): no drain/flush, waits or transport side effects.
// Failure after application attempts rollback with readback; disappearance or
// concurrent external setting changes can make rollback uncertain, never hidden.
SerialResult configure_serial(int borrowed_fd, SerialProfile = {}) noexcept;
} // namespace x1::f2::posix
