#pragma once
#include <mantis/device_api.hpp>
#include <mantis/projected_light.hpp>

namespace mantis::device {
inline constexpr std::string_view projected_light = "org.mantis.acquisition.projected-light.v1";
inline bool has_capability(const Descriptor &d, std::string_view capability) {
    return std::find(d.capabilities.begin(), d.capabilities.end(), capability) != d.capabilities.end();
}
// Image capability is required; presentation role and parenthood are insufficient.
inline bool image_participant(const Descriptor &d) {
    return has_capability(d, image_stream);
}
enum class ParticipantKind { parent, image, emitter, controller };
struct ProjectedImageSource {
    data::StreamId stream;
    std::string physical_identity;
    uint32_t width{}, height{};
};
struct ProjectedComponent {
    Descriptor descriptor;
    std::string role;
    ParticipantKind kind{};
    std::optional<ProjectedImageSource> image_source;
    std::vector<Id> controls, participants, trigger_endpoints;
    std::vector<data::EmitterState> emitter_states;
    std::vector<data::CaptureMode> capture_modes, trigger_modes;
    std::vector<data::EvidenceMethod> evidence_methods;
    std::vector<data::EvidenceScope> evidence_scopes;
    data::Evidence<data::PatternId> pattern;
    data::Evidence<uint64_t> pattern_revision;
    std::vector<data::LineIdentity> lines;
};
struct ProjectedLimits {
    uint32_t max_components{}, max_steps{}, max_bundle_members{}, max_cameras{};
    data::RunBounds bounds;
    data::Duration max_step_duration{};
    uint32_t max_pending_bundles{}, max_call_timeout_ms{};
    data::Evidence<bool> watchdog, interlock, fail_off;
};
struct ProjectedGraph {
    Id parent;
    std::vector<ProjectedComponent> components;
    ProjectedLimits limits;
    data::Evidence<data::StreamId> frameset_stream;
    std::vector<Descriptor> image_participants() const {
        std::vector<Descriptor> out;
        for (const auto &c : components)
            if (c.kind == ParticipantKind::image && image_participant(c.descriptor))
                out.push_back(c.descriptor);
        return out;
    }
};
struct ContractError {
    uint32_t category{}, code{};
};
struct ProgramValidation {
    bool accepted{};
    ContractError error;
    std::string diagnostic;
};
enum class ProjectedRunState { open, prepared, started, stopped, failed };
struct ProjectedStatus {
    ProjectedRunState state{};
    data::Evidence<data::RunId> run;
    data::Evidence<data::GenerationId> generation;
    data::Evidence<data::StepInstance> step;
    data::Evidence<bool> commands_available, evidence_available;
    ContractError error;
};
struct AbortOutcome {
    data::Evidence<data::RunId> run;
    data::Evidence<data::GenerationId> fenced_generation;
    data::Evidence<bool> inhibited, stale_work_fenced, off_requested;
    std::vector<data::EmitterEvidence> emitters;
    ContractError error;
};
// A projected-light instance owns parent/resources, independently of ImageStream.
// No daemon policy, program scheduler, queues or recording in this abstraction.
class ProjectedExecutor {
  public:
    virtual ~ProjectedExecutor() = default;
    virtual const ProjectedGraph &graph() const = 0;
    virtual Result<ProgramValidation> validate(const data::AcquisitionProgram &, uint32_t timeout_ms) = 0;
    virtual Result<ProgramValidation> prepare(const data::AcquisitionProgram &, uint32_t timeout_ms) = 0;
    virtual Result<void> start(const data::RunId &, const data::GenerationId &, uint32_t timeout_ms) = 0;
    virtual Result<std::optional<data::AcquisitionBundle>> next(uint32_t timeout_ms) = 0;
    virtual Result<ProjectedStatus> status(uint32_t timeout_ms) = 0;
    virtual Result<AbortOutcome> abort(data::AcquisitionReason, uint32_t timeout_ms) = 0;
    virtual Result<void> stop(uint32_t timeout_ms) = 0;
    virtual Result<void> close(uint32_t timeout_ms) = 0;
    virtual Result<std::string> diagnostics(uint32_t timeout_ms) = 0;
};
} // namespace mantis::device
