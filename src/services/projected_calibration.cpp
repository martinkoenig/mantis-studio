#include "projected_calibration.hpp"
#include <set>

namespace mantis::services {
namespace {
bool same(const calibration::Reference &a, const calibration::Reference &b) {
    return a.id == b.id && a.schema_version == b.schema_version && a.revision == b.revision;
}
bool same(const data::ExactCalibrationReference &a, const data::ExactCalibrationReference &b) {
    if (!same(a.calibration, b.calibration) || a.content.presence() != b.content.presence())
        return false;
    auto x = a.content.get(), y = b.content.get();
    return !x || (x->id == y->id && x->type == y->type && x->revision == y->revision && x->hash == y->hash);
}
void check(bool value, std::string_view message) {
    if (!value)
        fail(Status::incompatible, std::string(message), "projected-calibration");
}
void reference(data::Metadata &p, const std::string &prefix, const calibration::Reference &r) {
    p[prefix + "id"] = r.id.value;
    p[prefix + "schema_version"] = std::to_string(r.schema_version);
    p[prefix + "revision"] = std::to_string(r.revision);
}
std::string presence(data::Presence p) {
    switch (p) {
    case data::Presence::unknown:
        return "unknown";
    case data::Presence::unavailable:
        return "unavailable";
    case data::Presence::established:
        return "established";
    }
    fail(Status::invalid_argument, "Invalid calibration presence");
}
void reference(data::Metadata &p, const std::string &prefix,
               const data::Evidence<data::ExactCalibrationReference> &r) {
    p[prefix + "presence"] = presence(r.presence());
    if (auto exact = r.get()) {
        reference(p, prefix, exact->calibration);
        p[prefix + "content_presence"] = presence(exact->content.presence());
        if (auto content = exact->content.get()) {
            p[prefix + "artifact_id"] = content->id.value;
            p[prefix + "artifact_type"] = content->type.name;
            p[prefix + "artifact_schema_version"] = std::to_string(content->type.version);
            p[prefix + "artifact_revision"] = std::to_string(content->revision);
            p[prefix + "hash_presence"] = presence(content->hash.presence());
            if (auto hash = content->hash.get()) {
                p[prefix + "hash_algorithm"] = hash->algorithm;
                p[prefix + "hash"] = hash->hex;
            }
        }
    }
}
class BoundExecutor final : public device::ProjectedExecutor {
    std::unique_ptr<device::ProjectedExecutor> executor_;
    std::shared_ptr<ProjectedCalibrationBinding> binding_;

