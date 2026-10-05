#pragma once
#include <mantis/artifact_store.hpp>
#include <mantis/calibration.hpp>

namespace mantis::calibration::artifacts {
inline const artifact::ArtifactType target_type{"org.mantis.CalibrationTarget", 1},
    dataset_type{"org.mantis.CalibrationDataset", 1}, camera_type{"org.mantis.CameraCalibration", 1},
    rig_type{"org.mantis.RigCalibration", 1};
inline const schema::DataTypeId document_type{"org.mantis.CalibrationDocument", 1};
inline constexpr std::string_view document_attribute = "org.mantis.calibration.document";
struct SolverImplementation {
    std::string opencv_version; // Actual M4 backend, supplied by the solve caller.
    SemanticVersion mantis_version{application_version};
    std::string mantis_build{build_version};
};
struct TargetArtifact {
    Reference revision;
    CalibrationTarget target;
};
struct DatasetArtifact {
    Reference revision;
    artifact::ArtifactReference target_reference;
    std::vector<artifact::ArtifactReference> raw_capture_references;
    CalibrationDataset dataset;
};
struct CameraArtifact {
    Reference revision;
    artifact::ArtifactReference dataset_reference, target_reference;
    SolverImplementation implementation;
    CameraCalibrationSolution solution;
};
struct RigArtifact {
    Reference revision;
    artifact::ArtifactReference dataset_reference, target_reference, left_camera_reference,
        right_camera_reference;
    SolverImplementation implementation;
    StereoCalibrationSolution solution;
};
template <class T> struct Stored {
    artifact::ArtifactDescriptor descriptor;
    T value;
    artifact::ArtifactReference reference() const {
        return {descriptor.id, descriptor.hash};
    }
};
// Passing a series explicitly creates its next revision; callers never choose revision numbers.
// Target identity must be unassigned. The returned persisted identity is used BEFORE M3/M4.
Result<Stored<TargetArtifact>> create_calibration_target(artifact::Store &, CalibrationTarget,
                                                         std::optional<Id> series = {});
Result<Stored<DatasetArtifact>> create_calibration_dataset(artifact::Store &, const CalibrationDataset &,
                                                           const artifact::ArtifactReference &target,
                                                           std::optional<Id> series = {});
Result<Stored<CameraArtifact>> create_camera_calibration(artifact::Store &, const CameraCalibrationSolution &,
                                                         const artifact::ArtifactReference &dataset,
                                                         const artifact::ArtifactReference &target,
                                                         const SolverImplementation &,
                                                         std::optional<Id> series = {});
Result<Stored<RigArtifact>> create_rig_calibration(artifact::Store &, const StereoCalibrationSolution &,
                                                   const artifact::ArtifactReference &dataset,
                                                   const artifact::ArtifactReference &target,
                                                   const artifact::ArtifactReference &left_camera,
                                                   const artifact::ArtifactReference &right_camera,
                                                   const SolverImplementation &,
                                                   std::optional<Id> series = {});
Result<TargetArtifact> load_calibration_target(const artifact::Store &, const Id &);
Result<DatasetArtifact> load_calibration_dataset(const artifact::Store &, const Id &);
Result<CameraArtifact> load_camera_calibration(const artifact::Store &, const Id &);
Result<RigArtifact> load_rig_calibration(const artifact::Store &, const Id &);
// Canonical document bytes, also used by persistence. No JSON type escapes this API.
Result<std::string> encode_document(const TargetArtifact &);
Result<std::string> encode_document(const DatasetArtifact &);
Result<std::string> encode_document(const CameraArtifact &);
Result<std::string> encode_document(const RigArtifact &);
struct CameraComponent {
    std::string role;
    Id camera_id;
};
Result<void> validate_rig_device(const RigArtifact &, const Id &logical_device_id,
                                 std::span<const CameraComponent> components = {});
Result<void> activate_rig_calibration(artifact::Store &, const Id &logical_device_id,
                                      const Id &rig_artifact_id,
                                      std::span<const CameraComponent> components = {});
} // namespace mantis::calibration::artifacts
