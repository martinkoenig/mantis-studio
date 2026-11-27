#pragma once
#include "../fixtures/calibration_artifacts.hpp"
#include "../unit/laser_observation_values.hpp"
#include <cmath>
#include <mantis/l1_ray_plane.hpp>

// Software-only M4 graph. Values are solved from independent synthetic forward
// observations, not from a physical camera/laser or an active calibration lookup.
namespace alg03_fixture {
using namespace mantis;
namespace ca = calibration::artifacts;
namespace a3 = alg03;
template <class T> T get(Result<T> r) {
    if (!r)
        throw Failure(r.error());
    return std::move(*r);
}
inline void get(Result<void> r) {
    if (!r)
        throw Failure(r.error());
}
struct Temp {
    std::filesystem::path path = std::filesystem::temp_directory_path() / Id::random().value;
    ~Temp() {
        std::error_code e;
        std::filesystem::remove_all(path, e);
    }
};
inline Id raw(artifact::Store &s) {
    auto id = s.begin({"org.mantis.RawCapture", 2}, {});
    for (uint64_t i = 100; i < 114; ++i) {
        data::Packet fs;
        fs.type = schema::frameset;
        fs.header.sequence.value = i;
        for (auto role : {"left", "right"}) {
            data::Packet image;
            image.type = schema::image;
            image.header.metadata = {{"role", role}, {"identity", "camera." + std::string(role)}};
            image.attributes = {{{"org.mantis.pixels", schema::ScalarType::u8, {8, 8}, {8, 1}, "intensity"},
                                 memory::copy(std::vector<std::byte>(64, std::byte{42}))}};
            fs.frames.push_back(data::publish(std::move(image)));
        }
        s.append(id, fs);
    }
    s.finalize(id);
    return id;
}
struct Graph {
    ca::Stored<ca::TargetArtifact> target;
    ca::Stored<ca::DatasetArtifact> dataset;
    ca::Stored<ca::CameraArtifact> left, right;
    ca::Stored<ca::RigArtifact> rig;
};
inline std::pair<ca::Stored<ca::TargetArtifact>, std::vector<Id>> calibration_sources(artifact::Store &s) {
    calibration::CalibrationTarget target;
    target.grid = {8, 6, 40};
    target.pattern = calibration::CharucoDefinition{"DICT_6X6_250", 25};
    // Declared mathematical target extents; no claim of physical measurements.
    target.measurement = {8 * 41.3, 6 * 40.6, {}};
    auto t = get(ca::create_calibration_target(s, target));
    std::vector<Id> captures{raw(s), raw(s)};
    std::sort(captures.begin(), captures.end());
    return {std::move(t), std::move(captures)};
}
inline Graph graph(artifact::Store &s) {
    auto [t, captures] = calibration_sources(s);
    auto data = m5fixture::fixture(t.value.target, captures);
    auto d = get(ca::create_calibration_dataset(s, data, t.reference()));
    calibration::MonoSolveConfig mono;
    mono.heldout_per_camera = 3;
    auto left = get(calibration::solve_camera_intrinsics(data, "left", mono)),
         right = get(calibration::solve_camera_intrinsics(data, "right", mono));
    ca::SolverImplementation backend{std::string(calibration::solver_opencv_version())};
    auto l = get(ca::create_camera_calibration(s, left, d.reference(), t.reference(), backend));
    auto r = get(ca::create_camera_calibration(s, right, d.reference(), t.reference(), backend));
    calibration::StereoSolveConfig stereo;
    stereo.left_role = "left";
    stereo.right_role = "right";
    stereo.heldout_pairs = 3;
    stereo.rig_frame = {{"alg03.synthetic.rig"}, "+X left to right, +Y forward, +Z cross; millimeters"};
    auto solution = get(calibration::solve_stereo_rig(data, left, right, stereo));
    auto rig = get(ca::create_rig_calibration(s, solution, d.reference(), t.reference(), l.reference(),
                                              r.reference(), backend));
    return {std::move(t), std::move(d), std::move(l), std::move(r), std::move(rig)};
}
template <class T> inline data::ExactCalibrationReference exact(const ca::Stored<T> &v) {
    return {v.value.revision,
            data::ContentReference{
                v.descriptor.id, {v.descriptor.type.name, 1}, v.descriptor.hash, v.value.revision.revision}};
}
inline a3::Plane plane(const Graph &g, const data::ComponentId &component, const data::LineIdentity &line) {
    a3::Plane p;
    p.rig = exact(g.rig);
    p.frame = g.rig.value.solution.config.rig_frame;
    p.normal_xyz = {0, -1, 0};
    p.offset_mm = 800;
    p.line = line;
    p.cameras = {{component, g.left.value.solution.camera, exact(g.left)}};
    p.producer = {{"org.mantis.alg03.declared-synthetic-plane"},
                  {1, 0, 0},
                  "fixture-no-optical-calibration",
                  data::Unavailable{}};
    return p;
}
inline a3::Plane plane(const Graph &g) {
    const auto source = observation_fixture::observation();
    return plane(g, source.context.source.frame.camera, source.line_dictionary[0]);
}
inline a3::Policy policy(a3::SourceContract source = a3::SourceContract::synthetic_reference_1) {
    a3::Policy p;
    p.source_contract = source;
    p.producer = {
        {"org.mantis.alg03.synthetic-policy"}, {1, 0, 0}, "fixture-numeric-budget-only", data::Unavailable{}};
    return p;
}
// Independent forward oracle: transform a rig point with R^T and evaluate Brown5
// in long double. No inverse/intersection or OpenCV undistortion is used here.
inline std::array<long double, 2> forward(const calibration::PinholeBrown5 &m, const spatial::Transform &t,
                                          const std::array<long double, 3> &point) {
    std::array<long double, 3> c{};
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 3; ++j)
            c[i] += static_cast<long double>(t.matrix[j * 4 + i]) * (point[j] - t.matrix[j * 4 + 3]);
    long double x = c[0] / c[2], y = c[1] / c[2], r = x * x + y * y,
                k = 1 + m.k1 * r + m.k2 * r * r + m.k3 * r * r * r;
    return {m.fx * (x * k + 2 * m.p1 * x * y + m.p2 * (r + 2 * x * x)) + m.cx,
            m.fy * (y * k + m.p1 * (r + 2 * y * y) + 2 * m.p2 * x * y) + m.cy};
}
// Independent oracle for the *rounded input pixel*: solve for X,Z on known Y
// using only forward projection and a long-double central-difference Jacobian.
inline std::array<long double, 3> oracle(const Graph &g, float u, float v, bool right = false) {
    std::array<long double, 3> p{0, 800, 0};
    const auto &m = right ? g.right.value.solution.final_model : g.left.value.solution.final_model;
    const auto &t =
        right ? g.rig.value.solution.rig.T_rig_from_right : g.rig.value.solution.rig.T_rig_from_left;
    for (unsigned step = 0; step < 20; ++step) {
        auto f = forward(m, t, p);
        long double a = f[0] - u, b = f[1] - v;
        if (std::hypot(a, b) < 1e-14L)
            return p;
        constexpr long double h = .01L;
        std::array<std::array<long double, 2>, 2> jac;
        for (size_t j = 0; j < 2; ++j) {
            auto l = p, r = p;
            l[j * 2] -= h;
            r[j * 2] += h;
            auto fl = forward(m, t, l), fr = forward(m, t, r);
            jac[j] = {(fr[0] - fl[0]) / (2 * h), (fr[1] - fl[1]) / (2 * h)};
        }
        long double determinant = jac[0][0] * jac[1][1] - jac[1][0] * jac[0][1];
        if (std::abs(determinant) < 1e-12L)
            throw std::runtime_error("Degenerate independent forward oracle");
        p[0] -= (jac[1][1] * a - jac[1][0] * b) / determinant;
        p[2] -= (-jac[0][1] * a + jac[0][0] * b) / determinant;
    }
    throw std::runtime_error("Independent forward oracle did not converge");
}
inline void replace(data::LaserObservation &o, data::Attribute a) {
    for (auto &v : o.attributes)
        if (v.descriptor.name == a.descriptor.name) {
            v = std::move(a);
            return;
        }
    o.attributes.push_back(std::move(a));
}
inline data::LaserObservation observation(const Graph &g, uint64_t n = 2) {
    auto o = observation_fixture::observation(n ? 4 : 2, n);
    auto &c = o.context;
    const auto &camera = g.left.value.solution.camera;
    c.source.camera_role = camera.role;
    c.source.width = camera.image_width;
    c.source.height = camera.image_height;
    c.source.camera_calibration = exact(g.left);
    c.source.rig_calibration = exact(g.rig);
    c.optical_frame = camera.optical_frame;
    data::ContentReference parameters{{"alg03-math-fixture"},
                                      {"org.mantis.Alg03SyntheticParameters", 1},
                                      Hash{"fnv1a64", "1234567890abcdef"},
                                      1};
    c.producer = {
        {"org.mantis.alg03.synthetic-reference"}, {1, 0, 0}, "fixture-forward-long-double", parameters};
    c.parameters = parameters;
    if (n) {
        std::vector<float> pixels(static_cast<size_t>(n) * 2);
        for (uint64_t i = 0; i < n; ++i) {
            auto p =
                forward(g.left.value.solution.final_model, g.rig.value.solution.rig.T_rig_from_left,
                        {static_cast<long double>(i % 31) - 15, 800, static_cast<long double>(i % 17) - 8});
            pixels[i * 2] = float(p[0]);
            pixels[i * 2 + 1] = float(p[1]);
        }
        replace(o, {data::laser::source_pixel_descriptor(n), memory::copy(std::as_bytes(std::span(pixels)))});
    }
    return o;
}
inline data::ContentReference persist(artifact::Store &s, const data::LaserObservation &o) {
    auto id = s.begin_laser_observation();
    s.append_laser_observation(id, o);
    auto d = s.finalize(id);
    return {id, schema::laser_observation, d.hash, 1};
}
inline a3::Bindings bind(artifact::Store &s, const Graph &g, const data::LaserObservation &o, a3::Plane p,
                         a3::Policy policy) {
    return {persist(s, o), get(a3::create_plane(s, p)),      get(a3::create_policy(s, policy)), exact(g.left),
            p.rig,         a3::Selection::recorded_effective};
}
template <class T>
inline T value(const data::Packet &p, std::string_view name, size_t row, size_t component = 0) {
    for (auto &a : p.attributes)
        if (a.descriptor.name == std::string(a3::prefix) + std::string(name)) {
            auto bytes = get(a.buffer.map_read());
            T v;
            std::memcpy(&v, bytes.data() + row * a.descriptor.stride[0] + component * sizeof(T), sizeof(T));
            return v;
        }
    throw std::runtime_error("Missing ALG03 test column");
}
} // namespace alg03_fixture
