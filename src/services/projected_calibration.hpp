#pragma once
#include <mantis/calibration_artifacts.hpp>
#include <mantis/projected_light_device.hpp>
#include <mutex>
#include <set>

namespace mantis::services {
// Project policy lives above device-runtime. Stamp precedes all L3 correlation/publication.
class ProjectedCalibrationBinding {
    std::optional<artifact::ActiveCalibration> snapshot_;
    std::optional<calibration::Reference> source_;
    std::optional<data::ExactCalibrationReference> source_rig_;
    std::map<data::ComponentId, data::ExactCalibrationReference> camera_rigs_;
    std::vector<data::ComponentId> cameras_;
    std::set<data::ComponentId> seen_cameras_;
    struct Audit {
        uint64_t sequence;
        data::Metadata fields;
    };
    std::vector<Audit> audit_; // At most 3 + 2 * declared cameras bounded one-time initializations.
    bool initial_{};
    std::mutex mutex_;

  public:
    ProjectedCalibrationBinding(const artifact::Store &, const device::ProjectedGraph &,
                                const data::AcquisitionProgram &);
    void provenance(artifact::Provenance &) const;
    data::AcquisitionBundle stamp(data::AcquisitionBundle);
    data::Metadata source_provenance(data::BundleSequence);
};
std::unique_ptr<device::ProjectedExecutor>
    calibration_bound_executor(std::unique_ptr<device::ProjectedExecutor>,
                               std::shared_ptr<ProjectedCalibrationBinding>);
} // namespace mantis::services
