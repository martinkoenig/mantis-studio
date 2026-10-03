#pragma once
#include <mantis/data.hpp>
namespace mantis::artifact {
using ArtifactId = Id;
struct ArtifactType {
    std::string name;
    uint32_t schema_version{1};
};
enum class ArtifactState { open, finalizing, finalized, recoverable };
inline std::string state_name(ArtifactState s) {
    switch (s) {
    case ArtifactState::open:
        return "OPEN";
    case ArtifactState::finalizing:
        return "FINALIZING";
    case ArtifactState::finalized:
        return "FINALIZED";
    case ArtifactState::recoverable:
        return "RECOVERABLE";
    }
    return "UNKNOWN";
}
struct Provenance {
    std::string producer;
    SemanticVersion version{0, 1, 0};
    std::vector<ArtifactId> inputs;
    data::Metadata parameters;
    calibration::Reference calibration;
};
struct ArtifactDescriptor {
    ArtifactId id;
    ArtifactType type;
    ArtifactState state{};
    Provenance provenance;
    Hash hash;
    uint64_t bytes{}, chunks{};
};
struct ArtifactReference {
    ArtifactId id;
    Hash hash;
};
} // namespace mantis::artifact
