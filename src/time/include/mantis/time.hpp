#pragma once
#include <chrono>
#include <mantis/base.hpp>
namespace mantis::time {
struct ClockDomain {
    Id id;
    std::string name;
};
struct DeviceTimestamp {
    int64_t nanoseconds{};
    ClockDomain domain;
};
struct MonotonicTimestamp {
    int64_t nanoseconds{};
    static MonotonicTimestamp now() {
        return {std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count()};
    }
};
struct SequenceNumber {
    uint64_t value{};
};
struct SyncGroup {
    Id id;
    uint64_t trigger{};
};
enum class SyncQuality { unknown, software, hardware };
struct ClockMapping {
    ClockDomain source, target;
    double scale{1}, offset_ns{}, uncertainty_ns{};
};
} // namespace mantis::time
