#include "backend.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace x1 {
int64_t monotonic_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
Profile load_profile(const std::string &path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot read X1 profile: " + path);
    auto j = nlohmann::json::parse(input);
    if (j.at("format_version") != 1) throw std::runtime_error("Unsupported X1 profile version");
    Profile p;
    for (size_t i = 0; i < 2; ++i) {
        const auto &camera = j.at("measurement_cameras").at(i == 0 ? "left" : "right");
        p.sensors[i] = camera.at("sensor_identity");
        p.buses[i] = camera.value("bus_identity", "");
    }
    if (p.sensors[0].empty() || p.sensors[1].empty() ||
        (p.sensors[0] == p.sensors[1] && p.buses[0] == p.buses[1]))
        throw std::runtime_error("Profile requires two distinct explicit sensor identities");
    auto mode = j.at("mode");
    p.mode = {mode.at("width"), mode.at("height"), mode.at("fps"), mode.at("fourcc")};
    const std::vector<std::string> raw8{"GREY", "Y8  ", "BA81", "RGGB", "GRBG", "GBRG"};
    if (std::find(raw8.begin(), raw8.end(), p.mode.fourcc) == raw8.end())
        throw std::runtime_error("v0.2 requires an explicitly selected native RAW8 fourcc");
    if (!p.mode.width || !p.mode.height || p.mode.width > 8192 || p.mode.height > 8192 ||
        !p.mode.fps || p.mode.fps > 1000) throw std::runtime_error("Invalid requested camera mode");
    p.hardware_sync_configured = j.value("hardware_sync_configured", false);
    p.stall_ms = j.value("stall_timeout_ms", 1000u);
    p.max_timestamp_delta_ns = j.value("max_v4l2_delta_ns", int64_t{4000000});
    if (p.stall_ms < 100 || p.stall_ms > 10000 || p.max_timestamp_delta_ns < 0)
        throw std::runtime_error("Invalid pairing limits");
    p.calibration_id = j.value("calibration_id", "");
    p.calibration_revision = j.value("calibration_revision", uint64_t{});
    p.json = j.dump();
    return p;
}
std::array<CameraInfo, 2> assign(const Profile &p, const std::vector<CameraInfo> &available) {
    std::array<CameraInfo, 2> selected;
    for (size_t role = 0; role < 2; ++role) {
        std::vector<CameraInfo> matches;
        for (const auto &camera : available)
            if (camera.sensor == p.sensors[role] &&
                (p.buses[role].empty() || camera.bus == p.buses[role])) matches.push_back(camera);
        if (matches.empty()) throw std::runtime_error("Configured sensor unavailable: " + p.sensors[role]);
        if (matches.size() != 1) throw std::runtime_error("Ambiguous topology for sensor: " + p.sensors[role]);
        selected[role] = matches.front();
        if (std::find(selected[role].formats.begin(), selected[role].formats.end(), p.mode.fourcc) == selected[role].formats.end())
            throw std::runtime_error("Requested RAW8 format " + p.mode.fourcc + " unavailable on " + p.sensors[role]);
    }
    if (selected[0].video == selected[1].video)
        throw std::runtime_error("Both logical cameras resolve to the same video node");
    return selected;
}
} // namespace x1
