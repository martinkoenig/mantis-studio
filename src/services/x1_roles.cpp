#include "x1_roles.hpp"
#include <algorithm>
#include <mantis/calibration_artifacts.hpp>
#include <mantis/capture_calibration.hpp>
namespace mantis::services {
namespace {
namespace ca = calibration::artifacts;
using Json = nlohmann::json;
std::string parent(const Json &base, bool swapped) {
    auto p = x1::roles::effective(base, {0, swapped});
    const auto id = [&](const char *role) {
        const auto &camera = p.at("measurement_cameras").at(role);
        return camera.at("bus_identity").get<std::string>() + "/" +
               camera.at("sensor_identity").get<std::string>();
    };
    return "org.mantis.x1:" + id("left") + ":" + id("right");
}
void require(bool condition, std::string message) {
    if (!condition)
        fail(Status::incompatible, std::move(message), "x1-role-config");
}
} // namespace
struct X1Roles::Permit {
    X1Roles &owner;
    ~Permit() {
        std::lock_guard lock(owner.mutex_);
        owner.info_.busy = false;
        if (owner.info_.diagnostic == "Configuration validation/apply incomplete")
            owner.info_.diagnostic = "Configuration Job retired without a committed change";
    }
};
X1Roles::X1Roles(const std::filesystem::path &root, const std::filesystem::path &profile,
                 plugins::Registry &registry, jobs::Manager &jobs, std::unique_ptr<device::Runtime> &devices,
                 x1::roles::Store::Checkpoint hook)
    : store_(root, profile, std::move(hook)), registry_(registry), jobs_(jobs), devices_(devices) {
    cached_ = devices_->list();
    info_ = inspect(cached_, store_.mapping());
}
X1RoleInfo X1Roles::inspect(const std::vector<device::Descriptor> &descriptors,
                            x1::roles::Mapping mapping) const {
    X1RoleInfo info;
    info.supported = true;
    info.swapped = mapping.swapped;
    info.generation = mapping.generation;
    info.base_digest = store_.base_digest();
    info.parent_id = parent(store_.base(), mapping.swapped);
    info.discovery_token = Id::random().value;
    const auto found = std::find_if(descriptors.begin(), descriptors.end(), [&](const auto &d) {
        return d.plugin_id == "org.mantis.x1" && d.id.value == info.parent_id && d.parent.value.empty();
    });
    require(found != descriptors.end(), "Configured X1 physical pair is unavailable");
    const auto metadata = [&](const char *key) -> std::string {
        auto field = found->metadata.find(key);
        require(field != found->metadata.end(), "X1 role-aware discovery is unavailable");
        return field->second;
    };
    require(metadata("x1_role_base_digest") == info.base_digest &&
                metadata("x1_role_generation") == std::to_string(mapping.generation) &&
                metadata("x1_role_swapped") == (mapping.swapped ? "true" : "false"),
            "X1 role discovery configuration mismatch");
    const auto effective = x1::roles::effective(store_.base(), mapping);
    for (size_t i = 0; i < 2; ++i) {
        auto &c = info.cameras[i];
        c.role = i ? "right" : "left";
        const auto &expected = effective.at("measurement_cameras").at(c.role);
        c.bus = expected.at("bus_identity");
        c.sensor = expected.at("sensor_identity");
        c.identity = c.bus + "/" + c.sensor;
        c.route = expected.at("route").get<std::vector<std::string>>();
        size_t count = 0;
        for (const auto &child : descriptors) {
            if (child.parent != found->id || child.plugin_id != found->plugin_id)
                continue;
            auto role = child.metadata.find("role");
            if (role == child.metadata.end() || role->second != c.role)
                continue;
            ++count;
            require(child.metadata.at("identity") == c.identity && child.metadata.at("bus") == c.bus &&
                        child.metadata.at("sensor") == c.sensor &&
                        Json::parse(child.metadata.at("route")) == c.route,
                    "Discovered X1 bus/sensor/route mismatch");
            c.video_node = child.metadata.at("video_node");
            require(!c.video_node.empty(), "Discovered X1 capture node is missing");
        }
        require(count == 1, "X1 requires exactly one discovered component per role");
    }
    require(info.cameras[0].video_node != info.cameras[1].video_node, "X1 roles share a capture node");
    return info;
}
X1RoleInfo X1Roles::query() const {
    std::lock_guard lock(mutex_);
    return info_;
}
std::vector<device::Descriptor> X1Roles::devices() const {
    std::lock_guard lock(mutex_);
    auto descriptors = cached_;
    if (!info_.blocked)
        for (auto &d : descriptors)
            if (d.id.value == info_.parent_id)
                d.capabilities.push_back(x1_role_capability);
    return descriptors;
}
void X1Roles::guard() const {
    auto info = query();
    if (info.busy)
        fail(Status::busy, "X1 role configuration Job retains the physical-pair reservation",
             "x1-role-config");
    if (info.blocked)
        fail(Status::incompatible, info.diagnostic, "x1-role-config");
}
void X1Roles::publish(X1RoleInfo info, std::vector<device::Descriptor> descriptors) {
    std::lock_guard lock(mutex_);
    info.busy = info_.busy;
    info.job = info_.job;
    info_ = std::move(info);
    cached_ = std::move(descriptors);
}
Id X1Roles::submit(const X1RoleRequest &request, bool apply, std::shared_ptr<artifact::Store> project,
                   std::function<void()> quiescent) {
    guard();
    auto before = query();
    if (request.discovery_token.size() > 128 || request.parent_id.size() > 2048)
        fail(Status::invalid_argument, "X1 role request identity exceeds bounds", "x1-role-config");
    require(request.schema_version == 1, "Unsupported X1 role request version");
    require(request.expected_generation == before.generation &&
                request.discovery_token == before.discovery_token && request.parent_id == before.parent_id,
            "Stale X1 role discovery/parent/generation");
    quiescent();
    if (jobs_.busy())
        fail(Status::busy, "X1 role configuration requires idle Jobs", "x1-role-config");
    auto permit = std::make_shared<Permit>(*this);
    {
        std::lock_guard lock(mutex_);
        info_.busy = true;
        info_.diagnostic = "Configuration validation/apply incomplete";
    }
    auto id = jobs_.submit(
        apply ? "Apply X1 camera roles" : "Validate X1 camera roles",
        [this, request, apply, project = std::move(project), quiescent = std::move(quiescent), permit,
         before](jobs::Context &context) -> std::optional<artifact::ArtifactReference> {
            // Queued cancellation destroys permit without invoking this task. Once entered,
            // cancellation cannot interrupt a filesystem commit or fabricate returned ownership.
            for (const auto &job : jobs_.list())
                if (job.id != context.id &&
                    (job.state == jobs::State::queued || job.state == jobs::State::running))
                    fail(Status::busy, "Another Job prevents X1 configuration", "x1-role-config");
            quiescent();
            try {
                context.update(0.1, "Validating fresh X1 topology and calibration");
                auto fresh = std::make_unique<device::Runtime>(registry_);
                auto descriptors = fresh->list();
                auto observed = inspect(descriptors, {before.generation, before.swapped});
                for (size_t i = 0; i < 2; ++i)
                    require(observed.cameras[i].video_node == before.cameras[i].video_node,
                            "X1 node resolution changed since query; query refreshed state");
                if (request.swapped != before.swapped)
                    require(!project->active_calibration({before.parent_id}),
                            "Clear the current X1 active rig explicitly before changing roles");
                const auto prospective = parent(store_.base(), request.swapped);
                if (auto binding = project->active_calibration({prospective})) {
                    auto rig = ca::load_rig_calibration(*project, binding->artifact.id);
                    if (!rig)
                        throw Failure(rig.error());
                    std::vector<ca::CameraComponent> components;
                    for (size_t i = 0; i < 2; ++i) {
                        const auto &camera = before.cameras[request.swapped == before.swapped ? i : 1 - i];
                        const auto &mode = store_.base().at("mode");
                        components.push_back({i ? "right" : "left",
                                              {camera.identity},
                                              mode.at("width").get<uint32_t>(),
                                              mode.at("height").get<uint32_t>()});
                    }
                    auto valid = ca::validate_rig_device(*rig, {prospective}, components);
                    if (!valid)
                        throw Failure(valid.error());
                }
                // Stopped adapters can still retain the process-local X1 lease. Destroy
                // them here, on the worker, after proven Session retirement, never on control.
                devices_.reset();
                fresh.reset();
                auto refresh = [&](x1::roles::Mapping mapping) {
                    auto replacement = std::make_unique<device::Runtime>(registry_);
                    auto list = replacement->list();
                    auto state = inspect(list, mapping);
                    devices_ = std::move(replacement);
                    publish(std::move(state), std::move(list));
                };
                if (apply)
                    store_.apply({before.generation, before.swapped}, request.swapped, refresh);
                else
                    store_.apply({before.generation, before.swapped}, before.swapped, refresh);
                context.update(1, apply ? "X1 roles applied durably" : "X1 role proposal validated");
                return {};
            } catch (...) {
                const auto primary = std::current_exception();
                bool uncertain = false;
                try {
                    std::rethrow_exception(primary);
                } catch (const x1::roles::Error &error) {
                    uncertain = error.uncertain;
                } catch (...) {
                }
                if (!uncertain) {
                    try {
                        auto replacement = std::make_unique<device::Runtime>(registry_);
                        auto descriptors = replacement->list();
                        auto state = inspect(descriptors, store_.mapping());
                        devices_.reset();
                        devices_ = std::move(replacement);
                        publish(std::move(state), std::move(descriptors));
                    } catch (...) {
                        uncertain = true;
                    }
                }
                if (uncertain) {
                    devices_.reset();
                    std::lock_guard lock(mutex_);
                    info_.blocked = true;
                    info_.diagnostic =
                        "X1 configuration/retirement is indeterminate; restart/reconciliation required";
                    cached_.clear();
                }
                std::rethrow_exception(primary);
            }
        });
    {
        std::lock_guard lock(mutex_);
        info_.job = id;
    }
    return id;
}
} // namespace mantis::services