  public:
    BoundExecutor(std::unique_ptr<device::ProjectedExecutor> e,
                  std::shared_ptr<ProjectedCalibrationBinding> b)
        : executor_(std::move(e)), binding_(std::move(b)) {}
    const device::ProjectedGraph &graph() const override { return executor_->graph(); }
    Result<device::ProgramValidation> validate(const data::AcquisitionProgram &p, uint32_t t) override {
        return executor_->validate(p, t);
    }
    Result<device::ProgramValidation> prepare(const data::AcquisitionProgram &p, uint32_t t) override {
        return executor_->prepare(p, t);
    }
    Result<void> start(const data::RunId &r, const data::GenerationId &g, uint32_t t) override {
        return executor_->start(r, g, t);
    }
    Result<std::optional<data::AcquisitionBundle>> next(uint32_t t) override {
        auto result = executor_->next(t);
        if (!result || !*result)
            return result;
        try {
            return std::optional{binding_->stamp(std::move(**result))};
        } catch (const Failure &e) {
            return std::unexpected(e.error);
        }
    }
    Result<device::ProjectedStatus> status(uint32_t t) override { return executor_->status(t); }
    // Abort never takes the calibration/provenance mutex and never waits on storage.
    Result<device::AbortOutcome> abort(data::AcquisitionReason r, uint32_t t) override {
        return executor_->abort(r, t);
    }
    Result<void> stop(uint32_t t) override { return executor_->stop(t); }
    Result<void> close(uint32_t t) override { return executor_->close(t); }
    Result<std::string> diagnostics(uint32_t t) override { return executor_->diagnostics(t); }
};
} // namespace
ProjectedCalibrationBinding::ProjectedCalibrationBinding(const artifact::Store &store,
                                                         const device::ProjectedGraph &graph,
                                                         const data::AcquisitionProgram &program)
    : snapshot_(store.active_calibration(graph.parent)) {
    for (const auto &camera : program.participants.cameras)
        cameras_.push_back(camera.component);
    check(cameras_.size() <= data::max_participants, "Too many projected calibration participants");
    if (!snapshot_)
        return;
    auto a = store.get(snapshot_->artifact.id);
    check(a.state == artifact::ArtifactState::finalized && a.type.name == "org.mantis.RigCalibration" &&
              a.type.schema_version == 1,
          "Active binding requires finalized RigCalibration schema 1");
    if (a.hash != snapshot_->artifact.hash)
        fail(Status::corrupt, "Active RigCalibration hash mismatch", "projected-calibration");
    auto loaded = calibration::artifacts::load_rig_calibration(store, a.id);
    if (!loaded)
        throw Failure(loaded.error());
    check(same(loaded->revision, snapshot_->reference), "Active RigCalibration logical revision mismatch");
    // A program may use one camera from a stereo rig. Match declared image participants,
    // not every graph child, and require each selected camera's exact physical geometry.
    std::set<std::string> roles;
    for (const auto &camera : program.participants.cameras) {
        auto c = std::find_if(graph.components.begin(), graph.components.end(), [&](const auto &component) {
            return component.descriptor.id == camera.component.id;
        });
        check(c != graph.components.end() && c->kind == device::ParticipantKind::image &&
                  device::image_participant(c->descriptor) && c->image_source &&
                  c->image_source->stream == camera.stream && c->role == camera.role,
              "Calibration participant must match a typed projected image source");
        check(roles.insert(c->role).second, "Duplicate calibrated camera role");
        const auto &left = loaded->solution.left_camera, &right = loaded->solution.right_camera;
        const auto *calibrated = c->role == left.role ? &left : c->role == right.role ? &right : nullptr;
        check(calibrated, "RigCalibration camera role does not match projected participant");
        check(calibrated->camera_id.value == c->image_source->physical_identity,
              "RigCalibration physical camera identity does not match projected participant");
        check(calibrated->image_width == c->image_source->width &&
                  calibrated->image_height == c->image_source->height,
              "RigCalibration image geometry does not match projected participant");
    }
}
void ProjectedCalibrationBinding::provenance(artifact::Provenance &p) const {
    if (!snapshot_)
        return;
    p.calibration = snapshot_->reference;
    if (std::find(p.inputs.begin(), p.inputs.end(), snapshot_->artifact.id) == p.inputs.end())
        p.inputs.push_back(snapshot_->artifact.id);
    reference(p.parameters, "active_calibration_", snapshot_->reference);
    p.parameters["active_rig_artifact_id"] = snapshot_->artifact.id.value;
    p.parameters["active_rig_artifact_hash_algorithm"] = snapshot_->artifact.hash.algorithm;
    p.parameters["active_rig_artifact_hash"] = snapshot_->artifact.hash.hex;
}
data::AcquisitionBundle ProjectedCalibrationBinding::stamp(data::AcquisitionBundle b) {
    std::lock_guard guard(mutex_);
    if (!initial_) {
        data::Metadata fields;
        reference(fields, "source_initial_rig_calibration_", b.evidence.rig_calibration);
        audit_.push_back({b.key.sequence.value, std::move(fields)});
        initial_ = true;
    }
    auto validate_exact = [&](const data::Evidence<data::ExactCalibrationReference> &r,
                              std::optional<data::ExactCalibrationReference> &previous) {
        if (auto exact = r.get()) {
            check(!previous || same(*previous, *exact),
                  "Source rig calibration reference changed during projected capture");
            previous = *exact;
            if (source_)
                check(same(exact->calibration, *source_),
                      "Source rig evidence and FrameSet calibration disagree");
        }
    };
    const bool first_exact = !source_rig_ && b.evidence.rig_calibration.get();
    validate_exact(b.evidence.rig_calibration, source_rig_);
    if (first_exact) {
        data::Metadata fields;
        reference(fields, "source_established_rig_calibration_", b.evidence.rig_calibration);
        audit_.push_back({b.key.sequence.value, std::move(fields)});
    }
    if (b.frameset) {
        const auto original = b.frameset->header.calibration;
        for (const auto &image : b.frameset->frames)
            check(image && same(image->header.calibration, original),
                  "Source FrameSet/child calibration references disagree");
        check(!source_ || same(*source_, original),
              "Source FrameSet calibration changed during projected capture");
        if (source_rig_)
            check(same(source_rig_->calibration, original),
                  "Source rig evidence and FrameSet calibration disagree");
        for (const auto &[camera, exact] : camera_rigs_)
            check(same(exact.calibration, original),
                  "Source camera rig evidence and FrameSet calibration disagree");
        if (!source_) {
            data::Metadata fields;
            reference(fields, "source_device_calibration_", original);
            reference(fields, "source_frameset_rig_calibration_", b.evidence.rig_calibration);
            audit_.push_back({b.key.sequence.value, std::move(fields)});
            source_ = original;
        }
        if (snapshot_) {
            auto frameset = *b.frameset;
            frameset.header.calibration = snapshot_->reference;
            frameset.frames.clear();
            for (const auto &image : b.frameset->frames) {
                auto bound = *image;
                bound.header.calibration = snapshot_->reference;
                frameset.frames.push_back(data::publish(std::move(bound)));
            }
            b.frameset = data::publish(std::move(frameset));
        }
    }
    // Late CameraFrameEvidence may arrive without an actual FrameSet. Validate and audit
    // the original references before replacing them, once per declared camera/state.
    for (const auto &frame : b.evidence.frames) {
        auto camera = std::find(cameras_.begin(), cameras_.end(), frame.frame.camera);
        check(camera != cameras_.end(), "Undeclared source calibration camera");
        const auto prefix = "source_camera_" + std::to_string(camera - cameras_.begin()) + "_";
        if (auto exact = frame.rig_calibration.get()) {
            if (source_)
                check(same(exact->calibration, *source_),
                      "Source camera rig evidence and FrameSet calibration disagree");
            if (source_rig_)
                check(same(exact->calibration, source_rig_->calibration),
                      "Source camera and acquisition rig calibration disagree");
            for (const auto &[id, previous] : camera_rigs_)
                check(same(exact->calibration, previous.calibration),
                      "Source camera rig calibration references disagree");
            auto [previous, inserted] = camera_rigs_.emplace(frame.frame.camera, *exact);
            check(inserted || same(previous->second, *exact),
                  "Source camera rig calibration reference changed during capture");
            if (inserted) {
                data::Metadata fields;
                reference(fields, prefix + "established_rig_calibration_", frame.rig_calibration);
                audit_.push_back({b.key.sequence.value, std::move(fields)});
            }
        }
        if (seen_cameras_.insert(frame.frame.camera).second) {
            data::Metadata fields;
            fields[prefix + "component"] = frame.frame.camera.id.value;
            reference(fields, prefix + "rig_calibration_", frame.rig_calibration);
            audit_.push_back({b.key.sequence.value, std::move(fields)});
        }
    }
    if (source_rig_)
        for (const auto &[camera, exact] : camera_rigs_)
            check(same(exact.calibration, source_rig_->calibration),
                  "Source camera and acquisition rig calibration disagree");
    if (snapshot_) {
        const data::ExactCalibrationReference bound{
            snapshot_->reference,
            data::ContentReference{snapshot_->artifact.id,
                                   {"org.mantis.RigCalibration", snapshot_->reference.schema_version},
                                   snapshot_->artifact.hash,
                                   snapshot_->reference.revision}};
        b.evidence.rig_calibration = bound;
        for (auto &frame : b.evidence.frames)
            frame.rig_calibration = bound;
    }
    return b;
}
data::Metadata ProjectedCalibrationBinding::source_provenance(data::BundleSequence sequence) {
    std::lock_guard guard(mutex_);
    data::Metadata fields;
    for (auto it = audit_.begin(); it != audit_.end();) {
        if (it->sequence <= sequence.value) {
            fields.merge(it->fields);
            it = audit_.erase(it);
        } else
            ++it;
    }
    return fields;
}
std::unique_ptr<device::ProjectedExecutor>
calibration_bound_executor(std::unique_ptr<device::ProjectedExecutor> e,
                           std::shared_ptr<ProjectedCalibrationBinding> b) {
    return std::make_unique<BoundExecutor>(std::move(e), std::move(b));
}
} // namespace mantis::services
