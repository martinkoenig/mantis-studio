#pragma once
#include <array>
#include <mantis/base.hpp>
#include <vector>
namespace mantis::services {
inline constexpr const char *x1_role_capability = "org.mantis.x1.role-config.v1";
struct X1RoleCamera {
    std::string role, sensor, bus, identity, video_node;
    std::vector<std::string> route;
};
struct X1RoleInfo {
    uint32_t schema_version{1};
    bool supported{}, busy{}, blocked{}, swapped{};
    uint64_t generation{};
    std::string discovery_token, parent_id, base_digest, diagnostic;
    Id job;
    std::array<X1RoleCamera, 2> cameras;
};
struct X1RoleRequest {
    uint32_t schema_version{1};
    uint64_t expected_generation{};
    std::string discovery_token, parent_id;
    bool swapped{}; // Desired order relative to the provisioned base; never a toggle.
};
} // namespace mantis::services
