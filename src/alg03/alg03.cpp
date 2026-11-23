#include "private.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mantis/l1_candidates.hpp>
#include <set>
#ifndef MANTIS_ALG03_SOURCE_HASH
#error "ALG03 source fingerprint must be supplied by CMake"
#endif

namespace mantis::alg03 {
using namespace detail;
namespace {
struct Column {
    const char *name;
    schema::ScalarType scalar;
    uint64_t width, bytes;
    const char *unit;
};
constexpr std::array<Column, 16> columns{
    {{"source_row", schema::ScalarType::u32, 1, 4, ""},
     {"source_pixel", schema::ScalarType::f32, 2, 8, "pixel"},
     {"source_quality_flags", schema::ScalarType::u32, 1, 4, ""},
     {"accepted", schema::ScalarType::u8, 1, 1, ""},
     {"reason_bits", schema::ScalarType::u32, 1, 4, ""},
     {"position", schema::ScalarType::f64, 3, 24, "mm"},
     {"diagnostic_valid", schema::ScalarType::u32, 1, 4, ""},
     {"inverse_residual_px", schema::ScalarType::f64, 1, 8, "pixel"},
     {"inverse_error_bound", schema::ScalarType::f64, 1, 8, ""},
     {"ray_denominator", schema::ScalarType::f64, 1, 8, ""},
     {"incidence_cosine", schema::ScalarType::f64, 1, 8, ""},
     {"ray_parameter_mm", schema::ScalarType::f64, 1, 8, "mm"},
     {"optical_depth_mm", schema::ScalarType::f64, 1, 8, "mm"},
     {"numeric_error_bound_mm", schema::ScalarType::f64, 1, 8, "mm"},
     {"declared_model_bound_mm", schema::ScalarType::f64, 1, 8, "mm"},
     {"source_quantization_bound_mm", schema::ScalarType::f64, 1, 8, "mm"}}};
constexpr uint64_t row_bytes = [] {
    uint64_t n = 0;
    for (auto c : columns)
        n += c.bytes;
    return n;
}();
static_assert(row_bytes == 121);
struct ReadColumn {
    const data::Attribute *attribute{};
    std::span<const std::byte> bytes;
};
ReadColumn column(const data::LaserObservation &o, std::string_view name, const CancellationToken &t,
                  bool required = true) {
    auto it = std::find_if(o.attributes.begin(), o.attributes.end(),
                           [&](const auto &a) { return a.descriptor.name == name; });
    if (it == o.attributes.end()) {
        require(!required, "Required source column missing", Status::incompatible);
        return {};
    }
    auto b = it->buffer.map_read(t);
    if (!b)
        throw Failure(b.error());
    return {&*it, *b};
}
template <class T> T read(const ReadColumn &c, uint64_t row, uint64_t component = 0) {
    require(c.attribute, "Missing source column");
    const auto &d = c.attribute->descriptor;
    uint64_t offset = row * d.stride[0] + (component ? component * d.stride[1] : 0);
    require(offset <= c.bytes.size() && sizeof(T) <= c.bytes.size() - offset, "Source column extent");
    T v;
    std::memcpy(&v, c.bytes.data() + offset, sizeof(v));
    return v;
}
// Map each caller fence with its cancellation token once. Subsequent canonical
// validation/serialization sees no fence and retains the original backing owner.
data::LaserObservation mapped(const data::LaserObservation &o, uint64_t cap, const CancellationToken &t) {
    require(o.sample_count <= alg03::max_samples && o.attributes.size() <= data::max_semantic_attributes,
            "Source structural admission", Status::busy);
    std::set<const void *> seen;
    uint64_t backing = 0;
    std::vector<memory::BufferView> views;
    views.reserve(o.attributes.size());
    for (const auto &a : o.attributes) {
        t.check();
        if (seen.insert(a.buffer.identity()).second) {
            require(a.buffer.backing_size() <= cap - backing, "Source backing admission", Status::busy);
            backing += a.buffer.backing_size();
        }
        auto b = a.buffer.map_read(t);
        if (!b)
            throw Failure(b.error());
        auto storage = std::make_shared<memory::Storage>();
        storage->owner = std::make_shared<memory::BufferView>(a.buffer);
        storage->host = b->data();
        storage->size = b->size();
        storage->alignment = a.buffer.alignment();
        storage->retained_extent = a.buffer.backing_size();
        views.emplace_back(std::move(storage), 0, b->size());
    }
    auto copy = o;
    for (size_t i = 0; i < views.size(); ++i)
        copy.attributes[i].buffer = std::move(views[i]);
    auto v = data::validate(copy);
    if (!v)
        throw Failure(v.error());
    return copy;
}
void opaque_configuration(const data::LaserObservation &o, const Policy &p) {
    producer_shape(o.context.producer);
    auto c = o.context.producer.configuration.get(), r = o.context.parameters.get();
    require(c && r && same(*c, *r), "Source parameter/configuration identity mismatch", Status::incompatible);
    reference_shape(*r);
    require(o.context.producer.version == SemanticVersion{1, 0, 0}, "Source producer version",
            Status::incompatible);
    if (p.source_contract == SourceContract::alg02_subpixel_1) {
        require(o.context.producer.implementation.value == "org.mantis.l1-subpixel" &&
                    r->type == schema::DataTypeId{"org.mantis.L1CandidateParameters", 1} && r->revision == 1,
                "ALG02 opaque configuration type/version", Status::incompatible);
        const auto &hash = *r->hash.get();
        require(hash.algorithm == "fnv1a64" && hash.hex.size() == 16 &&
                    std::all_of(hash.hex.begin(), hash.hex.end(),
                                [](char x) { return (x >= '0' && x <= '9') || (x >= 'a' && x <= 'f'); }) &&
                    r->id.value == "l1-parameters-" + hash.hex,
                "ALG02 opaque configuration format", Status::incompatible);
    } else
        require(o.context.producer.implementation.value == "org.mantis.alg03.synthetic-reference" &&
                    o.context.origin == data::ObservationOrigin::synthetic,
                "Synthetic source identity/origin", Status::incompatible);
    if (p.source_contract == SourceContract::synthetic_reference_1) {
        require(!o.context.exact_inputs.empty(), "Synthetic exact input provenance required",
                Status::incompatible);
        for (const auto &input : o.context.exact_inputs)
            reference_shape(input);
    }
    // Deliberately no Store lookup, preimage reconstruction, or historical RAW/
    // dark/config artifact resolution. The accepted observation binds the bytes.
}
uint64_t known_historical_metadata(const artifact::Store &store, const data::LaserObservation &o,
                                   const CancellationToken &t, uint64_t cap) {
    const auto initial = cap;
    // Optional local descriptor checks preserve existing Store semantics. No
    // RAW/dark object read, network retrieval, ancestry walk or opaque lookup.
    auto check = [&](const data::ContentReference &r) {
        t.check();
        try {
            auto d = store.get_bounded(r.id, cap, t);
            require(d.state == artifact::ArtifactState::finalized && d.type.name == r.type.name &&
                        d.type.schema_version == r.type.version && r.hash.get() && d.hash == *r.hash.get(),
                    "Known historical input metadata contradicts source", Status::incompatible);
        } catch (const Failure &e) {
            if (e.error.code != Status::not_found)
                throw;
        }
    };
    if (auto r = o.context.raw_input.get())
        check(*r);
    for (auto &r : o.context.exact_inputs)
        check(r);
    for (auto c : {&o.context.source.camera_calibration, &o.context.source.rig_calibration,
                   &o.context.source.original_calibration})
        if (auto r = c->get())
            if (auto content = r->content.get())
                check(*content);
    return initial - cap;
}
void refinement(const data::LaserObservation &o, const Policy &p, const CancellationToken &t) {
    if (p.source_contract != SourceContract::alg02_subpixel_1 || o.sample_count == 0)
        return;
    auto valid = column(o, pipeline::l1::refinement_valid, t),
         curvature = column(o, pipeline::l1::curvature, t), flags = column(o, data::laser::quality_flags, t);
    require(valid.attribute->descriptor.scalar == schema::ScalarType::u8 &&
                valid.attribute->descriptor.shape == std::vector<uint64_t>{o.sample_count} &&
                valid.attribute->descriptor.unit.empty(),
            "ALG02 refinement column");
    require(curvature.attribute->descriptor.scalar == schema::ScalarType::u32 &&
                curvature.attribute->descriptor.shape == std::vector<uint64_t>{o.sample_count} &&
                curvature.attribute->descriptor.unit == "DN/pixel^2",
            "ALG02 curvature column");
    for (uint64_t i = 0; i < o.sample_count; ++i) {
        t.check();
        auto v = read<uint8_t>(valid, i);
        require(v <= 1 && (v != 0 || (read<uint32_t>(flags, i) & data::laser::producer_rejected)),
                "Contradictory ALG02 refinement/rejection");
    }
}
std::string outcome(uint64_t n, uint64_t accepted) {
    return n == 0 ? "empty_input" : accepted == n ? "all_accepted" : accepted == 0 ? "all_rejected" : "mixed";
}
void manifest_fields(const Json &j) {
    fields(j, {"algorithm", "inputs", "source_key", "source_frame", "camera_role", "image_width",
               "image_height", "optical_frame", "recorded_calibration", "origin", "validity_scope",
               "source_disposition", "sample_count", "accepted_count", "rejected_count", "outcome",
               "reason_version", "diagnostic_version", "layout_version"});
}
} // namespace

struct Prepared::State {
    Bindings binding;
    Plane plane;
    Policy policy;
    Geometry geometry;
    std::shared_ptr<const data::LaserObservation> source;
    std::string source_metadata;
    uint64_t source_backing{};
    spatial::Transform transform;
    data::ImplementationIdentity implementation;
};
Prepared::Prepared(std::shared_ptr<const State> s) : state_(std::move(s)) {}

Result<Prepared> prepare(const artifact::Store &store, const Bindings &b, const CancellationToken &t) {
    return result([&] {
        t.check();
        require(std::endian::native == std::endian::little && std::numeric_limits<float>::is_iec559 &&
                    sizeof(float) == 4,
                "Unsupported scalar representation", Status::unsupported);
        (void)selection(b.calibration_selection);
        auto state = std::make_shared<Prepared::State>();
        state->binding = b;
        auto policy = load_policy(store, b.policy, t);
        if (!policy)
            throw Failure(policy.error());
        state->policy = std::move(*policy);
        auto plane_desc = descriptor(store, b.plane, plane_type, t, state->policy.max_geometry_bytes);
        require(plane_desc.bytes <= state->policy.max_geometry_bytes, "Plane metadata admission",
                Status::busy);
        state->plane = plane_snapshot(store, b.plane, t, state->policy.max_geometry_bytes);
        auto source_desc = descriptor(store, b.observation, {schema::laser_observation.name, 1}, t,
                                      state->policy.max_geometry_bytes);
        require(b.observation.revision == 1 && source_desc.chunks == 1 &&
                    source_desc.bytes <= state->policy.max_source_bytes,
                "Observation snapshot admission", Status::busy);
        for (const auto &r : {b.observation, resolved(b.camera), resolved(b.rig), b.plane, b.policy})
            require(r.id != Id{}, "Processing ID required");
        auto ids = inputs(b);
        require(ids.size() == 5, "Cross-kind processing aliases", Status::incompatible);
        auto source = store.laser_observation(b.observation.id);
        t.check();
        std::set<const void *> roots;
        for (const auto &a : source.attributes)
            if (roots.insert(a.buffer.identity()).second) {
                require(a.buffer.backing_size() <= state->policy.max_source_bytes - state->source_backing,
                        "Pinned source backing admission", Status::busy);
                state->source_backing += a.buffer.backing_size();
            }
        auto safe = mapped(source, state->policy.max_source_bytes, t);
        encoded_size(safe, state->policy.max_source_bytes, t);
        require(safe.sample_count <= state->policy.max_samples, "ALG03 sample admission", Status::busy);
        require(safe.disposition.has_value(), "Source disposition required");
        if (*safe.disposition == data::ObservationDisposition::extractor_failed)
            fail(Status::plugin_failed, "Upstream extractor failed", "alg03");
        if (*safe.disposition == data::ObservationDisposition::extractor_unavailable)
            fail(Status::unsupported, "Upstream extractor unavailable", "alg03");
        opaque_configuration(safe, state->policy);
        refinement(safe, state->policy, t);
        state->geometry = geometry(store, b, state->plane, state->policy, t);
        state->geometry.retained_charge += known_historical_metadata(
            store, safe, t, state->policy.max_geometry_bytes - state->geometry.retained_charge);
        state->source_metadata =
            metadata(safe, (state->policy.max_geometry_bytes - state->geometry.retained_charge) / 8, t);
        // Includes two typed context copies and canonical metadata, with bounded
        // collection overhead. Bulk payload is charged separately by Storage.
        state->geometry.retained_charge += 8 * state->source_metadata.size();
        const auto &camera = state->geometry.camera.solution.camera;
        auto entry =
            std::find_if(state->plane.cameras.begin(), state->plane.cameras.end(),
                         [&](const auto &c) { return c.component == safe.context.source.frame.camera; });
        require(entry != state->plane.cameras.end() && same(entry->calibration, b.camera) &&
                    calibration::solve_detail::same_camera(entry->camera, camera) &&
                    camera.role == safe.context.source.camera_role &&
                    camera.image_width == safe.context.source.width &&
                    camera.image_height == safe.context.source.height &&
                    same(camera.optical_frame, safe.context.optical_frame),
                "Physical camera/component/role/image/frame mismatch", Status::incompatible);
        require(safe.context.origin && *safe.context.origin == state->plane.origin,
                "Plane/source origin mismatch", Status::incompatible);
        if (b.calibration_selection == Selection::recorded_effective) {
            auto rig = safe.context.source.rig_calibration.get();
            require(rig && same(*rig, b.rig), "Recorded effective rig mismatch", Status::incompatible);
            if (auto c = safe.context.source.camera_calibration.get())
                require(same(*c, b.camera), "Recorded effective camera mismatch", Status::incompatible);
        }
        for (const auto &pattern : safe.context.emitter_patterns)
            if (pattern.emitter == state->plane.line.emitter)
                require(pattern.pattern == state->plane.line.pattern &&
                            pattern.revision == state->plane.line.pattern_revision,
                        "Source pattern/plane mismatch", Status::incompatible);
        const auto &rig = state->geometry.rig.solution;
        state->transform =
            camera.role == rig.left_camera.role ? rig.rig.T_rig_from_left : rig.rig.T_rig_from_right;
        require(same(state->transform.source, safe.context.optical_frame) &&
                    same(state->transform.target, state->plane.frame),
                "Transform direction/frame mismatch", Status::incompatible);
        for (size_t i = 0; i < 3; ++i)
            require(std::abs(state->transform.matrix[i * 4 + 3]) <= 1e6, "Origin numerical domain",
                    Status::incompatible);
        auto certificate = certify_domain(state->geometry.camera.solution.final_model, state->policy.domain);
        if (!certificate)
            throw Failure(certificate.error());
        state->implementation = {Id{"org.mantis.alg03.ray-plane"},
                                 {1, 0, 0},
                                 std::string(build_version) +
                                     ";alg03-source-sha256:" + MANTIS_ALG03_SOURCE_HASH,
                                 b.policy};
        producer_shape(state->implementation);
        state->source = std::make_shared<const data::LaserObservation>(std::move(safe));
        t.check();
        return Prepared(std::move(state));
    });
}

Result<data::Published> process(const Prepared &prepared, const data::LaserObservation &input,
                                const CancellationToken &t) {
    return result([&] {
        t.check();
        require(bool(prepared.state_), "Moved-from prepared state", Status::invalid_argument);
        const auto &s = *prepared.state_;
        auto source = mapped(input, s.policy.max_source_bytes, t);
        equal_columns(source.attributes, s.source->attributes, t);
        metadata(source, s.source_metadata.size(), t, &s.source_metadata);
        const uint64_t n = source.sample_count;
        auto manifest = [&](uint64_t accepted, uint64_t rejected, std::string_view result_outcome) {
            auto &c = source.context.source;
            return envelope(
                       result_type.name,
                       {{"algorithm", implementation(s.implementation)},
                        {"inputs", bindings(s.binding)},
                        {"source_key", observation_key(source.key)},
                        {"source_frame", frame_key(c.frame)},
                        {"camera_role", c.camera_role},
                        {"image_width", c.width},
                        {"image_height", c.height},
                        {"optical_frame", enc(source.context.optical_frame)},
                        {"recorded_calibration",
                         {{"camera", evidence(c.camera_calibration, [](const auto &v) { return exact(v); })},
                          {"rig", evidence(c.rig_calibration, [](const auto &v) { return exact(v); })},
                          {"original",
                           evidence(c.original_calibration, [](const auto &v) { return exact(v); })}}},
                        {"origin", origin(*source.context.origin)},
                        {"validity_scope", s.policy.uncertainty_requirement == Uncertainty::numeric_only
                                               ? "declared_geometry_numeric_only"
                                               : "declared_geometry_parameter_bounded"},
                        {"source_disposition", "success"},
                        {"sample_count", n},
                        {"accepted_count", accepted},
                        {"rejected_count", rejected},
                        {"outcome", result_outcome},
                        {"reason_version", 1u},
                        {"diagnostic_version", 1u},
                        {"layout_version", 1u}})
                .dump();
        };
        // Upper-bound manifest digits/outcome length before allocating row output.
        const auto manifest_cap = manifest(n, n, "all_rejected").size();
        require(manifest_cap <= 262144 && n <= s.policy.max_samples &&
                    n * row_bytes + manifest_cap + 8192 <= s.policy.max_result_bytes,
                "Result byte admission", Status::busy);
        data::Packet packet;
        packet.type = schema::tensor;
        packet.header.frame = s.plane.frame;
        packet.header.calibration = s.binding.rig.calibration;
        packet.attributes.reserve(n ? 17 : 1);
        std::optional<memory::BufferBuilder> builder;
        std::array<std::span<std::byte>, 16> output;
        std::array<uint64_t, 16> offsets{};
        if (n) {
            builder.emplace(static_cast<size_t>(n * row_bytes));
            auto bytes = builder->writable();
            std::fill(bytes.begin(), bytes.end(), std::byte{});
            uint64_t offset = 0;
            for (size_t i = 0; i < columns.size(); ++i) {
                offsets[i] = offset;
                output[i] =
                    bytes.subspan(static_cast<size_t>(offset), static_cast<size_t>(n * columns[i].bytes));
                offset += n * columns[i].bytes;
            }
        }
        auto write = [&]<class T>(size_t col, uint64_t row, const T &value, uint64_t component = 0) {
            std::memcpy(output[col].data() + row * columns[col].bytes + component * sizeof(T), &value,
                        sizeof(T));
        };
        uint64_t accepted_count = 0;
        if (n) {
            auto pixels = column(source, data::laser::source_pixel, t),
                 flags = column(source, data::laser::quality_flags, t);
            auto ei = column(source, data::laser::emitter_index, t, false),
                 ev = column(source, data::laser::emitter_valid, t, false),
                 li = column(source, data::laser::line_index, t, false),
                 lv = column(source, data::laser::line_valid, t, false);
            for (uint64_t row = 0; row < n; ++row) {
                t.check();
                auto x = read<float>(pixels, row), y = read<float>(pixels, row, 1);
                auto original_flags = read<uint32_t>(flags, row);
                Sample sample;
                if (original_flags & data::laser::producer_rejected)
                    sample.reasons |= producer_rejected;
                if (original_flags & data::laser::ambiguous)
                    sample.reasons |= ambiguous;
                const bool attributed = ei.attribute && ev.attribute && li.attribute && lv.attribute &&
                                        read<uint8_t>(ev, row) == 1 && read<uint8_t>(lv, row) == 1;
                if (!attributed)
                    sample.reasons |= unresolved_attribution;
                else {
                    auto emitter = read<uint32_t>(ei, row), line = read<uint32_t>(li, row);
                    require(emitter < source.emitter_dictionary.size() &&
                                line < source.line_dictionary.size(),
                            "Source dictionary index");
                    if (source.emitter_dictionary[emitter] != s.plane.line.emitter ||
                        source.line_dictionary[line] != s.plane.line)
                        sample.reasons |= line_mismatch;
                }
                if (x < 0 || y < 0 || double(x) > source.context.source.width - 1. ||
                    double(y) > source.context.source.height - 1.)
                    sample.reasons |= image_fov;
                if (sample.reasons == 0)
                    sample = intersect(s.geometry.camera.solution.final_model, s.transform, s.plane, s.policy,
                                       x, y, t);
                const uint8_t accepted = sample.reasons == 0 ? 1 : 0;
                accepted_count += accepted;
                write(0, row, static_cast<uint32_t>(row));
                write(1, row, x);
                write(1, row, y, 1);
                write(2, row, original_flags);
                write(3, row, accepted);
                write(4, row, sample.reasons);
                write(6, row, sample.diagnostic_valid);
                for (size_t k = 0; k < 3; ++k)
                    write(5, row, sample.position[k], k);
                for (size_t k = 0; k < 9; ++k)
                    write(7 + k, row, sample.diagnostics[k]);
            }
        }
        t.check();
        auto text = manifest(accepted_count, n - accepted_count, outcome(n, accepted_count));
        // The existing semantic executor counts Storage::retained_extent. Couple
        // one output backing to the pinned source owner so factory-private source
        // buffers are visible to its normal input+candidate admission. Aliases may
        // conservatively overcharge, never undercharge or copy the bulk payload.
        auto pin = [&](memory::BufferView buffer) {
            struct Owner {
                std::shared_ptr<const void> state;
                memory::BufferView buffer;
            };
            auto bytes = buffer.map_read(t);
            if (!bytes)
                throw Failure(bytes.error());
            auto storage = std::make_shared<memory::Storage>();
            storage->owner = std::make_shared<Owner>(Owner{prepared.state_, buffer});
            storage->host = bytes->data();
            storage->size = bytes->size();
            storage->alignment = buffer.alignment();
            storage->retained_extent = buffer.backing_size() + s.source_backing;
            return memory::BufferView{std::move(storage), 0, buffer.size()};
        };
        auto manifest_buffer = memory::copy(std::as_bytes(std::span(text)));
        if (!n)
            manifest_buffer = pin(std::move(manifest_buffer));
        packet.attributes.push_back(
            {{std::string(manifest_attribute), schema::ScalarType::u8, {text.size()}, {1}, "utf8-json"},
             std::move(manifest_buffer)});
        if (n) {
            auto buffer = pin(std::move(*builder).publish());
            for (size_t i = 0; i < columns.size(); ++i) {
                auto c = columns[i];
                schema::AttributeDescriptor d{std::string(prefix) + c.name, c.scalar, {n}, {c.bytes}, c.unit};
                if (c.width > 1) {
                    d.shape.push_back(c.width);
                    d.stride.push_back(c.bytes / c.width);
                }
                packet.attributes.push_back({std::move(d), buffer.slice(static_cast<size_t>(offsets[i]),
                                                                        static_cast<size_t>(n * c.bytes))});
            }
        }
        encoded_size(packet, s.policy.max_result_bytes, t);
        t.check();
        return data::publish(std::move(packet));
    });
}
namespace {
class Node final : public pipeline::SemanticNodeInstance {
    Prepared state_;

