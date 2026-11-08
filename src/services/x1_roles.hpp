#pragma once
#include "../../plugins/first-party/devices/x1/role_config.hpp"
#include <mantis/artifact_store.hpp>
#include <mantis/device_runtime.hpp>
#include <mantis/jobs.hpp>
#include <mantis/x1_role_service.hpp>
namespace mantis::services {
// Control entry is serialized. The Job exclusively owns device mutation while reserved.
class X1Roles {
    x1::roles::Store store_;
    x1::roles::Store::Checkpoint checkpoint_;
    plugins::Registry &registry_;
    jobs::Manager &jobs_;
    std::unique_ptr<device::Runtime> &devices_;
    mutable std::mutex mutex_;
    X1RoleInfo info_;
    std::vector<device::Descriptor> cached_;
    struct Permit;
    X1RoleInfo inspect(const std::vector<device::Descriptor> &, x1::roles::Mapping) const;
    void publish(X1RoleInfo, std::vector<device::Descriptor>);

  public:
    X1Roles(const std::filesystem::path &, const std::filesystem::path &, plugins::Registry &,
            jobs::Manager &, std::unique_ptr<device::Runtime> &, x1::roles::Store::Checkpoint);
    X1RoleInfo query() const;
    std::vector<device::Descriptor> devices() const;
    void guard() const;
    Id submit(const X1RoleRequest &, bool apply, std::shared_ptr<artifact::Store>,
              std::function<void()> quiescent);
};
} // namespace mantis::services
