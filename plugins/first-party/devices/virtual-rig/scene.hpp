#pragma once
#include <array>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>
namespace virtual_rig {
struct OpticalCamera {
    double fx{}, fy{}, cx{}, cy{};
    std::array<double, 5> brown{}; // k1,k2,p1,p2,k3
    std::array<double, 16> camera_from_scene{};
    uint64_t integration_ns{}, row_time_ns{};
    double gain{}, shot{};
    uint32_t read_noise{};
};
struct Surface {
    std::array<double, 3> center{};
    std::array<double, 2> half{}, feature_center{}, feature_half{};
    uint16_t level{}, feature_level{};
};
struct Scene {
    double near_mm{}, far_mm{}, amplitude_mm{};
    uint64_t period_ns{};
    std::array<Surface, 2> surfaces;
    std::vector<OpticalCamera> cameras;
    std::string snapshot, content_tag;
};
Scene parse_scene(const nlohmann::json &, uint32_t width, uint32_t height, size_t cameras);
uint16_t scene_intensity(const Scene &, uint64_t seed, uint64_t event, int64_t exposure_ns, size_t camera,
                         uint32_t x, uint32_t y);
} // namespace virtual_rig