  public:
    explicit Node(Prepared p) : state_(std::move(p)) {}
    Result<data::SemanticPublished> process(std::span<const data::SemanticPublished> in,
                                            const CancellationToken &t) override {
        return result([&] {
            t.check();
            require(in.size() == 1 && in[0], "One observation input required", Status::invalid_argument);
            auto p = std::get_if<data::LaserObservation>(in[0].get());
            require(p, "Expected typed LaserObservation", Status::incompatible);
            auto output = alg03::process(state_, *p, t);
            if (!output)
                throw Failure(output.error());
            return data::publish(data::SemanticPacket{*output});
        });
    }
};
} // namespace
pipeline::SemanticNode triangulator(Prepared p) {
    require(bool(p.state_), "Moved-from prepared state", Status::invalid_argument);
    pipeline::NodeDescriptor d;
    d.resources.memory_bytes = p.state_->source_backing + p.state_->geometry.retained_charge;
    d.id = "alg03-ray-plane";
    d.algorithm_id = "org.mantis.alg03.ray-plane";
    d.version = {1, 0, 0};
    d.inputs = {{"observation", schema::laser_observation}};
    d.outputs = {{"outcomes", schema::tensor}};
    d.deterministic = true;
    d.stateful = false;
    return {std::move(d), [p = std::move(p)] { return std::make_unique<Node>(p); }};
}
Result<void> validate_result(const Prepared &p, const data::Packet &value, const CancellationToken &t) {
    return result([&] {
        t.check();
        require(bool(p.state_), "Moved-from prepared state", Status::invalid_argument);
        auto text = document_bytes(value, manifest_attribute, t);
        auto payload = parse_document(text, result_type.name);
        manifest_fields(payload);
        auto computed = process(p, *p.state_->source, t);
        if (!computed)
            throw Failure(computed.error());
        equal_packet(**computed, value, p.state_->policy.max_result_bytes, t);
    });
}
Result<artifact::ArtifactReference> persist_result(artifact::Store &store, const Prepared &prepared,
                                                   const data::Packet &value, const CancellationToken &t) {
    return result([&] {
        auto v = validate_result(prepared, value, t);
        if (!v)
            throw Failure(v.error());
        artifact::Provenance provenance;
        provenance.producer = prepared.state_->implementation.implementation.value;
        provenance.version = prepared.state_->implementation.version;
        provenance.inputs = inputs(prepared.state_->binding);
        provenance.calibration = prepared.state_->binding.rig.calibration;
        t.check();
        auto id = store.begin(result_type, provenance);
        std::string_view phase = "append result";
        try {
            store.append(id, value);
            t.check();
            phase = "finalize result";
            auto d = store.finalize(id, t);
            phase = "readback result";
            auto readback = load_result(store, id, t);
            if (!readback)
                throw Failure(readback.error());
            return artifact::ArtifactReference{id, d.hash};
        } catch (const Failure &error) {
            throw Failure(reserved_error(store, id, error.error));
        } catch (const std::bad_alloc &) {
            throw Failure(reserved_error(store, id, {Status::busy, "ALG03 allocation failed", "alg03"}));
        } catch (const std::filesystem::filesystem_error &error) {
            throw Failure(reserved_error(store, id, filesystem_error(error, phase)));
        } catch (const std::exception &) {
            throw Failure(reserved_error(store, id, {Status::io, "ALG03 materialization failed", "alg03"}));
        }
    });
}
Result<data::Published> load_result(const artifact::Store &store, const Id &id, const CancellationToken &t) {
    return result([&] {
        t.check();
        auto cap = max_geometry_bytes;
        auto d = store.get_bounded(id, cap, t);
        require(d.type.name == result_type.name && d.type.schema_version == 1 &&
                    d.state == artifact::ArtifactState::finalized && d.chunks == 1 &&
                    d.bytes <= max_result_bytes,
                "Result descriptor type/state/size");
        auto packet = store.packet(id);
        auto payload = parse_document(document_bytes(*packet, manifest_attribute, t), result_type.name);
        manifest_fields(payload);
        auto b = bindings(payload.at("inputs"));
        auto prepared = prepare(store, b, t);
        if (!prepared)
            throw Failure(prepared.error());
        auto ok = validate_result(*prepared, *packet, t);
        if (!ok)
            throw Failure(ok.error());
        require(d.provenance.producer == "org.mantis.alg03.ray-plane" &&
                    d.provenance.version == SemanticVersion{1, 0, 0} && d.provenance.parameters.empty() &&
                    d.provenance.inputs == inputs(b) &&
                    enc(d.provenance.calibration) == enc(b.rig.calibration),
                "Result descriptor provenance");
        return packet;
    });
}
} // namespace mantis::alg03
