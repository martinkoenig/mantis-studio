#include <climits>
#include <mantis/calibration_solver_opencv.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>

namespace mantis::calibration {
namespace {
// Flags explicitly select the ordinary five-coefficient pinhole model. All nine
// Brown5 parameters are estimated; rational/thin-prism/tilted models are disabled.
constexpr int mono_flags = 0;
constexpr int stereo_flags = cv::CALIB_FIX_INTRINSIC;
cv::TermCriteria criteria() {
    return {cv::TermCriteria::COUNT | cv::TermCriteria::EPS, solve_max_iterations, solve_epsilon};
}
template <class Function> auto boundary(Function function) -> decltype(function()) {
    try {
        return function();
    } catch (const cv::Exception &exception) {
        return solve_detail::error("OpenCV " CV_VERSION " calibration solver failure: " +
                                       std::string(exception.what()),
                                   Status::corrupt);
    }
}
cv::Mat camera_matrix(const PinholeBrown5 &m) {
    return (cv::Mat_<double>(3, 3) << m.fx, 0, m.cx, 0, m.fy, m.cy, 0, 0, 1);
}
cv::Mat distortion(const PinholeBrown5 &m) {
    return (cv::Mat_<double>(5, 1) << m.k1, m.k2, m.p1, m.p2, m.k3);
}
Result<PinholeBrown5> extract_model(const cv::Mat &k, const cv::Mat &d) {
    if (k.type() != CV_64F || k.rows != 3 || k.cols != 3 || d.type() != CV_64F || d.total() != 5)
        return solve_detail::error("OpenCV result does not conform to explicit pinhole-brown5 representation",
                                   Status::corrupt);
    const auto coefficients = d.reshape(1, 5);
    PinholeBrown5 model{CameraModel::pinhole_brown5, k.at<double>(0, 0),         k.at<double>(1, 1),
                        k.at<double>(0, 2),          k.at<double>(1, 2),         coefficients.at<double>(0),
                        coefficients.at<double>(1),  coefficients.at<double>(2), coefficients.at<double>(3),
                        coefficients.at<double>(4)};
    auto valid = validate_camera_model(model);
    if (!valid)
        return solve_detail::error(valid.error().message, Status::corrupt);
    return model;
}
Result<void> dimensions(const DatasetCamera &camera) {
    if (camera.image_width > INT_MAX || camera.image_height > INT_MAX)
        return solve_detail::error("Image dimensions exceed OpenCV signed representation");
    return {};
}
Result<float> float_point(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        return solve_detail::error("Calibration point exceeds OpenCV float representation");
    const auto converted = static_cast<float>(value);
    if (value != 0 && converted == 0)
        return solve_detail::error("Calibration point underflows OpenCV float representation");
    return converted;
}
struct FloatView {
    std::vector<cv::Point3f> object;
    std::vector<cv::Point2f> image;
};
Result<FloatView> float_view(const TargetObservation &o) {
    if (o.point_ids.size() > INT_MAX)
        return solve_detail::error("Correspondence count exceeds OpenCV signed representation");
    FloatView view;
    std::vector<TargetPoint> converted;
    for (size_t i = 0; i < o.point_ids.size(); ++i) {
        const auto &p = o.object_points_mm[i];
        const auto &q = o.image_points_px[i];
        auto x = float_point(p.x_mm), y = float_point(p.y_mm), z = float_point(p.z_mm),
             u = float_point(q.x_px), v = float_point(q.y_px);
        for (const auto *component : {&x, &y, &z, &u, &v})
            if (!*component)
                return std::unexpected(component->error());
        view.object.emplace_back(*x, *y, *z);
        view.image.emplace_back(*u, *v);
        converted.push_back({*x, *y, *z});
    }
    if (solver_eligibility(converted) != SolveSampleDisposition::training)
        return solve_detail::error(
            "Solver geometry degenerates in the required OpenCV float point representation",
            Status::incompatible);
    return view;
}
std::vector<cv::Point3d> object_double(const TargetObservation &o) {
    std::vector<cv::Point3d> points;
    for (const auto &p : o.object_points_mm)
        points.emplace_back(p.x_mm, p.y_mm, p.z_mm);
    return points;
}
std::vector<cv::Point2d> image_double(const TargetObservation &o) {
    std::vector<cv::Point2d> points;
    for (const auto &p : o.image_points_px)
        points.emplace_back(p.x_px, p.y_px);
    return points;
}
std::array<double, 3> vector3(const cv::Mat &matrix) {
    cv::Mat converted;
    matrix.reshape(1, 3).convertTo(converted, CV_64F);
    return {converted.at<double>(0), converted.at<double>(1), converted.at<double>(2)};
}
Result<MonoViewEvidence> reprojection(const TargetObservation &o, const PinholeBrown5 &model,
                                      const cv::Mat &r, const cv::Mat &t) {
    std::vector<cv::Point2d> projected;
    cv::projectPoints(object_double(o), r, t, camera_matrix(model), distortion(model), projected);
    MonoViewEvidence evidence;
    evidence.key = solve_detail::key(o);
    evidence.pose.rotation_vector_rad = vector3(r);
    evidence.pose.translation_mm = vector3(t);
    for (const auto &vector : {evidence.pose.rotation_vector_rad, evidence.pose.translation_mm})
        for (double value : vector)
            if (!std::isfinite(value))
                return solve_detail::error("OpenCV target pose must be finite", Status::corrupt);
    if (projected.size() != o.point_ids.size())
        return solve_detail::error("OpenCV projection count disagrees with correspondences", Status::corrupt);
    for (size_t i = 0; i < projected.size(); ++i)
        evidence.residuals_px.push_back(std::hypot(o.image_points_px[i].x_px - projected[i].x,
                                                   o.image_points_px[i].y_px - projected[i].y));
    auto residuals = summarize_residuals(evidence.residuals_px);
    if (!residuals)
        return std::unexpected(residuals.error());
    evidence.residuals = *residuals;
    auto coverage = summarize_coverage(o.image_points_px, o.image_width, o.image_height);
    if (!coverage)
        return std::unexpected(coverage.error());
    evidence.coverage = *coverage;
    return evidence;
}
Result<void> aggregate(MonoStageEvidence &stage) {
    std::vector<double> all;
    CoverageEvidence coverage{INFINITY, INFINITY, -INFINITY, -INFINITY, 0};
    for (const auto &view : stage.views) {
        all.insert(all.end(), view.residuals_px.begin(), view.residuals_px.end());
        coverage.min_x = std::min(coverage.min_x, view.coverage.min_x);
        coverage.min_y = std::min(coverage.min_y, view.coverage.min_y);
        coverage.max_x = std::max(coverage.max_x, view.coverage.max_x);
        coverage.max_y = std::max(coverage.max_y, view.coverage.max_y);
    }
    coverage.bounding_box_area = (coverage.max_x - coverage.min_x) * (coverage.max_y - coverage.min_y);
    auto summary = summarize_residuals(all);
    if (!summary)
        return std::unexpected(summary.error());
    stage.residuals = *summary;
    stage.coverage = coverage;
    return solve_detail::coverage_valid(coverage);
}
Result<void> aggregate(StereoStageEvidence &stage) {
    std::vector<double> all;
    for (const auto &pair : stage.pairs)
        all.insert(all.end(), pair.symmetric_epipolar_residuals_px.begin(),
                   pair.symmetric_epipolar_residuals_px.end());
    auto summary = summarize_residuals(all);
    if (!summary)
        return std::unexpected(summary.error());
    stage.residuals = *summary;
    return {};
}
struct MonoFit {
    PinholeBrown5 model;
    MonoStageEvidence evidence;
};
Result<MonoFit> fit_mono(const DatasetCamera &camera,
                         const std::vector<const TargetObservation *> &observations) {
    if (observations.empty() || observations.size() > INT_MAX)
        return solve_detail::error("Mono fit requires a representable nonempty view population");
    std::vector<std::vector<cv::Point3f>> objects;
    std::vector<std::vector<cv::Point2f>> images;
    uint64_t total{};
    for (const auto *o : observations) {
        auto view = float_view(*o);
        if (!view)
            return std::unexpected(view.error());
        total += view->object.size();
        if (total > INT_MAX)
            return solve_detail::error("Calibration point population exceeds OpenCV signed representation");
        objects.push_back(std::move(view->object));
        images.push_back(std::move(view->image));
    }
    cv::Mat k = cv::Mat::eye(3, 3, CV_64F), d = cv::Mat::zeros(5, 1, CV_64F);
    std::vector<cv::Mat> rotations, translations;
    const double rms = cv::calibrateCamera(
        objects, images,
        cv::Size(static_cast<int>(camera.image_width), static_cast<int>(camera.image_height)), k, d,
        rotations, translations, mono_flags, criteria());
    auto model = extract_model(k, d);
    if (!model)
        return std::unexpected(model.error());
    if (!solve_detail::nonnegative(rms) || rotations.size() != observations.size() ||
        translations.size() != observations.size())
        return solve_detail::error("OpenCV mono fit returned invalid pose or RMS evidence", Status::corrupt);
    MonoFit fit;
    fit.model = *model;
    fit.evidence.opencv_solver_rms_px = rms;
    for (size_t i = 0; i < observations.size(); ++i) {
        auto view = reprojection(*observations[i], fit.model, rotations[i], translations[i]);
        if (!view)
            return std::unexpected(view.error());
        fit.evidence.views.push_back(std::move(*view));
    }
    auto valid = aggregate(fit.evidence);
    if (!valid)
        return std::unexpected(valid.error());
    return fit;
}
Result<MonoViewEvidence> heldout(const TargetObservation &o, const PinholeBrown5 &model) {
    auto valid = validate_target_observation(o);
    if (!valid)
        return std::unexpected(valid.error());
    valid = validate_camera_model(model);
    if (!valid)
        return std::unexpected(valid.error());
    if (solver_eligibility(o.object_points_mm) != SolveSampleDisposition::training)
        return solve_detail::error("Held-out IPPE pose requires four non-collinear planar correspondences",
                                   Status::incompatible);
    std::vector<cv::Mat> rotations, translations;
    cv::solvePnPGeneric(object_double(o), image_double(o), camera_matrix(model), distortion(model), rotations,
                        translations, false, cv::SOLVEPNP_IPPE);
    if (rotations.empty() || rotations.size() != translations.size() || rotations.size() > UINT32_MAX)
        return solve_detail::error("IPPE did not return valid planar pose solutions", Status::corrupt);
    std::optional<MonoViewEvidence> chosen;
    std::vector<double> rms;
    uint32_t chosen_index{};
    for (size_t i = 0; i < rotations.size(); ++i) {
        auto evidence = reprojection(o, model, rotations[i], translations[i]);
        if (!evidence)
            return std::unexpected(evidence.error());
        rms.push_back(evidence->residuals.rms_px);
        if (!chosen || evidence->residuals.rms_px < chosen->residuals.rms_px) {
            chosen = std::move(*evidence);
            chosen_index = static_cast<uint32_t>(i);
        }
    }
    chosen->pose.ippe_solution_index = chosen_index;
    chosen->pose.ippe_solution_rms_px = std::move(rms);
    return *chosen;
}
Result<CameraCalibrationSolution> mono(const CalibrationDataset &dataset, std::string_view role,
                                       const MonoSolveConfig &config) {
    const DatasetCamera *camera{};
    for (const auto &c : dataset.cameras)
        if (c.role == role)
            camera = &c;
    if (!camera)
        return solve_detail::error("Mono role must resolve to a dataset camera");
    auto partition = partition_mono_samples(dataset, *camera, config);
    if (!partition)
        return std::unexpected(partition.error());
    auto valid = dimensions(*camera);
    if (!valid)
        return std::unexpected(valid.error());
    std::vector<const TargetObservation *> train, held, all;
    for (const auto &sample : partition->samples)
        if (solve_detail::eligible(sample.disposition)) {
            const auto *o = &*solve_detail::record(dataset, sample.key.frame, *camera)->observation;
            all.push_back(o);
            (sample.disposition == SolveSampleDisposition::training ? train : held).push_back(o);
        }
    auto training = fit_mono(*camera, train);
    if (!training)
        return std::unexpected(training.error());
    CameraCalibrationSolution solution;
    solution.target = dataset.target;
    solution.camera = *camera;
    solution.config = config;
    solution.partition = *partition;
    solution.training_model = training->model;
    solution.training_fit = std::move(training->evidence);
    for (const auto *o : held) {
        auto view = heldout(*o, solution.training_model);
        if (!view)
            return std::unexpected(view.error());
        solution.heldout_validation.views.push_back(std::move(*view));
    }
    valid = aggregate(solution.heldout_validation);
    if (!valid)
        return std::unexpected(valid.error());
    // Held-out evidence is frozen before those samples enter the final refit.
    auto final = fit_mono(*camera, all);
    if (!final)
        return std::unexpected(final.error());
    solution.final_model = final->model;
    solution.final_fit = std::move(final->evidence);
    valid = validate_camera_solution(solution, dataset);
    if (!valid)
        return std::unexpected(valid.error());
    return solution;
}
std::array<double, 9> matrix9(const cv::Mat &m) {
    std::array<double, 9> a{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            a[size_t(i * 3 + j)] = m.at<double>(i, j);
    return a;
}
Result<StereoPairEvidence> epipolar(const FrameSetKey &key, std::span<const CommonTargetPoint> points,
                                    const StereoModel &model, const PinholeBrown5 &left,
                                    const PinholeBrown5 &right) {
    auto valid = validate_camera_model(left);
    if (!valid)
        return std::unexpected(valid.error());
    valid = validate_camera_model(right);
    if (!valid)
        return std::unexpected(valid.error());
    valid = solve_detail::stereo_model_valid(model);
    if (!valid)
        return std::unexpected(valid.error());
    if (key.raw_capture_id.value.empty() || points.empty() || points.size() > INT_MAX)
        return solve_detail::error(
            "Epipolar evidence requires source identity and representable correspondences");
    std::vector<cv::Point2d> l, r;
    std::set<uint32_t> ids;
    for (const auto &point : points) {
        if (!ids.insert(point.point_id).second)
            return solve_detail::error("Epipolar correspondence IDs must be unique");
        for (double value :
             {point.left_px.x_px, point.left_px.y_px, point.right_px.x_px, point.right_px.y_px})
            if (!std::isfinite(value))
                return solve_detail::error("Epipolar image coordinates must be finite");
        l.emplace_back(point.left_px.x_px, point.left_px.y_px);
        r.emplace_back(point.right_px.x_px, point.right_px.y_px);
    }
    std::vector<cv::Point2d> ul, ur;
    const auto kl = camera_matrix(left), kr = camera_matrix(right);
    cv::undistortPoints(l, ul, kl, distortion(left), cv::noArray(), kl, criteria());
    cv::undistortPoints(r, ur, kr, distortion(right), cv::noArray(), kr, criteria());
    const auto &f = model.F;
    StereoPairEvidence evidence;
    evidence.key = key;
    double scale{};
    for (double value : f)
        scale = std::max(scale, std::abs(value));
    // Normalize F for numerical line tests; distances are invariant to F's scale.
    std::array<double, 9> normalized{};
    for (size_t i = 0; i < 9; ++i)
        normalized[i] = f[i] / scale;
    for (size_t i = 0; i < points.size(); ++i) {
        const std::array<double, 3> xl{ul[i].x, ul[i].y, 1}, xr{ur[i].x, ur[i].y, 1};
        std::array<double, 3> lr{}, ll{};
        for (size_t a = 0; a < 3; ++a)
            for (size_t b = 0; b < 3; ++b) {
                lr[a] += normalized[a * 3 + b] * xl[b];
                ll[a] += normalized[b * 3 + a] * xr[b];
            }
        const double nr = std::hypot(lr[0], lr[1]), nl = std::hypot(ll[0], ll[1]);
        // Only floating-point degeneracy, scaled by the homogeneous input norm.
        if (!std::isfinite(nr) || !std::isfinite(nl) ||
            nr <= solve_geometry_epsilon * solve_detail::norm(xl) ||
            nl <= solve_geometry_epsilon * solve_detail::norm(xr))
            return solve_detail::error("Numerically zero-length epipolar line", Status::corrupt);
        const double numerator = std::abs(solve_detail::dot(xr, lr));
        const double distance = std::hypot(numerator / nl, numerator / nr) / std::sqrt(2.0);
        evidence.common_point_ids.push_back(points[i].point_id);
        evidence.symmetric_epipolar_residuals_px.push_back(distance);
    }
    auto summary = summarize_residuals(evidence.symmetric_epipolar_residuals_px);
    if (!summary)
        return std::unexpected(summary.error());
    evidence.residuals = *summary;
    return evidence;
}
struct StereoInput {
    FrameSetKey key;
    std::vector<CommonTargetPoint> common;
};
Result<std::vector<StereoInput>> stereo_inputs(const CalibrationDataset &dataset,
                                               const StereoPartition &partition, const DatasetCamera &left,
                                               const DatasetCamera &right,
                                               std::optional<SolveSampleDisposition> disposition) {
    std::vector<StereoInput> inputs;
    for (const auto &sample : partition.samples)
        if (solve_detail::eligible(sample.disposition) &&
            (!disposition || sample.disposition == *disposition)) {
            const auto *l = solve_detail::record(dataset, sample.key, left),
                       *r = solve_detail::record(dataset, sample.key, right);
            auto common = intersect_observations(*l->observation, *r->observation);
            if (!common)
                return std::unexpected(common.error());
            inputs.push_back({sample.key, std::move(*common)});
        }
    return inputs;
}
struct StereoFit {
    StereoModel model;
    StereoStageEvidence evidence;
};
Result<StereoFit> fit_stereo(const DatasetCamera &camera, const std::vector<StereoInput> &inputs,
                             const PinholeBrown5 &left, const PinholeBrown5 &right) {
    if (inputs.empty() || inputs.size() > INT_MAX)
        return solve_detail::error("Stereo fit requires a representable nonempty pair population");
    std::vector<std::vector<cv::Point3f>> objects;
    std::vector<std::vector<cv::Point2f>> lefts, rights;
    uint64_t total{};
    for (const auto &input : inputs) {
        TargetObservation l, r;
        for (const auto &p : input.common) {
            l.point_ids.push_back(p.point_id);
            r.point_ids.push_back(p.point_id);
            l.object_points_mm.push_back(p.object_mm);
            r.object_points_mm.push_back(p.object_mm);
            l.image_points_px.push_back(p.left_px);
            r.image_points_px.push_back(p.right_px);
        }
        auto lv = float_view(l), rv = float_view(r);
        if (!lv)
            return std::unexpected(lv.error());
        if (!rv)
            return std::unexpected(rv.error());
        total += lv->object.size();
        if (total > INT_MAX)
            return solve_detail::error("Stereo point population exceeds OpenCV signed representation");
        objects.push_back(std::move(lv->object));
        lefts.push_back(std::move(lv->image));
        rights.push_back(std::move(rv->image));
    }
    auto kl = camera_matrix(left), kr = camera_matrix(right), dl = distortion(left), dr = distortion(right);
    cv::Mat rotation, translation, e, f;
    const double rms = cv::stereoCalibrate(
        objects, lefts, rights, kl, dl, kr, dr,
        cv::Size(static_cast<int>(camera.image_width), static_cast<int>(camera.image_height)), rotation,
        translation, e, f, stereo_flags, criteria());
    auto after_left = extract_model(kl, dl), after_right = extract_model(kr, dr);
    if (!after_left || !after_right || *after_left != left || *after_right != right)
        return solve_detail::error("CALIB_FIX_INTRINSIC changed a final mono camera model", Status::corrupt);
    if (!solve_detail::nonnegative(rms))
        return solve_detail::error("Stereo solver RMS must be finite and nonnegative", Status::corrupt);
    StereoFit fit;
    fit.model.R_right_from_left = matrix9(rotation);
    fit.model.T_right_from_left = vector3(translation);
    fit.model.E = matrix9(e);
    fit.model.F = matrix9(f);
    fit.evidence.opencv_solver_rms_px = rms;
    auto valid = solve_detail::stereo_model_valid(fit.model);
    if (!valid)
        return std::unexpected(valid.error());
    for (const auto &input : inputs) {
        auto evidence = epipolar(input.key, input.common, fit.model, left, right);
        if (!evidence)
            return std::unexpected(evidence.error());
        fit.evidence.pairs.push_back(std::move(*evidence));
    }
    valid = aggregate(fit.evidence);
    if (!valid)
        return std::unexpected(valid.error());
    return fit;
}
Result<StereoCalibrationSolution> stereo(const CalibrationDataset &dataset,
                                         const CameraCalibrationSolution &left,
                                         const CameraCalibrationSolution &right,
                                         const StereoSolveConfig &config) {
    // Enforce target policy before fitting or treating a Checkerboard as a detector failure.
    auto partition = partition_stereo_samples(dataset, config);
    if (!partition)
        return std::unexpected(partition.error());
    if (left.camera.role != config.left_role || right.camera.role != config.right_role)
        return solve_detail::error(
            "Stereo input mono solutions must match the exact configured left/right roles");
    auto valid = validate_camera_solution(left, dataset);
    if (!valid)
        return std::unexpected(valid.error());
    valid = validate_camera_solution(right, dataset);
    if (!valid)
        return std::unexpected(valid.error());
    valid = dimensions(left.camera);
    if (!valid)
        return std::unexpected(valid.error());
    valid = dimensions(right.camera);
    if (!valid)
        return std::unexpected(valid.error());
    auto train =
             stereo_inputs(dataset, *partition, left.camera, right.camera, SolveSampleDisposition::training),
         held =
             stereo_inputs(dataset, *partition, left.camera, right.camera, SolveSampleDisposition::held_out),
         all = stereo_inputs(dataset, *partition, left.camera, right.camera, std::nullopt);
    for (const auto *input : {&train, &held, &all})
        if (!*input)
            return std::unexpected(input->error());
    auto training = fit_stereo(left.camera, *train, left.final_model, right.final_model);
    if (!training)
        return std::unexpected(training.error());
    StereoCalibrationSolution solution;
    solution.target = dataset.target;
    solution.left_camera = left.camera;
    solution.right_camera = right.camera;
    solution.config = config;
    solution.left_final_intrinsics = left.final_model;
    solution.right_final_intrinsics = right.final_model;
    solution.partition = *partition;
    solution.training_model = training->model;
    solution.training_fit = std::move(training->evidence);
    for (const auto &input : *held) {
        auto pair =
            epipolar(input.key, input.common, solution.training_model, left.final_model, right.final_model);
        if (!pair)
            return std::unexpected(pair.error());
        solution.heldout_validation.pairs.push_back(std::move(*pair));
    }
    valid = aggregate(solution.heldout_validation);
    if (!valid)
        return std::unexpected(valid.error());
    auto final = fit_stereo(left.camera, *all, left.final_model, right.final_model);
    if (!final)
        return std::unexpected(final.error());
    solution.final_model = final->model;
    solution.final_fit = std::move(final->evidence);
    auto rig = derive_rig_geometry(solution.final_model, left.camera, right.camera, config.rig_frame);
    if (!rig)
        return std::unexpected(rig.error());
    solution.rig = *rig;
    valid = validate_stereo_solution(solution, dataset, left, right);
    if (!valid)
        return std::unexpected(valid.error());
    return solution;
}
} // namespace
std::string_view solver_opencv_version() {
    return CV_VERSION;
}
Result<CameraCalibrationSolution> solve_camera_intrinsics(const CalibrationDataset &dataset,
                                                          std::string_view role,
                                                          const MonoSolveConfig &config) {
    return boundary([&] { return mono(dataset, role, config); });
}
Result<std::vector<CameraCalibrationSolution>> solve_camera_intrinsics(const CalibrationDataset &dataset,
                                                                       const MonoSolveConfig &config) {
    return boundary([&]() -> Result<std::vector<CameraCalibrationSolution>> {
        auto valid = validate_calibration_dataset(dataset);
        if (!valid)
            return std::unexpected(valid.error());
        valid = validate_mono_solve_config(config);
        if (!valid)
            return std::unexpected(valid.error());
        std::vector<CameraCalibrationSolution> solutions;
        for (const auto &camera : dataset.cameras) {
            auto solution = mono(dataset, camera.role, config);
            if (!solution)
                return std::unexpected(solution.error());
            solutions.push_back(std::move(*solution));
        }
        return solutions;
    });
}
Result<StereoCalibrationSolution> solve_stereo_rig(const CalibrationDataset &dataset,
                                                   const CameraCalibrationSolution &left,
                                                   const CameraCalibrationSolution &right,
                                                   const StereoSolveConfig &config) {
    return boundary([&] { return stereo(dataset, left, right, config); });
}
Result<MonoViewEvidence> evaluate_heldout_target(const TargetObservation &o, const PinholeBrown5 &model) {
    return boundary([&] { return heldout(o, model); });
}
Result<StereoPairEvidence> evaluate_epipolar_pair(const FrameSetKey &key,
                                                  std::span<const CommonTargetPoint> points,
                                                  const StereoModel &model, const PinholeBrown5 &left,
                                                  const PinholeBrown5 &right) {
    return boundary([&] { return epipolar(key, points, model, left, right); });
}
} // namespace mantis::calibration
