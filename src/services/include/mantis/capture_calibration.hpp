#pragma once
#include <mantis/artifact_store.hpp>
#include <mutex>

namespace mantis::services {
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
