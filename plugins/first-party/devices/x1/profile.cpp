#include "backend.hpp"
#include "pairing.hpp"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <set>
namespace x1 {
int64_t monotonic_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
Profile load_profile(const std::string &path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot read X1 profile: " + path);
    auto j = nlohmann::json::parse(input);
    if (j.at("format_version") != 1 && j.at("format_version") != 2)
        throw std::runtime_error("Unsupported X1 profile version");
    Profile p;
    p.format_version = j.at("format_version");
    for (size_t i = 0; i < 2; ++i) {
        const auto &camera = j.at("measurement_cameras").at(i == 0 ? "left" : "right");
        p.sensors[i] = camera.at("sensor_identity");
        p.buses[i] = camera.value("bus_identity", "");
        if (p.format_version == 2) {
            p.routes[i] = camera.at("route").get<std::vector<std::string>>();
            if (p.routes[i].size() < 2 || p.routes[i].size() > 16 || p.routes[i].front() != p.sensors[i])
                throw std::runtime_error("Route must start with configured sensor and contain 2..16 entities");
            std::set<std::string> unique;
            for (const auto &entity : p.routes[i])
                if (entity.empty() || !unique.insert(entity).second)
                    throw std::runtime_error("Route contains an empty or repeated entity");
        }
    }
    if (p.sensors[0].empty() || p.sensors[1].empty() ||
        (p.sensors[0] == p.sensors[1] && p.buses[0] == p.buses[1]))
        throw std::runtime_error("Profile requires two distinct explicit sensor identities");
    auto mode = j.at("mode");
    p.mode = {mode.at("width"), mode.at("height"), mode.at("fps"), mode.at("fourcc")};
    const std::vector<std::string> raw8{"GREY", "Y8  ", "BA81", "RGGB", "GRBG", "GBRG"};
    if (std::find(raw8.begin(), raw8.end(), p.mode.fourcc) == raw8.end() && p.mode.fourcc != "Y10P")
        throw std::runtime_error("Select native RAW8 or packed Y10P");
    if (!p.mode.width || !p.mode.height || p.mode.width > 8192 || p.mode.height > 8192 ||
        !p.mode.fps || p.mode.fps > 1000) throw std::runtime_error("Invalid requested camera mode");
    if (p.mode.fourcc == "Y10P" && p.mode.width % 4)
        throw std::runtime_error("Y10P width must be a multiple of four pixels");
    if (p.format_version == 2) {
        p.media_bus_code = mode.at("media_bus_code");
        if (p.media_bus_code != "Y10_1X10" && p.media_bus_code != "Y8_1X8")
            throw std::runtime_error("Unsupported media-bus code in X1 runtime setup");
        if ((p.mode.fourcc == "Y10P") != (p.media_bus_code == "Y10_1X10"))
            throw std::runtime_error("Capture fourcc and media-bus sample depth disagree");
        if (mode.contains("vertical_blanking")) {
            p.vertical_blanking = mode.at("vertical_blanking");
            p.mode.sensor_timing_configured = true;
            if (*p.vertical_blanking < 0) throw std::runtime_error("VBLANK must be nonnegative");
        }
        p.disable_conflicting_links = j.at("runtime_setup").value("disable_conflicting_links", false);
        if (j.at("runtime_setup").at("ownership") != "selected-routes")
            throw std::runtime_error("Runtime setup must explicitly own selected routes");
    } else if (j.contains("runtime_setup") || mode.contains("media_bus_code") || mode.contains("vertical_blanking"))
        throw std::runtime_error("Runtime setup fields require profile version 2; version 1 remains externally configured");
    p.hardware_sync_configured = j.value("hardware_sync_configured", false);
    p.stall_ms = j.value("stall_timeout_ms", 1000u);
    p.max_timestamp_delta_ns = j.value("max_v4l2_delta_ns", p.hardware_sync_configured ? int64_t{4000000}
        : static_cast<int64_t>(recommended_software_tolerance_ns(p.mode.fps)));
    if (p.stall_ms < 100 || p.stall_ms > 10000 || p.max_timestamp_delta_ns < 0)
        throw std::runtime_error("Invalid pairing limits");
    if (!p.hardware_sync_configured && static_cast<uint64_t>(p.max_timestamp_delta_ns) < nominal_half_period_ns(p.mode.fps))
        throw std::runtime_error("Software pairing tolerance must cover half the requested frame period; review max_v4l2_delta_ns");
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
            throw std::runtime_error("Requested native format " + p.mode.fourcc + " unavailable on " + p.sensors[role]);
        selected[role].role = role ? "RIGHT" : "LEFT";
    }
    if (selected[0].video == selected[1].video)
        throw std::runtime_error("Both logical cameras resolve to the same video node");
    return selected;
}
std::string camera_context(const CameraInfo &camera) {
    return camera.role + " " + camera.sensor + " [" + camera.bus + "] (" + camera.video + ")";
}
} // namespace x1
