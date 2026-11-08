#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
namespace virtual_rig {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::invalid_argument(message);
}
void keys(const Json &j, std::initializer_list<const char *> fields) {
    require(j.is_object() && j.size() == fields.size(), "Incomplete scene object");
    for (auto f : fields)
        require(j.contains(f), "Missing scene field");
}
double number(const Json &j, double low, double high) {
    require(j.is_number(), "Scene requires a number");
    const double v = j.get<double>();
    require(std::isfinite(v) && v >= low && v <= high, "Scene number out of range");
    return v;
}
uint64_t integer(const Json &j, uint64_t low, uint64_t high) {
    require(j.is_number_integer() && (j.is_number_unsigned() || j.get<int64_t>() >= 0),
            "Scene requires a nonnegative integer");
    const auto v = j.get<uint64_t>();
    require(v >= low && v <= high, "Scene integer out of range");
    return v;
}
template <size_t N> std::array<double, N> array(const Json &j, double low, double high) {
    require(j.is_array() && j.size() == N, "Invalid scene array extent");
    std::array<double, N> a{};
    for (size_t i = 0; i < N; ++i)
        a[i] = number(j[i], low, high);
    return a;
}
std::array<double, 2> distort(const OpticalCamera &c, double x, double y) {
    const auto &k = c.brown;
    const double r = x * x + y * y, radial = 1 + r * (k[0] + r * (k[1] + r * k[4]));
    return {x * radial + 2 * k[2] * x * y + k[3] * (r + 2 * x * x),
            y * radial + k[2] * (r + 2 * y * y) + 2 * k[3] * x * y};
}
uint64_t mix(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}
} // namespace
Scene parse_scene(const Json &j, uint32_t width, uint32_t height, size_t cameras) {
    keys(j, {"version", "frame", "near_mm", "far_mm", "surfaces", "motion", "cameras"});
    require(integer(j.at("version"), 1, 1) == 1 && j.at("frame") == "rig-body",
            "Unsupported scene version/frame");
    Scene s;
    s.near_mm = number(j.at("near_mm"), 0.1, 1000000);
    s.far_mm = number(j.at("far_mm"), s.near_mm + 0.1, 1000000);
    const auto &surfaces = j.at("surfaces");
    require(surfaces.is_array() && surfaces.size() == 2, "Scene requires two finite surfaces");
    for (size_t i = 0; i < 2; ++i) {
        const auto &v = surfaces[i];
        keys(v, {"center_mm", "half_extent_mm", "level_dn", "feature_center_xz_mm", "feature_half_extent_mm",
                 "feature_level_dn"});
        auto &a = s.surfaces[i];
        a.center = array<3>(v.at("center_mm"), -1000000, 1000000);
        require(a.center[1] >= s.near_mm && a.center[1] <= s.far_mm, "Surface depth outside scene");
        a.half = array<2>(v.at("half_extent_mm"), 0.1, 1000000);
        a.feature_center = array<2>(v.at("feature_center_xz_mm"), -1000000, 1000000);
        a.feature_half = array<2>(v.at("feature_half_extent_mm"), 0.1, 1000000);
        for (size_t k = 0; k < 2; ++k)
            require(std::abs(a.feature_center[k] - a.center[k ? 2 : 0]) + a.feature_half[k] <= a.half[k],
                    "Feature outside surface");
        a.level = uint16_t(integer(v.at("level_dn"), 0, 1023));
        a.feature_level = uint16_t(integer(v.at("feature_level_dn"), 0, 1023));
    }
    require(std::abs(s.surfaces[0].center[1] - s.surfaces[1].center[1]) >= 0.1, "Coincident surface depths");
    const auto &motion = j.at("motion");
    keys(motion, {"amplitude_mm", "period_ns"});
    s.amplitude_mm = number(motion.at("amplitude_mm"), 0, 1000);
    s.period_ns = integer(motion.at("period_ns"), 1000000, 10000000000ull);
    const auto &views = j.at("cameras");
    require(views.is_array() && views.size() == cameras, "Missing camera calibration");
    for (const auto &v : views) {
        keys(v,
             {"camera_id", "width", "height", "fx", "fy", "cx", "cy", "lens", "brown5", "T_camera_from_scene",
              "integration_ns", "gain", "read_noise_dn", "shot_noise", "row_time_ns"});
        require(integer(v.at("width"), width, width) == width &&
                    integer(v.at("height"), height, height) == height,
                "Calibration dimensions mismatch");
        OpticalCamera c;
        // This deliberately narrow field of view makes Brown inversion a bounded
        // contraction across the entire sensor. Unsupported wider lenses fail.
        c.fx = number(v.at("fx"), width, width * 16.0);
        c.fy = number(v.at("fy"), height, height * 16.0);
        c.cx = number(v.at("cx"), width * 0.25, width * 0.75);
        c.cy = number(v.at("cy"), height * 0.25, height * 0.75);
        c.brown = array<5>(v.at("brown5"), -0.005, 0.005);
        require(std::abs(c.brown[2]) <= 0.001 && std::abs(c.brown[3]) <= 0.001,
                "Unsupported tangential distortion");
        require(v.at("lens") == "pinhole" || v.at("lens") == "pinhole_brown5", "Unsupported lens model");
        require(v.at("lens") != "pinhole" || c.brown == std::array<double, 5>{},
                "Pinhole requires zero distortion");
        c.camera_from_scene = array<16>(v.at("T_camera_from_scene"), -1000000, 1000000);
        const auto &t = c.camera_from_scene;
        require(t[12] == 0 && t[13] == 0 && t[14] == 0 && t[15] == 1, "Invalid homogeneous transform");
        for (size_t a = 0; a < 3; ++a)
            for (size_t b = 0; b < 3; ++b) {
                double dot{};
                for (size_t k = 0; k < 3; ++k)
                    dot += t[a * 4 + k] * t[b * 4 + k];
                require(std::abs(dot - (a == b ? 1.0 : 0.0)) <= 1e-12, "Non-rigid camera transform");
            }
        const double det = t[0] * (t[5] * t[10] - t[6] * t[9]) - t[1] * (t[4] * t[10] - t[6] * t[8]) +
                           t[2] * (t[4] * t[9] - t[5] * t[8]);
        require(std::abs(det - 1) <= 1e-12, "Reflected camera transform");
        c.integration_ns = integer(v.at("integration_ns"), 1, 100000000);
        c.gain = number(v.at("gain"), 0, 16);
        c.read_noise = uint32_t(integer(v.at("read_noise_dn"), 0, 32));
        c.shot = number(v.at("shot_noise"), 0, 4);
        c.row_time_ns = integer(v.at("row_time_ns"), 0, 100000);
        s.cameras.push_back(c);
    }
    if (s.cameras.size() == 2) {
        std::array<std::array<double, 3>, 2> centers{};
        for (size_t i = 0; i < 2; ++i) {
            const auto &t = s.cameras[i].camera_from_scene;
            for (size_t k = 0; k < 3; ++k)
                centers[i][k] = -(t[k] * t[3] + t[4 + k] * t[7] + t[8 + k] * t[11]);
        }
        double baseline_squared{};
        for (size_t k = 0; k < 3; ++k)
            baseline_squared += std::pow(centers[1][k] - centers[0][k], 2);
        require(baseline_squared >= 0.01, "Degenerate stereo camera centers");
    }
    s.snapshot = j.dump();
    // Content tag is accidental-conflict detection, not a security hash. The
    // complete immutable snapshot, rather than a lookup by tag, is authoritative.
    uint64_t tag = 14695981039346656037ull;
    for (unsigned char c : s.snapshot) {
        tag ^= c;
        tag *= 1099511628211ull;
    }
    std::ostringstream encoded;
    encoded << std::hex << std::setfill('0') << std::setw(16) << tag;
    s.content_tag = encoded.str();
    return s;
}
uint16_t scene_intensity(const Scene &s, uint64_t seed, uint64_t event, int64_t exposure_ns, size_t camera,
                         uint32_t x, uint32_t y) {
    const auto &c = s.cameras[camera];
    const double xd = (double(x) - c.cx) / c.fx, yd = (double(y) - c.cy) / c.fy;
    double u = xd, v = yd;
    if (c.brown != std::array<double, 5>{}) {
        for (unsigned step = 0; step < 16; ++step) {
            const auto d = distort(c, u, v);
            u += xd - d[0];
            v += yd - d[1];
        }
        const auto d = distort(c, u, v);
        if (std::abs(d[0] - xd) > 1e-12 || std::abs(d[1] - yd) > 1e-12)
            throw std::runtime_error("Brown inverse did not converge");
    }
    const auto &t = c.camera_from_scene;
    std::array<double, 3> origin{}, ray{};
    for (size_t k = 0; k < 3; ++k) {
        origin[k] = -(t[k] * t[3] + t[4 + k] * t[7] + t[8 + k] * t[11]);
        ray[k] = t[k] * u + t[4 + k] * v + t[8 + k];
    }
    // Event exposure denotes row-zero integration start in reference-clock ns.
    // A single midpoint sample approximates integration; no blur is claimed.
    const auto time = uint64_t(exposure_ns) + uint64_t(y) * c.row_time_ns + c.integration_ns / 2;
    const double phase = double(time % s.period_ns) / double(s.period_ns);
    const double motion = s.amplitude_mm * (phase < 0.25   ? 4 * phase
                                            : phase < 0.75 ? 2 - 4 * phase
                                                           : 4 * phase - 4);
    double nearest = s.far_mm, level{};
    if (ray[1] != 0) {
        for (const auto &a : s.surfaces) {
            const double depth = (a.center[1] - origin[1]) / ray[1];
            if (depth < s.near_mm || depth >= nearest)
                continue;
            const double px = origin[0] + depth * ray[0] - motion;
            const double pz = origin[2] + depth * ray[2];
            // Half-open rectangles give deterministic edge/occlusion ownership.
            auto inside = [](double p, double center, double half) {
                return p >= center - half && p < center + half;
            };
            if (!inside(px, a.center[0], a.half[0]) || !inside(pz, a.center[2], a.half[1]))
                continue;
            nearest = depth;
            level = inside(px, a.feature_center[0], a.feature_half[0]) &&
                            inside(pz, a.feature_center[1], a.feature_half[1])
                        ? a.feature_level
                        : a.level;
        }
    }
    level = std::clamp(level * (double(c.integration_ns) / 1000000.0) * c.gain, 0.0, 1023.0);
    if (c.read_noise == 0 && c.shot == 0)
        return uint16_t(std::floor(level + 0.5));
    const auto key =
        seed ^ (event * 0x9e3779b97f4a7c15ull) ^ (uint64_t(camera) << 61) ^ (uint64_t(y) << 24) ^ x;
    const auto shot = uint32_t(std::floor(std::sqrt(level) * c.shot));
    const int read = int(mix(key) % (2 * c.read_noise + 1)) - int(c.read_noise);
    const int photons = int(mix(key ^ 0xd1b54a32d192ed03ull) % (2 * shot + 1)) - int(shot);
    return uint16_t(std::clamp(std::floor(level + 0.5) + read + photons, 0.0, 1023.0));
}
} // namespace virtual_rig
