#include <cmath>
#include <iostream>
#include <mantis/l1_ray_plane.hpp>
#include <nlohmann/json.hpp>
#include <string_view>
using namespace mantis;
using namespace mantis::alg03;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__));                 \
    } while (false)
spatial::Transform transform(bool right = false) {
    spatial::Transform t;
    t.source = {{"camera"}, "optical"};
    t.target = {{"rig"}, "rig"};
    t.matrix = {1, 0, 0, right ? 50. : -50., 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1};
    return t;
}
int enclosure_data() {
    nlohmann::json rows = nlohmann::json::array();
    calibration::PinholeBrown5 m{
        calibration::CameraModel::pinhole_brown5, 800, 800, 319.5, 239.5, 0, 0, 0, 0, 0};
    Plane plane;
    plane.normal_xyz = {0, -1, 0};
    plane.offset_mm = 1000;
    Policy p;
    auto emit = [&](const char *name, float u, float v, bool threshold = false) {
        auto s = intersect(m, transform(), plane, p, u, v);
        CHECK(s.reasons == 0);
        auto allowance = p;
        if (threshold) {
            allowance.max_numeric_error_mm = 1e-8;
            s = intersect(m, transform(), plane, allowance, u, v);
        }
        rows.push_back({{"name", name},
                        {"pixel", {u, v}},
                        {"position", s.position},
                        {"radius", s.diagnostics[6]},
                        {"reasons", s.reasons},
                        {"allowance", allowance.max_numeric_error_mm},
                        {"domain", {p.domain.a, p.domain.b}},
                        {"model",
                         {{"fx", m.fx},
                          {"fy", m.fy},
                          {"cx", m.cx},
                          {"cy", m.cy},
                          {"k1", m.k1},
                          {"k2", m.k2},
                          {"k3", m.k3},
                          {"p1", m.p1},
                          {"p2", m.p2}}}});
    };
    emit("exact-centre", 319.5f, 239.5f);
    emit("rational-off-axis", 399.5f, 199.5f);
    emit("domain-interior-near-boundary", 718.7f, 239.5f);
    m.k1 = .01;
    emit("radial-stored-f32", 399.51f, 199.495f);
    m.k2 = -.002;
    m.k3 = .0004;
    m.p1 = .001;
    m.p2 = -.0007;
    emit("radial-tangential", 423.1234f, 181.5678f);
    emit("numeric-policy-tight", 423.1234f, 181.5678f, true);
    emit("distorted-domain-near-boundary", 718.7f, 239.5f);
    std::cout << rows.dump() << '\n';
    return 0;
}
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--enclosure-data")
            return enclosure_data();
        calibration::PinholeBrown5 camera{
            calibration::CameraModel::pinhole_brown5, 800, 800, 319.5, 239.5, 0, 0, 0, 0, 0};
        Plane plane;
        plane.normal_xyz = {0, -1, 0};
        plane.offset_mm = 1000;
        Policy policy;
        // Decisive initial-exact-root regression: no decreasing trial exists at
        // the exact optical centre, but convergence/certification must succeed.
        for (bool right : {false, true}) {
            auto s = intersect(camera, transform(right), plane, policy, 319.5f, 239.5f);
            CHECK(s.reasons == 0);
            CHECK(s.position[0] == (right ? 50 : -50));
            CHECK(s.position[1] == 1000);
            CHECK(s.position[2] == 0);
            CHECK((s.diagnostic_valid & 0x7f) == 0x7f);
            CHECK(s.diagnostics[0] == 0);
            CHECK(s.diagnostics[6] == 0);
        }
        auto s = intersect(camera, transform(), plane, policy, 399.5f, 199.5f);
        std::cerr.precision(17);
        std::cerr << "ALG03 off-axis reasons=" << s.reasons << " diagnostic_valid=" << s.diagnostic_valid;
        for (double x : s.position)
            std::cerr << " position=" << x;
        for (size_t i = 0; i < s.diagnostics.size(); ++i)
            std::cerr << " diagnostic[" << i << "]=" << s.diagnostics[i];
        std::cerr << '\n';
        CHECK(s.reasons == 0);
        CHECK(std::abs(s.position[0] - 50) < 2e-8);
        CHECK(std::abs(s.position[2] - 50) < 2e-8);
        // Both residual components enclose zero for these exact rational rays.
        // Their squared-norm enclosure must retain its known nonnegative domain.
        // The coordinate oracle is the independent inverse of the declared K/R
        // for plane Y=1000, not the production inverse/intersection calculation.
        for (const auto pixel :
             {std::array<float, 2>{399.5f, 199.5f}, std::array<float, 2>{239.5f, 279.5f}}) {
            auto residual_zero = intersect(camera, transform(), plane, policy, pixel[0], pixel[1]);
            const long double expected_x = -50.L + 1000.L * (pixel[0] - 319.5L) / 800.L;
            const long double expected_z = -1000.L * (pixel[1] - 239.5L) / 800.L;
            CHECK(residual_zero.reasons == 0);
            CHECK((residual_zero.diagnostic_valid & 0x7f) == 0x7f);
            CHECK(std::abs(residual_zero.position[0] - expected_x) < 2e-8L);
            CHECK(std::abs(residual_zero.position[1] - 1000.L) < 2e-8L);
            CHECK(std::abs(residual_zero.position[2] - expected_z) < 2e-8L);
            CHECK(residual_zero.diagnostics[0] <= 1e-9);
            CHECK(residual_zero.diagnostics[6] <= policy.max_numeric_error_mm);
        }
        // Independent forward Brown projection from exact rig point(50,1000,50).
        camera.k1 = .01;
        const long double x = .1L, y = -.05L, r = x * x + y * y, a = 1 + .01L * r;
        const float u = static_cast<float>(800 * x * a + 319.5L),
                    v = static_cast<float>(800 * y * a + 239.5L);
        s = intersect(camera, transform(), plane, policy, u, v);
        CHECK(s.reasons == 0);
        CHECK(std::abs(s.position[0] - 50) < 1e-4 && std::abs(s.position[2] - 50) < 1e-4);
        auto tight = policy;
        tight.max_numeric_error_mm = 1e-20;
        CHECK(intersect(camera, transform(), plane, tight, u, v).reasons & numeric_guard);
        CHECK((s.diagnostic_valid & (1u << 8)) != 0);
        CHECK(s.diagnostics[6] <= policy.max_numeric_error_mm);
        auto adjacent = policy;
        adjacent.max_numeric_error_mm = std::nextafter(s.diagnostics[6], 1.);
        CHECK(intersect(camera, transform(), plane, adjacent, u, v).reasons == 0);
        adjacent.max_numeric_error_mm = std::nextafter(s.diagnostics[6], 0.);
        CHECK(intersect(camera, transform(), plane, adjacent, u, v).reasons & numeric_guard);
        auto outside = intersect(camera, transform(), plane, policy, 100000.f, 239.5f);
        CHECK(outside.reasons & (inverse_failure | inverse_uncertified));
        auto invalid = camera;
        invalid.fx = 0;
        CHECK(!certify_domain(invalid, policy.domain));
        invalid = camera;
        invalid.k1 = std::numeric_limits<double>::infinity();
        CHECK(!certify_domain(invalid, policy.domain));
        auto certificate = certify_domain(camera, policy.domain);
        CHECK(certificate && *certificate > .98);
        camera.k1 = 10;
        CHECK(!certify_domain(camera, policy.domain));
        camera.k1 = 0;
        auto changed = plane;
        changed.normal_xyz = {1, 0, 0};
        changed.offset_mm = 0;
        s = intersect(camera, transform(), changed, policy, 319.5f, 239.5f);
        CHECK(s.reasons & parallel);
        changed.offset_mm = 50;
        s = intersect(camera, transform(), changed, policy, 319.5f, 239.5f);
        CHECK((s.reasons & (parallel | camera_on_plane)) == (parallel | camera_on_plane));
        changed = plane;
        changed.normal_xyz = {0, 1, 0};
        changed.offset_mm = -1000;
        CHECK(intersect(camera, transform(), changed, policy, 319.5f, 239.5f).reasons & backside);
        changed.side = Side::two_sided;
        CHECK(intersect(camera, transform(), changed, policy, 319.5f, 239.5f).reasons == 0);
        changed = plane;
        changed.offset_mm = -1000;
        changed.side = Side::two_sided;
        CHECK(intersect(camera, transform(), changed, policy, 319.5f, 239.5f).reasons & behind_camera);
        for (double depth : {100., 2000.}) {
            changed = plane;
            changed.offset_mm = depth;
            CHECK(intersect(camera, transform(), changed, policy, 319.5f, 239.5f).reasons == 0);
        }
        for (double depth : {99., 2001.}) {
            changed = plane;
            changed.offset_mm = depth;
            CHECK(intersect(camera, transform(), changed, policy, 319.5f, 239.5f).reasons &
                  depth_or_position_domain);
        }
        for (double delta : {.009, .011}) {
            changed = plane;
            changed.normal_xyz = {std::sqrt(1 - delta * delta), -delta, 0};
            changed.offset_mm = 50 * changed.normal_xyz[0] + 1000 * delta;
            s = intersect(camera, transform(), changed, policy, 319.5f, 239.5f);
            CHECK(delta < .01 ? (s.reasons & near_parallel) != 0 : s.reasons == 0);
        }
        auto bounded = policy;
        bounded.uncertainty_requirement = Uncertainty::require_declared_bound;
        bounded.max_declared_model_error_mm = 1;
        CHECK(intersect(camera, transform(), plane, bounded, 319.5f, 239.5f).reasons &
              declared_bound_unavailable_or_exceeded);
        changed = plane;
        changed.parameter_bounds = PlaneBounds{0, 0, {}};
        bounded.declared_ray_bounds = RayBounds{0, 0, {}};
        s = intersect(camera, transform(), changed, bounded, 319.5f, 239.5f);
        CHECK(s.reasons == 0 && s.diagnostics[7] == 0 && (s.diagnostic_valid & (1u << 7)));
        bounded.declared_ray_bounds->direction_l2 = .9;
        changed.parameter_bounds->normal_l2 = .9;
        CHECK(intersect(camera, transform(), changed, bounded, 319.5f, 239.5f).reasons &
              declared_bound_unavailable_or_exceeded);
        CancellationToken cancel;
        cancel.cancel();
        try {
            (void)intersect(camera, transform(), plane, policy, 319.5f, 239.5f, cancel);
            CHECK(false);
        } catch (const Failure &e) {
            CHECK(e.error.code == Status::cancelled);
        }
        std::cout << "ALG03 independent forward/reference/negative arithmetic source cases passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
