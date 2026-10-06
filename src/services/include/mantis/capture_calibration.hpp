#pragma once
#include <mantis/artifact_store.hpp>
#include <mantis/calibration_artifacts.hpp>
#include <mantis/device_api.hpp>
#include <mutex>

namespace mantis::services {
// Internal capture-start seam; strict discovery parsing, never called by the frame writer.
Result<calibration::artifacts::CameraComponent> discovered_calibration_component(const device::Descriptor &);
// Control-plane activation seam. A present parent with malformed component metadata
// must fail explicitly; only the caller decides whether the parent is discovered.
Result<std::vector<calibration::artifacts::CameraComponent>> discovered_activation_components(
    const device::Descriptor &parent, std::span<const device::Descriptor> current);
// Writer-side capture seam. Snapshot supplied before Session starts; no active lookup in stamp().
class CaptureCalibrationBinding {
    std::shared_ptr<artifact::Store> store_;
    Id raw_;
    std::optional<artifact::ActiveCalibration> snapshot_;
    std::optional<calibration::Reference> source_;
    std::mutex mutex_;

  public:
    CaptureCalibrationBinding(std::shared_ptr<artifact::Store>, Id raw,
                              std::optional<artifact::ActiveCalibration> snapshot);
    data::Published stamp(data::Published);
};
} // namespace mantis::services
