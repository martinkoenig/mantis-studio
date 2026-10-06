#pragma once
#include <mantis/artifact_store.hpp>
#include <mantis/calibration_artifacts.hpp>
#include <mantis/device_api.hpp>
#include <mutex>

namespace mantis::services {
// Internal capture-start seam; strict discovery parsing, never called by the frame writer.
Result<calibration::artifacts::CameraComponent> discovered_calibration_component(const device::Descriptor &);
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
