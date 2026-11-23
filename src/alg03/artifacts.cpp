#include "private.hpp"
#include <map>
#include <set>

namespace mantis::alg03::detail {
namespace {
class Graph {
    const artifact::Store &store_;
    const Policy &policy_;
    const CancellationToken &token_;
    std::map<Id, artifact::ArtifactDescriptor> descriptors_;
    std::map<Id, Json> documents_;
    struct CalibratedRig {
        calibration::artifacts::RigArtifact rig;
        calibration::artifacts::CameraArtifact left, right;
    };
    std::map<Id, CalibratedRig> rigs_;
    std::set<Id> active_, done_;
    uint64_t charged_{};
    uint32_t reserved_nodes_{};

  public:
    Graph(const artifact::Store &s, const Policy &p, const CancellationToken &t, uint32_t reserved = 0)
        : store_(s), policy_(p), token_(t), reserved_nodes_(reserved) {}
    uint64_t charged() const {
        return charged_;
    }
    artifact::ArtifactDescriptor get(const Id &id) {
        token_.check();
        if (auto it = descriptors_.find(id); it != descriptors_.end())
            return it->second;
        require(descriptors_.size() + reserved_nodes_ < policy_.max_dependency_nodes,
                "Processing dependency node cap", Status::busy);
        auto remaining = policy_.max_geometry_bytes - charged_;
        auto d = store_.get_bounded(id, remaining, token_);
        charged_ = policy_.max_geometry_bytes - remaining;
        require(d.state == artifact::ArtifactState::finalized, "Processing dependency not FINALIZED",
                Status::incompatible);
        descriptors_.emplace(id, d);
        return d;
    }
    artifact::ArtifactDescriptor ref(const data::ContentReference &r) {
        reference_shape(r);
        auto d = get(r.id);
        require(d.type.name == r.type.name && d.type.schema_version == r.type.version &&
                    d.hash == *r.hash.get(),
                "Processing dependency identity/hash", Status::incompatible);
        return d;
    }
    void ref(const artifact::ArtifactReference &r, const artifact::ArtifactType &type) {
        auto d = get(r.id);
        require(d.type.name == type.name && d.type.schema_version == type.schema_version && d.hash == r.hash,
                "M4 dependency type/hash", Status::incompatible);
    }
    const Json &doc(const Id &id, const artifact::ArtifactType &type, bool is_calibration) {
        token_.check();
        if (auto i = documents_.find(id); i != documents_.end())
            return i->second;
        auto d = get(id);
        require(d.type.name == type.name && d.type.schema_version == type.schema_version && d.chunks == 1,
                "Geometry artifact type/chunks", Status::incompatible);
        require(d.bytes <= (policy_.max_geometry_bytes - charged_) / 16,
                "Geometry canonical working admission", Status::busy);
        auto packet = store_.packet(id);
        require(bool(packet) && packet->frames.empty() && packet->attributes.size() == 1,
                "One document packet required");
        require(packet->type == (is_calibration ? calibration::artifacts::document_type : schema::tensor),
                "Geometry packet type");
        auto expected = *packet;
        expected.header = {};
        equal_packet(expected, *packet, d.bytes, token_);
        const auto &a = packet->attributes[0];
        auto r = a.buffer.map_read(token_);
        if (!r)
            throw Failure(r.error());
        const std::string name = is_calibration ? std::string(calibration::artifacts::document_attribute)
                                                : std::string(document_attribute);
        require(a.descriptor.name == name && a.descriptor.scalar == schema::ScalarType::u8 &&
                    a.descriptor.shape == std::vector<uint64_t>{a.buffer.size()} &&
                    a.descriptor.stride == std::vector<uint64_t>{1} && a.descriptor.unit == "utf8-json",
                "Document column layout");
        require(r->size() <= policy_.max_geometry_bytes - charged_, "Document byte cap", Status::busy);
        const std::string_view bytes(reinterpret_cast<const char *>(r->data()), r->size());
        // Admission is complete BEFORE Json::parse can grow a DOM or any typed
        // decoder/canonical comparison can copy it. Keep the reservation for the
        // lifetime of this graph, including typed values returned to its caller.
        const auto amount = geometry_charge(bytes, policy_.max_geometry_bytes - charged_, token_);
        charged_ += amount;
        // geometry_charge already bounds nesting before any DOM allocation.
        // A keep/discard callback is unnecessary and makes the JSON parser scan
        // parent containers for discarded values even though none are discarded.
        auto j = Json::parse(bytes);
        require(j.dump() == bytes, "Noncanonical geometry document");
        documents_.emplace(id, std::move(j));
        return documents_.at(id);
    }
    template <class T> T m4doc(const Id &id, const artifact::ArtifactType &type) {
        const auto &j = doc(id, type, true);
        fields(j, {"schema_version", "kind", "conventions", "payload"});
        require(dec<uint32_t>(j.at("schema_version")) == 1 && j.at("kind") == type.name, "M4 schema/kind");
        auto v = dec<T>(j.at("payload"));
        auto canonical = calibration::artifacts::encode_document(v);
        if (!canonical)
            throw Failure(canonical.error());
        require(*canonical == j.dump(), "M4 canonical typed document");
        auto registry = store_.artifact_revision(id);
        auto d = get(id);
        require(enc(registry.reference) == enc(v.revision) && registry.artifact_id == id &&
                    registry.kind == type.name && enc(d.provenance.calibration) == enc(v.revision),
                "M4 revision/registry/provenance");
        return v;
    }
    void lineage(const Id &id, const std::vector<Id> &expected) {
        require(get(id).provenance.inputs == expected, "Geometry ordered provenance mismatch");
    }
    template <class T> T snapshot(const data::ContentReference &r) {
        const auto &type = std::is_same_v<T, Plane> ? plane_type : policy_type;
        require(r.revision == 1, "ALG03 snapshot reference revision", Status::incompatible);
        auto d = ref(r);
        require(d.type.schema_version == 1, "ALG03 dependency schema/revision", Status::incompatible);
        const auto &j = doc(r.id, type, false);
        auto payload = parse_document(j.dump(), type.name);
        T p;
        if constexpr (std::is_same_v<T, Plane>)
            p = plane(payload);
        else
            p = policy(payload);
        auto ok = validate(p);
        if (!ok)
            throw Failure(ok.error());
        auto canonical = encode_document(p);
        if (!canonical)
            throw Failure(canonical.error());
        require(*canonical == j.dump(), "Noncanonical typed snapshot");
        lineage(r.id, inputs(p));
        require(d.provenance.producer == p.producer.implementation.value &&
                    d.provenance.version == p.producer.version && d.provenance.parameters.empty(),
                "Snapshot descriptor provenance");
        if constexpr (std::is_same_v<T, Plane>)
            require(enc(d.provenance.calibration) == enc(p.rig.calibration), "Plane descriptor calibration");
        else
            require(enc(d.provenance.calibration) == enc(calibration::Reference{}),
                    "Policy descriptor calibration");
        return p;
    }
    void new_dependency(const data::ContentReference &r, unsigned depth = 0) {
        token_.check();
        require(depth <= 64, "Dependency depth cap", Status::busy);
        auto d = ref(r);
        require(!active_.contains(r.id), "Cyclic processing dependency");
        if (d.type.name == plane_type.name || d.type.name == policy_type.name)
            require(d.type.schema_version == 1 && r.revision == 1, "ALG03 dependency schema/revision",
                    Status::incompatible);
        if (done_.contains(r.id))
            return;
        active_.insert(r.id);
        if (d.type.name == plane_type.name) {
            auto p = snapshot<Plane>(r);
            // The same selected M4 graph/applicability validation as the
            // standalone loader, inside this graph's budget/cycle context.
            Bindings b;
            b.rig = p.rig;
            b.camera = p.cameras.front().calibration;
            (void)calibration(b, p);
            if (p.parameter_bounds && p.parameter_bounds->evidence)
                new_dependency(*p.parameter_bounds->evidence, depth + 1);
            if (auto c = p.producer.configuration.get())
                new_dependency(*c, depth + 1);
        } else if (d.type.name == policy_type.name) {
            auto p = snapshot<Policy>(r);
            if (p.declared_ray_bounds && p.declared_ray_bounds->evidence)
                new_dependency(*p.declared_ray_bounds->evidence, depth + 1);
            if (auto c = p.producer.configuration.get())
                new_dependency(*c, depth + 1);
        } else {
            // Explicit evidence may have an arbitrary established artifact type.
            // Its historical Provenance.inputs are not implicitly processing data.
            require(std::find(d.provenance.inputs.begin(), d.provenance.inputs.end(), r.id) ==
                        d.provenance.inputs.end(),
                    "Self-referencing processing evidence");
        }
        active_.erase(r.id);
        done_.insert(r.id);
    }
    void evidence(const Plane &p, const Policy &policy) {
        if (p.parameter_bounds && p.parameter_bounds->evidence)
            new_dependency(*p.parameter_bounds->evidence);
        if (auto c = p.producer.configuration.get())
            new_dependency(*c);
        if (policy.declared_ray_bounds && policy.declared_ray_bounds->evidence)
            new_dependency(*policy.declared_ray_bounds->evidence);
        if (auto c = policy.producer.configuration.get())
            new_dependency(*c);
    }
    void consume_reserved_node() {
        require(reserved_nodes_ != 0, "Missing snapshot node reservation");
        --reserved_nodes_;
    }
    Geometry calibration(const Bindings &b, const Plane &plane) {
        namespace ca = calibration::artifacts;
        auto rr = resolved(b.rig), cr = resolved(b.camera);
        ref(rr);
        ref(cr);
        require(rr.type.name == ca::rig_type.name && cr.type.name == ca::camera_type.name,
                "Selected calibration kinds", Status::incompatible);
        if (!rigs_.contains(rr.id)) {
            auto rig = m4doc<ca::RigArtifact>(rr.id, ca::rig_type);
            require(enc(rig.revision) == enc(b.rig.calibration), "Rig logical reference mismatch",
                    Status::incompatible);
            auto dataset = m4doc<ca::DatasetArtifact>(rig.dataset_reference.id, ca::dataset_type);
            ref(rig.dataset_reference, ca::dataset_type);
            auto target = m4doc<ca::TargetArtifact>(rig.target_reference.id, ca::target_type);
            ref(rig.target_reference, ca::target_type);
            require(dataset.target_reference.id == rig.target_reference.id &&
                        dataset.target_reference.hash == rig.target_reference.hash &&
                        calibration::solve_detail::same_target(dataset.dataset.target, target.target),
                    "M4 shared target mismatch");
            auto vd = calibration::validate_calibration_dataset(dataset.dataset);
            if (!vd)
                throw Failure(vd.error());
            require(dataset.raw_capture_references.size() == dataset.dataset.raw_capture_ids.size(),
                    "Calibration source descriptor count");
            std::vector<Id> dataset_ids{dataset.target_reference.id};
            for (size_t i = 0; i < dataset.raw_capture_references.size(); ++i) {
                token_.check();
                require(dataset.raw_capture_references[i].id == dataset.dataset.raw_capture_ids[i],
                        "Calibration source order");
                ref(dataset.raw_capture_references[i], {"org.mantis.RawCapture", 2});
                dataset_ids.push_back(dataset.raw_capture_references[i].id);
            }
            lineage(rig.target_reference.id, {});
            lineage(rig.dataset_reference.id, dataset_ids);
            auto camera = [&](const artifact::ArtifactReference &r) {
                token_.check();
                ref(r, ca::camera_type);
                auto c = m4doc<ca::CameraArtifact>(r.id, ca::camera_type);
                require(c.dataset_reference.id == rig.dataset_reference.id &&
                            c.dataset_reference.hash == rig.dataset_reference.hash &&
                            c.target_reference.id == rig.target_reference.id &&
                            c.target_reference.hash == rig.target_reference.hash,
                        "Camera graph mismatch");
                lineage(r.id, {c.dataset_reference.id, c.target_reference.id});
                auto ok = calibration::validate_camera_solution(c.solution, dataset.dataset);
                if (!ok)
                    throw Failure(ok.error());
                return c;
            };
            auto left = camera(rig.left_camera_reference), right = camera(rig.right_camera_reference);
            lineage(rr.id, {rig.dataset_reference.id, rig.target_reference.id, rig.left_camera_reference.id,
                            rig.right_camera_reference.id});
            auto vr = calibration::validate_stereo_solution(rig.solution, dataset.dataset, left.solution,
                                                            right.solution);
            if (!vr)
                throw Failure(vr.error());
            // Validation of immutable M4 content is shared across all new-schema
            // dependencies in this bounded graph. Applicability is checked below
            // for EVERY plane/camera selection, never cached by type alone.
            rigs_.emplace(rr.id, CalibratedRig{std::move(rig), std::move(left), std::move(right)});
        }
        const auto &[rig, left, right] = rigs_.at(rr.id);
        require(enc(rig.revision) == enc(b.rig.calibration), "Rig logical reference mismatch",
                Status::incompatible);
        require(
            (cr.id == rig.left_camera_reference.id && *cr.hash.get() == rig.left_camera_reference.hash) ||
                (cr.id == rig.right_camera_reference.id && *cr.hash.get() == rig.right_camera_reference.hash),
            "Selected camera not in rig", Status::incompatible);
        const auto &chosen = cr.id == rig.left_camera_reference.id ? left : right;
        require(enc(chosen.revision) == enc(b.camera.calibration), "Selected camera logical reference",
                Status::incompatible);
        require(same(plane.rig, b.rig) && same(plane.frame, rig.solution.config.rig_frame),
                "Plane/processing rig mismatch", Status::incompatible);
        for (const auto &c : plane.cameras) {
            const auto &refc = resolved(c.calibration);
            const auto &actual = refc.id == rig.left_camera_reference.id ? left : right;
            const auto &artifact = refc.id == rig.left_camera_reference.id ? rig.left_camera_reference
                                                                           : rig.right_camera_reference;
            require(refc.id == artifact.id && *refc.hash.get() == artifact.hash &&
                        enc(c.calibration.calibration) == enc(actual.revision) &&
                        calibration::solve_detail::same_camera(c.camera, actual.solution.camera),
                    "Plane camera applicability mismatch", Status::incompatible);
        }
        return {cr.id == rig.left_camera_reference.id ? left : right, rig, charged_};
    }
};

template <class T>
T load(const artifact::Store &s, const data::ContentReference &r, const artifact::ArtifactType &type,
       const CancellationToken &t, uint64_t cap = max_geometry_bytes) {
    Policy limits;
    limits.max_geometry_bytes = cap;
    Graph graph(s, limits, t);
    require(type.name == (std::is_same_v<T, Plane> ? plane_type.name : policy_type.name),
            "Snapshot loader kind", Status::incompatible);
    return graph.snapshot<T>(r);
}
} // namespace
Plane plane_snapshot(const artifact::Store &s, const data::ContentReference &r, const CancellationToken &t,
                     uint64_t cap) {
    return load<Plane>(s, r, plane_type, t, cap);
}
Geometry geometry(const artifact::Store &s, const Bindings &b, const Plane &p, const Policy &policy,
                  const CancellationToken &t) {
    Graph graph(s, policy, t);
    (void)graph.ref(b.observation); // Admit source descriptor metadata too, not its historical ancestry.
    graph.new_dependency(b.plane);
    graph.new_dependency(b.policy);
    return graph.calibration(b, p);
}
void evidence_dependencies(const artifact::Store &s, const Plane &p, const Policy &policy,
                           const CancellationToken &t) {
    Graph graph(s, policy, t);
    graph.evidence(p, policy);
}
} // namespace mantis::alg03::detail

