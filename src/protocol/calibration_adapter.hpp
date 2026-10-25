#pragma once
#include <mantis/service_adapter.hpp>
namespace mantis::protocol {
void write_artifact(wire::v1::Artifact *, const artifact::ArtifactDescriptor &);
bool dispatch_calibration(services::CalibrationService &, const wire::v1::Request &, wire::v1::Response &);
}
