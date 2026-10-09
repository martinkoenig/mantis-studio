#pragma once
#include <mantis/projected_light_device.hpp>
#include <mantis/sdk.hpp>
#include <mantis/semantic_packet.hpp>

namespace mantis::plugins::semantic {
using Packet = data::SemanticPacket;
// Owner keeps all borrowed strings/views/buffers alive until it is destroyed.
// The input domain object need not outlive this view.
class ProgramView {
    std::shared_ptr<void> owner_;
    MantisAcquisitionProgramV1 view_{};

  public:
    explicit ProgramView(const data::AcquisitionProgram &);
    const MantisAcquisitionProgramV1 *get() const {
        return &view_;
    }
};
class PacketView {
    std::shared_ptr<void> owner_;
    MantisSemanticPacketV1 view_{};

  public:
    explicit PacketView(const Packet &, const MantisHostV1 *);
    const MantisSemanticPacketV1 *get() const {
        return &view_;
    }
};
data::AcquisitionProgram program(const MantisAcquisitionProgramV1 *);
device::ProjectedGraph graph(const MantisProjectedGraphV1 *);
device::ProgramValidation validation(const MantisProgramValidationV1 *);
device::ProjectedStatus status(const MantisProjectedStatusV1 *);
device::AbortOutcome abort_outcome(const MantisAbortOutcomeV1 *);
Packet packet(const MantisSemanticPacketV1 *, const MantisHostV1 *);
} // namespace mantis::plugins::semantic
