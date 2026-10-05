#include <mantis/calibration_observation.hpp> // Self-contained pure public model.
#include <mantis/calibration.hpp>
#include <iostream>
#include <limits>

using namespace mantis;
using namespace mantis::calibration;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed: " #x); } while (false)
TargetObservation observation(std::initializer_list<uint32_t> ids = {4, 5, 6, 13, 14, 22}) {
    TargetObservation value;
    value.source = {{"capture.test"}, 17, {"camera.left"}, "LEFT"};
    value.target = {{"target.test"}, 3};
    value.target_type = TargetType::charuco;
    value.image_width = 1280;
    value.image_height = 720;
    for (auto id : ids) {
        value.point_ids.push_back(id);
        value.image_points_px.push_back({double(id) + 0.25, double(id) + 0.75});
        value.object_points_mm.push_back({double(id) * 41.3, double(id) * 40.6, 0});
    }
    value.evidence = {static_cast<uint32_t>(ids.size()), 7, true};
    return value;
}
void rejects(const TargetObservation &value) {
    const auto result = validate_target_observation(value);
    CHECK(!result && result.error().code == Status::invalid_argument);
    CHECK(result.error().component == "calibration" && !result.error().message.empty());
    const auto again = validate_target_observation(value);
    CHECK(!again && again.error().message == result.error().message);
    CHECK(!intersect_observations(value, observation()));
}
void structural_validation() {
    auto value = observation();
    CHECK(validate_target_observation(value));
    CHECK(value.point_id_semantics() == PointIdSemantics::physical_board);
    value.target_type = TargetType::checkerboard;
    value.evidence = {6, 0, false};
    CHECK(validate_target_observation(value));
    CHECK(value.point_id_semantics() == PointIdSemantics::detector_grid);
    CHECK(validate_target_observation(observation({22}))); // No quality threshold.
    rejects(observation({}));
    for (int field = 0; field < 3; ++field) {
        value = observation();
        if (field == 0) value.point_ids.pop_back();
        if (field == 1) value.image_points_px.pop_back();
        if (field == 2) value.object_points_mm.pop_back();
        rejects(value);
    }
    value = observation(); value.point_ids[1] = value.point_ids[0]; rejects(value);
    value = observation(); ++value.evidence.detected_points; rejects(value);
    value = observation(); value.image_width = 0; rejects(value);
    value = observation(); value.image_height = 0; rejects(value);
    value = observation(); value.source.raw_capture_id.value.clear(); rejects(value);
    value = observation(); value.source.camera_id.value.clear(); rejects(value);
    value = observation(); value.source.camera_role.clear(); rejects(value);
    value = observation(); value.target.id.value.clear(); rejects(value);
    value = observation(); value.target_type = static_cast<TargetType>(99); rejects(value);
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
        for (auto member : {&ImagePoint::x_px, &ImagePoint::y_px}) {
            value = observation(); value.image_points_px[0].*member = invalid; rejects(value);
        }
        for (auto member : {&TargetPoint::x_mm, &TargetPoint::y_mm, &TargetPoint::z_mm}) {
            value = observation(); value.object_points_mm[0].*member = invalid; rejects(value);
        }
    }
}
void common_ids() {
    const auto left = observation({22, 4, 6, 5, 14, 13});
    auto right = observation({30, 14, 22, 5, 6});
    right.source.camera_id = {"camera.right"};
    right.source.camera_role = "RIGHT";
    for (auto &point : right.image_points_px) point.x_px += 100;
    const auto common = intersect_observations(left, right);
    CHECK(common && common->size() == 4);
    const std::vector<uint32_t> expected{5, 6, 14, 22};
    for (size_t i = 0; i < expected.size(); ++i) {
        const auto &point = (*common)[i];
        CHECK(point.point_id == expected[i]);
        CHECK(point.left_px.x_px == double(expected[i]) + 0.25);
        CHECK(point.right_px.x_px == double(expected[i]) + 100.25);
        CHECK(point.object_mm.x_mm == double(expected[i]) * 41.3);
        CHECK(point.object_mm.y_mm == double(expected[i]) * 40.6);
    }
    auto disjoint = intersect_observations(left, observation({90}));
    CHECK(disjoint && disjoint->empty());
    for (int mismatch = 0; mismatch < 6; ++mismatch) {
        auto other = right;
        if (mismatch == 0) other.target.id = {"different"};
        if (mismatch == 1) ++other.target.revision;
        if (mismatch == 2) other.target_type = TargetType::checkerboard;
        if (mismatch == 3) other.object_points_mm[1].x_mm += 1e-9;
        if (mismatch == 4) other.object_points_mm[1].y_mm += 1e-9;
        if (mismatch == 5) other.object_points_mm[1].z_mm += 1e-9;
        auto result = intersect_observations(left, other);
        CHECK(!result && result.error().code == Status::invalid_argument && result.error().component == "calibration");
    }
}
int main() {
    try {
        structural_validation(); common_ids();
        std::cout << "Calibration observations: pure validation, ID semantics and correspondence intersection passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
