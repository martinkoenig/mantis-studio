#include "profile.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
namespace virtual_rig {
namespace {
using Json = nlohmann::json;
void require(bool condition, const char *error) {
    if (!condition)
        throw std::invalid_argument(error);
}
void keys(const Json &j, std::initializer_list<const char *> allowed) {
    require(j.is_object(), "Profile entry must be an object");
    for (auto it = j.begin(); it != j.end(); ++it)
        require(std::any_of(allowed.begin(), allowed.end(), [&](const char *s) { return it.key() == s; }),
                "Unknown profile field");
}
int64_t integer(const Json &j, const char *key, int64_t low, int64_t high) {
    const auto &v = j.at(key);
    require(v.is_number_integer(), "Profile requires an integer");
    if (v.is_number_unsigned())
        require(v.get<uint64_t>() <= uint64_t(high), "Profile integer out of range");
    auto n = v.get<int64_t>();
    require(n >= low && n <= high, "Profile integer out of range");
    return n;
}
std::string identity(const Json &j, const char *key) {
    auto s = j.at(key).get<std::string>();
    require(!s.empty() && s.size() <= 96, "Invalid identity");
    for (unsigned char c : s)
        require((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                    c == '_' || c == '-',
                "Invalid identity character");
    return s;
}
} // namespace
Profile load_profile() {
    const auto path = std::getenv("MANTIS_VIRTUAL_RIG_PROFILE");
    require(path && *path, "Set MANTIS_VIRTUAL_RIG_PROFILE explicitly");
    require(std::filesystem::is_regular_file(path), "Virtual rig profile must be a regular file");
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "Cannot read virtual rig profile");
    // Bound input before parsing, including pipes and files changing during read.
    std::string text(16385, '\0');
    in.read(text.data(), std::streamsize(text.size()));
    require(!in.bad(), "Cannot read complete virtual rig profile");
    text.resize(size_t(in.gcount()));
    require(text.size() <= 16384, "Virtual rig profile exceeds 16 KiB");
    std::vector<std::set<std::string>> object_keys;
    auto j = Json::parse(text, [&](int depth, Json::parse_event_t event, Json &value) {
        require(depth <= 8, "Profile nesting exceeds eight levels");
        if (event == Json::parse_event_t::object_start)
            object_keys.emplace_back();
        if (event == Json::parse_event_t::key)
            require(object_keys.back().insert(value.get<std::string>()).second, "Duplicate profile key");
        if (event == Json::parse_event_t::object_end)
            object_keys.pop_back();
        return true;
    });
    keys(j, {"version", "id", "mode", "seed", "width", "height", "format", "stride", "capacity",
             "tolerance_ns", "calibration_id", "calibration_revision", "cameras", "faults", "scene"});
    const auto version = integer(j, "version", 1, 2);
    require(version == 2 || !j.contains("scene"), "Scene requires profile version 2");
    Profile p;
    p.id = "org.mantis.virtual-rig:" + identity(j, "id");
    auto mode = j.at("mode").get<std::string>();
    require(mode == "trigger" || mode == "free", "Unsupported acquisition mode");
    p.triggered = mode == "trigger";
    p.seed = uint64_t(integer(j, "seed", 0, INT64_MAX));
    p.width = uint32_t(integer(j, "width", 16, 1280));
    p.height = uint32_t(integer(j, "height", 8, 720));
    p.format = j.at("format").get<std::string>();
    require(p.format == "GREY" || p.format == "Y10P", "Unsupported pixel format");
    require(p.format != "Y10P" || p.width % 4 == 0, "Y10P width must be divisible by four");
    auto row = p.format == "GREY" ? p.width : p.width / 4 * 5;
    p.stride = uint32_t(integer(j, "stride", row, int64_t(row) + 4096));
    p.bytes = uint64_t(p.stride) * p.height; // bounded operands, <= 4.2 MiB
    p.capacity = uint32_t(integer(j, "capacity", 2, 16));
    p.tolerance = uint64_t(integer(j, "tolerance_ns", 0, 10000000000));
    p.calibration = j.at("calibration_id").get<std::string>();
    if (version == 1) {
        require(p.calibration == "org.mantis.virtual-rig.pinhole-" + std::to_string(p.width) + "x" +
                                     std::to_string(p.height) + ".v1" &&
                    integer(j, "calibration_revision", 1, 1) == 1,
                "Unknown immutable test calibration");
    } else {
        require(p.calibration == "org.mantis.virtual-rig.scene-v1", "Unknown scene calibration namespace");
        p.revision = uint64_t(integer(j, "calibration_revision", 1, 1000000));
    }
    const auto &cameras = j.at("cameras");
    require(cameras.is_array() && !cameras.empty() && cameras.size() <= 2, "Expected one or two cameras");
    std::set<std::string> ids, clocks;
    for (const auto &c : cameras) {
        keys(c, {"id", "role", "stream", "clock", "rate_num", "rate_den", "phase_ns", "latency_ns",
                 "offset_ns", "drift_ppm", "jitter_ns", "delivery_ns", "comparable"});
        Camera v;
        v.id = identity(c, "id");
        v.role = c.at("role").get<std::string>();
        v.stream = identity(c, "stream");
        v.clock = identity(c, "clock");
        require(ids.insert(v.id).second && ids.insert(v.stream).second && clocks.insert(v.clock).second,
                "Camera/component/stream/clock identities must be distinct");
        require(v.role == (p.cameras.empty() ? "left" : "right"), "Camera roles must be left then right");
        v.rate_num = uint32_t(integer(c, "rate_num", 1, 240000));
        v.rate_den = uint32_t(integer(c, "rate_den", 1, 1001));
        require(uint64_t(v.rate_num) <= 240ull * v.rate_den && uint64_t(v.rate_num) * 10 >= v.rate_den,
                "Rate must be between 0.1 and 240 FPS");
        const auto period = 1000000000ll * v.rate_den / v.rate_num;
        v.phase = integer(c, "phase_ns", 0, 10000000000);
        v.latency = integer(c, "latency_ns", 0, 10000000000);
        v.offset = integer(c, "offset_ns", -1000000000000, 1000000000000);
        v.drift_ppm = integer(c, "drift_ppm", -100000, 100000);
        v.jitter = integer(c, "jitter_ns", 0, period / 8);
        v.delivery = integer(c, "delivery_ns", 0, 10000000000);
        v.comparable = c.at("comparable").get<bool>();
        require(!p.triggered || v.phase == 0, "Trigger camera phases must be zero; configure latency");
        require(p.triggered || v.latency == 0, "Free-running cameras cannot have trigger latency");
        if (!p.cameras.empty() && p.triggered) {
            const auto &a = p.cameras[0];
            require(uint64_t(a.rate_num) * v.rate_den == uint64_t(v.rate_num) * a.rate_den,
                    "Shared trigger requires one common rate");
        }
        p.cameras.push_back(std::move(v));
    }
    if (!p.triggered && p.cameras.size() == 2) {
        uint64_t half{};
        for (const auto &c : p.cameras)
            half = std::max<uint64_t>(half, (500000000ull * c.rate_den + c.rate_num - 1) / c.rate_num);
        require(p.tolerance >= half, "Software tolerance is below nominal half-period");
    }
    const auto &faults = j.at("faults");
    require(faults.is_array() && faults.size() <= 32, "At most 32 faults are allowed");
    std::set<std::pair<size_t, uint64_t>> positions;
    for (const auto &f : faults) {
        keys(f, {"camera", "at", "kind", "delay_ns"});
        Fault v;
        v.camera = size_t(integer(f, "camera", 0, int64_t(p.cameras.size() - 1)));
        v.at = uint64_t(integer(f, "at", 1, 20736000));
        v.kind = f.at("kind").get<std::string>();
        const std::set<std::string> kinds{"drop",
                                          "missing-trigger",
                                          "duplicate-trigger",
                                          "stale-trigger",
                                          "duplicate-counter",
                                          "reverse-counter",
                                          "reset-counter",
                                          "generation-change",
                                          "missing-time",
                                          "incomparable-time",
                                          "delay",
                                          "stall",
                                          "disconnect"};
        require(kinds.contains(v.kind), "Unsupported fault kind");
        require(v.kind.find("trigger") == std::string::npos || p.triggered,
                "Trigger fault in free-running mode");
        v.delay = integer(f, "delay_ns", 0, 10000000000);
        require(v.delay == 0 || v.kind == "delay" || v.kind == "stall", "Unexpected fault delay");
        require((v.kind != "delay" && v.kind != "stall") || v.delay > 0, "Fault requires a positive delay");
        require(positions.emplace(v.camera, v.at).second, "Multiple faults at one camera event");
        p.faults.push_back(std::move(v));
    }
    if (version == 2) {
        p.scene = parse_scene(j.at("scene"), p.width, p.height, p.cameras.size());
        p.calibration += "." + p.scene->content_tag;
        // Integration/row scan cannot cross the next nominal exposure event.
        for (size_t i = 0; i < p.cameras.size(); ++i) {
            const auto &c = p.cameras[i];
            const auto &o = p.scene->cameras[i];
            require(j.at("scene").at("cameras")[i].at("camera_id") == p.id + "/" + c.id,
                    "Calibration camera identity mismatch");
            require(o.integration_ns + (p.height - 1) * o.row_time_ns <=
                        1000000000ull * c.rate_den / c.rate_num - uint64_t(c.jitter),
                    "Integration/row scan exceeds shortest camera period");
            require(uint64_t(c.delivery) >= o.integration_ns + (p.height - 1) * o.row_time_ns,
                    "Delivery precedes completed integration/row scan");
        }
    }
    p.json = j.dump();
    return p;
}
} // namespace virtual_rig
