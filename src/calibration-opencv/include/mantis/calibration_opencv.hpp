#pragma once
#include <mantis/calibration_observation.hpp>

namespace mantis::calibration::opencv {
// Synchronous borrowed RAW8 view. Padding between rows is allowed; the last row
// needs only width bytes. No pixel ownership or lifetime is retained by detect().
struct GrayImageView {
    std::span<const uint8_t> bytes;
    uint32_t width{}, height{};
    size_t row_stride{};
};
Result<void> validate_gray_image_view(GrayImageView image);
std::span<const std::string_view> supported_charuco_dictionaries();
// Fixed offline detector configuration; no intrinsics, pose or quality selection.
// A valid image without visible target corners returns a successful nullopt.
Result<std::optional<TargetObservation>> detect_target(
    const CalibrationTarget &target, GrayImageView image, const ObservationSource &source);
} // namespace mantis::calibration::opencv