namespace mantis::alg03 {
using namespace detail;
Result<Plane> load_plane(const artifact::Store &s, const data::ContentReference &r,
                         const CancellationToken &t) {
    return result([&] {
        Policy limits;
        Graph graph(s, limits, t);
        graph.new_dependency(r);
        return graph.snapshot<Plane>(r);
    });
}
Result<Policy> load_policy(const artifact::Store &s, const data::ContentReference &r,
                           const CancellationToken &t) {
    return result([&] {
        // The policy must first be decoded under the fixed hard cap before its
        // own (possibly smaller) selected budget can be known. Its required
        // closure, including its descriptor/document, then uses that budget.
        auto p = load<Policy>(s, r, policy_type, t);
        Graph graph(s, p, t);
        graph.new_dependency(r);
        return p;
    });
}
template <class T>
Result<data::ContentReference> create(artifact::Store &s, const T &value, const artifact::ArtifactType &type,
                                      const CancellationToken &t) {
    return result([&] {
        t.check();
        auto encoded = encode_document(value);
        if (!encoded)
            throw Failure(encoded.error());
        Plane empty;
        Policy limits;
        std::optional<Graph> plane_graph;
        if constexpr (std::is_same_v<T, Plane>) {
            // One bounded operation owns the exact immutable dependency graph
            // through finalized readback. Reserve the new snapshot node before
            // writing; only immutable M4 content is shared, never applicability
            // of a different plane or any mutable current-calibration lookup.
            auto &graph = plane_graph.emplace(s, limits, t, 1);
            graph.evidence(value, limits);
            Bindings b;
            b.rig = value.rig;
            b.camera = value.cameras.front().calibration;
            (void)graph.calibration(b, value);
        } else
            evidence_dependencies(s, empty, value, t);
        artifact::Provenance provenance;
        provenance.producer = value.producer.implementation.value;
        provenance.version = value.producer.version;
        provenance.inputs = inputs(value);
        if constexpr (std::is_same_v<T, Plane>)
            provenance.calibration = value.rig.calibration;
        t.check();
        auto id = s.begin(type, provenance);
        std::string_view phase = "append snapshot";
        try {
            s.append(id, document_packet(*encoded));
            t.check();
            phase = "finalize snapshot";
            auto d = s.finalize(id, t);
            phase = "readback snapshot";
            data::ContentReference r{id, {type.name, type.schema_version}, d.hash, 1};
            if constexpr (std::is_same_v<T, Plane>) {
                // Decode and validate the actual newly stored snapshot, including
                // descriptor/hash, typed canonical bytes, ordered provenance,
                // dependency closure and per-plane calibration applicability.
                plane_graph->consume_reserved_node();
                plane_graph->new_dependency(r);
                (void)plane_graph->snapshot<Plane>(r);
            } else {
                auto v = load_policy(s, r, t);
                if (!v)
                    throw Failure(v.error());
            }
            return r;
        } catch (const Failure &error) {
            throw Failure(reserved_error(s, id, error.error));
        } catch (const std::bad_alloc &) {
            throw Failure(reserved_error(s, id, {Status::busy, "ALG03 allocation failed", "alg03"}));
        } catch (const std::filesystem::filesystem_error &error) {
            throw Failure(reserved_error(s, id, filesystem_error(error, phase)));
        } catch (const std::exception &) {
            throw Failure(reserved_error(s, id, {Status::io, "ALG03 snapshot failed", "alg03"}));
        }
    });
}
Result<data::ContentReference> create_plane(artifact::Store &s, const Plane &p, const CancellationToken &t) {
    return create(s, p, plane_type, t);
}
Result<data::ContentReference> create_policy(artifact::Store &s, const Policy &p,
                                             const CancellationToken &t) {
    return create(s, p, policy_type, t);
}
} // namespace mantis::alg03
