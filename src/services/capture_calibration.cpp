#include <mantis/capture_calibration.hpp>

namespace mantis::services {
namespace {
bool same(const calibration::Reference &a, const calibration::Reference &b) {
    return a.id == b.id && a.schema_version == b.schema_version && a.revision == b.revision;
}
} // namespace
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
