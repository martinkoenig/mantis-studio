#pragma once
#include <mantis/calibration_solver_opencv.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
namespace m5fixture {
using namespace mantis;
using namespace mantis::calibration;
template <class T> T checked(Result<T> result) {
    if (!result)
        throw Failure(result.error());
    return std::move(*result);
}
// Mathematical M4 fixture, with persistence-owned target/source/camera identities assigned BEFORE solving.
const PinholeBrown5 ground_left{
    CameraModel::pinhole_brown5, 1000, 1015, 638, 482, -.06, .008, .0007, -.0004, -.0006};
const PinholeBrown5 ground_right{
    CameraModel::pinhole_brown5, 1020, 1005, 643, 477, -.05, .006, -.0005, .0008, -.0004};
cv::Mat k(const PinholeBrown5 &m) {
    return (cv::Mat_<double>(3, 3) << m.fx, 0, m.cx, 0, m.fy, m.cy, 0, 0, 1);
}
cv::Mat d(const PinholeBrown5 &m) {
    return (cv::Mat_<double>(5, 1) << m.k1, m.k2, m.p1, m.p2, m.k3);
}
const cv::Vec3d stereo_rvec{.008, -.025, .005}, stereo_translation{-120, 1.5, .8};
FrameSetKey key(uint64_t index) {
    return {{index < 12 ? "A" : "B"}, 100 + (index < 12 ? index : index - 12)};
}
CalibrationDataset fixture(const CalibrationTarget &target, const std::vector<Id> &raw_ids,
                           std::vector<DatasetCamera> cameras = {}) {
    const bool checker = target.type() == TargetType::checkerboard;
    const double noise = 0;
    CalibrationDataset dataset;
    dataset.target = target;
    dataset.config = {1, {"left", "right"}, 1, 24};
    dataset.raw_capture_ids = {{"A"}, {"B"}};
    for (const auto &role : dataset.config.camera_roles)
        dataset.cameras.push_back({role,
                                   {"camera." + role},
                                   1280,
                                   960,
                                   {{"optical." + role}, "+X right, +Y down, +Z forward; millimeters"}});
    cv::Mat stereo_rotation;
    cv::Rodrigues(stereo_rvec, stereo_rotation);
    for (uint64_t i = 0; i < 26; ++i)
        for (const auto &camera : dataset.cameras) {
            DatasetObservationRecord record;
            record.key = {key(i), camera.role, camera.camera_id};
            const bool left = camera.role == "left";
            if (i != 25 && (checker || !(left ? i == 23 : i == 0))) {
                record.outcome = DetectionOutcome::detected;
                TargetObservation o;
                o.source = {record.key.frame.raw_capture_id, record.key.frame.frameset_sequence,
                            camera.camera_id, camera.role};
                o.target = dataset.target.identity;
                o.target_type = dataset.target.type();
                o.image_width = 1280;
                o.image_height = 960;
                std::vector<cv::Point3d> points;
                for (uint32_t id = 0; id < 35; ++id) {
                    if (!checker) {
                        if (left && i == 7 && id != 0 && id != 1 && id != 7)
                            continue;
                        if (left && i == 8 && id > 3)
                            continue;
                        if (i == 6 && (left ? !(id == 0 || id == 1 || id == 7 || id == 8)
                                            : !(id == 2 || id == 3 || id == 9 || id == 10)))
                            continue;
                        if (left && i % 3 == 1 && id % 7 == 6)
                            continue;
                        if (!left && i % 4 == 2 && (id % 7 == 0 || id / 7 == 4))
                            continue;
                    }
                    o.point_ids.push_back(id);
                    auto p = checked(scale_target_point(
                        dataset.target, {double(id % 7 + 1) * 40, double(id / 7 + 1) * 40, 0}));
                    o.object_points_mm.push_back(p);
                    points.emplace_back(p.x_mm, p.y_mm, 0);
                }
                // Diverse, deterministic target poses in LEFT optical coordinates.
                const double index = double(i);
                cv::Vec3d rotation{-.35 + .11 * double(i % 7), -.32 + .13 * double(i % 6),
                                   -.12 + .04 * double(i % 5)};
                cv::Vec3d translation{-140 + 100 * std::sin(index * .73), -100 + 70 * std::cos(index * .57),
                                      600 + 20 * double(i % 7) + 30 * std::sin(index * .33)};
                if (!left) {
                    cv::Mat board_rotation;
                    cv::Rodrigues(rotation, board_rotation);
                    cv::Mat combined = stereo_rotation * board_rotation;
                    cv::Rodrigues(combined, rotation);
                    cv::Mat t = stereo_rotation * cv::Mat(translation);
                    translation =
                        cv::Vec3d(t.at<double>(0), t.at<double>(1), t.at<double>(2)) + stereo_translation;
                }
                std::vector<cv::Point2d> projected;
                cv::projectPoints(points, rotation, translation, k(left ? ground_left : ground_right),
                                  d(left ? ground_left : ground_right), projected);
                for (size_t j = 0; j < projected.size(); ++j)
                    o.image_points_px.push_back(
                        {projected[j].x + noise * std::sin(index * 1.7 + double(o.point_ids[j]) * 2.3),
                         projected[j].y + noise * std::cos(index * .9 + double(o.point_ids[j]) * 1.1)});
                o.evidence = {static_cast<uint32_t>(points.size()), checker ? 0u : 20u, points.size() < 35};
                record.observation = std::move(o);
                record.diversity = checked(describe_diversity(dataset.target, *record.observation));
                if (checker ? i < 24 : (left ? i < 18 : i >= 6 && i < 24))
                    record.selection_rank = static_cast<uint32_t>(checker || left ? i : i - 6);
            }
            dataset.records.push_back(std::move(record));
        }
    dataset.raw_capture_ids = raw_ids;
    if (!cameras.empty())
        dataset.cameras = std::move(cameras);
    for (auto &record : dataset.records) {
        record.key.frame.raw_capture_id = raw_ids.at(record.key.frame.raw_capture_id.value == "A" ? 0 : 1);
        const auto &camera = *std::find_if(dataset.cameras.begin(), dataset.cameras.end(),
                                           [&](const auto &c) { return c.role == record.key.camera_role; });
        record.key.camera_id = camera.camera_id;
        if (record.observation) {
            record.observation->source = {record.key.frame.raw_capture_id, record.key.frame.frameset_sequence,
                                          camera.camera_id, camera.role};
        }
    }
    // The two source sequence ranges overlap; identities retain their capture IDs.
    std::sort(dataset.records.begin(), dataset.records.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });
    auto valid = validate_calibration_dataset(dataset);
    if (!valid)
        throw std::runtime_error(valid.error().message);
    return dataset;
}
} // namespace m5fixture
