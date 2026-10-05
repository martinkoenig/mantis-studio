#pragma once
#include <mantis/calibration_observation.hpp>
#include <mantis/spatial.hpp>
#include <algorithm>
#include <limits>
#include <numeric>

namespace mantis::calibration {
struct FrameSetKey {
    Id raw_capture_id;
    uint64_t frameset_sequence{};
    auto operator<=>(const FrameSetKey &) const = default;
};
struct ObservationKey {
    FrameSetKey frame;
    std::string camera_role;
    Id camera_id;
    auto operator<=>(const ObservationKey &) const = default;
};
struct DatasetCamera {
    std::string role;
    Id camera_id;
    uint32_t image_width{}, image_height{};
    spatial::CoordinateFrame optical_frame; // Exact recorded id/name representation.
};
struct DatasetAnalysisConfig {
    uint32_t schema_version{1};
    std::vector<std::string> camera_roles;
    uint32_t selection_policy_version{1};
    uint32_t max_selected_per_camera{}; // Required caller choice; no recommended budget.
};
inline constexpr int64_t diversity_descriptor_scale = 1'000'000; // Selection policy v1.
using FixedDiversityDescriptor = std::array<int64_t, 8>;
struct DiversityDescriptor {
    double centroid_x{}, centroid_y{}, extent_x{}, extent_y{};
    double variance_x{}, variance_y{}, covariance_xy{}, visible_fraction{};
    std::array<double, 8> components() const {
        return {centroid_x, centroid_y, extent_x, extent_y, variance_x, variance_y, covariance_xy, visible_fraction};
    }
    bool operator==(const DiversityDescriptor &) const = default;
};
enum class DetectionOutcome { detected, no_target };
struct DatasetObservationRecord {
    ObservationKey key;
    DetectionOutcome outcome{DetectionOutcome::no_target};
    std::optional<TargetObservation> observation;
    std::optional<DiversityDescriptor> diversity;
    std::optional<uint32_t> selection_rank;
};
struct CalibrationDataset {
    CalibrationTarget target;
    DatasetAnalysisConfig config;
    std::vector<Id> raw_capture_ids;
    std::vector<DatasetCamera> cameras;
    std::vector<DatasetObservationRecord> records; // Metadata/correspondences only; no image ownership.
};
inline Result<void> validate_analysis_config(const DatasetAnalysisConfig &config) {
    if (config.schema_version != 1 || config.selection_policy_version != 1)
        return detail::target_error("Unsupported calibration dataset schema or selection policy version");
    if (!config.max_selected_per_camera)
        return detail::target_error("Calibration sample budget must be positive");
    if (config.camera_roles.empty()) return detail::target_error("Requested camera roles must not be empty");
    for (size_t i = 0; i < config.camera_roles.size(); ++i)
        if (config.camera_roles[i].empty() || (i && config.camera_roles[i - 1] >= config.camera_roles[i]))
            return detail::target_error("Requested camera roles must be nonempty, sorted and unique");
    return {};
}
inline Result<DatasetAnalysisConfig> canonical_analysis_config(DatasetAnalysisConfig config) {
    std::sort(config.camera_roles.begin(), config.camera_roles.end());
    auto valid = validate_analysis_config(config);
    if (!valid) return std::unexpected(valid.error());
    return config;
}
inline Result<std::vector<Id>> canonical_raw_capture_ids(std::vector<Id> ids) {
    std::sort(ids.begin(), ids.end());
    if (ids.empty()) return detail::target_error("Calibration dataset requires RawCapture sources");
    for (size_t i = 0; i < ids.size(); ++i)
        if (ids[i].value.empty() || (i && ids[i - 1] == ids[i]))
            return detail::target_error("RawCapture source identities must be nonempty and unique");
    return ids;
}
inline Result<FixedDiversityDescriptor> quantize_diversity_descriptor(const DiversityDescriptor &descriptor) {
    if (descriptor.extent_x < 0 || descriptor.extent_y < 0 || descriptor.variance_x < 0 || descriptor.variance_y < 0 ||
        descriptor.visible_fraction <= 0 || descriptor.visible_fraction > 1)
        return detail::target_error("Invalid diversity descriptor extents, variances or visible fraction");
    FixedDiversityDescriptor fixed{};
    const auto values = descriptor.components();
    for (size_t i = 0; i < values.size(); ++i) {
        const double scaled = values[i] * double(diversity_descriptor_scale);
        // These exact binary bounds avoid rounding INT64_MAX up to 2^63.
        if (!std::isfinite(scaled) || scaled < -0x1p63 || scaled >= 0x1p63)
            return detail::target_error("Diversity descriptor exceeds fixed-point representation");
        fixed[i] = static_cast<int64_t>(std::llround(scaled)); // Halfway cases away from zero.
    }
    return fixed;
}
inline Result<DiversityDescriptor> describe_diversity(const CalibrationTarget &target, const TargetObservation &observation) {
    auto valid_target = validate_target(target);
    if (!valid_target) return std::unexpected(valid_target.error());
    auto valid = validate_target_observation(observation);
    if (!valid) return std::unexpected(valid.error());
    if (target.identity.id.value.empty() || observation.target.id != target.identity.id ||
        observation.target.revision != target.identity.revision || observation.target_type != target.type())
        return detail::target_error("Diversity observation must match dataset target identity, revision and type");
    const uint64_t total = uint64_t(target.grid.squares_x - 1) * (target.grid.squares_y - 1);
    if (observation.point_ids.size() > total)
        return detail::target_error("Observed point count exceeds target corner count");
    std::vector<size_t> order(observation.point_ids.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return observation.point_ids[a] < observation.point_ids[b]; });
    DiversityDescriptor descriptor;
    double min_x = std::numeric_limits<double>::infinity(), min_y = min_x;
    double max_x = -min_x, max_y = -min_y;
    for (auto i : order) {
        if (observation.point_ids[i] >= total) return detail::target_error("Observation point ID is outside target grid");
        const auto &point = observation.image_points_px[i];
        const double x = point.x_px / observation.image_width, y = point.y_px / observation.image_height;
        descriptor.centroid_x += x; descriptor.centroid_y += y;
        min_x = std::min(min_x, x); max_x = std::max(max_x, x);
        min_y = std::min(min_y, y); max_y = std::max(max_y, y);
    }
    const double count = double(order.size());
    descriptor.centroid_x /= count; descriptor.centroid_y /= count;
    descriptor.extent_x = max_x - min_x; descriptor.extent_y = max_y - min_y;
    for (auto i : order) {
        const auto &point = observation.image_points_px[i];
        const double dx = point.x_px / observation.image_width - descriptor.centroid_x;
        const double dy = point.y_px / observation.image_height - descriptor.centroid_y;
        descriptor.variance_x += dx * dx; descriptor.variance_y += dy * dy;
        descriptor.covariance_xy += dx * dy;
    }
    // Population second moments, using all visible points in increasing-ID order.
    descriptor.variance_x /= count; descriptor.variance_y /= count; descriptor.covariance_xy /= count;
    descriptor.visible_fraction = count / double(total);
    auto fixed = quantize_diversity_descriptor(descriptor);
    if (!fixed) return std::unexpected(fixed.error());
    return descriptor;
}
inline Result<uint64_t> diversity_squared_distance(const FixedDiversityDescriptor &a, const FixedDiversityDescriptor &b) {
    uint64_t sum{};
    constexpr auto max = std::numeric_limits<uint64_t>::max();
    for (size_t i = 0; i < a.size(); ++i) {
        // Unsigned subtraction gives the exact nonnegative magnitude even across
        // INT64_MIN/MAX; no signed subtraction or compiler-specific wide integer.
        const uint64_t delta = a[i] >= b[i] ? uint64_t(a[i]) - uint64_t(b[i]) : uint64_t(b[i]) - uint64_t(a[i]);
        if (delta && delta > max / delta) return detail::target_error("Diversity squared distance overflows uint64");
        const auto square = delta * delta;
        if (square > max - sum) return detail::target_error("Diversity distance accumulation overflows uint64");
        sum += square;
    }
    return sum;
}
struct DiversityCandidate { ObservationKey key; DiversityDescriptor diversity; };
struct SelectedObservation {
    ObservationKey key;
    uint32_t rank{};
    bool operator==(const SelectedObservation &) const = default;
};
// Policy v1. Returns ranks in selection order, independent of candidate input order.
inline Result<std::vector<SelectedObservation>> select_diverse_observations(
    std::vector<DiversityCandidate> candidates, uint32_t budget) {
    if (!budget) return detail::target_error("Calibration sample budget must be positive");
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) { return a.key < b.key; });
    std::vector<FixedDiversityDescriptor> fixed;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto &key = candidates[i].key;
        if (key.frame.raw_capture_id.value.empty() || key.camera_id.value.empty() || key.camera_role.empty() ||
            (i && candidates[i - 1].key == key))
            return detail::target_error("Diversity candidate keys must be valid and unique");
        if (i && (key.camera_role != candidates[0].key.camera_role || key.camera_id != candidates[0].key.camera_id))
            return detail::target_error("Diversity selection requires one physical camera and role");
        auto descriptor = quantize_diversity_descriptor(candidates[i].diversity);
        if (!descriptor) return std::unexpected(descriptor.error());
        fixed.push_back(*descriptor);
    }
    std::vector<SelectedObservation> selected;
    if (candidates.size() <= budget) {
        for (const auto &candidate : candidates) selected.push_back({candidate.key, static_cast<uint32_t>(selected.size())});
        return selected;
    }
    size_t next{};
    for (size_t i = 1; i < fixed.size(); ++i) if (fixed[i] < fixed[next]) next = i;
    std::vector<bool> used(candidates.size());
    std::vector<uint64_t> minimum(candidates.size(), std::numeric_limits<uint64_t>::max());
    while (selected.size() < budget) {
        used[next] = true;
        selected.push_back({candidates[next].key, static_cast<uint32_t>(selected.size())});
        if (selected.size() == budget) break;
        const auto last = next;
        std::optional<size_t> best;
        for (size_t i = 0; i < candidates.size(); ++i) if (!used[i]) {
            auto distance = diversity_squared_distance(fixed[i], fixed[last]);
            if (!distance) return std::unexpected(distance.error());
            minimum[i] = std::min(minimum[i], *distance);
            // Sorted keys make strict comparison implement the canonical tie-break.
            if (!best || minimum[i] > minimum[*best]) best = i;
        }
        next = *best;
    }
    return selected;
}
inline Result<void> validate_calibration_dataset(const CalibrationDataset &dataset) {
    auto target = validate_target(dataset.target);
    if (!target) return std::unexpected(target.error());
    if (dataset.target.identity.id.value.empty()) return detail::target_error("Dataset requires target identity");
    auto config = validate_analysis_config(dataset.config);
    if (!config) return std::unexpected(config.error());
    auto sources = canonical_raw_capture_ids(dataset.raw_capture_ids);
    if (!sources) return std::unexpected(sources.error());
    if (*sources != dataset.raw_capture_ids) return detail::target_error("Dataset RawCapture sources must be sorted");
    if (dataset.cameras.size() != dataset.config.camera_roles.size())
        return detail::target_error("Dataset cameras must match requested roles");
    std::map<std::string, std::set<uint32_t>> ranks;
    for (size_t i = 0; i < dataset.cameras.size(); ++i) {
        const auto &camera = dataset.cameras[i];
        if (camera.role != dataset.config.camera_roles[i] || camera.camera_id.value.empty() ||
            !camera.image_width || !camera.image_height || camera.optical_frame.id.value.empty() || camera.optical_frame.name.empty())
            return detail::target_error("Dataset camera descriptors must be canonical and complete");
    }
    size_t frame_roles{};
    for (size_t i = 0; i < dataset.records.size(); ++i) {
        const auto &record = dataset.records[i];
        if (i && !(dataset.records[i - 1].key < record.key))
            return detail::target_error("Dataset record keys must be sorted and unique");
        if (!std::binary_search(dataset.raw_capture_ids.begin(), dataset.raw_capture_ids.end(), record.key.frame.raw_capture_id))
            return detail::target_error("Dataset record refers to unknown RawCapture source");
        const auto camera = std::lower_bound(dataset.cameras.begin(), dataset.cameras.end(), record.key.camera_role,
            [](const auto &value, const auto &role) { return value.role < role; });
        if (camera == dataset.cameras.end() || camera->role != record.key.camera_role || camera->camera_id != record.key.camera_id)
            return detail::target_error("Dataset record refers to unknown camera role or identity");
        if (!i || dataset.records[i - 1].key.frame != record.key.frame) {
            if (i && frame_roles != dataset.cameras.size()) return detail::target_error("Dataset FrameSet lacks requested camera records");
            frame_roles = 0;
        }
        ++frame_roles;
        if (record.outcome == DetectionOutcome::no_target) {
            if (record.observation || record.diversity || record.selection_rank)
                return detail::target_error("No-target record must not contain detection, diversity or selection");
            continue;
        }
        if (record.outcome != DetectionOutcome::detected || !record.observation || !record.diversity)
            return detail::target_error("Detected record requires observation and diversity");
        const auto &observation = *record.observation;
        const auto &source = observation.source;
        if (source.raw_capture_id != record.key.frame.raw_capture_id || source.frameset_sequence != record.key.frame.frameset_sequence ||
            source.camera_role != record.key.camera_role || source.camera_id != record.key.camera_id)
            return detail::target_error("Dataset observation source must match record key");
        if (observation.image_width != camera->image_width || observation.image_height != camera->image_height)
            return detail::target_error("Dataset observation dimensions must match camera");
        auto descriptor = describe_diversity(dataset.target, observation);
        if (!descriptor) return std::unexpected(descriptor.error());
        if (*descriptor != *record.diversity) return detail::target_error("Dataset diversity descriptor does not match observation");
        for (size_t j = 0; j < observation.point_ids.size(); ++j) {
            const auto id = observation.point_ids[j], columns = dataset.target.grid.squares_x - 1;
            auto physical = scale_target_point(dataset.target, {double(id % columns + 1) * dataset.target.grid.nominal_square_size_mm,
                double(id / columns + 1) * dataset.target.grid.nominal_square_size_mm, 0});
            if (!physical) return std::unexpected(physical.error());
            const auto &point = observation.object_points_mm[j];
            if (physical->x_mm != point.x_mm || physical->y_mm != point.y_mm || physical->z_mm != point.z_mm)
                return detail::target_error("Dataset object points must match physical target geometry");
        }
        if (record.selection_rank && !ranks[camera->role].insert(*record.selection_rank).second)
            return detail::target_error("Dataset selection ranks must be unique per camera");
    }
    if (!dataset.records.empty() && frame_roles != dataset.cameras.size())
        return detail::target_error("Dataset FrameSet lacks requested camera records");
    for (const auto &[role, values] : ranks) {
        (void)role;
        if (values.size() > dataset.config.max_selected_per_camera)
            return detail::target_error("Dataset selection exceeds per-camera budget");
        uint32_t expected{};
        for (auto rank : values) if (rank != expected++) return detail::target_error("Dataset selection ranks must be contiguous from zero");
    }
    return {};
}
// Transactional: invalid input/distance overflow leaves existing ranks unchanged.
inline Result<void> select_dataset_samples(CalibrationDataset &dataset) {
    auto valid = validate_calibration_dataset(dataset);
    if (!valid) return std::unexpected(valid.error());
    std::map<ObservationKey, uint32_t> ranks;
    for (const auto &camera : dataset.cameras) {
        std::vector<DiversityCandidate> candidates;
        for (const auto &record : dataset.records) if (record.key.camera_role == camera.role && record.diversity)
            candidates.push_back({record.key, *record.diversity});
        auto selected = select_diverse_observations(std::move(candidates), dataset.config.max_selected_per_camera);
        if (!selected) return std::unexpected(selected.error());
        for (const auto &value : *selected) ranks.emplace(value.key, value.rank);
    }
    for (auto &record : dataset.records) {
        const auto found = ranks.find(record.key);
        record.selection_rank = found == ranks.end() ? std::nullopt : std::optional<uint32_t>{found->second};
    }
    return {};
}
struct CameraAnalysisSummary {
    uint64_t analyzed{}, detected{}, no_target{}, selected{}, partial_charuco{}, full_charuco{};
    std::optional<uint32_t> minimum_points, maximum_points;
};
struct DatasetAnalysisSummary {
    std::map<Id, uint64_t> framesets_per_capture;
    std::map<std::string, CameraAnalysisSummary> cameras;
};
inline Result<DatasetAnalysisSummary> summarize_dataset(const CalibrationDataset &dataset) {
    auto valid = validate_calibration_dataset(dataset);
    if (!valid) return std::unexpected(valid.error());
    DatasetAnalysisSummary summary;
    for (const auto &id : dataset.raw_capture_ids) summary.framesets_per_capture.emplace(id, 0);
    for (const auto &camera : dataset.cameras) summary.cameras.emplace(camera.role, CameraAnalysisSummary{});
    std::optional<FrameSetKey> previous;
    for (const auto &record : dataset.records) {
        if (!previous || *previous != record.key.frame) ++summary.framesets_per_capture[record.key.frame.raw_capture_id];
        previous = record.key.frame;
        auto &camera = summary.cameras[record.key.camera_role];
        ++camera.analyzed;
        if (!record.observation) { ++camera.no_target; continue; }
        ++camera.detected;
        if (record.selection_rank) ++camera.selected;
        if (record.observation->target_type == TargetType::charuco)
            record.observation->evidence.partial ? ++camera.partial_charuco : ++camera.full_charuco;
        const auto points = record.observation->evidence.detected_points;
        camera.minimum_points = camera.minimum_points ? std::min(*camera.minimum_points, points) : points;
        camera.maximum_points = camera.maximum_points ? std::max(*camera.maximum_points, points) : points;
    }
    return summary;
}
} // namespace mantis::calibration
