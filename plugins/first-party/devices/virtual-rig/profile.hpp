#pragma once
#include "scene.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace virtual_rig {
inline constexpr int64_t horizon_ns = 86400000000000; // finite 24-hour generation
struct Camera {
    std::string id, role, stream, clock;
    uint32_t rate_num{}, rate_den{};
    int64_t phase{}, latency{}, offset{}, drift_ppm{}, jitter{}, delivery{};
    bool comparable{};
};
struct Fault {
    size_t camera{};
    uint64_t at{};
    std::string kind;
    int64_t delay{};
};
struct Profile {
    std::string id, format, calibration, json;
    uint32_t width{}, height{}, stride{}, capacity{};
    uint64_t seed{}, bytes{}, tolerance{}, revision{1};
    std::optional<Scene> scene;
    bool triggered{};
    std::vector<Camera> cameras;
    std::vector<Fault> faults;
};
Profile load_profile();
} // namespace virtual_rig
