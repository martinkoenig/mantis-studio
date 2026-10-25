#pragma once
#include <mantis/projected_light.hpp>
namespace mantis::data {
// Internal immutable processing envelope. Canonical semantic models remain unchanged.
using SemanticPacket =
    std::variant<Published, AcquisitionEvidence, TriggerEvent, AcquisitionBundle, LaserObservation>;
using SemanticPublished = std::shared_ptr<const SemanticPacket>;
schema::DataTypeId semantic_type(const SemanticPacket &);
Result<void> validate(const SemanticPacket &);
SemanticPublished publish(SemanticPacket);
} // namespace mantis::data
