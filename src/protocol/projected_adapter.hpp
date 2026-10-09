#pragma once
#include <mantis/service_adapter.hpp>
namespace mantis::protocol {
data::AcquisitionProgram read_projected_program(const wire::v1::ProjectedAcquisitionProgram &);
void write_projected_program(wire::v1::ProjectedAcquisitionProgram *, const data::AcquisitionProgram &);
bool dispatch_projected(services::Runtime &, const wire::v1::Request &, wire::v1::Response &);
} // namespace mantis::protocol
