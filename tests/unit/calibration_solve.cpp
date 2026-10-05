#include <iostream>
#include <mantis/calibration.hpp>
#include <mantis/calibration_solve.hpp> // Self-contained; no OpenCV/storage linkage.
using namespace mantis;
using namespace mantis::calibration;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error("Check failed at line " + std::to_string(__LINE__) + ": " #x);          \
    } while (false)
void near(double actual, double expected, double tolerance = 1e-12) {
    CHECK(std::isfinite(actual) && std::abs(actual - expected) <= tolerance);
}
CalibrationDataset fixture(bool checker = false) {
    CalibrationDataset d;
    d.target.identity = {{"target"}, 4};
    d.target.grid = {8, 6, 40};
    if (!checker)
        d.target.pattern = CharucoDefinition{"DICT_6X6_250", 25};
    d.config = {1, {"left", "right"}, 1, 8};
    d.raw_capture_ids = {{"A"}, {"B"}};
    for (auto role : d.config.camera_roles)
        d.cameras.push_back(
            {role, {"camera." + role}, 1000, 800, {{"optical." + role}, "+X right +Y down +Z forward, mm"}});
    for (uint64_t i = 0; i < 10; ++i)
        for (const auto &camera : d.cameras) {
            DatasetObservationRecord r;
            r.key = {{{i < 5 ? "A" : "B"}, 17 + i % 5}, camera.role, camera.camera_id};
            if (i != 9) {
                r.outcome = DetectionOutcome::detected;
                TargetObservation o;
                o.source = {r.key.frame.raw_capture_id, r.key.frame.frameset_sequence, camera.camera_id,
                            camera.role};
                o.target = d.target.identity;
                o.target_type = d.target.type();
                o.image_width = 1000;
                o.image_height = 800;
                for (uint32_t id = 0; id < 35; ++id) {
                    o.point_ids.push_back(id);
                    o.object_points_mm.push_back(
                        *scale_target_point(d.target, {double(id % 7 + 1) * 40, double(id / 7 + 1) * 40, 0}));
                    o.image_points_px.push_back(
                        {100 + double(id % 7) * 20 + double(i), 120 + double(id / 7) * 25});
                }
                o.evidence = {35, checker ? 0u : 20u, false};
                r.observation = o;
                r.diversity = *describe_diversity(d.target, o);
                if (i < 8)
                    r.selection_rank = static_cast<uint32_t>(i);
            }
            d.records.push_back(r);
        }
    CHECK(validate_calibration_dataset(d));
    return d;
}
void subset(CalibrationDataset &d, size_t record, std::vector<uint32_t> ids) {
    auto &r = d.records[record];
    const auto original = *r.observation;
    auto &o = *r.observation;
    o.point_ids.clear();
    o.image_points_px.clear();
    o.object_points_mm.clear();
    for (auto id : ids) {
        o.point_ids.push_back(id);
        o.image_points_px.push_back(original.image_points_px[id]);
        o.object_points_mm.push_back(original.object_points_mm[id]);
    }
    o.evidence.detected_points = static_cast<uint32_t>(ids.size());
    o.evidence.partial = true;
    r.diversity = *describe_diversity(d.target, o);
}
void split_contract() {
    for (auto [n, h, expected] : {std::tuple{uint64_t{2}, uint32_t{1}, std::vector<uint64_t>{0}},
                                  {5, 1, {2}},
                                  {6, 2, {1, 3}},
                                  {8, 3, {1, 3, 5}},
                                  {10, 3, {1, 4, 7}},
                                  {5, 4, {0, 1, 2, 3}},
                                  {11, 2, {3, 7}}}) {
        auto positions = heldout_positions(n, h);
        CHECK(positions && *positions == expected);
        CHECK(std::set<uint64_t>(positions->begin(), positions->end()).size() == h);
    }
    CHECK(!heldout_positions(5, 0));
    CHECK(!heldout_positions(5, 5));
    CHECK(!heldout_positions(4, 5));
    CHECK(!heldout_positions(UINT64_MAX, 1));
    CHECK(*heldout_positions(UINT64_MAX - 1, 2) ==
          std::vector<uint64_t>({UINT64_MAX / 3 - 1, 2 * (UINT64_MAX / 3) - 1}));
    auto d = fixture();
    subset(d, 2, {0, 1, 2});
    subset(d, 6, {0, 1, 2, 3});
    CHECK(validate_calibration_dataset(d));
    MonoSolveConfig config;
    config.heldout_per_camera = 2;
    auto a = partition_mono_samples(d, d.cameras[0], config);
    CHECK(a && a->samples.size() == 8);
    CHECK(a->samples[1].disposition == SolveSampleDisposition::ineligible_insufficient_points);
    CHECK(a->samples[3].disposition == SolveSampleDisposition::ineligible_degenerate_geometry);
    CHECK(a->samples[2].disposition == SolveSampleDisposition::held_out); // A/19
    CHECK(a->samples[5].disposition == SolveSampleDisposition::held_out); // B/17
    CHECK(a->samples[2].key.frame == FrameSetKey({{"A"}, 19}));
    CHECK(a->samples[5].key.frame == FrameSetKey({{"B"}, 17}));
    auto again = partition_mono_samples(d, d.cameras[0], config);
    CHECK(again && a->samples == again->samples);
    for (size_t i = 0; i < 8; ++i)
        CHECK(a->samples[i].selection_rank == i);
    config.heldout_per_camera = 6;
    CHECK(!partition_mono_samples(d, d.cameras[0], config));
    config.heldout_per_camera = 1;
    auto camera = d.cameras[0];
    camera.camera_id = {"wrong"};
    CHECK(!partition_mono_samples(d, camera, config));
    std::vector<TargetPoint> p{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
    CHECK(solver_eligibility(p) == SolveSampleDisposition::ineligible_degenerate_geometry);
    p[3].y_mm = 1;
    CHECK(solver_eligibility(p) == SolveSampleDisposition::training);
    for (auto &point : p) {
        point.x_mm *= 1e200;
        point.y_mm *= 1e200;
    }
    CHECK(solver_eligibility(p) == SolveSampleDisposition::training);
    p[3].z_mm = 1;
    CHECK(solver_eligibility(p) == SolveSampleDisposition::ineligible_degenerate_geometry);
    StereoSolveConfig s;
    s.left_role = "left";
    s.right_role = "right";
    s.heldout_pairs = 2;
    s.rig_frame = {{"rig"}, "rig"};
    auto stereo = partition_stereo_samples(fixture(), s);
    CHECK(stereo && stereo->samples.size() == 8);
    CHECK(stereo->samples[2].disposition == SolveSampleDisposition::held_out);
    CHECK(stereo->samples[5].disposition == SolveSampleDisposition::held_out);
    auto checker = partition_stereo_samples(fixture(true), s);
    CHECK(!checker && checker.error().code == Status::incompatible);
    CHECK(checker.error().message.find("physical stereo correspondence") != std::string::npos);
    s.right_role = "unknown";
    CHECK(!partition_stereo_samples(d, s));
}
void metric_contract() {
    auto a = summarize_residuals(std::vector<double>{0, 3, 4});
    CHECK(a && a->point_count == 3);
    near(a->rms_px, std::sqrt(25.0 / 3));
    near(a->mean_px, 7.0 / 3);
    near(a->median_px, 3);
    near(a->p95_px, 4);
    near(a->max_px, 4);
    auto b = summarize_residuals(std::vector<double>{4, 1, 3, 2});
    CHECK(b);
    near(b->median_px, 2.5);
    near(b->rms_px, std::sqrt(7.5));
    near(b->mean_px, 2.5);
    std::vector<double> values;
    for (int i = 1; i <= 20; ++i)
        values.push_back(i);
    near(summarize_residuals(values)->p95_px, 19);
    CHECK(!summarize_residuals({}));
    for (double bad : {-1.0, double(INFINITY), std::numeric_limits<double>::quiet_NaN()})
        CHECK(!summarize_residuals(std::vector<double>{1, bad}));
    auto large = summarize_residuals(std::vector<double>{1e300, 1e300});
    CHECK(large);
    near(large->rms_px / 1e300, 1);
    auto coverage =
        summarize_coverage(std::vector<ImagePoint>{{-100, 200}, {1200, 700}, {500, 400}}, 1000, 1000);
    CHECK(coverage);
    near(coverage->min_x, -.1);
    near(coverage->max_x, 1.2);
    near(coverage->min_y, .2);
    near(coverage->max_y, .7);
    near(coverage->bounding_box_area, .65);
    CHECK(!summarize_coverage({}, 100, 100));
    CHECK(!summarize_coverage(std::vector<ImagePoint>{{0, 0}}, 0, 100));
    CHECK(!summarize_coverage(std::vector<ImagePoint>{{INFINITY, 0}}, 100, 100));
}
void rig_contract() {
    auto d = fixture();
    StereoModel model;
    model.R_right_from_left = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    model.T_right_from_left = {-120, 0, 0};
    auto rig = derive_rig_geometry(model, d.cameras[0], d.cameras[1], {{"rig"}, "rig"});
    CHECK(rig);
    near(rig->baseline_mm, 120);
    near(rig->relative_rotation_angle_rad, 0);
    const std::array<double, 16> expected{1, 0, 0, -60, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1};
    CHECK(rig->T_rig_from_left.matrix == expected);
    CHECK((rig->T_rig_from_left.apply({0, 0, 0}) == std::array<double, 3>{-60, 0, 0}));
    CHECK((rig->T_rig_from_right.apply({0, 0, 0}) == std::array<double, 3>{60, 0, 0}));
    const auto composed = spatial::compose(rig->T_rig_from_right, rig->T_right_from_left);
    CHECK(composed.matrix == expected);
    auto reject = [&](StereoModel bad) {
        CHECK(!derive_rig_geometry(bad, d.cameras[0], d.cameras[1], {{"rig"}, "rig"}));
    };
    auto bad = model;
    bad.T_right_from_left = {0, 0, 0};
    reject(bad);
    bad.T_right_from_left = {-1e-16, 0, 0};
    reject(bad);
    bad = model;
    bad.T_right_from_left[0] = INFINITY;
    reject(bad);
    bad = model;
    bad.R_right_from_left[0] = 2;
    reject(bad);
    bad = model;
    bad.R_right_from_left = {1, 0, 0, 0, -1, 0, 0, 0, -1};
    reject(bad); // Opposing optical forwards.
    bad = model;
    bad.T_right_from_left = {0, 0, -120};
    reject(bad); // Forward parallel to baseline.
    bad = model;
    bad.R_right_from_left = {-1, 0, 0, 0, 1, 0, 0, 0, 1};
    reject(bad); // Reflection.
    CHECK(!derive_rig_geometry(model, d.cameras[0], d.cameras[1], d.cameras[0].optical_frame));
}
CameraCalibrationSolution solution_fixture() {
    const auto d = fixture();
    CameraCalibrationSolution s;
    s.target = d.target;
    s.camera = d.cameras[0];
    s.config.heldout_per_camera = 2;
    s.partition = *partition_mono_samples(d, s.camera, s.config);
    s.training_model = {CameraModel::pinhole_brown5, 900, 910, 500, 400, -.1, .01, .001, -.001, .001};
    s.final_model = s.training_model;
    auto add = [&](MonoStageEvidence &stage, const MonoSample &sample, bool held) {
        const auto *r = solve_detail::record(d, sample.key.frame, s.camera);
        MonoViewEvidence v;
        v.key = sample.key;
        v.residuals_px.assign(sample.point_count, 0);
        v.residuals = *summarize_residuals(v.residuals_px);
        v.coverage = *summarize_coverage(r->observation->image_points_px, 1000, 800);
        if (held) {
            v.pose.ippe_solution_index = 1;
            v.pose.ippe_solution_rms_px = {1, 0};
        }
        stage.views.push_back(v);
    };
    for (const auto &sample : s.partition.samples) {
        add(s.final_fit, sample, false);
        add(sample.disposition == SolveSampleDisposition::training ? s.training_fit : s.heldout_validation,
            sample, sample.disposition == SolveSampleDisposition::held_out);
    }
    for (auto *stage : {&s.training_fit, &s.heldout_validation, &s.final_fit}) {
        std::vector<double> all;
        CoverageEvidence c{INFINITY, INFINITY, -INFINITY, -INFINITY, 0};
        for (const auto &v : stage->views) {
            all.insert(all.end(), v.residuals_px.begin(), v.residuals_px.end());
            c.min_x = std::min(c.min_x, v.coverage.min_x);
            c.min_y = std::min(c.min_y, v.coverage.min_y);
            c.max_x = std::max(c.max_x, v.coverage.max_x);
            c.max_y = std::max(c.max_y, v.coverage.max_y);
        }
        c.bounding_box_area = (c.max_x - c.min_x) * (c.max_y - c.min_y);
        stage->coverage = c;
        stage->residuals = *summarize_residuals(all);
        if (stage != &s.heldout_validation)
            stage->opencv_solver_rms_px = 0;
    }
    CHECK(validate_camera_solution(s, d));
    return s;
}
void stereo_result_validation() {
    const auto dataset = fixture();
    auto left = solution_fixture(), right = left;
    right.camera = dataset.cameras[1];
    for (auto &sample : right.partition.samples) {
        sample.key.camera_role = right.camera.role;
        sample.key.camera_id = right.camera.camera_id;
    }
    for (auto *stage : {&right.training_fit, &right.heldout_validation, &right.final_fit})
        for (auto &view : stage->views) {
            view.key.camera_role = right.camera.role;
            view.key.camera_id = right.camera.camera_id;
        }
    CHECK(validate_camera_solution(right, dataset));
    StereoCalibrationSolution solution;
    solution.target = dataset.target;
    solution.left_camera = left.camera;
    solution.right_camera = right.camera;
    solution.left_final_intrinsics = left.final_model;
    solution.right_final_intrinsics = right.final_model;
    solution.config.left_role = "left";
    solution.config.right_role = "right";
    solution.config.heldout_pairs = 2;
    solution.config.rig_frame = {{"rig"}, "rig"};
    solution.partition = *partition_stereo_samples(dataset, solution.config);
    solution.training_model.R_right_from_left = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    solution.training_model.T_right_from_left = {-120, 0, 0};
    solution.training_model.E = {0, 0, 0, 0, 0, 120, 0, -120, 0};
    solution.training_model.F = {0, 0, 0, 0, 0, 1, 0, -1, 0};
    solution.final_model = solution.training_model;
    for (const auto &sample : solution.partition.samples) {
        StereoPairEvidence pair;
        pair.key = sample.key;
        pair.common_point_ids = sample.common_point_ids;
        pair.symmetric_epipolar_residuals_px.assign(sample.common_point_ids.size(), 0);
        pair.residuals = *summarize_residuals(pair.symmetric_epipolar_residuals_px);
        solution.final_fit.pairs.push_back(pair);
        (sample.disposition == SolveSampleDisposition::training ? solution.training_fit
                                                                : solution.heldout_validation)
            .pairs.push_back(pair);
    }
    for (auto *stage : {&solution.training_fit, &solution.heldout_validation, &solution.final_fit}) {
        std::vector<double> all;
        for (const auto &pair : stage->pairs)
            all.insert(all.end(), pair.symmetric_epipolar_residuals_px.begin(),
                       pair.symmetric_epipolar_residuals_px.end());
        stage->residuals = *summarize_residuals(all);
        if (stage != &solution.heldout_validation)
            stage->opencv_solver_rms_px = 0;
    }
    solution.rig =
        *derive_rig_geometry(solution.final_model, left.camera, right.camera, solution.config.rig_frame);
    CHECK(validate_stereo_solution(solution, dataset, left, right));
    auto reject = [&](auto change) {
        auto bad = solution;
        change(bad);
        CHECK(!validate_stereo_solution(bad));
    };
    reject([](auto &s) { s.final_model.F.fill(0); });
    reject([](auto &s) { s.partition.samples[1].key = s.partition.samples[0].key; });
    reject([](auto &s) {
        s.partition.samples[1].left_selection_rank = s.partition.samples[0].left_selection_rank;
    });
    reject([](auto &s) { s.partition.samples[0].common_point_ids[0] = 99; });
    reject([](auto &s) {
        s.partition.samples[0].disposition = SolveSampleDisposition::ineligible_missing_detection;
    });
    reject([](auto &s) { s.rig.T_rig_from_left.source.name = "changed"; });
    reject([](auto &s) { s.rig.relative_rotation_angle_rad = -1; });
    reject([](auto &s) {
        s.rig.T_rig_from_left.covariance = std::array<double, 36>{};
        (*s.rig.T_rig_from_left.covariance)[0] = INFINITY;
    });
    auto changed = solution;
    changed.left_final_intrinsics.fx += 1;
    CHECK(!validate_stereo_solution(changed, dataset, left, right));
}
void result_validation() {
    auto good = solution_fixture();
    auto reject = [&](auto mutate) {
        auto s = good;
        mutate(s);
        CHECK(!validate_camera_solution(s));
    };
    reject([](auto &s) { s.camera.camera_id.value.clear(); });
    reject([](auto &s) { s.camera.optical_frame.name.clear(); });
    reject([](auto &s) { s.final_model.fx = 0; });
    reject([](auto &s) { s.final_model.k3 = INFINITY; });
    reject([](auto &s) { s.final_model.model = static_cast<CameraModel>(99); });
    reject([](auto &s) { s.training_fit.views.pop_back(); });
    reject([](auto &s) { s.heldout_validation.views[0].key = s.training_fit.views[0].key; });
    reject([](auto &s) { s.final_fit.views[0] = s.final_fit.views[1]; });
    reject([](auto &s) { ++s.training_fit.residuals.point_count; });
    reject([](auto &s) { s.final_fit.views[0].residuals_px[0] = -1; });
    reject([](auto &s) { s.heldout_validation.views[0].pose.ippe_solution_index = 0; });
    reject([](auto &s) { s.training_fit.views[0].pose.ippe_solution_index = 0; });
    reject([](auto &s) { s.heldout_validation.opencv_solver_rms_px = 0; });
    reject([](auto &s) { s.partition.samples[0].disposition = SolveSampleDisposition::held_out; });
    reject([](auto &s) { s.partition.samples[0].key = s.partition.samples[1].key; });
    reject([](auto &s) { s.final_fit.coverage.bounding_box_area = 2; });
    reject([](auto &s) { s.config.heldout_per_camera = 0; });
    auto wrong = good;
    wrong.target.identity.revision++;
    CHECK(!validate_camera_solution(wrong, fixture()));
    MonoSolveConfig mono;
    mono.heldout_per_camera = 1;
    CHECK(validate_mono_solve_config(mono));
    mono.solver_policy_version = 2;
    CHECK(!validate_mono_solve_config(mono));
    StereoSolveConfig stereo;
    stereo.left_role = "left";
    stereo.right_role = "right";
    stereo.heldout_pairs = 1;
    stereo.rig_frame = {{"rig"}, "rig"};
    CHECK(validate_stereo_solve_config(stereo));
    stereo.left_role = stereo.right_role;
    CHECK(!validate_stereo_solve_config(stereo));
}
int main() {
    try {
        split_contract();
        metric_contract();
        rig_contract();
        result_validation();
        stereo_result_validation();
        std::cout << "Pure calibration solve contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
