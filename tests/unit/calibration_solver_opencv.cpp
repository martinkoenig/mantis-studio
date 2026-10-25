#include <iomanip>
#include <iostream>
#include <mantis/calibration_solver_opencv.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
using namespace mantis;
using namespace mantis::calibration;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error("Check failed at line " + std::to_string(__LINE__) + ": " #x);          \
    } while (false)
template <class T> T checked(Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void near(double a, double b, double tolerance) {
    CHECK(std::isfinite(a) && std::abs(a - b) <= tolerance);
}
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
CalibrationDataset fixture(bool checker = false, double noise = 0) {
    CalibrationDataset dataset;
    dataset.target.identity = {{"target.synthetic"}, 9};
    dataset.target.grid = {8, 6, 40};
    if (!checker)
        dataset.target.pattern = CharucoDefinition{"DICT_6X6_250", 25};
    dataset.target.measurement.active_width_mm = 8 * 41.3;
    dataset.target.measurement.active_height_mm = 6 * 40.6;
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
    // The two source sequence ranges overlap; identities retain their capture IDs.
    std::sort(dataset.records.begin(), dataset.records.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });
    auto valid = validate_calibration_dataset(dataset);
    if (!valid)
        throw std::runtime_error(valid.error().message);
    return dataset;
}
std::vector<FrameSetKey> mono_keys(const CameraCalibrationSolution &s, SolveSampleDisposition disposition) {
    std::vector<FrameSetKey> keys;
    for (const auto &sample : s.partition.samples)
        if (sample.disposition == disposition)
            keys.push_back(sample.key.frame);
    return keys;
}
void model_recovery(const PinholeBrown5 &actual, const PinholeBrown5 &expected) {
    // Synthetic recovery tolerances only; no product acceptance thresholds.
    near(actual.fx, expected.fx, .05);
    near(actual.fy, expected.fy, .05);
    near(actual.cx, expected.cx, .05);
    near(actual.cy, expected.cy, .05);
    near(actual.k1, expected.k1, .001);
    near(actual.k2, expected.k2, .003);
    near(actual.k3, expected.k3, .005);
    near(actual.p1, expected.p1, .0001);
    near(actual.p2, expected.p2, .0001);
}
void mono_contract() {
    auto dataset = fixture(true);
    MonoSolveConfig config;
    config.heldout_per_camera = 3;
    auto solutions = checked(solve_camera_intrinsics(dataset, config));
    CHECK(solutions.size() == 2);
    auto &left = solutions[0];
    model_recovery(left.final_model, ground_left);
    model_recovery(solutions[1].final_model, ground_right);
    CHECK(mono_keys(left, SolveSampleDisposition::held_out) ==
          std::vector<FrameSetKey>({key(5), key(11), key(17)}));
    CHECK(left.training_fit.views.size() == 21 && left.heldout_validation.views.size() == 3 &&
          left.final_fit.views.size() == 24);
    CHECK(left.heldout_validation.residuals.rms_px < .01);
    for (const auto &record : dataset.records)
        if (record.observation)
            CHECK(record.observation->point_id_semantics() == PointIdSemantics::detector_grid);
    auto repeat = checked(solve_camera_intrinsics(dataset, "left", config));
    CHECK(repeat.partition.samples == left.partition.samples);
    near(repeat.final_model.fx, left.final_model.fx, 1e-8);
    // Changing only held-out pixels cannot influence the preliminary model.
    auto changed = dataset;
    for (auto &record : changed.records)
        if (record.key.camera_role == "left" && record.selection_rank &&
            (*record.selection_rank == 5 || *record.selection_rank == 11 || *record.selection_rank == 17)) {
            for (size_t i = 0; i < record.observation->point_ids.size(); ++i)
                record.observation->image_points_px[i].x_px += 3 * std::sin(double(i) * 1.4);
            record.diversity = checked(describe_diversity(changed.target, *record.observation));
        }
    auto changed_solution = checked(solve_camera_intrinsics(changed, "left", config));
    CHECK(changed_solution.training_model == left.training_model);
    CHECK(changed_solution.heldout_validation.residuals.rms_px > 1);
    CHECK(changed_solution.final_model != left.final_model);
    auto fixed = left.training_model;
    const auto *record = solve_detail::record(dataset, key(5), left.camera);
    auto evidence = checked(evaluate_heldout_target(*record->observation, fixed));
    CHECK(fixed == left.training_model);
    auto evidence_again = checked(evaluate_heldout_target(*record->observation, fixed));
    CHECK(evidence.pose.ippe_solution_index == evidence_again.pose.ippe_solution_index);
    CHECK(evidence.pose.ippe_solution_rms_px.size() >= 2);
    const auto best = std::min_element(evidence.pose.ippe_solution_rms_px.begin(),
                                       evidence.pose.ippe_solution_rms_px.end());
    CHECK(*evidence.pose.ippe_solution_index == size_t(best - evidence.pose.ippe_solution_rms_px.begin()));
    near(evidence.residuals.rms_px, *best, 1e-12);
    StereoSolveConfig stereo;
    stereo.left_role = "left";
    stereo.right_role = "right";
    stereo.heldout_pairs = 3;
    stereo.rig_frame = {{"rig"}, "Rig"};
    auto rejected = solve_stereo_rig(dataset, left, solutions[1], stereo);
    CHECK(!rejected && rejected.error().code == Status::incompatible &&
          rejected.error().component == "calibration");
    CHECK(rejected.error().message.find("physical stereo correspondence") != std::string::npos);
    std::cout << "Checkerboard mono: fx=" << left.final_model.fx << " fy=" << left.final_model.fy
              << " held-out RMS=" << left.heldout_validation.residuals.rms_px << " px; stereo rejected\n";
}
void opencv_precision_contract() {
    std::vector<std::vector<cv::Point3d>> objects{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}}};
    std::vector<std::vector<cv::Point2d>> images{{{10, 10}, {20, 10}, {10, 20}, {20, 20}}};
    cv::Mat camera = cv::Mat::eye(3, 3, CV_64F), dist = cv::Mat::zeros(5, 1, CV_64F);
    std::vector<cv::Mat> r, t;
    bool rejected = false;
    try {
        cv::calibrateCamera(objects, images, {100, 100}, camera, dist, r, t, 0,
                            {cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 100, 1e-12});
    } catch (const cv::Exception &e) {
        rejected = e.code == cv::Error::StsUnsupportedFormat;
    }
    CHECK(rejected); // Executable 4.6-generation float-input contract.
}
void stereo_contract() {
    const auto dataset = fixture();
    MonoSolveConfig mono;
    mono.heldout_per_camera = 3;
    auto cameras = checked(solve_camera_intrinsics(dataset, mono));
    auto left = cameras[0], right = cameras[1];
    model_recovery(left.final_model, ground_left);
    model_recovery(right.final_model, ground_right);
    CHECK(mono_keys(left, SolveSampleDisposition::held_out) ==
          std::vector<FrameSetKey>({key(3), key(9), key(13)}));
    CHECK(mono_keys(right, SolveSampleDisposition::held_out) ==
          std::vector<FrameSetKey>({key(9), key(14), key(19)}));
    CHECK(left.partition.samples[7].disposition == SolveSampleDisposition::ineligible_insufficient_points);
    CHECK(left.partition.samples[8].disposition == SolveSampleDisposition::ineligible_degenerate_geometry);
    StereoSolveConfig config;
    config.left_role = "left";
    config.right_role = "right";
    config.heldout_pairs = 3;
    config.rig_frame = {{"rig.synthetic"}, "+X left to right; +Y forward; +Z cross, mm"};
    auto solved = checked(solve_stereo_rig(dataset, left, right, config));
    CHECK(validate_stereo_solution(solved, dataset, left, right));
    CHECK(left.final_model == cameras[0].final_model && right.final_model == cameras[1].final_model);
    CHECK(solved.left_final_intrinsics == left.final_model &&
          solved.right_final_intrinsics == right.final_model);
    std::vector<FrameSetKey> candidates, held;
    for (const auto &sample : solved.partition.samples) {
        candidates.push_back(sample.key);
        if (sample.disposition == SolveSampleDisposition::held_out)
            held.push_back(sample.key);
    }
    const std::vector<uint64_t> expected_order{6,  0,  7,  1,  8,  2,  9,  3,  10, 4,  11, 5,
                                               12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
    std::vector<FrameSetKey> expected;
    for (auto i : expected_order)
        expected.push_back(key(i));
    CHECK(candidates == expected);
    CHECK(held == std::vector<FrameSetKey>({key(10), key(13), key(18)}));
    CHECK(solved.training_fit.pairs.size() == 16 && solved.heldout_validation.pairs.size() == 3 &&
          solved.final_fit.pairs.size() == 19);
    std::map<FrameSetKey, SolveSampleDisposition> dispositions;
    for (const auto &s : solved.partition.samples)
        dispositions[s.key] = s.disposition;
    CHECK(dispositions[key(0)] == SolveSampleDisposition::ineligible_missing_detection);
    CHECK(dispositions[key(23)] == SolveSampleDisposition::ineligible_missing_detection);
    CHECK(dispositions[key(6)] == SolveSampleDisposition::ineligible_insufficient_points);
    CHECK(dispositions[key(7)] == SolveSampleDisposition::ineligible_insufficient_points);
    CHECK(dispositions[key(8)] == SolveSampleDisposition::ineligible_degenerate_geometry);
    cv::Mat rotation;
    cv::Rodrigues(stereo_rvec, rotation);
    double rotation_error{}, translation_error{};
    for (size_t i = 0; i < 9; ++i) {
        const double error = std::abs(solved.final_model.R_right_from_left[i] -
                                      rotation.at<double>(static_cast<int>(i / 3), static_cast<int>(i % 3)));
        rotation_error = std::max(rotation_error, error);
        CHECK(error < 1e-4);
    }
    for (size_t i = 0; i < 3; ++i) {
        const double error =
            std::abs(solved.final_model.T_right_from_left[i] - stereo_translation[static_cast<int>(i)]);
        translation_error = std::max(translation_error, error);
        CHECK(error < .05);
    }
    const double expected_baseline = cv::norm(stereo_translation);
    near(solved.rig.baseline_mm, expected_baseline, .05);
    near(solved.rig.relative_rotation_angle_rad, cv::norm(stereo_rvec), 1e-4);
    const auto composition = spatial::compose(solved.rig.T_rig_from_right, solved.rig.T_right_from_left);
    for (size_t i = 0; i < 16; ++i)
        near(composition.matrix[i], solved.rig.T_rig_from_left.matrix[i], 1e-9);
    CHECK(solved.rig.T_right_from_left.source.id == left.camera.optical_frame.id &&
          solved.rig.T_right_from_left.target.id == right.camera.optical_frame.id);
    CHECK(solved.rig.T_rig_from_left.target.id == config.rig_frame.id &&
          solved.rig.T_rig_from_right.source.id == right.camera.optical_frame.id);
    CHECK(solve_detail::rotation_valid(
        {solved.rig.T_rig_from_left.matrix[0], solved.rig.T_rig_from_left.matrix[1],
         solved.rig.T_rig_from_left.matrix[2], solved.rig.T_rig_from_left.matrix[4],
         solved.rig.T_rig_from_left.matrix[5], solved.rig.T_rig_from_left.matrix[6],
         solved.rig.T_rig_from_left.matrix[8], solved.rig.T_rig_from_left.matrix[9],
         solved.rig.T_rig_from_left.matrix[10]}));
    CHECK(solved.heldout_validation.residuals.rms_px < .01);
    auto repeat = checked(solve_stereo_rig(dataset, left, right, config));
    CHECK(repeat.partition.samples == solved.partition.samples);
    near(repeat.rig.baseline_mm, solved.rig.baseline_mm, 1e-8);
    auto common =
        checked(intersect_observations(*solve_detail::record(dataset, key(13), left.camera)->observation,
                                       *solve_detail::record(dataset, key(13), right.camera)->observation));
    auto exact = checked(
        evaluate_epipolar_pair(key(13), common, solved.final_model, left.final_model, right.final_model));
    CHECK(exact.residuals.rms_px < .01);
    for (auto &p : common)
        p.right_px.y_px += 2;
    auto perturbed = checked(
        evaluate_epipolar_pair(key(13), common, solved.final_model, left.final_model, right.final_model));
    CHECK(perturbed.residuals.rms_px > exact.residuals.rms_px + .5);
    CHECK(perturbed.residuals == checked(summarize_residuals(perturbed.symmetric_epipolar_residuals_px)));
    // Exact ground-truth epipolar contract, independent of fitted coefficients.
    StereoModel ground;
    for (size_t i = 0; i < 9; ++i)
        ground.R_right_from_left[i] = rotation.at<double>(static_cast<int>(i / 3), static_cast<int>(i % 3));
    ground.T_right_from_left = {stereo_translation[0], stereo_translation[1], stereo_translation[2]};
    cv::Mat skew =
        (cv::Mat_<double>(3, 3) << 0, -stereo_translation[2], stereo_translation[1], stereo_translation[2], 0,
         -stereo_translation[0], -stereo_translation[1], stereo_translation[0], 0);
    cv::Mat e = skew * rotation, f = k(ground_right).inv().t() * e * k(ground_left).inv();
    for (size_t i = 0; i < 9; ++i) {
        ground.E[i] = e.at<double>(static_cast<int>(i / 3), static_cast<int>(i % 3));
        ground.F[i] = f.at<double>(static_cast<int>(i / 3), static_cast<int>(i % 3));
    }
    auto original_common =
        checked(intersect_observations(*solve_detail::record(dataset, key(13), left.camera)->observation,
                                       *solve_detail::record(dataset, key(13), right.camera)->observation));
    const auto ground_residual =
        checked(evaluate_epipolar_pair(key(13), original_common, ground, ground_left, ground_right));
    CHECK(ground_residual.residuals.max_px < 1e-8);
    // Parallel, distortion-free stereo gives exactly two pixels for a two-pixel
    // vertical displacement on either side of a horizontal epipolar line.
    auto parallel = ground;
    parallel.R_right_from_left = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    parallel.T_right_from_left = {-120, 0, 0};
    parallel.E = {0, 0, 0, 0, 0, 120, 0, -120, 0};
    parallel.F = {0, 0, 0, 0, 0, 1, 0, -1, 0};
    const PinholeBrown5 ideal{CameraModel::pinhole_brown5, 1000, 1000, 640, 480, 0, 0, 0, 0, 0};
    const std::vector<CommonTargetPoint> displaced{{4, {500, 300}, {300, 302}, {40, 40, 0}},
                                                   {8, {700, 600}, {500, 602}, {80, 80, 0}}};
    auto two_pixels = checked(evaluate_epipolar_pair(key(13), displaced, parallel, ideal, ideal));
    near(two_pixels.residuals.rms_px, 2, 1e-10);
    near(two_pixels.residuals.mean_px, 2, 1e-10);
    near(two_pixels.residuals.p95_px, 2, 1e-10);

    auto reject = [&](auto mutate) {
        auto bad = solved;
        mutate(bad);
        CHECK(!validate_stereo_solution(bad));
    };
    reject([](auto &s) { s.config.left_role = s.config.right_role; });
    reject([](auto &s) { s.target.pattern = CheckerboardDefinition{}; });
    reject([](auto &s) { s.final_model.R_right_from_left[0] = 2; });
    reject([](auto &s) { s.final_model.T_right_from_left[0] = INFINITY; });
    reject([](auto &s) { s.final_model.F[0] = INFINITY; });
    reject([](auto &s) { s.rig.baseline_mm = 0; });
    reject([](auto &s) { s.rig.T_rig_from_left.source = s.right_camera.optical_frame; });
    reject([](auto &s) { s.rig.T_rig_from_right.matrix[3] += 1; });
    reject([](auto &s) { s.heldout_validation.pairs[0].key = s.training_fit.pairs[0].key; });
    reject([](auto &s) { s.final_fit.pairs.pop_back(); });
    reject([](auto &s) { s.final_fit.residuals.point_count++; });
    reject([](auto &s) { s.training_fit.pairs[0].common_point_ids[0]++; });
    auto wrong_left = left;
    wrong_left.camera.optical_frame.name = "changed";
    CHECK(!solve_stereo_rig(dataset, wrong_left, right, config));
    auto numerical = solved.final_model;
    numerical.F = {0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto zero_line = evaluate_epipolar_pair(key(13), common, numerical, left.final_model, right.final_model);
    CHECK(!zero_line && zero_line.error().code == Status::corrupt);
    std::cout << "ChArUco mono: LEFT fx error=" << left.final_model.fx - ground_left.fx
              << " RIGHT fx error=" << right.final_model.fx - ground_right.fx
              << " held-out RMS LEFT/RIGHT=" << left.heldout_validation.residuals.rms_px << '/'
              << right.heldout_validation.residuals.rms_px << " px\n";
    std::cout << "Stereo: max R error=" << rotation_error << " max T error=" << translation_error
              << " mm; baseline=" << solved.rig.baseline_mm << " (expected " << expected_baseline
              << ") mm; training/held-out/final epipolar RMS=" << solved.training_fit.residuals.rms_px << '/'
              << solved.heldout_validation.residuals.rms_px << '/' << solved.final_fit.residuals.rms_px
              << " px\n";
}
void bad_fit_and_errors() {
    MonoSolveConfig config;
    config.heldout_per_camera = 3;
    auto noisy = fixture(true, 3);
    auto fit = checked(solve_camera_intrinsics(noisy, "left", config));
    CHECK(validate_camera_solution(fit, noisy));
    CHECK(fit.final_fit.residuals.rms_px > 1);
    std::cout << "Finite noisy fit retained: final RMS=" << fit.final_fit.residuals.rms_px
              << " px held-out RMS=" << fit.heldout_validation.residuals.rms_px << " px\n";
    config.heldout_per_camera = 0;
    CHECK(!solve_camera_intrinsics(noisy, "left", config));
    config.heldout_per_camera = 24;
    CHECK(!solve_camera_intrinsics(noisy, "left", config));
    config.heldout_per_camera = 3;
    CHECK(!solve_camera_intrinsics(noisy, "LEFT", config));
    auto bad = noisy;
    bad.records[0].observation->source.camera_id = {"wrong"};
    CHECK(!solve_camera_intrinsics(bad, "left", config));
    bad = fixture(true);
    bad.target.grid.nominal_square_size_mm = 1e40;
    bad.target.measurement = {};
    for (auto &record : bad.records)
        if (record.observation) {
            for (size_t i = 0; i < record.observation->point_ids.size(); ++i) {
                auto id = record.observation->point_ids[i];
                record.observation->object_points_mm[i] = checked(scale_target_point(
                    bad.target, {double(id % 7 + 1) * 1e40, double(id / 7 + 1) * 1e40, 0}));
            }
            record.diversity = checked(describe_diversity(bad.target, *record.observation));
        }
    CHECK(validate_calibration_dataset(bad));
    auto range = solve_camera_intrinsics(bad, "left", config);
    CHECK(!range && range.error().message.find("float representation") != std::string::npos);
    // Degenerate image data can reach OpenCV while object geometry is valid;
    // internal exceptions must become structured errors, never escape the API.
    bad = fixture(true);
    for (auto &record : bad.records)
        if (record.observation) {
            for (auto &point : record.observation->image_points_px)
                point = {640, 480};
            record.diversity = checked(describe_diversity(bad.target, *record.observation));
        }
    auto failed = solve_camera_intrinsics(bad, "left", config);
    CHECK(!failed && failed.error().component == "calibration" && failed.error().code == Status::corrupt);
    std::cout << "Numerical failure converted: " << failed.error().message.substr(0, 100) << "\n";
}
int main() {
    try {
        std::cout << std::setprecision(12) << "M4 solver OpenCV " << solver_opencv_version()
                  << " (core/calib3d)\n";
        opencv_precision_contract();
        mono_contract();
        stereo_contract();
        bad_fit_and_errors();
        std::cout << "OpenCV calibration solver contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
