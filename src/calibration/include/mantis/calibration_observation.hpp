#pragma once
#include <mantis/calibration_target.hpp>
#include <map>
#include <set>
#include <vector>

namespace mantis::calibration {
struct ObservationSource {
    Id raw_capture_id;
    uint64_t frameset_sequence{};
    Id camera_id;
    std::string camera_role;
};
struct ImagePoint {
    double x_px{}, y_px{};
};
struct DetectionEvidence {
    uint32_t detected_points{}, detected_markers{};
    bool partial{};
};
enum class PointIdSemantics { detector_grid, physical_board };
struct TargetObservation {
    ObservationSource source;
    TargetIdentity target;
    TargetType target_type{TargetType::checkerboard};
    uint32_t image_width{}, image_height{};
    std::vector<uint32_t> point_ids;
    std::vector<ImagePoint> image_points_px;
    std::vector<TargetPoint> object_points_mm;
    DetectionEvidence evidence;

    // A plain checkerboard cannot encode an absolute physical origin under symmetry.
    PointIdSemantics point_id_semantics() const {
        return target_type == TargetType::charuco ? PointIdSemantics::physical_board : PointIdSemantics::detector_grid;
    }
};
inline Result<void> validate_observation_source(const ObservationSource &source) {
    if (source.raw_capture_id.value.empty() || source.camera_id.value.empty() || source.camera_role.empty())
        return detail::target_error("Observation source requires RawCapture identity, camera identity and camera role");
    return {};
}
inline Result<void> validate_target_observation(const TargetObservation &observation) {
    auto source = validate_observation_source(observation.source);
    if (!source) return std::unexpected(source.error());
    if (observation.target.id.value.empty())
        return detail::target_error("Observation requires target identity");
    if (observation.target_type != TargetType::checkerboard && observation.target_type != TargetType::charuco)
        return detail::target_error("Observation target type is invalid");
    if (!observation.image_width || !observation.image_height)
        return detail::target_error("Observation image dimensions must be nonzero");
    const auto count = observation.point_ids.size();
    if (!count || count != observation.image_points_px.size() || count != observation.object_points_mm.size())
        return detail::target_error("Observation correspondence arrays must have equal nonzero lengths");
    if (count != observation.evidence.detected_points)
        return detail::target_error("Observation detected point count must match correspondences");
    std::set<uint32_t> ids;
    for (size_t i = 0; i < count; ++i) {
        if (!ids.insert(observation.point_ids[i]).second)
            return detail::target_error("Observation point IDs must be unique");
        const auto &image = observation.image_points_px[i];
        const auto &object = observation.object_points_mm[i];
        if (!std::isfinite(image.x_px) || !std::isfinite(image.y_px))
            return detail::target_error("Observation image points must be finite");
        if (!std::isfinite(object.x_mm) || !std::isfinite(object.y_mm) || !std::isfinite(object.z_mm))
            return detail::target_error("Observation object points must be finite");
    }
    return {};
}
struct CommonTargetPoint {
    uint32_t point_id{};
    ImagePoint left_px, right_px;
    TargetPoint object_mm;
};
// Sorted by ID, independent of input vector order. This is not a stereo solve or
// a frame-pairing rule. Checkerboard grid joins require externally established
// physical orientation; this helper cannot resolve an unobservable board origin.
inline Result<std::vector<CommonTargetPoint>> intersect_observations(
    const TargetObservation &left, const TargetObservation &right) {
    for (const auto *observation : {&left, &right}) {
        auto valid = validate_target_observation(*observation);
        if (!valid) return std::unexpected(valid.error());
    }
    if (left.target.id != right.target.id || left.target.revision != right.target.revision ||
        left.target_type != right.target_type)
        return detail::target_error("Observation intersection requires the same target identity, revision and type");
    std::map<uint32_t, size_t> right_indices;
    for (size_t i = 0; i < right.point_ids.size(); ++i) right_indices.emplace(right.point_ids[i], i);
    std::map<uint32_t, size_t> left_indices;
    for (size_t i = 0; i < left.point_ids.size(); ++i) left_indices.emplace(left.point_ids[i], i);
    std::vector<CommonTargetPoint> common;
    for (const auto &[id, i] : left_indices) {
        const auto found = right_indices.find(id);
        if (found == right_indices.end()) continue;
        const auto j = found->second;
        const auto &a = left.object_points_mm[i];
        const auto &b = right.object_points_mm[j];
        if (a.x_mm != b.x_mm || a.y_mm != b.y_mm || a.z_mm != b.z_mm)
            return detail::target_error("Common observation point has inconsistent object geometry");
        common.push_back({id, left.image_points_px[i], right.image_points_px[j], a});
    }
    return common;
}
} // namespace mantis::calibration
