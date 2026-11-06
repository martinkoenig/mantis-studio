#include "../../plugins/first-party/devices/virtual-rig/control.hpp"
#include "portable_environment.hpp"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mantis/data_io.hpp>
#include <mantis/device_runtime.hpp>
#include <mantis/image_layout.hpp>
#include <mantis/replay.hpp>
#include <nlohmann/json.hpp>
#include <opencv2/calib3d.hpp>
#include <sstream>
#ifdef __linux__
#include <sys/resource.h>
#endif
using namespace mantis;
using Json = nlohmann::json;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error(std::string("scene line ") + std::to_string(__LINE__) +                 \
                                     ": " #__VA_ARGS__);                                                     \
    } while (false)
namespace {
struct Control {
    const virtual_rig::ClockV1 *clock{};
    void *instance{};
    std::mutex mutex;
    std::condition_variable changed;
    bool gated{}, released{}, parked{}, failed{}, initial{};
    int64_t initial_delivery{};
    unsigned credits{1};
    std::chrono::steady_clock::time_point render_begin;
    std::clock_t cpu_begin{};
    int64_t renderer_wall_us{}, renderer_cpu_us{};
} control;
std::vector<std::weak_ptr<const data::Packet>> owners;
struct Workspace {
    std::filesystem::path root = std::filesystem::temp_directory_path() / Id::random().value;
    Workspace() {
        std::filesystem::create_directory(root);
    }
    ~Workspace() {
        test::environment("MANTIS_VIRTUAL_RIG_PROFILE", nullptr);
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    void write(const Json &j) {
        std::ofstream out(root / "profile.json");
        out << j.dump();
        out.close();
        CHECK(out.good());
        CHECK(test::environment("MANTIS_VIRTUAL_RIG_PROFILE", (root / "profile.json").string().c_str()) == 0);
    }
};
Json profile(uint32_t width = 64, uint32_t height = 48, bool packed = false) {
    Json p{{"version", 2},
           {"id", "scene"},
           {"mode", "trigger"},
           {"seed", 17},
           {"width", width},
           {"height", height},
           {"format", packed ? "Y10P" : "GREY"},
           {"stride", (packed ? width / 4 * 5 : width) + 4},
           {"capacity", 4},
           {"tolerance_ns", 20000000},
           {"calibration_id", "org.mantis.virtual-rig.scene-v1"},
           {"calibration_revision", 1},
           {"cameras", Json::array()},
           {"faults", Json::array()}};
    Json scene{{"version", 1},
               {"frame", "rig-body"},
               {"near_mm", 1},
               {"far_mm", double(width) * 30},
               {"motion", {{"amplitude_mm", 0}, {"period_ns", 1000000000}}},
               {"surfaces", Json::array()},
               {"cameras", Json::array()}};
    // Independent fixture coordinates: canonical scene X right, Y forward, Z up.
    // Far plane spans the field; near rectangle creates an occluding depth step.
    for (int i = 0; i < 2; ++i) {
        scene["surfaces"].push_back({{"center_mm", {i ? -100 : 0, double(width) * (i ? 5 : 10), 0}},
                                     {"half_extent_mm", {i ? 60 : 400, i ? 80 : 300}},
                                     {"level_dn", i ? 403 : 101},
                                     {"feature_center_xz_mm", {i ? -100 : 40, 0}},
                                     {"feature_half_extent_mm", {5, 5}},
                                     {"feature_level_dn", i ? 803 : 1001}});
        std::string role = i ? "right" : "left";
        p["cameras"].push_back({{"id", role},
                                {"role", role},
                                {"stream", role + "-image"},
                                {"clock", "org.test." + role},
                                {"rate_num", 30},
                                {"rate_den", 1},
                                {"phase_ns", 0},
                                {"latency_ns", 0},
                                {"offset_ns", 0},
                                {"drift_ppm", 0},
                                {"jitter_ns", 0},
                                {"delivery_ns", 1000000},
                                {"comparable", true}});
        scene["cameras"].push_back(
            {{"camera_id", "org.mantis.virtual-rig:scene/" + role},
             {"width", width},
             {"height", height},
             {"fx", width},
             {"fy", width},
             {"cx", width / 2},
             {"cy", height / 2},
             {"lens", "pinhole"},
             {"brown5", {0, 0, 0, 0, 0}},
             {"T_camera_from_scene", {1, 0, 0, i ? -40 : 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1}},
             {"integration_ns", 1000000},
             {"gain", 1},
             {"read_noise_dn", 0},
             {"shot_noise", 0},
             {"row_time_ns", 0}});
    }
    p["scene"] = scene;
    return p;
}
std::string canonical(const data::Packet &p) {
    std::ostringstream out(std::ios::binary);
    data::write_packet(out, p);
    return out.str();
}
std::vector<unsigned char> bytes(const data::Packet &p) {
    const auto b = p.attributes.at(0).buffer.map_read().value();
    return {reinterpret_cast<const unsigned char *>(b.data()),
            reinterpret_cast<const unsigned char *>(b.data() + b.size())};
}
uint16_t pixel(const data::Packet &p, unsigned x, unsigned y) {
    const auto span = p.attributes.at(0).buffer.map_read().value();
    const auto *b = reinterpret_cast<const unsigned char *>(span.data());
    auto l = data::image_layout(p);
    const size_t off = size_t(y) * l.row_stride;
    if (l.packing == data::ImagePacking::mipi_raw10)
        return uint16_t((b[off + x / 4 * 5 + x % 4] << 2) | ((b[off + x / 4 * 5 + 4] >> (2 * (x % 4))) & 3));
    return b[off + x];
}
// Independent projection uses OpenCV's Rodrigues/Brown implementation and a
// forward point projection, never the emitter's inverse-ray implementation.
cv::Matx33d intrinsics(const Json &c) {
    return {c.at("fx"), 0, c.at("cx"), 0, c.at("fy"), c.at("cy"), 0, 0, 1};
}
cv::Matx33d rotation(const Json &c) {
    const auto &t = c.at("T_camera_from_scene");
    return {t[0], t[1], t[2], t[4], t[5], t[6], t[8], t[9], t[10]};
}
cv::Vec3d translation(const Json &c) {
    const auto &t = c.at("T_camera_from_scene");
    return {t[3], t[7], t[11]};
}
std::vector<double> distortion(const Json &c) {
    return c.at("brown5").get<std::vector<double>>();
}
cv::Point2d project(const Json &c, cv::Point3d world) {
    cv::Vec3d r;
    cv::Rodrigues(rotation(c), r);
    std::vector<cv::Point2d> out;
    cv::projectPoints(std::vector<cv::Point3d>{world}, r, translation(c), intrinsics(c), distortion(c), out);
    return out.at(0);
}
void epipolar(const Json &p) {
    const auto &a = p["scene"]["cameras"][0], &b = p["scene"]["cameras"][1];
    const auto relative = rotation(b) * rotation(a).inv();
    const cv::Vec3d t = translation(b) - relative * translation(a);
    const cv::Matx33d cross{0, -t[2], t[1], t[2], 0, -t[0], -t[1], t[0], 0};
    for (const auto &surface : p["scene"]["surfaces"]) {
        const cv::Point3d world{surface["feature_center_xz_mm"][0], surface["center_mm"][1],
                                surface["feature_center_xz_mm"][1]};
        std::array<cv::Vec3d, 2> rays;
        for (size_t i = 0; i < 2; ++i) {
            const auto &c = p["scene"]["cameras"][i];
            const auto point = project(c, world);
            std::vector<cv::Point2d> normalized;
            cv::undistortPoints(std::vector<cv::Point2d>{point}, normalized, intrinsics(c), distortion(c),
                                cv::noArray(), cv::noArray(),
                                cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 1e-14));
            rays[i] = {normalized[0].x, normalized[0].y, 1};
        }
        CHECK(std::abs(rays[1].dot(cross * relative * rays[0])) < 1e-12);
    }
}
// A separately specified ray/rectangle oracle uses OpenCV undistortPoints and
// general matrix inversion. Fixture surfaces are enumerated back-to-front; a
// source fault that reverses nearest-hit ordering must fail exact pixels.
uint16_t expected(const Json &p, size_t camera, int64_t exposure, unsigned x, unsigned y) {
    const auto &s = p.at("scene"), &c = s.at("cameras")[camera];
    std::vector<cv::Point2d> ray;
    cv::undistortPoints(std::vector<cv::Point2d>{{double(x), double(y)}}, ray, intrinsics(c), distortion(c),
                        cv::noArray(), cv::noArray(),
                        cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 1e-14));
    const auto inv = rotation(c).inv();
    const cv::Vec3d o = -(inv * translation(c));
    const cv::Vec3d d = inv * cv::Vec3d(ray[0].x, ray[0].y, 1);
    const auto period = s["motion"]["period_ns"].get<uint64_t>();
    const auto time = uint64_t(exposure) + uint64_t(y) * c["row_time_ns"].get<uint64_t>() +
                      c["integration_ns"].get<uint64_t>() / 2;
    const double q = double(time % period) / double(period);
    // Triangle displacement via quarter-cycle knots, independently interpolated.
    constexpr double knots[]{0, 1, 0, -1, 0};
    const auto k = unsigned(q * 4);
    const double f = q * 4 - k;
    const double move = (knots[k] * (1 - f) + knots[k + 1] * f) * s["motion"]["amplitude_mm"].get<double>();
    double depth = s["far_mm"], signal = 0;
    if (d[1] != 0)
        for (const auto &a : s["surfaces"]) {
            const double hit = (a["center_mm"][1].get<double>() - o[1]) / d[1];
            if (hit < s["near_mm"].get<double>() || hit >= depth)
                continue;
            const double px = o[0] + hit * d[0] - move, pz = o[2] + hit * d[2];
            auto inside = [](double v, double center, double extent) {
                return center - extent <= v && v < center + extent;
            };
            if (!inside(px, a["center_mm"][0], a["half_extent_mm"][0]) ||
                !inside(pz, a["center_mm"][2], a["half_extent_mm"][1]))
                continue;
            depth = hit;
            signal = a["level_dn"];
            if (inside(px, a["feature_center_xz_mm"][0], a["feature_half_extent_mm"][0]) &&
                inside(pz, a["feature_center_xz_mm"][1], a["feature_half_extent_mm"][1]))
                signal = a["feature_level_dn"];
        }
    const double dn =
        std::floor(std::clamp(signal * c["integration_ns"].get<double>() / 1000000 * c["gain"].get<double>(),
                              0.0, 1023.0) +
                   .5);
    return uint16_t(dn) >> (p["format"] == "GREY" ? 2 : 0);
}
void oracle(const data::Packet &set, const Json &p, bool all = true) {
    CHECK(set.frames.size() == p["cameras"].size());
    CHECK(set.header.sync_quality == time::SyncQuality::software);
    const auto w = p["width"].get<unsigned>(), h = p["height"].get<unsigned>();
    for (size_t i = 0; i < set.frames.size(); ++i) {
        const auto &image = *set.frames[i];
        const auto &m = image.header.metadata;
        CHECK(image.header.calibration.revision == p["calibration_revision"].get<uint64_t>());
        CHECK(image.header.calibration.id == set.header.calibration.id &&
              image.header.calibration.revision == set.header.calibration.revision &&
              image.header.calibration.schema_version == set.header.calibration.schema_version);
        CHECK(image.header.calibration.id.value.starts_with("org.mantis.virtual-rig.scene-v1."));
        CHECK(Json::parse(m.at("org.mantis.virtual-rig.scene")) == p["scene"]);
        CHECK(m.at("org.mantis.virtual-rig.camera_index") == std::to_string(i));
        CHECK(m.at("org.mantis.virtual-rig.scene_frame") == "org.mantis.virtual-rig:scene/body");
        CHECK(m.at("org.mantis.virtual-rig.seed") == std::to_string(p["seed"].get<uint64_t>()));
        CHECK(m.at("native_clock_id") == p["cameras"][i]["clock"].get<std::string>());
        CHECK(image.header.timestamp.domain.id.value.starts_with(p["cameras"][i]["clock"].get<std::string>() +
                                                                 "/epoch/"));
        const int64_t e = std::stoll(m.at("capture_event_ns"));
        const auto &optical = p["scene"]["cameras"][i];
        CHECK(std::stoll(m.at("availability_ns")) >=
              e + optical["integration_ns"].get<int64_t>() + (h - 1) * optical["row_time_ns"].get<int64_t>());
        CHECK(image.header.received.nanoseconds >= std::stoll(m.at("availability_ns")));
        const auto &c = p["cameras"][i];
        CHECK(c["jitter_ns"] == 0); // these independent optical fixtures use exact cadence
        const auto expected_event = int64_t(image.header.sequence.value * 1000000000ull *
                                            c["rate_den"].get<uint64_t>() / c["rate_num"].get<uint64_t>()) +
                                    c[p["mode"] == "trigger" ? "latency_ns" : "phase_ns"].get<int64_t>();
        CHECK(e == expected_event);
        const auto native = e + c["offset_ns"].get<int64_t>() + e * c["drift_ppm"].get<int64_t>() / 1000000;
        CHECK(image.header.timestamp.nanoseconds == native);
        const auto layout = data::image_layout(image);
        CHECK(layout.width == w && layout.height == h && layout.row_stride == p["stride"].get<unsigned>());
        const auto raw = bytes(image);
        CHECK(raw.size() == p["stride"].get<size_t>() * h);
        for (unsigned y = 0; y < h; ++y) {
            for (unsigned x = 0; x < w; x += (all ? 1 : 37)) {
                const auto actual = pixel(image, x, y), truth = expected(p, i, e, x, y);
                if (actual != truth)
                    throw std::runtime_error("scene pixel oracle camera=" + std::to_string(i) +
                                             " x=" + std::to_string(x) + " y=" + std::to_string(y) +
                                             " actual=" + std::to_string(actual) +
                                             " expected=" + std::to_string(truth));
            }
            const unsigned used = p["format"] == "GREY" ? w : w / 4 * 5;
            for (unsigned b = used; b < p["stride"].get<unsigned>(); ++b)
                CHECK(raw[y * p["stride"].get<unsigned>() + b] == 0);
        }
    }
}
struct Fixture {
    Workspace workspace;
    std::unique_ptr<plugins::Registry> registry;
    std::vector<std::unique_ptr<device::ImageStream>> streams;
    int64_t delivery{};
    Fixture(const std::filesystem::path &library, const Json &p) {
        workspace.write(p);
        for (const auto &c : p["cameras"])
            delivery = std::max(delivery, c["delivery_ns"].get<int64_t>());
        const auto dir = workspace.root / "plugins";
        std::filesystem::create_directory(dir);
        std::filesystem::copy_file(library, dir / library.filename());
        std::filesystem::copy_file(library.parent_path() / "virtual-rig.json", dir / "virtual-rig.json");
        registry = std::make_unique<plugins::Registry>(std::filesystem::path{}, workspace.root / "hosts",
                                                       std::function<void(const LogRecord &)>{});
        registry->discover(dir, {"org.mantis.virtual-rig"});
        streams = registry->devices();
        CHECK(streams.size() == 1 && streams[0]->components().size() == p["cameras"].size());
        const auto components = streams[0]->components();
        for (size_t i = 0; i < components.size(); ++i) {
            CHECK(components[i].id.value == p["scene"]["cameras"][i]["camera_id"].get<std::string>());
            CHECK(Json::parse(components[i].metadata.at("org.mantis.virtual-rig.scene")) == p["scene"]);
            CHECK(components[i].metadata.at("width") == std::to_string(p["width"].get<unsigned>()));
            CHECK(components[i].metadata.at("height") == std::to_string(p["height"].get<unsigned>()));
        }
    }
    void advance(int64_t exposure) {
        CHECK(control.clock->advance(control.instance, exposure + delivery) == 0);
    }
    void start() {
        CHECK(streams[0]->start());
        advance(0);
    }
    data::Published take() {
        for (unsigned n = 0; n < 4; ++n) {
            auto f = streams[0]->next();
            CHECK(f);
            if (*f) {
                owners.push_back(*f);
                return *f;
            }
        }
        throw std::runtime_error("No scene publication within bounded reads");
    }
};
void geometry(const std::filesystem::path &lib) {
    const auto p = profile();
    Fixture f(lib, p);
    f.start();
    const auto a = f.take();
    oracle(*a, p);
    CHECK(pixel(*a->frames[0], 36, 24) == 250 && pixel(*a->frames[1], 32, 24) == 250);
    CHECK(pixel(*a->frames[0], 12, 24) == 200 &&
          pixel(*a->frames[1], 4, 24) == 200);  // disparity 8 on near plane
    CHECK(pixel(*a->frames[0], 20, 24) == 100); // near occludes far
    CHECK(pixel(*a->frames[0], 0, 0) == 25);    // far surface edge, no invented point
    const auto left = project(p["scene"]["cameras"][0], {40, 640, 0});
    const auto right = project(p["scene"]["cameras"][1], {40, 640, 0});
    CHECK(std::abs(left.x - 36) < 1e-12 && std::abs(right.x - 32) < 1e-12 && std::abs(left.y - 24) < 1e-12 &&
          std::abs(right.y - 24) < 1e-12);
    // Realistic wrong calibration controls must disagree with actual source
    // pixels, without changing the emitted metadata or invoking production math.
    for (unsigned fault = 0; fault < 5; ++fault) {
        auto wrong = p;
        auto &view = wrong["scene"]["cameras"][1];
        if (fault == 0)
            view["T_camera_from_scene"][3] = -80;
        if (fault == 1)
            view["T_camera_from_scene"] = {0, 0, 1, 0, 1, 0, 0, -40, 0, 1, 0, 0, 0, 0, 0, 1};
        if (fault == 2)
            view["T_camera_from_scene"][7] = 20;
        if (fault == 3)
            view["fx"] = 96;
        if (fault == 4)
            view["cx"] = 36;
        bool disagrees = false;
        for (unsigned y = 0; y < 48; ++y)
            for (unsigned x = 0; x < 64; ++x)
                disagrees |= pixel(*a->frames[1], x, y) != expected(wrong, 1, 0, x, y);
        CHECK(disagrees);
    }
    for (unsigned fault = 0; fault < 2; ++fault) {
        auto wrong = p;
        if (fault == 0)
            wrong["calibration_revision"] = 2;
        else
            wrong["cameras"][0]["clock"] = "org.test.unrelated";
        bool detected = false;
        try {
            oracle(*a, wrong);
        } catch (const std::exception &e) {
            detected = std::string(e.what()).find(fault == 0 ? "calibration.revision" : "native_clock_id") !=
                       std::string::npos;
        }
        CHECK(detected);
    }
    epipolar(p);
    const auto old = canonical(*a);
    f.advance(33333333);
    auto b = f.take();
    oracle(*b, p);
    CHECK(a->frames[0]->attributes[0].buffer.map_read().value().data() !=
          b->frames[0]->attributes[0].buffer.map_read().value().data());
    CHECK(f.streams[0]->stop());
    f.start();
    const auto c = f.take();
    CHECK(c->frames[0]->header.timestamp.domain.id != a->frames[0]->header.timestamp.domain.id);
    CHECK(canonical(*a) == old);
    CHECK(f.streams[0]->stop());
    // Single camera opt-in and nonrectified views through the same public path.
    auto single = p;
    single["cameras"].erase(1);
    single["scene"]["cameras"].erase(1);
    {
        Fixture g(lib, single);
        g.start();
        oracle(*g.take(), single);
        CHECK(g.streams[0]->stop());
    }
}
void variants(const std::filesystem::path &lib) {
    for (unsigned mode = 0; mode < 9; ++mode) {
        auto p = profile();
        if (mode == 0) { // Nonrectified: proper optical rotation around Z by 90 degrees.
            p["scene"]["cameras"][1]["T_camera_from_scene"] = {0, 0, 1, 0, 1, 0, 0, -40,
                                                               0, 1, 0, 0, 0, 0, 0, 1};
        } else if (mode == 1) {
            for (auto &c : p["scene"]["cameras"]) {
                c["lens"] = "pinhole_brown5";
                c["brown5"] = {.004, -.002, .001, -.001, .001};
            }
        } else if (mode == 2) {
            for (auto &c : p["scene"]["cameras"]) {
                c["gain"] = 3;
                c["integration_ns"] = 2000000;
            }
        } else if (mode == 3) {
            p["scene"]["motion"]["amplitude_mm"] = 1000;
            for (auto &c : p["scene"]["cameras"])
                c["row_time_ns"] = 100000;
        } else if (mode == 4) {
            p["scene"]["motion"]["amplitude_mm"] = 40;
            p["mode"] = "free";
            p["cameras"][1]["phase_ns"] = 2000000;
            p["cameras"][1]["offset_ns"] = 900000000;
            p["cameras"][1]["delivery_ns"] = 4000000;
        } else if (mode == 5) {
            p["scene"]["surfaces"][0]["half_extent_mm"] = {200, 100};
        } else if (mode == 7) {
            std::swap(p["scene"]["surfaces"][0], p["scene"]["surfaces"][1]);
        } else if (mode == 8) {
            for (auto &c : p["scene"]["cameras"])
                c["gain"] = 0;
        } else {
            p["calibration_revision"] = 2;
            p["scene"]["cameras"][1]["fx"] = 80;
            p["scene"]["cameras"][1]["cx"] = 34;
        }
        for (size_t i = 0; i < p["cameras"].size(); ++i) {
            const auto &c = p["scene"]["cameras"][i];
            p["cameras"][i]["delivery_ns"] =
                std::max(p["cameras"][i]["delivery_ns"].get<uint64_t>(),
                         c["integration_ns"].get<uint64_t>() + 47 * c["row_time_ns"].get<uint64_t>());
        }
        epipolar(p);
        Fixture f(lib, p);
        f.start();
        if (mode == 4)
            f.advance(40000000);
        auto a = f.take();
        oracle(*a, p);
        CHECK(f.streams[0]->stop());
        if (mode == 0) {
            const auto q = project(p["scene"]["cameras"][1], {40, 640, 0});
            CHECK(std::abs(q.x - 32) < 1e-12 && std::abs(q.y - 24) < 1e-12);
        }
        if (mode == 3) {
            CHECK(pixel(*a->frames[0], 36, 24) == 25);
        }
    }
}
void grazing(const std::filesystem::path &lib) {
    auto p = profile();
    const double e = std::ldexp(1.0, -52);
    const double t = std::nextafter(std::nextafter(std::nextafter(512.0, 0.0), 0.0), 0.0);
    p["scene"]["cameras"][0]["T_camera_from_scene"] = {e, -1, 0, 640, 0, 0, -1, 0, 1, e, 0, t, 0, 0, 0, 1};
    p["scene"]["surfaces"][0]["feature_center_xz_mm"] = {0, 0};
    // This proper rotation has center-ray scene-Y=2^-52, not zero.
    // The camera lies just below Y=640; the finite center-ray hit is at
    // optical depth 512 mm, X approximately zero, Z=0: the known feature.
    Fixture f(lib, p);
    f.start();
    const auto image = f.take();
    const auto actual = pixel(*image->frames[0], 32, 24);
    if (actual != 250)
        throw std::runtime_error("grazing center feature actual=" + std::to_string(actual) + " expected=250");
    oracle(*image, p);
    CHECK(f.streams[0]->stop());
}
void negatives(const std::filesystem::path &lib) {
    plugins::Loaded loaded(lib);
    const auto *api = loaded.query<MantisAcquisitionV1>(MANTIS_ACQUISITION_V1);
    Workspace w;
    for (unsigned mode = 0; mode < 23; ++mode) {
        auto p = profile();
        auto &c = p["scene"]["cameras"][0];
        switch (mode) {
        case 0:
            p.erase("scene");
            break;
        case 1:
            p["scene"].erase("cameras");
            break;
        case 2:
            c["fx"] = 0;
            break;
        case 3:
            c["cx"] = -1;
            break;
        case 4:
            c["T_camera_from_scene"][0] = 0;
            break;
        case 5:
            c["T_camera_from_scene"][0] = -1;
            break;
        case 6:
            c["brown5"][0] = 2;
            break;
        case 7:
            c["lens"] = "unknown";
            break;
        case 8:
            c["integration_ns"] = 0;
            break;
        case 9:
            c["row_time_ns"] = 100001;
            break;
        case 10:
            c["gain"] = 17;
            break;
        case 11:
            c["width"] = 65;
            break;
        case 12:
            p["calibration_revision"] = 0;
            break;
        case 13:
            p["scene"]["surfaces"][1]["center_mm"][1] = 640;
            break;
        }
        if (mode == 14)
            c["camera_id"] = "wrong-camera";
        if (mode == 15)
            p["scene"]["cameras"][1]["T_camera_from_scene"][3] = 0;
        if (mode == 16)
            c["integration_ns"] = 100000000;
        if (mode == 17)
            p["scene"]["surfaces"][0]["half_extent_mm"][0] = 0;
        if (mode == 18)
            c["gain"] = "NaN";
        if (mode == 19)
            c["T_camera_from_scene"][15] = 0;
        if (mode == 20)
            p["cameras"][0]["delivery_ns"] = 0;
        if (mode == 21)
            p.erase("calibration_id");
        if (mode == 22)
            p["calibration_id"] = "invented-calibration";
        w.write(p);
        void *instance = nullptr;
        CHECK(api->open(plugins::host_api(), "org.mantis.virtual-rig:scene", &instance) != 0 && !instance);
    }
    // JSON numeric overflow must fail at the actual public open boundary; it
    // must not become a repaired finite camera or silently fall back to v1.
    for (const auto *token : {"1e999", "-1e999", "NaN"}) {
        const auto valid = profile();
        w.write(valid);
        auto text = valid.dump();
        const auto pos = text.find("\"fx\":64");
        CHECK(pos != std::string::npos);
        text.replace(pos, 7, std::string("\"fx\":") + token);
        std::ofstream out(w.root / "profile.json");
        out << text;
        out.close();
        CHECK(out.good());
        void *instance = nullptr;
        CHECK(api->open(plugins::host_api(), "org.mantis.virtual-rig:scene", &instance) != 0 && !instance);
    }
}
void noise(const std::filesystem::path &lib) {
    auto p = profile(64, 48, true);
    for (auto &c : p["scene"]["cameras"]) {
        c["read_noise_dn"] = 8;
        c["shot_noise"] = 1;
    }
    std::vector<unsigned char> first;
    for (unsigned run = 0; run < 3; ++run) {
        if (run == 2)
            p["seed"] = 18;
        Fixture f(lib, p);
        f.start();
        auto a = f.take();
        auto b = bytes(*a->frames[0]);
        if (run == 0) {
            first = b;
            CHECK(pixel(*a->frames[0], 0, 0) == 96);
            CHECK(pixel(*a->frames[0], 36, 24) == 1006);
            CHECK(pixel(*a->frames[0], 12, 24) == 815);
        } else if (run == 1)
            CHECK(b == first);
        else
            CHECK(b != first);
        for (unsigned y = 0; y < 48; ++y)
            for (unsigned x = 0; x < 64; ++x) {
                const auto truth = expected(p, 0, 0, x, y);
                const auto actual = pixel(*a->frames[0], x, y);
                CHECK(std::abs(int(actual) - int(truth)) <= 39);
            }
        f.advance(33333333);
        const auto later = f.take();
        CHECK(bytes(*later->frames[0]) != b);
        CHECK(bytes(*a->frames[0]) == b);
        CHECK(f.streams[0]->stop());
    }
}
void recording(const std::filesystem::path &lib, bool real_size = false) {
    auto p = real_size ? profile(1280, 720, true) : profile();
    const unsigned count = real_size ? 1 : 3;
    Fixture f(lib, p);
    auto store = std::make_shared<artifact::Store>(f.workspace.root / "Scene.mantis");
    auto id = store->begin({"org.mantis.RawCapture", 2}, {});
    std::vector<data::Published> live;
    std::exception_ptr writer_error;
    control.gated = true;
    control.released = false;
    control.credits = 1;
    control.failed = false;
    control.initial = true;
    control.initial_delivery = f.delivery;
    auto session = std::make_unique<device::Session>(
        device::CaptureDescriptor{Id::random(), {f.streams[0]->descriptor().id}, {}},
        std::vector<device::ImageStream *>{f.streams[0].get()},
        [&](data::Published packet) {
            try {
                store->append(id, *packet);
                std::lock_guard lock(control.mutex);
                owners.push_back(packet);
                live.push_back(std::move(packet));
                control.changed.notify_all();
            } catch (...) {
                std::lock_guard lock(control.mutex);
                writer_error = std::current_exception();
                control.failed = true;
                control.changed.notify_all();
                throw;
            }
        },
        [&](const LogRecord &r) {
            if (r.level == "error") {
                std::lock_guard lock(control.mutex);
                if (!writer_error)
                    writer_error = std::make_exception_ptr(std::runtime_error(r.message));
                control.failed = true;
                control.changed.notify_all();
            }
        });
    struct Cleanup {
        std::unique_ptr<device::Session> &session;
        ~Cleanup() {
            {
                std::lock_guard lock(control.mutex);
                control.released = true;
                control.changed.notify_all();
            }
            if (session)
                session->stop();
            control.gated = false;
        }
    } cleanup{session};
    for (unsigned n = 0; n < count; ++n) {
        std::unique_lock lock(control.mutex);
        CHECK(control.changed.wait_for(lock, std::chrono::seconds(5), [&] {
            return writer_error || (live.size() == n + 1 && control.parked);
        }));
        if (writer_error)
            std::rethrow_exception(writer_error);
        if (n + 1 < count) {
            f.advance(int64_t(n + 1) * 1000000000 / 30);
            ++control.credits;
            control.changed.notify_all();
        }
    }
    {
        std::lock_guard lock(control.mutex);
        control.released = true;
        control.changed.notify_all();
    }
    session->stop();
    control.gated = false;
    CHECK(session->error().empty() && session->produced() == count && session->committed() == count &&
          session->saturation() == 0);
    for (auto &packet : live)
        oracle(*packet, p, !real_size);
    if (real_size) {
        CHECK(pixel(*live[0]->frames[0], 644, 360) == 1001);
        CHECK(pixel(*live[0]->frames[1], 640, 360) == 1001);
        const auto d = f.streams[0]->diagnostics();
        CHECK(d.at("host_pixel_allocations") == "2");
        CHECK(d.at("materialized_pixel_bytes") == "2309760");
        CHECK(d.at("left.source_depth") == "0" && d.at("right.source_depth") == "0");
        std::cout << "Renderer+ABI only, 1280x720 stereo Y10P: wall_us=" << control.renderer_wall_us
                  << " CPU_us=" << control.renderer_cpu_us << " pixel allocations=2 bytes=2309760; "
                  << "Session/Store and two replays checked separately; no throughput claim\n";
    }
    std::vector<std::string> recorded;
    for (auto &packet : live)
        recorded.push_back(canonical(*packet));
    CHECK(store->finalize(id).state == artifact::ArtifactState::finalized);
    store.reset();
    // Different current calibration and seed cannot reinterpret historical frames.
    auto revised = p;
    revised["calibration_revision"] = 2;
    revised["scene"]["cameras"][1]["T_camera_from_scene"][3] = -80;
    revised["seed"] = 999;
    f.workspace.write(revised);
    f.start();
    auto fresh = f.take();
    CHECK(f.streams[0]->stop());
    CHECK(fresh->frames[0]->header.calibration.revision == 2);
    CHECK(fresh->frames[0]->header.calibration.id != live[0]->frames[0]->header.calibration.id);
    store = std::make_shared<artifact::Store>(f.workspace.root / "Scene.mantis");
    for (unsigned pass = 0; pass < 2; ++pass) {
        auto replay = device::recorded_source(store, id, false);
        CHECK(replay->start());
        for (size_t n = 0; n < count; ++n) {
            auto packet = replay->next();
            CHECK(packet && *packet);
            CHECK(canonical(**packet) == recorded[n]);
            oracle(**packet, p, !real_size);
        }
        auto end = replay->next();
        CHECK(end && !*end && replay->stop());
    }
    for (size_t n = 0; n < count; ++n)
        CHECK(canonical(*live[n]) == recorded[n]);
}
void startup_failure(const std::filesystem::path &lib) {
    Fixture f(lib, profile());
    control.gated = true;
    control.released = control.failed = false;
    control.credits = 1;
    control.initial = true;
    control.initial_delivery = f.delivery;
    bool detected = false;
    try {
        device::Session session(
            device::CaptureDescriptor{Id::random(), {f.streams[0]->descriptor().id}, {}},
            std::vector<device::ImageStream *>{f.streams[0].get()},
            [](data::Published) { throw std::runtime_error("Injected scene startup writer failure"); },
            [](const LogRecord &r) {
                if (r.level == "error") {
                    std::lock_guard lock(control.mutex);
                    control.failed = true;
                    control.changed.notify_all();
                }
            });
    } catch (const std::exception &e) {
        detected = std::string(e.what()).find("Injected scene startup writer failure") != std::string::npos;
    }
    control.gated = false;
    CHECK(detected);
    const auto d = f.streams[0]->diagnostics();
    CHECK(d.at("left.source_depth") == "0" && d.at("right.source_depth") == "0");
}
} // namespace
namespace mantis::plugins::qualification_test {
void opened(const Loaded &loaded, void *instance) {
    control.clock = loaded.query<virtual_rig::ClockV1>(virtual_rig::clock_interface);
    CHECK(control.clock && control.clock->use_virtual_time(instance) == 0);
    control.instance = instance;
}
void before_next() {
    if (control.gated) {
        std::unique_lock lock(control.mutex);
        control.parked = true;
        control.changed.notify_all();
        control.changed.wait(lock, [] { return control.released || control.credits || control.failed; });
        control.parked = false;
        if (control.credits)
            --control.credits;
        if (control.initial) {
            CHECK(control.clock->advance(control.instance, control.initial_delivery) == 0);
            control.initial = false;
        }
        control.render_begin = std::chrono::steady_clock::now();
        control.cpu_begin = std::clock();
    }
}
void after_next(int status) {
    if (control.gated && status == 0) {
        control.renderer_wall_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - control.render_begin)
                                       .count();
        control.renderer_cpu_us = (std::clock() - control.cpu_begin) * 1000000 / CLOCKS_PER_SEC;
    }
    if (control.gated && status == 2) {
        std::lock_guard lock(control.mutex);
        if (!control.released && !control.failed)
            ++control.credits;
        control.changed.notify_all();
    }
}
} // namespace mantis::plugins::qualification_test
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
#ifdef __linux__
        auto count = [](const char *path) {
            return std::distance(std::filesystem::directory_iterator(path),
                                 std::filesystem::directory_iterator{});
        };
        const auto fds = count("/proc/self/fd"), threads = count("/proc/self/task");
#endif
        geometry(argv[1]);
        variants(argv[1]);
        grazing(argv[1]);
        negatives(argv[1]);
        noise(argv[1]);
        recording(argv[1]);
        recording(argv[1], true);
        startup_failure(argv[1]);
        for (const auto &owner : owners)
            CHECK(owner.expired());
#ifdef __linux__
        CHECK(count("/proc/self/fd") == fds && count("/proc/self/task") == threads);
        rusage usage{};
        CHECK(getrusage(RUSAGE_SELF, &usage) == 0);
        std::cout << "Process peak RSS_KiB=" << usage.ru_maxrss << "; FDs/threads restored; " << owners.size()
                  << " packet owners retired\n";
#endif
        std::cout << "Calibrated scene independent projection/pixel, optical, lifetime, revision and "
                     "two-replay checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
