#include <charconv>
#include <mantis/capture_calibration.hpp>
#include <mantis/projected_light_device.hpp>

namespace mantis::services {
namespace {
bool same(const calibration::Reference &a, const calibration::Reference &b) {
    return a.id == b.id && a.schema_version == b.schema_version && a.revision == b.revision;
}
} // namespace
Result<calibration::artifacts::CameraComponent>
discovered_calibration_component(const device::Descriptor &child) {
    try {
        const auto role = child.metadata.find("role"), identity = child.metadata.find("identity");
        if (role == child.metadata.end() || role->second.empty() || identity == child.metadata.end() ||
            identity->second.empty())
            fail(Status::incompatible,
                 "Discovered calibration component '" + child.id.value + "' lacks role/physical identity",
                 "capture");
        auto dimension = [&](const char *field) {
            const auto it = child.metadata.find(field);
            const std::string context = "Discovered " + role->second + " component '" + child.id.value + "' ";
            if (it == child.metadata.end())
                fail(Status::incompatible, context + "is missing required " + field, "capture");
            const auto &value = it->second;
            uint32_t parsed{};
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
            if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                !parsed)
                fail(Status::incompatible,
                     context + "has invalid " + field + " '" + value +
                         "': expected a positive uint32 decimal",
                     "capture");
            return parsed;
        };
        return calibration::artifacts::CameraComponent{
            role->second, {identity->second}, dimension("width"), dimension("height")};
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    }
}
Result<std::vector<calibration::artifacts::CameraComponent>> discovered_activation_components(
    const device::Descriptor &parent, std::span<const device::Descriptor> current) {
    std::vector<calibration::artifacts::CameraComponent> components;
    for (const auto &child : parent.children) {
        auto descriptor = std::find_if(current.begin(), current.end(), [&](const auto &d) { return d.id == child; });
        if (descriptor == current.end())
            return std::unexpected(Error{Status::incompatible, "Discovered calibration component descriptor is missing", "calibration"});
        if (!device::image_participant(*descriptor)) continue;
        auto component = discovered_calibration_component(*descriptor);
        if (!component) return std::unexpected(component.error());
        components.push_back(std::move(*component));
    }
    if (parent.children.empty() && device::image_participant(parent) && parent.metadata.contains("role")) {
        auto component = discovered_calibration_component(parent);
        if (!component) return std::unexpected(component.error());
        components.push_back(std::move(*component));
    }
    return components;
}
CaptureCalibrationBinding::CaptureCalibrationBinding(std::shared_ptr<artifact::Store> store, Id raw,
                                                     std::optional<artifact::ActiveCalibration> snapshot)
    : store_(std::move(store)), raw_(std::move(raw)), snapshot_(std::move(snapshot)) {}
data::Published CaptureCalibrationBinding::stamp(data::Published packet) {
    if (!snapshot_)
        return packet;
    std::lock_guard guard(mutex_);
    if (!packet || packet->type != schema::frameset)
        fail(Status::incompatible, "Project RigCalibration binding requires FrameSet acquisition", "capture");
    const auto original = packet->header.calibration;
    for (const auto &child : packet->frames)
        if (!child || child->type != schema::image || !same(child->header.calibration, original))
            fail(Status::incompatible,
                 "Source parent/child calibration references disagree before project override", "capture");
    if (source_ && !same(*source_, original))
        fail(Status::incompatible, "Source device calibration reference changed during project-bound capture",
             "capture");
    if (!source_) {
        auto provenance = store_->get(raw_).provenance;
        provenance.parameters["source_device_calibration_id"] = original.id.value;
        provenance.parameters["source_device_calibration_schema_version"] =
            std::to_string(original.schema_version);
        provenance.parameters["source_device_calibration_revision"] = std::to_string(original.revision);
        provenance.calibration = snapshot_->reference;
        store_->initialize_provenance(raw_, std::move(provenance));
        source_ = original;
    }
    data::Packet bound = *packet;
    bound.header.calibration = snapshot_->reference;
    bound.frames.clear();
    for (const auto &child : packet->frames) {
        data::Packet image = *child;
        image.header.calibration = snapshot_->reference;
        bound.frames.push_back(data::publish(std::move(image)));
    }
    // Packet/attribute containers copied; BufferView storage ownership and pixel bytes are shared unchanged.
    return data::publish(std::move(bound));
}
} // namespace mantis::services
