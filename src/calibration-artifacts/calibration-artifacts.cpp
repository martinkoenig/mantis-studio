#include "codecs.hpp"
#include <cstring>
#include <set>

namespace mantis::calibration::artifacts {
namespace {
using codec::Json;
template <class F> auto boundary(F f) -> Result<decltype(f())> {
    try {
        return f();
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    } catch (const std::filesystem::filesystem_error &e) {
        return std::unexpected(Error{Status::io, e.what(), "calibration-artifacts"});
    } catch (const Json::exception &e) {
        return std::unexpected(Error{Status::corrupt, std::string("Invalid calibration JSON: ") + e.what(),
                                     "calibration-artifacts"});
    }
}
void checked(Result<void> result, bool loading = false) {
    if (!result) {
        auto error = result.error();
        if (loading)
            error.code = Status::corrupt;
        throw Failure(std::move(error));
    }
}
bool same_reference(const Reference &a, const Reference &b) {
    return a.id == b.id && a.schema_version == b.schema_version && a.revision == b.revision;
}
bool same_artifact(const artifact::ArtifactReference &a, const artifact::ArtifactReference &b) {
    return a.id == b.id && a.hash == b.hash;
}
void revision_valid(const Reference &r) {
    codec::require(!r.id.value.empty() && r.revision > 0 && r.schema_version == 1,
                   "Invalid calibration logical identity/revision/schema");
}
void implementation_valid(const SolverImplementation &p) {
    codec::require(!p.opencv_version.empty() && !p.mantis_build.empty(),
                   "Solver implementation provenance is required");
}
void valid(const TargetArtifact &v, bool loading = false) {
    revision_valid(v.revision);
    checked(validate_target(v.target), loading);
    codec::require(v.target.identity.id == v.revision.id && v.target.identity.revision == v.revision.revision,
                   "Target payload and allocated identity differ");
}
void valid(const DatasetArtifact &v, bool loading = false) {
    revision_valid(v.revision);
    checked(validate_calibration_dataset(v.dataset), loading);
}
void valid(const CameraArtifact &v, bool loading = false) {
    revision_valid(v.revision);
    implementation_valid(v.implementation);
    checked(validate_camera_solution(v.solution), loading);
}
void valid(const RigArtifact &v, bool loading = false) {
    revision_valid(v.revision);
    implementation_valid(v.implementation);
    checked(validate_stereo_solution(v.solution), loading);
}
Json conventions() {
    return {{"target_pose", "T_camera_from_target"},
            {"transform", "row-major T_target_from_source"},
            {"stereo", "X_right = R_right_from_left * X_left + T_right_from_left"}};
}
template <class T> std::string document(const T &v, const artifact::ArtifactType &type) {
    valid(v);
    return Json{{"schema_version", 1},
                {"kind", type.name},
                {"conventions", conventions()},
                {"payload", codec::encode(v)}}
        .dump();
}
data::Published packet_document(const std::string &bytes) {
    data::Packet p;
    p.type =
        document_type; // Header defaults: zero clocks/sequence, unknown sync, empty calibration, world frame.
    memory::BufferBuilder b(bytes.size());
    std::memcpy(b.writable().data(), bytes.data(), bytes.size());
    p.attributes.push_back(
        {{std::string(document_attribute), schema::ScalarType::u8, {bytes.size()}, {1}, "utf8-json"},
         std::move(b).publish()});
    return data::publish(std::move(p));
}
void reference_valid(const artifact::Store &store, const artifact::ArtifactReference &ref,
                     const artifact::ArtifactType &type) {
    artifact::ArtifactDescriptor a;
    try {
        a = store.get(ref.id);
    } catch (const Failure &e) {
        if (e.error.code == Status::not_found)
            fail(Status::corrupt, "Missing upstream calibration artifact", "calibration-artifacts");
        throw;
    }
    codec::require(!ref.hash.hex.empty() && a.hash == ref.hash,
                   "Upstream calibration artifact hash mismatch");
    codec::require(a.type.name == type.name && a.type.schema_version == type.schema_version &&
                       a.state == artifact::ArtifactState::finalized,
                   "Invalid upstream calibration artifact type/schema/state");
}
std::vector<Id> inputs(const TargetArtifact &) {
    return {};
}
std::vector<Id> inputs(const DatasetArtifact &v) {
    std::vector<Id> ids{v.target_reference.id};
    for (auto &ref : v.raw_capture_references)
        ids.push_back(ref.id);
    return ids;
}
std::vector<Id> inputs(const CameraArtifact &v) {
    return {v.dataset_reference.id, v.target_reference.id};
}
std::vector<Id> inputs(const RigArtifact &v) {
    return {v.dataset_reference.id, v.target_reference.id, v.left_camera_reference.id,
            v.right_camera_reference.id};
}
template <class T> T read(const artifact::Store &store, const Id &id, const artifact::ArtifactType &type) {
    const auto a = store.get(id);
    if (a.type.name != type.name || a.type.schema_version != 1 ||
        a.state != artifact::ArtifactState::finalized)
        fail(Status::incompatible, "Load requires finalized " + type.name + " schema 1",
             "calibration-artifacts");
    codec::require(a.chunks == 1 && !a.hash.hex.empty(),
                   "Calibration artifact requires exactly one committed document chunk");
    const auto p = store.packet(id);
    codec::require(p->type == document_type && p->frames.empty() && p->attributes.size() == 1,
                   "Invalid calibration document packet type/schema/attributes");
    const auto &h = p->header;
    codec::require(h.sequence.value == 0 && h.timestamp.nanoseconds == 0 &&
                       h.timestamp.domain.id.value.empty() && h.timestamp.domain.name.empty() &&
                       h.received.nanoseconds == 0 && h.sync.id.value.empty() && h.sync.trigger == 0 &&
                       h.sync_quality == time::SyncQuality::unknown && h.calibration.id.value.empty() &&
                       h.calibration.schema_version == 1 && h.calibration.revision == 0 &&
                       h.metadata.empty() && solve_detail::same_frame(h.frame, spatial::world),
                   "Non-neutral calibration document packet header");
    const auto &attr = p->attributes[0];
    const auto &d = attr.descriptor;
    codec::require(d.name == document_attribute && d.scalar == schema::ScalarType::u8 &&
                       d.shape == std::vector<uint64_t>{attr.buffer.size()} &&
                       d.stride == std::vector<uint64_t>{1} && d.unit == "utf8-json",
                   "Invalid calibration document attribute layout");
    auto mapped = attr.buffer.map_read();
    if (!mapped)
        throw Failure(mapped.error());
    std::string bytes(reinterpret_cast<const char *>(mapped->data()), mapped->size());
    const auto j = Json::parse(bytes);
    codec::fields(j, {"schema_version", "kind", "conventions", "payload"});
    if (codec::decode<uint32_t>(j.at("schema_version")) != 1)
        fail(Status::incompatible, "Unsupported calibration document schema", "calibration-artifacts");
    codec::require(j.at("kind") == type.name && j.at("conventions") == conventions(),
                   "Calibration document kind/conventions mismatch");
    // Enforces UTF-8, shortest round-trip numbers, sorted unique object keys, and no whitespace/duplicate
    // JSON keys.
    codec::require(j.dump() == bytes, "Calibration document is not canonical UTF-8 JSON");
    auto v = codec::decode<T>(j.at("payload"));
    valid(v, true);
    codec::require(document(v, type) == bytes, "Calibration document is not canonical for its typed schema");
    artifact::CalibrationRevision r;
    try {
        r = store.artifact_revision(id);
    } catch (const Failure &e) {
        if (e.error.code == Status::not_found)
            fail(Status::corrupt, "Calibration artifact is absent from revision registry",
                 "calibration-artifacts");
        throw;
    }
    codec::require(same_reference(v.revision, r.reference) && r.artifact_id == a.id && r.kind == type.name &&
                       same_reference(a.provenance.calibration, v.revision),
                   "Calibration payload/registry/provenance revision mismatch");
    codec::require(a.provenance.inputs == inputs(v), "Calibration payload/provenance inputs mismatch");
    return v;
}
TargetArtifact target(const artifact::Store &s, const Id &id) {
    return read<TargetArtifact>(s, id, target_type);
}
DatasetArtifact dataset(const artifact::Store &s, const Id &id) {
    auto v = read<DatasetArtifact>(s, id, dataset_type);
    reference_valid(s, v.target_reference, target_type);
    auto t = target(s, v.target_reference.id);
    codec::require(solve_detail::same_target(v.dataset.target, t.target),
                   "Dataset and persisted target revision/content mismatch");
    codec::require(v.raw_capture_references.size() == v.dataset.raw_capture_ids.size(),
                   "Dataset source reference count mismatch");
    for (size_t i = 0; i < v.raw_capture_references.size(); ++i) {
        codec::require(v.raw_capture_references[i].id == v.dataset.raw_capture_ids[i],
                       "Dataset source reference ordering mismatch");
        reference_valid(s, v.raw_capture_references[i], {"org.mantis.RawCapture", 2});
    }
    return v;
}
CameraArtifact camera(const artifact::Store &s, const Id &id) {
    auto v = read<CameraArtifact>(s, id, camera_type);
    reference_valid(s, v.dataset_reference, dataset_type);
    reference_valid(s, v.target_reference, target_type);
    auto d = dataset(s, v.dataset_reference.id);
    codec::require(same_artifact(d.target_reference, v.target_reference),
                   "Camera and dataset target references differ");
    checked(validate_camera_solution(v.solution, d.dataset), true);
    return v;
}
RigArtifact rig(const artifact::Store &s, const Id &id) {
    auto v = read<RigArtifact>(s, id, rig_type);
    reference_valid(s, v.dataset_reference, dataset_type);
    reference_valid(s, v.target_reference, target_type);
    reference_valid(s, v.left_camera_reference, camera_type);
    reference_valid(s, v.right_camera_reference, camera_type);
    auto d = dataset(s, v.dataset_reference.id);
    auto l = camera(s, v.left_camera_reference.id), r = camera(s, v.right_camera_reference.id);
    codec::require(same_artifact(v.target_reference, d.target_reference) &&
                       same_artifact(l.target_reference, v.target_reference) &&
                       same_artifact(r.target_reference, v.target_reference) &&
                       same_artifact(l.dataset_reference, v.dataset_reference) &&
                       same_artifact(r.dataset_reference, v.dataset_reference),
                   "Rig upstream target/dataset references disagree");
    checked(validate_stereo_solution(v.solution, d.dataset, l.solution, r.solution), true);
    return v;
}
artifact::Provenance provenance(std::string producer, std::vector<Id> dependencies = {}) {
    artifact::Provenance p;
    p.producer = std::move(producer);
    p.version = application_version;
    p.inputs = std::move(dependencies);
    return p;
}
template <class T>
Stored<T> write(artifact::Store &s, T v, const artifact::ArtifactType &type, std::string producer,
                std::optional<Id> series) {
    auto reserved = s.begin_calibration(type, provenance(std::move(producer), inputs(v)), std::move(series));
    v.revision = reserved.reference;
    try {
        s.append(reserved.artifact_id, *packet_document(document(v, type)));
        return {s.finalize(reserved.artifact_id), std::move(v)};
    } catch (...) {
        s.abandon(reserved.artifact_id);
        throw;
    }
}
} // namespace
Result<std::string> encode_document(const TargetArtifact &v) {
    return boundary([&] { return document(v, target_type); });
}
Result<std::string> encode_document(const DatasetArtifact &v) {
    return boundary([&] { return document(v, dataset_type); });
}
Result<std::string> encode_document(const CameraArtifact &v) {
    return boundary([&] { return document(v, camera_type); });
}
Result<std::string> encode_document(const RigArtifact &v) {
    return boundary([&] { return document(v, rig_type); });
}
Result<TargetArtifact> load_calibration_target(const artifact::Store &s, const Id &id) {
    return boundary([&] { return target(s, id); });
}
Result<DatasetArtifact> load_calibration_dataset(const artifact::Store &s, const Id &id) {
    return boundary([&] { return dataset(s, id); });
}
Result<CameraArtifact> load_camera_calibration(const artifact::Store &s, const Id &id) {
    return boundary([&] { return camera(s, id); });
}
Result<RigArtifact> load_rig_calibration(const artifact::Store &s, const Id &id) {
    return boundary([&] { return rig(s, id); });
}
Result<Stored<TargetArtifact>> create_calibration_target(artifact::Store &s, CalibrationTarget t,
                                                         std::optional<Id> series) {
    return boundary([&] {
        checked(validate_target(t));
        if (!t.identity.id.value.empty() || t.identity.revision)
            fail(Status::invalid_argument,
                 "Create target requires unassigned identity; persistence allocates it before M3/M4",
                 "calibration-artifacts");
        auto reserved =
            s.begin_calibration(target_type, provenance("org.mantis.calibration.target"), std::move(series));
        t.identity = {reserved.reference.id, reserved.reference.revision};
        TargetArtifact v{reserved.reference, std::move(t)};
        try {
            s.append(reserved.artifact_id, *packet_document(document(v, target_type)));
            return Stored<TargetArtifact>{s.finalize(reserved.artifact_id), std::move(v)};
        } catch (...) {
            s.abandon(reserved.artifact_id);
            throw;
        }
    });
}
Result<Stored<DatasetArtifact>> create_calibration_dataset(artifact::Store &s, const CalibrationDataset &d,
                                                           const artifact::ArtifactReference &ref,
                                                           std::optional<Id> series) {
    return boundary([&] {
        checked(validate_calibration_dataset(d));
        reference_valid(s, ref, target_type);
        auto t = target(s, ref.id);
        codec::require(solve_detail::same_target(t.target, d.target),
                       "Dataset target differs from persisted target revision/content");
        DatasetArtifact v;
        v.dataset = d;
        v.target_reference = ref;
        for (auto &id : d.raw_capture_ids) {
            auto a = s.get(id);
            artifact::ArtifactReference source{id, a.hash};
            reference_valid(s, source, {"org.mantis.RawCapture", 2});
            v.raw_capture_references.push_back(source);
        }
        return write(s, std::move(v), dataset_type, "org.mantis.calibration.dataset", std::move(series));
    });
}
Result<Stored<CameraArtifact>> create_camera_calibration(artifact::Store &s,
                                                         const CameraCalibrationSolution &solution,
                                                         const artifact::ArtifactReference &data_ref,
                                                         const artifact::ArtifactReference &target_ref,
                                                         const SolverImplementation &implementation,
                                                         std::optional<Id> series) {
    return boundary([&] {
        reference_valid(s, data_ref, dataset_type);
        reference_valid(s, target_ref, target_type);
        auto d = dataset(s, data_ref.id);
        codec::require(same_artifact(target_ref, d.target_reference),
                       "Camera target reference differs from dataset");
        checked(validate_camera_solution(solution, d.dataset));
        implementation_valid(implementation);
        return write(s, CameraArtifact{{}, data_ref, target_ref, implementation, solution}, camera_type,
                     "org.mantis.calibration.camera", std::move(series));
    });
}
Result<Stored<RigArtifact>> create_rig_calibration(
    artifact::Store &s, const StereoCalibrationSolution &solution,
    const artifact::ArtifactReference &data_ref, const artifact::ArtifactReference &target_ref,
    const artifact::ArtifactReference &left_ref, const artifact::ArtifactReference &right_ref,
    const SolverImplementation &implementation, std::optional<Id> series) {
    return boundary([&] {
        reference_valid(s, data_ref, dataset_type);
        reference_valid(s, target_ref, target_type);
        reference_valid(s, left_ref, camera_type);
        reference_valid(s, right_ref, camera_type);
        auto d = dataset(s, data_ref.id);
        auto l = camera(s, left_ref.id), r = camera(s, right_ref.id);
        codec::require(
            same_artifact(target_ref, d.target_reference) && same_artifact(l.target_reference, target_ref) &&
                same_artifact(r.target_reference, target_ref) &&
                same_artifact(l.dataset_reference, data_ref) && same_artifact(r.dataset_reference, data_ref),
            "Rig dependencies refer to different dataset/target artifacts");
        checked(validate_stereo_solution(solution, d.dataset, l.solution, r.solution));
        implementation_valid(implementation);
        return write(s, RigArtifact{{}, data_ref, target_ref, left_ref, right_ref, implementation, solution},
                     rig_type, "org.mantis.calibration.rig", std::move(series));
    });
}
Result<void> validate_rig_device(const RigArtifact &v, const Id &device,
                                 std::span<const CameraComponent> components) {
    try {
        valid(v);
        if (device.value.empty())
            fail(Status::invalid_argument, "Logical device identity is required", "calibration-artifacts");
        const auto &l = v.solution.left_camera, &r = v.solution.right_camera;
        if (device.value.starts_with("org.mantis.x1:")) {
            // X1 discovery orders its physical components by their recorded roles.
            // StereoSolveConfig independently chooses solve/rig handedness.
            const DatasetCamera *physical_left = nullptr, *physical_right = nullptr;
            for (const auto *camera : {&l, &r}) {
                if (camera->role == "left")
                    physical_left = camera;
                if (camera->role == "right")
                    physical_right = camera;
            }
            if (!physical_left || !physical_right ||
                device.value !=
                    "org.mantis.x1:" + physical_left->camera_id.value + ":" + physical_right->camera_id.value)
                fail(Status::incompatible,
                     "RigCalibration physical cameras do not match logical X1 device identity",
                     "calibration-artifacts");
        }

        if (!components.empty()) {
            for (const auto *camera : {&l, &r}) {
                size_t count = 0;
                for (auto &c : components)
                    if (c.role == camera->role) {
                        ++count;
                        if (c.camera_id != camera->camera_id)
                            fail(Status::incompatible,
                                 "RigCalibration camera identity does not match discovered component",
                                 "calibration-artifacts");
                    }
                if (count != 1)
                    fail(Status::incompatible,
                         "RigCalibration requires exactly one matching discovered component per role",
                         "calibration-artifacts");
            }
        }
        return {};
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    }
}
Result<void> activate_rig_calibration(artifact::Store &s, const Id &device, const Id &id,
                                      std::span<const CameraComponent> components) {
    try {
        auto v = load_rig_calibration(s, id);
        if (!v)
            return std::unexpected(v.error());
        checked(validate_rig_device(*v, device, components));
        s.activate_calibration(device, id);
        return {};
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    }
}
} // namespace mantis::calibration::artifacts
