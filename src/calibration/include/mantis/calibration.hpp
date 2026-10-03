#pragma once
#include <mantis/spatial.hpp>
#include <mantis/time.hpp>
namespace mantis::calibration {
struct Reference {
    Id id;
    uint32_t schema_version{1};
    uint64_t revision{};
};
struct Calibration {
    Reference reference;
    spatial::TransformGraph transforms;
    std::map<std::string, std::vector<double>> parameters;
    std::string validity;
};
using Snapshot = std::shared_ptr<const Calibration>;
} // namespace mantis::calibration
