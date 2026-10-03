#pragma once
#include <mantis/pipeline_api.hpp>
namespace mantis::compute {
struct ComputeDevice {
    Id id;
    std::string backend;
    uint64_t memory_bytes{};
    uint32_t threads{};
};
struct WorkerDescriptor {
    Id id;
    std::vector<ComputeDevice> devices;
    std::vector<std::string> algorithms;
    uint32_t protocol_version{1};
};
inline Result<std::string> select(const pipeline::Resources &r) {
    if (r.location == pipeline::Location::remote || r.location == pipeline::Location::device)
        return std::unexpected(
            Error{Status::unsupported, "Requested execution location unavailable in v0.1", "compute"});
    for (auto &b : r.backends)
        if (b == "cpu")
            return b;
    return std::unexpected(
        Error{Status::unsupported, "No compatible backend (v0.1 provides CPU)", "compute"});
}
} // namespace mantis::compute
