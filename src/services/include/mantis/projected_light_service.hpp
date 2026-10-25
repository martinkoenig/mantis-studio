#pragma once
#include <mantis/projected_run.hpp>
namespace mantis::services {
inline constexpr size_t projected_inline_bytes = 512 * 1024;
struct ProjectedDeviceInfo {
    std::string plugin_id;
    device::ProjectedGraph graph;
};
struct ProjectedCaptureRequest {
    std::string plugin_id;
    Id parent;
    // Exactly one canonical program or finalized RawCapture-3 header source.
    std::variant<data::AcquisitionProgram, Id> program;
    device::ProjectedRunConfig config = [] {
        device::ProjectedRunConfig c;
        c.queue_capacity = 1; // A positive conservative daemon default within every accepted graph.
        return c;
    }();
};
enum class ProjectedStopMode { normal_stop, cancel };
struct ProjectedStopRequest {
    Id capture;
    data::RunId run;
    data::GenerationId generation;
    ProjectedStopMode mode{ProjectedStopMode::normal_stop};
};
struct ProjectedEvidenceSummary {
    uint64_t bundles{}, trigger_events{}, evidence_only{}, captured{}, unresolved_request_records{},
        loss_records{};
    uint64_t commanded_established{}, commanded_unknown{}, commanded_unavailable{};
    uint64_t acknowledgement_success{}, acknowledgement_rejected{}, acknowledgement_failed{};
    uint64_t acknowledgement_unknown{}, acknowledgement_unavailable{};
    uint64_t effective_established{}, effective_unknown{}, effective_unavailable{};
};
struct ProjectedCaptureInfo {
    Id id, parent, raw_artifact, finalization_job;
    std::string plugin_id;
    data::ProgramReference program;
    device::ProjectedRunSnapshot run;
    artifact::ArtifactState storage_state{artifact::ArtifactState::open};
    bool active{};
    uint64_t committed{}, bytes{};
    std::optional<Error> recording_error;
    std::optional<data::StepInstance> last_evidence_step;
    std::optional<data::BundleSequence> latest_bundle_sequence;
    ProjectedEvidenceSummary evidence;
};
class ProjectedLightService {
  public:
    virtual ~ProjectedLightService() = default;
    virtual std::vector<ProjectedDeviceInfo> projected_devices() const = 0;
    virtual device::ProjectedValidation validate_projected(const ProjectedCaptureRequest &) = 0;
    virtual ProjectedCaptureInfo start_projected(const ProjectedCaptureRequest &,
                                                 const std::string &request_id) = 0;
    virtual ProjectedCaptureInfo projected_status(const Id &) const = 0;
    virtual std::vector<ProjectedCaptureInfo> projected_captures() const = 0;
    virtual ProjectedCaptureInfo stop_projected(const ProjectedStopRequest &) = 0;
};
} // namespace mantis::services
