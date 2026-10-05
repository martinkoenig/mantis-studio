#pragma once
#include <mantis/base.hpp>
#include <cmath>
#include <optional>
#include <utility>
#include <variant>

namespace mantis::calibration {
struct TargetIdentity {
    Id id;
    uint64_t revision{}; // Retained as supplied; allocation/versioning belongs to persistence.
};
struct TargetGrid {
    uint32_t squares_x{}, squares_y{}; // Number of SQUARES, not inner corners; each >= 2.
    double nominal_square_size_mm{};
};
struct CheckerboardDefinition {};
struct CharucoDefinition {
    std::string dictionary; // Explicit, nonempty; supported dictionaries are validated by M2.
    double nominal_marker_size_mm{};
};
using TargetPattern = std::variant<CheckerboardDefinition, CharucoDefinition>;
enum class TargetType { checkerboard, charuco };
struct MeasurementProvenance {
    std::optional<double> width_uncertainty_mm, height_uncertainty_mm;
    std::optional<std::string> instrument, note;
};
struct PhysicalMeasurement {
    // Outer active-grid edge to opposite outer edge; never paper/margin/substrate.
    // Both dimensions are present or both absent. Provenance never changes scale.
    std::optional<double> active_width_mm, active_height_mm;
    std::optional<MeasurementProvenance> provenance;
};
struct CalibrationTarget {
    TargetIdentity identity;
    TargetGrid grid;
    TargetPattern pattern{CheckerboardDefinition{}};
    PhysicalMeasurement measurement;
    TargetType type() const {
        return std::holds_alternative<CharucoDefinition>(pattern) ? TargetType::charuco : TargetType::checkerboard;
    }
};
enum class GeometrySource { nominal, measured };
struct MarkerDimensions {
    double width_mm{}, height_mm{}; // Physical diagnostics; anisotropic prints need not be square.
};
struct TargetGeometry {
    GeometrySource source{GeometrySource::nominal};
    double nominal_active_width_mm{}, nominal_active_height_mm{};
    double scale_x{1}, scale_y{1};
    double effective_square_pitch_x_mm{}, effective_square_pitch_y_mm{};
    std::optional<MarkerDimensions> effective_marker;
};
struct TargetPoint {
    // Target-local origin at the increasing-column/row active-grid outer corner.
    // +X columns, +Y rows, +Z = +X cross +Y. Actual board points have Z=0, in mm.
    // This is distinct from camera optical and rig coordinates.
    double x_mm{}, y_mm{}, z_mm{};
};
namespace detail {
inline bool positive_finite(double value) { return std::isfinite(value) && value > 0; }
inline std::unexpected<Error> target_error(std::string message) {
    return std::unexpected(Error{Status::invalid_argument, std::move(message), "calibration"});
}
} // namespace detail

// Pure derivation and structural validation, with no detector, quality threshold or IO.
inline Result<TargetGeometry> derive_target_geometry(const CalibrationTarget &target) {
    const auto &grid = target.grid;
    if (grid.squares_x < 2 || grid.squares_y < 2)
        return detail::target_error("Grid squares_x and squares_y must each be at least 2");
    if (!detail::positive_finite(grid.nominal_square_size_mm))
        return detail::target_error("nominal_square_size_mm must be finite and positive");
    TargetGeometry geometry;
    geometry.nominal_active_width_mm = double(grid.squares_x) * grid.nominal_square_size_mm;
    geometry.nominal_active_height_mm = double(grid.squares_y) * grid.nominal_square_size_mm;
    if (!detail::positive_finite(geometry.nominal_active_width_mm) ||
        !detail::positive_finite(geometry.nominal_active_height_mm))
        return detail::target_error("Nominal active extents are not representable as finite positive doubles");
    const auto *charuco = std::get_if<CharucoDefinition>(&target.pattern);
    if (target.pattern.valueless_by_exception())
        return detail::target_error("Target pattern is unavailable");
    if (charuco) {
        if (charuco->dictionary.empty()) return detail::target_error("ChArUco dictionary must not be empty");
        if (!detail::positive_finite(charuco->nominal_marker_size_mm) ||
            charuco->nominal_marker_size_mm >= grid.nominal_square_size_mm)
            return detail::target_error("nominal_marker_size_mm must be finite, positive and smaller than nominal_square_size_mm");
    }
    const auto &measurement = target.measurement;
    if (measurement.active_width_mm.has_value() != measurement.active_height_mm.has_value())
        return detail::target_error("Measured active_width_mm and active_height_mm must be present together or absent together");
    if (measurement.active_width_mm) {
        if (!detail::positive_finite(*measurement.active_width_mm) || !detail::positive_finite(*measurement.active_height_mm))
            return detail::target_error("Measured active extents must be finite and positive");
        geometry.source = GeometrySource::measured;
        geometry.scale_x = *measurement.active_width_mm / geometry.nominal_active_width_mm;
        geometry.scale_y = *measurement.active_height_mm / geometry.nominal_active_height_mm;
    }
    if (measurement.provenance) {
        for (auto uncertainty : {measurement.provenance->width_uncertainty_mm, measurement.provenance->height_uncertainty_mm})
            if (uncertainty && (!std::isfinite(*uncertainty) || *uncertainty < 0))
                return detail::target_error("Measurement uncertainties must be finite and non-negative");
    }
    geometry.effective_square_pitch_x_mm = grid.nominal_square_size_mm * geometry.scale_x;
    geometry.effective_square_pitch_y_mm = grid.nominal_square_size_mm * geometry.scale_y;
    if (!detail::positive_finite(geometry.scale_x) || !detail::positive_finite(geometry.scale_y) ||
        !detail::positive_finite(geometry.effective_square_pitch_x_mm) ||
        !detail::positive_finite(geometry.effective_square_pitch_y_mm))
        return detail::target_error("Physical scales and pitches are not representable as finite positive doubles");
    if (charuco) {
        geometry.effective_marker = MarkerDimensions{charuco->nominal_marker_size_mm * geometry.scale_x,
                                                     charuco->nominal_marker_size_mm * geometry.scale_y};
        if (!detail::positive_finite(geometry.effective_marker->width_mm) ||
            !detail::positive_finite(geometry.effective_marker->height_mm))
            return detail::target_error("Physical marker dimensions are not representable as finite positive doubles");
    }
    return geometry;
}
inline Result<void> validate_target(const CalibrationTarget &target) {
    auto geometry = derive_target_geometry(target);
    if (!geometry) return std::unexpected(geometry.error());
    return {};
}
// Scale about the declared target origin; preserves Z, not a board-warp correction.
// M2 must verify canonical detector coordinates/IDs before supplying nominal points.
inline Result<TargetPoint> scale_target_point(const CalibrationTarget &target, TargetPoint nominal) {
    auto geometry = derive_target_geometry(target);
    if (!geometry) return std::unexpected(geometry.error());
    if (!std::isfinite(nominal.x_mm) || !std::isfinite(nominal.y_mm) || !std::isfinite(nominal.z_mm))
        return detail::target_error("Nominal target point must be finite");
    TargetPoint effective{nominal.x_mm * geometry->scale_x, nominal.y_mm * geometry->scale_y, nominal.z_mm};
    if (!std::isfinite(effective.x_mm) || !std::isfinite(effective.y_mm))
        return detail::target_error("Effective target point is not representable as finite doubles");
    return effective;
}
} // namespace mantis::calibration
