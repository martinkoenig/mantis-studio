#pragma once
#include <mantis/artifact_api.hpp>
#include <mantis/calibration.hpp>

namespace mantis::services {
// Resource bounds, in UTF-8 bytes/counts; shared by all public calibration operations.
inline constexpr size_t calibration_string_limit = 4096;
inline constexpr size_t calibration_source_limit = 1024;
inline constexpr size_t calibration_role_limit = 64;
struct TargetSpecification {
    calibration::TargetGrid grid;
    calibration::TargetPattern pattern;
    calibration::PhysicalMeasurement measurement;
};
struct TargetCreate {
    TargetSpecification target;
    std::string series_id; // Empty means a new series. Persistence owns identity/revision.
};
struct DatasetBuild {
    Id target_artifact_id;
    std::vector<Id> raw_capture_artifact_ids;
    std::vector<std::string> camera_roles;
    uint32_t max_selected_per_camera{};
    std::string series_id;
};
struct CameraSolve {
    Id dataset_artifact_id;
    std::string camera_role;
    uint32_t heldout_per_camera{};
    std::string series_id;
};
struct RigSolve {
    Id dataset_artifact_id, left_camera_artifact_id, right_camera_artifact_id;
    uint32_t heldout_pairs{};
    spatial::CoordinateFrame rig_frame;
    std::string series_id;
};
struct CalibrationEntry {
    artifact::ArtifactDescriptor artifact;
    calibration::Reference reference;
};
struct TargetInfo { TargetSpecification target; };
struct DatasetCameraInfo {
    calibration::DatasetCamera camera;
    uint64_t analyzed{}, detected{}, no_target{}, selected{};
};
struct DatasetInfo {
    artifact::ArtifactReference target;
    std::vector<Id> raw_capture_ids;
    calibration::DatasetAnalysisConfig config;
    std::vector<DatasetCameraInfo> cameras;
    uint64_t total_records{};
};
struct MonoStageInfo {
    uint64_t sample_count{};
    calibration::ResidualSummary residuals;
    calibration::CoverageEvidence coverage;
    std::optional<double> opencv_solver_rms_px;
};
struct StereoStageInfo {
    uint64_t pair_count{};
    calibration::ResidualSummary residuals;
    std::optional<double> opencv_solver_rms_px;
};
struct SolverInfo {
    std::string opencv_version;
    SemanticVersion mantis_version;
    std::string mantis_build;
};
struct CameraInfo {
    artifact::ArtifactReference dataset, target;
    calibration::DatasetCamera camera;
    calibration::MonoSolveConfig config;
    calibration::PinholeBrown5 training_model, final_model;
    MonoStageInfo training, heldout, final;
    SolverInfo implementation;
};
struct RigInfo {
    artifact::ArtifactReference dataset, target, left_camera, right_camera;
    calibration::DatasetCamera left, right;
    calibration::StereoSolveConfig config;
    calibration::PinholeBrown5 left_final_model, right_final_model;
    calibration::StereoModel final_model;
    calibration::RigGeometry geometry;
    StereoStageInfo training, heldout, final;
    SolverInfo implementation;
};
struct CalibrationInfo {
    CalibrationEntry entry;
    std::variant<TargetInfo, DatasetInfo, CameraInfo, RigInfo> detail;
};
struct ActiveCalibrationInfo {
    Id logical_device_id;
    calibration::Reference reference;
    artifact::ArtifactReference artifact;
};
class CalibrationService {
  public:
    virtual ~CalibrationService() = default;
    virtual CalibrationInfo create_calibration_target(const TargetCreate &) = 0;
    virtual Id build_calibration_dataset(const DatasetBuild &) = 0;
    virtual Id solve_camera_calibration(const CameraSolve &) = 0;
    virtual Id solve_rig_calibration(const RigSolve &) = 0;
    virtual std::vector<CalibrationEntry> calibrations() const = 0;
    virtual CalibrationInfo calibration_info(const Id &) const = 0;
    virtual std::optional<ActiveCalibrationInfo> active_calibration(const Id &logical_device_id) const = 0;
    virtual void activate_calibration(const Id &logical_device_id, const Id &rig_artifact_id) = 0;
    virtual void clear_calibration(const Id &logical_device_id) = 0;
};
} // namespace mantis::services
