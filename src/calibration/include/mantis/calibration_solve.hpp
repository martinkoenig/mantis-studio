#pragma once
#include <mantis/calibration_dataset.hpp>
#include <span>
#include <tuple>

namespace mantis::calibration {
// Policy v1: optimizer termination, not a calibration quality threshold.
inline constexpr int solve_max_iterations = 100;
inline constexpr double solve_epsilon = 1e-12;
inline constexpr double solve_geometry_epsilon = 256 * std::numeric_limits<double>::epsilon();
enum class CameraModel { pinhole_brown5 };
struct PinholeBrown5 {
    CameraModel model{CameraModel::pinhole_brown5};
    double fx{}, fy{}, cx{}, cy{}, k1{}, k2{}, p1{}, p2{}, k3{};
    bool operator==(const PinholeBrown5 &) const = default;
};
struct MonoSolveConfig {
    uint32_t schema_version{1}, solver_policy_version{1}, split_policy_version{1};
    uint32_t heldout_per_camera{}; // Explicit caller choice.
};
struct StereoSolveConfig {
    uint32_t schema_version{1}, solver_policy_version{1}, split_policy_version{1};
    std::string left_role, right_role;
    uint32_t heldout_pairs{};
    spatial::CoordinateFrame rig_frame;
};
enum class SolveSampleDisposition {
    training,
    held_out,
    ineligible_insufficient_points,
    ineligible_degenerate_geometry,
    ineligible_missing_detection
};
struct MonoSample {
    ObservationKey key;
    uint32_t selection_rank{};
    uint64_t point_count{};
    SolveSampleDisposition disposition{};
    bool operator==(const MonoSample &) const = default;
};
struct SolvePartition {
    std::vector<MonoSample> samples;
};
struct StereoSample {
    FrameSetKey key;
    std::optional<uint32_t> left_selection_rank, right_selection_rank;
    std::vector<uint32_t> common_point_ids;
    SolveSampleDisposition disposition{};
    bool operator==(const StereoSample &) const = default;
};
struct StereoPartition {
    std::vector<StereoSample> samples;
};
struct ResidualSummary {
    uint64_t point_count{};
    double rms_px{}, mean_px{}, median_px{}, p95_px{}, max_px{};
    bool operator==(const ResidualSummary &) const = default;
};
struct CoverageEvidence {
    double min_x{}, min_y{}, max_x{}, max_y{}, bounding_box_area{};
    bool operator==(const CoverageEvidence &) const = default;
};
struct TargetViewPose {
    std::array<double, 3> rotation_vector_rad{}, translation_mm{};
    std::optional<uint32_t> ippe_solution_index;
    std::vector<double> ippe_solution_rms_px; // All recomputed candidates, in OpenCV return order.
};
struct MonoViewEvidence {
    ObservationKey key;
    TargetViewPose pose;
    std::vector<double> residuals_px;
    ResidualSummary residuals;
    CoverageEvidence coverage;
};
struct MonoStageEvidence {
    std::vector<MonoViewEvidence> views;
    ResidualSummary residuals;
    CoverageEvidence coverage;
    std::optional<double> opencv_solver_rms_px;
};
struct CameraCalibrationSolution {
    CalibrationTarget target;
    DatasetCamera camera;
    MonoSolveConfig config;
    SolvePartition partition;
    PinholeBrown5 training_model;
    MonoStageEvidence training_fit, heldout_validation;
    PinholeBrown5 final_model;
    MonoStageEvidence final_fit;
};
struct StereoModel {
    // X_right = R_right_from_left * X_left + T_right_from_left; translation in mm.
    std::array<double, 9> R_right_from_left{}, E{}, F{};
    std::array<double, 3> T_right_from_left{};
};
struct StereoPairEvidence {
    FrameSetKey key;
    std::vector<uint32_t> common_point_ids;
    std::vector<double> symmetric_epipolar_residuals_px;
    ResidualSummary residuals;
};
struct StereoStageEvidence {
    std::vector<StereoPairEvidence> pairs;
    ResidualSummary residuals;
    std::optional<double> opencv_solver_rms_px;
};
struct RigGeometry {
    spatial::Transform T_right_from_left, T_rig_from_left, T_rig_from_right;
    double baseline_mm{}, relative_rotation_angle_rad{};
};
struct StereoCalibrationSolution {
    CalibrationTarget target;
    DatasetCamera left_camera, right_camera;
    StereoSolveConfig config;
    PinholeBrown5 left_final_intrinsics, right_final_intrinsics;
    StereoPartition partition;
    StereoModel training_model;
    StereoStageEvidence training_fit, heldout_validation;
    StereoModel final_model;
    StereoStageEvidence final_fit;
    RigGeometry rig;
};
namespace solve_detail {
inline std::unexpected<Error> error(std::string message, Status code = Status::invalid_argument) {
    return std::unexpected(Error{code, std::move(message), "calibration"});
}
inline bool same_frame(const spatial::CoordinateFrame &a, const spatial::CoordinateFrame &b) {
    return a.id == b.id && a.name == b.name;
}
inline bool same_camera(const DatasetCamera &a, const DatasetCamera &b) {
    return a.role == b.role && a.camera_id == b.camera_id && a.image_width == b.image_width &&
           a.image_height == b.image_height && same_frame(a.optical_frame, b.optical_frame);
}
inline bool same_target(const CalibrationTarget &a, const CalibrationTarget &b) {
    if (a.identity.id != b.identity.id || a.identity.revision != b.identity.revision ||
        a.type() != b.type() || a.grid.squares_x != b.grid.squares_x ||
        a.grid.squares_y != b.grid.squares_y ||
        a.grid.nominal_square_size_mm != b.grid.nominal_square_size_mm ||
        a.measurement.active_width_mm != b.measurement.active_width_mm ||
        a.measurement.active_height_mm != b.measurement.active_height_mm ||
        a.measurement.provenance.has_value() != b.measurement.provenance.has_value())
        return false;
    if (a.type() == TargetType::charuco) {
        const auto &x = std::get<CharucoDefinition>(a.pattern), &y = std::get<CharucoDefinition>(b.pattern);
        if (x.dictionary != y.dictionary || x.nominal_marker_size_mm != y.nominal_marker_size_mm ||
            x.pattern_layout != y.pattern_layout)
            return false;
    }
    if (a.measurement.provenance) {
        const auto &x = *a.measurement.provenance, &y = *b.measurement.provenance;
        if (x.width_uncertainty_mm != y.width_uncertainty_mm ||
            x.height_uncertainty_mm != y.height_uncertainty_mm || x.instrument != y.instrument ||
            x.note != y.note)
            return false;
    }
    return true;
}
inline bool frame_valid(const spatial::CoordinateFrame &frame) {
    return !frame.id.value.empty() && !frame.name.empty();
}
inline bool eligible(SolveSampleDisposition d) {
    return d == SolveSampleDisposition::training || d == SolveSampleDisposition::held_out;
}
inline Result<void> versions(uint32_t schema, uint32_t solver, uint32_t split) {
    if (schema != 1 || solver != 1 || split != 1)
        return error("Unsupported calibration solve schema, solver or split policy");
    return {};
}
inline Result<void> camera_valid(const DatasetCamera &camera) {
    if (camera.role.empty() || camera.camera_id.value.empty() || !camera.image_width ||
        !camera.image_height || !frame_valid(camera.optical_frame))
        return error("Calibration camera requires identity, role, image dimensions and optical frame");
    return {};
}
inline bool nonnegative(double value) {
    return std::isfinite(value) && value >= 0;
}
inline bool close(double a, double b, double scale = 1) {
    return std::isfinite(a) && std::isfinite(b) &&
           std::abs(a - b) <= 16 * solve_geometry_epsilon * std::max({1.0, scale, std::abs(a), std::abs(b)});
}
inline ObservationKey key(const TargetObservation &o) {
    return {{o.source.raw_capture_id, o.source.frameset_sequence}, o.source.camera_role, o.source.camera_id};
}
inline const DatasetObservationRecord *record(const CalibrationDataset &dataset, const FrameSetKey &frame,
                                              const DatasetCamera &camera) {
    const ObservationKey key{frame, camera.role, camera.camera_id};
    const auto it =
        std::lower_bound(dataset.records.begin(), dataset.records.end(), key,
                         [](const auto &record, const auto &value) { return record.key < value; });
    return it != dataset.records.end() && it->key == key ? &*it : nullptr;
}
inline auto stereo_priority(const StereoSample &sample) {
    const auto l = sample.left_selection_rank.value_or(UINT32_MAX),
               r = sample.right_selection_rank.value_or(UINT32_MAX);
    return std::tuple{std::min(l, r), std::max(l, r), sample.key};
}
} // namespace solve_detail
inline Result<void> validate_camera_model(const PinholeBrown5 &model) {
    if (model.model != CameraModel::pinhole_brown5 || model.fx <= 0 || model.fy <= 0)
        return solve_detail::error("Camera model must be pinhole-brown5 with positive focal lengths");
    for (double value :
         {model.fx, model.fy, model.cx, model.cy, model.k1, model.k2, model.p1, model.p2, model.k3})
        if (!std::isfinite(value))
            return solve_detail::error("Camera model coefficients must be finite");
    return {};
}
inline Result<void> validate_mono_solve_config(const MonoSolveConfig &config) {
    auto versions = solve_detail::versions(config.schema_version, config.solver_policy_version,
                                           config.split_policy_version);
    if (!versions)
        return versions;
    if (!config.heldout_per_camera)
        return solve_detail::error("Mono held-out count must be positive");
    return {};
}
inline Result<void> validate_stereo_solve_config(const StereoSolveConfig &config) {
    auto versions = solve_detail::versions(config.schema_version, config.solver_policy_version,
                                           config.split_policy_version);
    if (!versions)
        return versions;
    if (!config.heldout_pairs || config.left_role.empty() || config.right_role.empty() ||
        config.left_role == config.right_role || !solve_detail::frame_valid(config.rig_frame))
        return solve_detail::error(
            "Stereo solve requires distinct roles, positive held-out count and rig frame");
    return {};
}
// Exact v1 positions. H is uint32, so (k+1)*remainder fits uint64.
inline Result<std::vector<uint64_t>> heldout_positions(uint64_t n, uint32_t h) {
    if (!h || uint64_t(h) >= n)
        return solve_detail::error("Held-out count must be positive and smaller than eligible sample count",
                                   Status::incompatible);
    if (n == UINT64_MAX)
        return solve_detail::error("Eligible sample count exceeds split arithmetic representation");
    std::vector<uint64_t> positions;
    const uint64_t denominator = uint64_t(h) + 1, quotient = (n + 1) / denominator,
                   remainder = (n + 1) % denominator;
    for (uint64_t k = 1; k <= h; ++k)
        positions.push_back(k * quotient + k * remainder / denominator - 1);
    return positions;
}
// Numerical collinearity test on scale-normalized planar differences. No coverage/area threshold.
inline SolveSampleDisposition solver_eligibility(std::span<const TargetPoint> points) {
    if (points.size() < 4)
        return SolveSampleDisposition::ineligible_insufficient_points;
    double scale{};
    for (const auto &p : points) {
        if (!std::isfinite(p.x_mm) || !std::isfinite(p.y_mm) || p.z_mm != 0)
            return SolveSampleDisposition::ineligible_degenerate_geometry;
        scale = std::max({scale, std::abs(p.x_mm), std::abs(p.y_mm)});
    }
    if (!std::isfinite(scale) || scale == 0)
        return SolveSampleDisposition::ineligible_degenerate_geometry;
    const double x0 = points[0].x_mm / scale, y0 = points[0].y_mm / scale;
    double dx{}, dy{}, farthest{};
    for (const auto &p : points) {
        const double x = p.x_mm / scale - x0, y = p.y_mm / scale - y0;
        const double distance = std::hypot(x, y);
        if (distance > farthest) {
            farthest = distance;
            dx = x;
            dy = y;
        }
    }
    if (!farthest)
        return SolveSampleDisposition::ineligible_degenerate_geometry;
    for (const auto &p : points)
        if (std::abs((dx / farthest) * (p.y_mm / scale - y0) / farthest -
                     (dy / farthest) * (p.x_mm / scale - x0) / farthest) > solve_geometry_epsilon)
            return SolveSampleDisposition::training; // Eligible; split supplies final disposition.
    return SolveSampleDisposition::ineligible_degenerate_geometry;
}
inline Result<SolvePartition> partition_mono_samples(const CalibrationDataset &dataset,
                                                     const DatasetCamera &camera,
                                                     const MonoSolveConfig &config) {
    auto valid = validate_calibration_dataset(dataset);
    if (!valid)
        return std::unexpected(valid.error());
    valid = validate_mono_solve_config(config);
    if (!valid)
        return std::unexpected(valid.error());
    if (std::none_of(dataset.cameras.begin(), dataset.cameras.end(),
                     [&](const auto &c) { return solve_detail::same_camera(c, camera); }))
        return solve_detail::error("Mono camera must match a dataset camera");
    SolvePartition partition;
    for (const auto &record : dataset.records)
        if (record.key.camera_role == camera.role && record.selection_rank)
            partition.samples.push_back({record.key, *record.selection_rank,
                                         record.observation->point_ids.size(),
                                         solver_eligibility(record.observation->object_points_mm)});
    std::sort(partition.samples.begin(), partition.samples.end(), [](const auto &a, const auto &b) {
        return std::tuple{a.selection_rank, a.key} < std::tuple{b.selection_rank, b.key};
    });
    uint64_t n{};
    for (const auto &s : partition.samples)
        if (solve_detail::eligible(s.disposition))
            ++n;
    auto positions = heldout_positions(n, config.heldout_per_camera);
    if (!positions)
        return std::unexpected(positions.error());
    uint64_t position{};
    for (auto &s : partition.samples)
        if (solve_detail::eligible(s.disposition)) {
            if (std::binary_search(positions->begin(), positions->end(), position))
                s.disposition = SolveSampleDisposition::held_out;
            ++position;
        }
    return partition;
}
inline Result<StereoPartition> partition_stereo_samples(const CalibrationDataset &dataset,
                                                        const StereoSolveConfig &config) {
    auto valid = validate_calibration_dataset(dataset);
    if (!valid)
        return std::unexpected(valid.error());
    valid = validate_stereo_solve_config(config);
    if (!valid)
        return std::unexpected(valid.error());
    if (dataset.target.type() != TargetType::charuco)
        return solve_detail::error(
            "Plain Checkerboard detector-grid IDs do not establish physical stereo correspondence; stereo "
            "and rig solving require ChArUco physical-board IDs",
            Status::incompatible);
    const DatasetCamera *left{}, *right{};
    for (const auto &camera : dataset.cameras) {
        if (camera.role == config.left_role)
            left = &camera;
        if (camera.role == config.right_role)
            right = &camera;
    }
    if (!left || !right)
        return solve_detail::error("Stereo roles must resolve to dataset cameras");
    std::set<FrameSetKey> frames;
    for (const auto &record : dataset.records)
        if (record.selection_rank &&
            (record.key.camera_role == left->role || record.key.camera_role == right->role))
            frames.insert(record.key.frame);
    StereoPartition partition;
    for (const auto &frame : frames) {
        const auto *l = solve_detail::record(dataset, frame, *left),
                   *r = solve_detail::record(dataset, frame, *right);
        if (!l || !r)
            return solve_detail::error("Stereo candidate is missing a dataset camera record");
        StereoSample sample{frame,
                            l->selection_rank,
                            r->selection_rank,
                            {},
                            SolveSampleDisposition::ineligible_missing_detection};
        if (l->observation && r->observation) {
            if (l->observation->point_id_semantics() != PointIdSemantics::physical_board ||
                r->observation->point_id_semantics() != PointIdSemantics::physical_board)
                return solve_detail::error("Stereo observations require physical-board point identities",
                                           Status::incompatible);
            auto common = intersect_observations(*l->observation, *r->observation);
            if (!common)
                return std::unexpected(common.error());
            std::vector<TargetPoint> points;
            for (const auto &p : *common) {
                sample.common_point_ids.push_back(p.point_id);
                points.push_back(p.object_mm);
            }
            sample.disposition = solver_eligibility(points);
        }
        partition.samples.push_back(std::move(sample));
    }
    std::sort(partition.samples.begin(), partition.samples.end(), [](const auto &a, const auto &b) {
        return solve_detail::stereo_priority(a) < solve_detail::stereo_priority(b);
    });
    uint64_t n{};
    for (const auto &s : partition.samples)
        if (solve_detail::eligible(s.disposition))
            ++n;
    auto positions = heldout_positions(n, config.heldout_pairs);
    if (!positions)
        return std::unexpected(positions.error());
    uint64_t position{};
    for (auto &s : partition.samples)
        if (solve_detail::eligible(s.disposition)) {
            if (std::binary_search(positions->begin(), positions->end(), position))
                s.disposition = SolveSampleDisposition::held_out;
            ++position;
        }
    return partition;
}
inline Result<ResidualSummary> summarize_residuals(std::span<const double> values) {
    if (values.empty())
        return solve_detail::error("Residual evidence must not be empty");
    double max{};
    for (double value : values) {
        if (!solve_detail::nonnegative(value))
            return solve_detail::error("Residuals must be finite and nonnegative");
        max = std::max(max, value);
    }
    // Scaled moments avoid overflowing squared finite residuals.
    double sum{}, squares{};
    if (max)
        for (double value : values) {
            const double scaled = value / max;
            sum += scaled;
            squares += scaled * scaled;
        }
    std::vector<double> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    const size_t n = sorted.size();
    // ceil(95*n/100), without overflowing an integer product.
    const size_t rank95 = (n / 100) * 95 + ((n % 100) * 95 + 99) / 100;
    const double median = n % 2 ? sorted[n / 2] : sorted[n / 2 - 1] / 2 + sorted[n / 2] / 2;
    ResidualSummary result{
        n, max * std::sqrt(squares / double(n)), max * (sum / double(n)), median, sorted[rank95 - 1], max};
    if (!solve_detail::nonnegative(result.rms_px) || !solve_detail::nonnegative(result.mean_px))
        return solve_detail::error("Residual aggregation exceeds numerical representation");
    return result;
}
inline Result<CoverageEvidence> summarize_coverage(std::span<const ImagePoint> points, uint32_t width,
                                                   uint32_t height) {
    if (points.empty() || !width || !height)
        return solve_detail::error("Coverage requires points and nonzero image dimensions");
    CoverageEvidence result{INFINITY, INFINITY, -INFINITY, -INFINITY, 0};
    for (const auto &p : points) {
        if (!std::isfinite(p.x_px) || !std::isfinite(p.y_px))
            return solve_detail::error("Coverage points must be finite");
        const double x = p.x_px / width, y = p.y_px / height;
        result.min_x = std::min(result.min_x, x);
        result.max_x = std::max(result.max_x, x);
        result.min_y = std::min(result.min_y, y);
        result.max_y = std::max(result.max_y, y);
    }
    result.bounding_box_area = (result.max_x - result.min_x) * (result.max_y - result.min_y);
    if (!solve_detail::nonnegative(result.bounding_box_area))
        return solve_detail::error("Coverage exceeds numerical representation");
    return result;
}
namespace solve_detail {
using Vec3 = std::array<double, 3>;
inline double dot(const Vec3 &a, const Vec3 &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline double norm(const Vec3 &v) {
    return std::hypot(v[0], v[1], v[2]);
}
inline Result<Vec3> normalize(Vec3 v, double scale = 1) {
    const double length = norm(v);
    if (!std::isfinite(length) || length <= solve_geometry_epsilon * std::max(1.0, scale))
        return error("Stereo geometry cannot define a numerically nondegenerate rig axis",
                     Status::incompatible);
    for (auto &x : v)
        x /= length;
    return v;
}
inline Result<void> rotation_valid(const std::array<double, 9> &r) {
    for (double value : r)
        if (!std::isfinite(value))
            return error("Stereo rotation must be finite");
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 3; ++j) {
            double product{};
            for (size_t k = 0; k < 3; ++k)
                product += r[k * 3 + i] * r[k * 3 + j];
            if (!std::isfinite(product) || std::abs(product - double(i == j)) > 16 * solve_geometry_epsilon)
                return error("Stereo rotation must be orthonormal");
        }
    const double determinant = dot({r[0], r[1], r[2]}, cross({r[3], r[4], r[5]}, {r[6], r[7], r[8]}));
    if (!std::isfinite(determinant) || std::abs(determinant - 1) > 16 * solve_geometry_epsilon)
        return error("Stereo rotation must be proper and right-handed");
    return {};
}
} // namespace solve_detail
inline Result<RigGeometry> derive_rig_geometry(const StereoModel &model, const DatasetCamera &left,
                                               const DatasetCamera &right,
                                               const spatial::CoordinateFrame &rig_frame) {
    auto valid = solve_detail::camera_valid(left);
    if (!valid)
        return std::unexpected(valid.error());
    valid = solve_detail::camera_valid(right);
    if (!valid)
        return std::unexpected(valid.error());
    if (left.role == right.role || left.camera_id == right.camera_id ||
        left.optical_frame.id == right.optical_frame.id || !solve_detail::frame_valid(rig_frame) ||
        rig_frame.id == left.optical_frame.id || rig_frame.id == right.optical_frame.id)
        return solve_detail::error("Rig requires distinct cameras, optical frames and rig frame");
    const auto &r = model.R_right_from_left;
    const auto &t = model.T_right_from_left;
    valid = solve_detail::rotation_valid(r);
    if (!valid)
        return std::unexpected(valid.error());
    for (double value : t)
        if (!std::isfinite(value))
            return solve_detail::error("Stereo translation must be finite");
    solve_detail::Vec3 center{}, forward{r[6], r[7], r[8] + 1};
    for (size_t i = 0; i < 3; ++i)
        for (size_t k = 0; k < 3; ++k)
            center[i] -= r[k * 3 + i] * t[k];
    const double baseline = solve_detail::norm(center);
    auto x = solve_detail::normalize(center, solve_detail::norm(t));
    if (!x)
        return std::unexpected(x.error());
    auto bisector = solve_detail::normalize(forward, 2);
    if (!bisector)
        return std::unexpected(bisector.error());
    const double parallel = solve_detail::dot(*bisector, *x);
    for (size_t i = 0; i < 3; ++i)
        forward[i] = (*bisector)[i] - parallel * (*x)[i];
    auto y = solve_detail::normalize(forward);
    if (!y)
        return std::unexpected(y.error());
    auto z = solve_detail::normalize(solve_detail::cross(*x, *y));
    if (!z)
        return std::unexpected(z.error());
    y = solve_detail::normalize(solve_detail::cross(*z, *x));
    if (!y)
        return std::unexpected(y.error());
    std::array<solve_detail::Vec3, 3> axes{*x, *y, *z};
    std::array<double, 9> basis{};
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 3; ++j)
            basis[i * 3 + j] = axes[i][j];
    valid = solve_detail::rotation_valid(basis);
    if (!valid)
        return std::unexpected(valid.error());
    RigGeometry result;
    result.baseline_mm = baseline;
    result.relative_rotation_angle_rad = std::acos(std::clamp((r[0] + r[4] + r[8] - 1) / 2, -1.0, 1.0));
    result.T_right_from_left.source = left.optical_frame;
    result.T_right_from_left.target = right.optical_frame;
    result.T_rig_from_left.source = left.optical_frame;
    result.T_rig_from_left.target = rig_frame;
    result.T_rig_from_right.source = right.optical_frame;
    result.T_rig_from_right.target = rig_frame;
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            result.T_right_from_left.matrix[i * 4 + j] = r[i * 3 + j];
            result.T_rig_from_left.matrix[i * 4 + j] = axes[i][j];
            double entry{};
            for (size_t k = 0; k < 3; ++k)
                entry += axes[i][k] * r[j * 3 + k];
            result.T_rig_from_right.matrix[i * 4 + j] = entry;
        }
        result.T_right_from_left.matrix[i * 4 + 3] = t[i];
        result.T_rig_from_left.matrix[i * 4 + 3] = -solve_detail::dot(axes[i], center) / 2;
        result.T_rig_from_right.matrix[i * 4 + 3] = solve_detail::dot(axes[i], center) / 2;
    }
    for (const auto *transform :
         {&result.T_right_from_left, &result.T_rig_from_left, &result.T_rig_from_right})
        for (double value : transform->matrix)
            if (!std::isfinite(value))
                return solve_detail::error("Rig transform exceeds numerical representation");
    return result;
}
namespace solve_detail {
inline Result<void> coverage_valid(const CoverageEvidence &c) {
    for (double v : {c.min_x, c.min_y, c.max_x, c.max_y, c.bounding_box_area})
        if (!std::isfinite(v))
            return error("Coverage evidence must be finite");
    if (c.min_x > c.max_x || c.min_y > c.max_y || c.bounding_box_area < 0 ||
        !close(c.bounding_box_area, (c.max_x - c.min_x) * (c.max_y - c.min_y)))
        return error("Inconsistent coverage evidence");
    return {};
}
inline Result<void> residuals_valid(const std::vector<double> &values, const ResidualSummary &summary) {
    auto expected = summarize_residuals(values);
    if (!expected)
        return std::unexpected(expected.error());
    if (summary != *expected)
        return error("Residual counts or statistics disagree with evidence");
    return {};
}
inline bool valid_solver_rms(const std::optional<double> &rms, bool fitted) {
    return fitted ? rms && nonnegative(*rms) : !rms;
}
inline Result<void> mono_stage_valid(const MonoStageEvidence &stage,
                                     const std::map<ObservationKey, uint64_t> &expected, bool fitted) {
    if (expected.empty() || stage.views.size() != expected.size() ||
        !valid_solver_rms(stage.opencv_solver_rms_px, fitted))
        return error("Mono stage requires nonempty matching sample evidence");
    std::set<ObservationKey> seen;
    std::vector<double> all;
    CoverageEvidence coverage{INFINITY, INFINITY, -INFINITY, -INFINITY, 0};
    for (const auto &view : stage.views) {
        auto it = expected.find(view.key);
        if (it == expected.end() || !seen.insert(view.key).second || view.residuals_px.size() != it->second)
            return error("Mono stage sample keys or residual counts disagree with partition");
        auto valid = residuals_valid(view.residuals_px, view.residuals);
        if (!valid)
            return valid;
        valid = coverage_valid(view.coverage);
        if (!valid)
            return valid;
        for (double v : view.pose.rotation_vector_rad)
            if (!std::isfinite(v))
                return error("Target pose must be finite");
        for (double v : view.pose.translation_mm)
            if (!std::isfinite(v))
                return error("Target pose must be finite");
        if (fitted) {
            if (view.pose.ippe_solution_index || !view.pose.ippe_solution_rms_px.empty())
                return error("Fitted target poses must not claim held-out IPPE evidence");
        } else {
            if (!view.pose.ippe_solution_index ||
                *view.pose.ippe_solution_index >= view.pose.ippe_solution_rms_px.size())
                return error("Held-out pose requires IPPE solution evidence");
            for (double rms : view.pose.ippe_solution_rms_px)
                if (!nonnegative(rms))
                    return error("IPPE candidate residuals must be finite and nonnegative");
            const auto first = std::min_element(view.pose.ippe_solution_rms_px.begin(),
                                                view.pose.ippe_solution_rms_px.end());
            if (size_t(first - view.pose.ippe_solution_rms_px.begin()) != *view.pose.ippe_solution_index ||
                !close(*first, view.residuals.rms_px))
                return error(
                    "Held-out pose must minimize recomputed IPPE RMS with returned-order tie breaking");
        }
        all.insert(all.end(), view.residuals_px.begin(), view.residuals_px.end());
        coverage.min_x = std::min(coverage.min_x, view.coverage.min_x);
        coverage.min_y = std::min(coverage.min_y, view.coverage.min_y);
        coverage.max_x = std::max(coverage.max_x, view.coverage.max_x);
        coverage.max_y = std::max(coverage.max_y, view.coverage.max_y);
    }
    coverage.bounding_box_area = (coverage.max_x - coverage.min_x) * (coverage.max_y - coverage.min_y);
    if (coverage != stage.coverage)
        return error("Mono stage coverage disagrees with per-view evidence");
    return residuals_valid(all, stage.residuals);
}
inline Result<void> stereo_stage_valid(const StereoStageEvidence &stage,
                                       const std::map<FrameSetKey, std::vector<uint32_t>> &expected,
                                       bool fitted) {
    if (expected.empty() || stage.pairs.size() != expected.size() ||
        !valid_solver_rms(stage.opencv_solver_rms_px, fitted))
        return error("Stereo stage requires nonempty matching pair evidence");
    std::set<FrameSetKey> seen;
    std::vector<double> all;
    for (const auto &pair : stage.pairs) {
        auto it = expected.find(pair.key);
        if (it == expected.end() || !seen.insert(pair.key).second || pair.common_point_ids != it->second ||
            pair.symmetric_epipolar_residuals_px.size() != it->second.size())
            return error("Stereo stage pair keys, physical IDs or counts disagree with partition");
        auto valid = residuals_valid(pair.symmetric_epipolar_residuals_px, pair.residuals);
        if (!valid)
            return valid;
        all.insert(all.end(), pair.symmetric_epipolar_residuals_px.begin(),
                   pair.symmetric_epipolar_residuals_px.end());
    }
    return residuals_valid(all, stage.residuals);
}
inline Result<void> stereo_model_valid(const StereoModel &model) {
    auto valid = rotation_valid(model.R_right_from_left);
    if (!valid)
        return valid;
    for (double v : model.T_right_from_left)
        if (!std::isfinite(v))
            return error("Stereo translation must be finite");
    for (const auto &matrix : {model.E, model.F}) {
        double magnitude{};
        for (double v : matrix) {
            if (!std::isfinite(v))
                return error("Stereo E/F must be finite");
            magnitude = std::max(magnitude, std::abs(v));
        }
        if (!magnitude)
            return error("Stereo E/F must be nonzero");
    }
    return {};
}
} // namespace solve_detail
inline Result<void> validate_camera_solution(const CameraCalibrationSolution &solution) {
    auto valid = validate_target(solution.target);
    if (!valid)
        return valid;
    if (solution.target.identity.id.value.empty())
        return solve_detail::error("Camera solution requires target identity");
    valid = solve_detail::camera_valid(solution.camera);
    if (!valid)
        return valid;
    valid = validate_mono_solve_config(solution.config);
    if (!valid)
        return valid;
    valid = validate_camera_model(solution.training_model);
    if (!valid)
        return valid;
    valid = validate_camera_model(solution.final_model);
    if (!valid)
        return valid;
    std::map<ObservationKey, uint64_t> train, held, final;
    std::set<ObservationKey> seen;
    uint64_t eligible_count{};
    for (size_t i = 0; i < solution.partition.samples.size(); ++i) {
        const auto &sample = solution.partition.samples[i];
        if (sample.point_count >
                uint64_t(solution.target.grid.squares_x - 1) * (solution.target.grid.squares_y - 1) ||
            sample.selection_rank != i || sample.key.camera_role != solution.camera.role ||
            sample.key.camera_id != solution.camera.camera_id ||
            sample.key.frame.raw_capture_id.value.empty() || !seen.insert(sample.key).second)
            return solve_detail::error(
                "Mono partition must preserve unique camera source keys in contiguous selection order");
        if (solve_detail::eligible(sample.disposition)) {
            if (sample.point_count < 4)
                return solve_detail::error("Eligible mono sample requires at least four points");
            ++eligible_count;
            final.emplace(sample.key, sample.point_count);
            (sample.disposition == SolveSampleDisposition::training ? train : held)
                .emplace(sample.key, sample.point_count);
        } else if (sample.disposition == SolveSampleDisposition::ineligible_insufficient_points) {
            if (sample.point_count >= 4)
                return solve_detail::error("Insufficient-point mono disposition disagrees with point count");
        } else if (sample.disposition != SolveSampleDisposition::ineligible_degenerate_geometry ||
                   sample.point_count < 4)
            return solve_detail::error("Invalid mono sample disposition");
    }
    auto positions = heldout_positions(eligible_count, solution.config.heldout_per_camera);
    if (!positions)
        return std::unexpected(positions.error());
    uint64_t position{};
    for (const auto &sample : solution.partition.samples)
        if (solve_detail::eligible(sample.disposition)) {
            if ((sample.disposition == SolveSampleDisposition::held_out) !=
                std::binary_search(positions->begin(), positions->end(), position++))
                return solve_detail::error("Mono partition violates split policy v1");
        }
    valid = solve_detail::mono_stage_valid(solution.training_fit, train, true);
    if (!valid)
        return valid;
    valid = solve_detail::mono_stage_valid(solution.heldout_validation, held, false);
    if (!valid)
        return valid;
    return solve_detail::mono_stage_valid(solution.final_fit, final, true);
}
inline Result<void> validate_camera_solution(const CameraCalibrationSolution &solution,
                                             const CalibrationDataset &dataset) {
    auto valid = validate_camera_solution(solution);
    if (!valid)
        return valid;
    if (!solve_detail::same_target(solution.target, dataset.target))
        return solve_detail::error("Camera solution target does not match dataset");
    auto expected = partition_mono_samples(dataset, solution.camera, solution.config);
    if (!expected)
        return std::unexpected(expected.error());
    if (expected->samples != solution.partition.samples)
        return solve_detail::error("Camera solution partition does not match selected dataset observations");
    return {};
}
inline Result<void> validate_stereo_solution(const StereoCalibrationSolution &solution) {
    auto valid = validate_target(solution.target);
    if (!valid)
        return valid;
    if (solution.target.identity.id.value.empty() || solution.target.type() != TargetType::charuco)
        return solve_detail::error(
            "Stereo solution requires a ChArUco target with physical-board identities");
    valid = validate_stereo_solve_config(solution.config);
    if (!valid)
        return valid;
    if (solution.left_camera.role != solution.config.left_role ||
        solution.right_camera.role != solution.config.right_role)
        return solve_detail::error("Stereo cameras must match explicitly configured roles");
    valid = validate_camera_model(solution.left_final_intrinsics);
    if (!valid)
        return valid;
    valid = validate_camera_model(solution.right_final_intrinsics);
    if (!valid)
        return valid;
    valid = solve_detail::stereo_model_valid(solution.training_model);
    if (!valid)
        return valid;
    valid = solve_detail::stereo_model_valid(solution.final_model);
    if (!valid)
        return valid;
    std::map<FrameSetKey, std::vector<uint32_t>> train, held, final;
    std::set<FrameSetKey> seen;
    std::set<uint32_t> left_ranks, right_ranks;
    uint64_t eligible_count{};
    for (size_t i = 0; i < solution.partition.samples.size(); ++i) {
        const auto &s = solution.partition.samples[i];
        if (s.key.raw_capture_id.value.empty() || !seen.insert(s.key).second ||
            (!s.left_selection_rank && !s.right_selection_rank) ||
            (i && solve_detail::stereo_priority(solution.partition.samples[i - 1]) >=
                      solve_detail::stereo_priority(s)))
            return solve_detail::error(
                "Stereo candidates must be unique and canonically ordered from the selected union");
        for (auto [rank, ranks] :
             {std::pair{s.left_selection_rank, &left_ranks}, std::pair{s.right_selection_rank, &right_ranks}})
            if (rank && !ranks->insert(*rank).second)
                return solve_detail::error("Stereo candidates contain duplicate camera selection ranks");
        std::vector<TargetPoint> common_geometry;
        const uint32_t columns = solution.target.grid.squares_x - 1;
        const uint64_t total = uint64_t(columns) * (solution.target.grid.squares_y - 1);
        for (auto id : s.common_point_ids) {
            if (id >= total)
                return solve_detail::error("Stereo physical point ID is outside the target grid");
            auto point = scale_target_point(
                solution.target, {double(id % columns + 1) * solution.target.grid.nominal_square_size_mm,
                                  double(id / columns + 1) * solution.target.grid.nominal_square_size_mm, 0});
            if (!point)
                return std::unexpected(point.error());
            common_geometry.push_back(*point);
        }
        if (s.disposition != SolveSampleDisposition::ineligible_missing_detection &&
            (solve_detail::eligible(s.disposition)
                 ? solver_eligibility(common_geometry) != SolveSampleDisposition::training
                 : solver_eligibility(common_geometry) != s.disposition))
            return solve_detail::error("Stereo eligibility disagrees with physical common-point geometry");
        if (!std::is_sorted(s.common_point_ids.begin(), s.common_point_ids.end()) ||
            std::adjacent_find(s.common_point_ids.begin(), s.common_point_ids.end()) !=
                s.common_point_ids.end())
            return solve_detail::error("Stereo common physical IDs must be sorted and unique");
        if (solve_detail::eligible(s.disposition)) {
            if (s.common_point_ids.size() < 4)
                return solve_detail::error("Eligible stereo pair requires four common points");
            ++eligible_count;
            final.emplace(s.key, s.common_point_ids);
            (s.disposition == SolveSampleDisposition::training ? train : held)
                .emplace(s.key, s.common_point_ids);
        } else if (s.disposition == SolveSampleDisposition::ineligible_missing_detection) {
            if (!s.common_point_ids.empty())
                return solve_detail::error("Missing detection cannot have common physical IDs");
        } else if (s.disposition == SolveSampleDisposition::ineligible_insufficient_points) {
            if (s.common_point_ids.size() >= 4)
                return solve_detail::error("Insufficient common-point disposition disagrees with count");
        } else if (s.disposition != SolveSampleDisposition::ineligible_degenerate_geometry ||
                   s.common_point_ids.size() < 4)
            return solve_detail::error("Invalid stereo sample disposition");
    }
    for (const auto *ranks : {&left_ranks, &right_ranks}) {
        uint64_t expected_rank{};
        for (auto rank : *ranks)
            if (rank != expected_rank++)
                return solve_detail::error("Stereo selected ranks must be contiguous per camera");
    }
    auto positions = heldout_positions(eligible_count, solution.config.heldout_pairs);
    if (!positions)
        return std::unexpected(positions.error());
    uint64_t position{};
    for (const auto &s : solution.partition.samples)
        if (solve_detail::eligible(s.disposition)) {
            if ((s.disposition == SolveSampleDisposition::held_out) !=
                std::binary_search(positions->begin(), positions->end(), position++))
                return solve_detail::error("Stereo partition violates split policy v1");
        }
    valid = solve_detail::stereo_stage_valid(solution.training_fit, train, true);
    if (!valid)
        return valid;
    valid = solve_detail::stereo_stage_valid(solution.heldout_validation, held, false);
    if (!valid)
        return valid;
    valid = solve_detail::stereo_stage_valid(solution.final_fit, final, true);
    if (!valid)
        return valid;
    auto rig = derive_rig_geometry(solution.final_model, solution.left_camera, solution.right_camera,
                                   solution.config.rig_frame);
    if (!rig)
        return std::unexpected(rig.error());
    if (!solve_detail::nonnegative(solution.rig.baseline_mm) || solution.rig.baseline_mm <= 0 ||
        !solve_detail::close(rig->baseline_mm, solution.rig.baseline_mm) ||
        !solve_detail::close(rig->relative_rotation_angle_rad, solution.rig.relative_rotation_angle_rad))
        return solve_detail::error("Rig geometry evidence disagrees with final stereo model");
    for (auto [actual, expected] : {std::pair{&solution.rig.T_right_from_left, &rig->T_right_from_left},
                                    std::pair{&solution.rig.T_rig_from_left, &rig->T_rig_from_left},
                                    std::pair{&solution.rig.T_rig_from_right, &rig->T_rig_from_right}}) {
        if (!solve_detail::same_frame(actual->source, expected->source) ||
            !solve_detail::same_frame(actual->target, expected->target))
            return solve_detail::error("Rig transform frame identities disagree with cameras/configuration");
        if (actual->covariance)
            for (double value : *actual->covariance)
                if (!std::isfinite(value))
                    return solve_detail::error("Rig covariance, when supplied, must be finite");
        for (size_t i = 0; i < 16; ++i)
            if (!solve_detail::close(actual->matrix[i], expected->matrix[i], rig->baseline_mm))
                return solve_detail::error("Rig transform disagrees with calibrated geometry");
    }
    const auto composition = spatial::compose(solution.rig.T_rig_from_right, solution.rig.T_right_from_left);
    for (size_t i = 0; i < 16; ++i)
        if (!solve_detail::close(composition.matrix[i], solution.rig.T_rig_from_left.matrix[i],
                                 rig->baseline_mm))
            return solve_detail::error("Rig transforms do not compose");
    return {};
}
inline Result<void> validate_stereo_solution(const StereoCalibrationSolution &solution,
                                             const CalibrationDataset &dataset,
                                             const CameraCalibrationSolution &left,
                                             const CameraCalibrationSolution &right) {
    auto valid = validate_stereo_solution(solution);
    if (!valid)
        return valid;
    valid = validate_camera_solution(left, dataset);
    if (!valid)
        return valid;
    valid = validate_camera_solution(right, dataset);
    if (!valid)
        return valid;
    if (!solve_detail::same_target(solution.target, dataset.target) ||
        !solve_detail::same_camera(solution.left_camera, left.camera) ||
        !solve_detail::same_camera(solution.right_camera, right.camera) ||
        solution.left_final_intrinsics != left.final_model ||
        solution.right_final_intrinsics != right.final_model)
        return solve_detail::error(
            "Stereo solution must preserve exact target, cameras and final mono intrinsics");
    auto expected = partition_stereo_samples(dataset, solution.config);
    if (!expected)
        return std::unexpected(expected.error());
    if (expected->samples != solution.partition.samples)
        return solve_detail::error("Stereo solution partition does not match the selected dataset union");
    return {};
}
} // namespace mantis::calibration
