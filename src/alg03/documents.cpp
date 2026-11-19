#include "private.hpp"
#include <algorithm>
#include <cmath>

namespace mantis::alg03::detail {
Json content(const data::ContentReference &v) {
    return {{"id", enc(v.id)},
            {"type", {{"name", v.type.name}, {"version", v.type.version}}},
            {"hash", evidence(v.hash, [](const Hash &h) { return enc(h); })},
            {"revision", v.revision}};
}
data::ContentReference content(const Json &j) {
    fields(j, {"id", "type", "hash", "revision"});
    fields(j.at("type"), {"name", "version"});
    return {dec<Id>(j.at("id")),
            {dec<std::string>(j.at("type").at("name")), dec<uint32_t>(j.at("type").at("version"))},
            evidence<Hash>(j.at("hash"), [](const Json &h) { return dec<Hash>(h); }),
            dec<uint64_t>(j.at("revision"))};
}
Json exact(const data::ExactCalibrationReference &v) {
    return {{"calibration", enc(v.calibration)},
            {"content", evidence(v.content, [](const auto &c) { return content(c); })}};
}
data::ExactCalibrationReference exact(const Json &j) {
    fields(j, {"calibration", "content"});
    return {dec<calibration::Reference>(j.at("calibration")),
            evidence<data::ContentReference>(j.at("content"), [](const Json &c) { return content(c); })};
}
Json implementation(const data::ImplementationIdentity &v) {
    return {{"implementation", enc(v.implementation)},
            {"version", enc(v.version)},
            {"build", v.build},
            {"configuration", evidence(v.configuration, [](const auto &c) { return content(c); })}};
}
data::ImplementationIdentity implementation(const Json &j) {
    fields(j, {"implementation", "version", "build", "configuration"});
    return {
        dec<Id>(j.at("implementation")), dec<SemanticVersion>(j.at("version")),
        dec<std::string>(j.at("build")),
        evidence<data::ContentReference>(j.at("configuration"), [](const Json &c) { return content(c); })};
}
Json observation_key(const data::ObservationKey &v) {
    return {{"run_id", evidence(v.run_id, [](const auto &r) { return semantic_id(r); })},
            {"producer_stream", semantic_id(v.producer_stream)},
            {"producer_generation", semantic_id(v.producer_generation)},
            {"sequence", {{"value", v.sequence.value}}}};
}
Json frame_key(const data::SourceFrameKey &v) {
    return {{"camera", semantic_id(v.camera)},
            {"stream", {{"id", semantic_id(v.stream.id)}, {"generation", semantic_id(v.stream.generation)}}},
            {"native_sequence", v.native_sequence}};
}
std::string origin(data::ObservationOrigin v) {
    switch (v) {
    case data::ObservationOrigin::real:
        return "real";
    case data::ObservationOrigin::synthetic:
        return "synthetic";
    case data::ObservationOrigin::imported:
        return "imported";
    }
    fail(Status::corrupt, "Invalid origin", "alg03");
}
data::ObservationOrigin origin(const Json &j) {
    auto s = dec<std::string>(j);
    if (s == "real")
        return data::ObservationOrigin::real;
    if (s == "synthetic")
        return data::ObservationOrigin::synthetic;
    require(s == "imported", "Invalid origin");
    return data::ObservationOrigin::imported;
}
std::string selection(Selection v) {
    require(v == Selection::recorded_effective || v == Selection::explicit_processing,
            "Invalid calibration selection");
    return v == Selection::recorded_effective ? "recorded_effective" : "explicit_processing";
}
Json bindings(const Bindings &v) {
    return {{"observation", content(v.observation)},
            {"camera", exact(v.camera)},
            {"rig", exact(v.rig)},
            {"plane", content(v.plane)},
            {"policy", content(v.policy)},
            {"calibration_selection", selection(v.calibration_selection)}};
}
Bindings bindings(const Json &j) {
    fields(j, {"observation", "camera", "rig", "plane", "policy", "calibration_selection"});
    auto mode = dec<std::string>(j.at("calibration_selection"));
    require(mode == "recorded_effective" || mode == "explicit_processing", "Invalid calibration selection");
    return {content(j.at("observation")),
            content(j.at("plane")),
            content(j.at("policy")),
            exact(j.at("camera")),
            exact(j.at("rig")),
            mode == "recorded_effective" ? Selection::recorded_effective : Selection::explicit_processing};
}
Json plane(const Plane &v) {
    Json cameras = Json::array();
    for (const auto &c : v.cameras)
        cameras.push_back({{"component", semantic_id(c.component)},
                           {"camera", enc(c.camera)},
                           {"calibration", exact(c.calibration)}});
    Json bounds = nullptr;
    if (v.parameter_bounds)
        bounds = {{"normal_l2", v.parameter_bounds->normal_l2},
                  {"offset_abs_mm", v.parameter_bounds->offset_abs_mm},
                  {"evidence",
                   v.parameter_bounds->evidence ? content(*v.parameter_bounds->evidence) : Json(nullptr)}};
    return {{"rig", exact(v.rig)},
            {"frame", enc(v.frame)},
            {"unit", "mm"},
            {"normal_xyz", enc(v.normal_xyz)},
            {"offset_mm", v.offset_mm},
            {"line",
             {{"emitter", semantic_id(v.line.emitter)},
              {"pattern", semantic_id(v.line.pattern)},
              {"pattern_revision", v.line.pattern_revision},
              {"local_line", semantic_id(v.line.local_line)}}},
            {"cameras", std::move(cameras)},
            {"side", v.side == Side::two_sided ? "two_sided" : "camera_on_positive_side"},
            {"origin", origin(v.origin)},
            {"parameter_bounds", bounds},
            {"producer", implementation(v.producer)}};
}
Plane plane(const Json &j) {
    fields(j, {"rig", "frame", "unit", "normal_xyz", "offset_mm", "line", "cameras", "side", "origin",
               "parameter_bounds", "producer"});
    require(j.at("unit") == "mm", "Plane units must be mm");
    Plane v;
    v.rig = exact(j.at("rig"));
    v.frame = dec<spatial::CoordinateFrame>(j.at("frame"));
    v.normal_xyz = dec<std::array<double, 3>>(j.at("normal_xyz"));
    v.offset_mm = dec<double>(j.at("offset_mm"));
    const auto &line = j.at("line");
    fields(line, {"emitter", "pattern", "pattern_revision", "local_line"});
    v.line.emitter = semantic_id<data::ComponentTag>(line.at("emitter"));
    v.line.pattern = semantic_id<data::PatternTag>(line.at("pattern"));
    v.line.pattern_revision = dec<uint64_t>(line.at("pattern_revision"));
    v.line.local_line = semantic_id<data::LineTag>(line.at("local_line"));
    require(j.at("cameras").is_array() && j.at("cameras").size() <= 2, "Plane camera bound");
    for (const auto &c : j.at("cameras")) {
        fields(c, {"component", "camera", "calibration"});
        v.cameras.push_back({semantic_id<data::ComponentTag>(c.at("component")),
                             dec<calibration::DatasetCamera>(c.at("camera")), exact(c.at("calibration"))});
    }
    auto side = dec<std::string>(j.at("side"));
    require(side == "two_sided" || side == "camera_on_positive_side", "Invalid side");
    v.side = side == "two_sided" ? Side::two_sided : Side::camera_on_positive_side;
    v.origin = origin(j.at("origin"));
    if (!j.at("parameter_bounds").is_null()) {
        const auto &b = j.at("parameter_bounds");
        fields(b, {"normal_l2", "offset_abs_mm", "evidence"});
        v.parameter_bounds =
            PlaneBounds{dec<double>(b.at("normal_l2")), dec<double>(b.at("offset_abs_mm")), {}};
        if (!b.at("evidence").is_null())
            v.parameter_bounds->evidence = content(b.at("evidence"));
    }
    v.producer = implementation(j.at("producer"));
    return v;
}
Json policy(const Policy &v) {
    Json bounds = nullptr;
    if (v.declared_ray_bounds)
        bounds = {{"origin_l2_mm", v.declared_ray_bounds->origin_l2_mm},
                  {"direction_l2", v.declared_ray_bounds->direction_l2},
                  {"evidence", v.declared_ray_bounds->evidence ? content(*v.declared_ray_bounds->evidence)
                                                               : Json(nullptr)}};
    return {
        {"algorithm", "org.mantis.alg03.ray-plane/1.0.0"},
        {"arithmetic", "binary64-outward-enclosure-v1"},
        {"source_contract", v.source_contract == SourceContract::alg02_subpixel_1 ? "alg02-subpixel-1"
                                                                                  : "synthetic-reference-1"},
        {"domain", {{"a", v.domain.a}, {"b", v.domain.b}}},
        {"min_incidence_cosine", v.min_incidence_cosine},
        {"z_min_mm", v.z_min_mm},
        {"z_max_mm", v.z_max_mm},
        {"max_abs_position_mm", v.max_abs_position_mm},
        {"max_numeric_error_mm", v.max_numeric_error_mm},
        {"uncertainty_requirement",
         v.uncertainty_requirement == Uncertainty::numeric_only ? "numeric_only" : "require_declared_bound"},
        {"declared_ray_bounds", bounds},
        {"max_declared_model_error_mm",
         v.max_declared_model_error_mm ? Json(*v.max_declared_model_error_mm) : Json(nullptr)},
        {"max_samples", v.max_samples},
        {"max_source_bytes", v.max_source_bytes},
        {"max_result_bytes", v.max_result_bytes},
        {"max_geometry_bytes", v.max_geometry_bytes},
        {"max_dependency_nodes", v.max_dependency_nodes},
        {"producer", implementation(v.producer)}};
}
Policy policy(const Json &j) {
    fields(j, {"algorithm", "arithmetic", "source_contract", "domain", "min_incidence_cosine", "z_min_mm",
               "z_max_mm", "max_abs_position_mm", "max_numeric_error_mm", "uncertainty_requirement",
               "declared_ray_bounds", "max_declared_model_error_mm", "max_samples", "max_source_bytes",
               "max_result_bytes", "max_geometry_bytes", "max_dependency_nodes", "producer"});
    require(j.at("algorithm") == "org.mantis.alg03.ray-plane/1.0.0" &&
                j.at("arithmetic") == "binary64-outward-enclosure-v1",
            "Unsupported algorithm/arithmetic", Status::incompatible);
    Policy v;
    auto source = dec<std::string>(j.at("source_contract"));
    require(source == "alg02-subpixel-1" || source == "synthetic-reference-1", "Invalid source contract");
    v.source_contract = source == "alg02-subpixel-1" ? SourceContract::alg02_subpixel_1
                                                     : SourceContract::synthetic_reference_1;
    fields(j.at("domain"), {"a", "b"});
    v.domain = {dec<double>(j.at("domain").at("a")), dec<double>(j.at("domain").at("b"))};
    v.min_incidence_cosine = dec<double>(j.at("min_incidence_cosine"));
    v.z_min_mm = dec<double>(j.at("z_min_mm"));
    v.z_max_mm = dec<double>(j.at("z_max_mm"));
    v.max_abs_position_mm = dec<double>(j.at("max_abs_position_mm"));
    v.max_numeric_error_mm = dec<double>(j.at("max_numeric_error_mm"));
    auto uncertainty = dec<std::string>(j.at("uncertainty_requirement"));
    require(uncertainty == "numeric_only" || uncertainty == "require_declared_bound",
            "Invalid uncertainty rule");
    v.uncertainty_requirement =
        uncertainty == "numeric_only" ? Uncertainty::numeric_only : Uncertainty::require_declared_bound;
    if (!j.at("declared_ray_bounds").is_null()) {
        const auto &b = j.at("declared_ray_bounds");
        fields(b, {"origin_l2_mm", "direction_l2", "evidence"});
        v.declared_ray_bounds =
            RayBounds{dec<double>(b.at("origin_l2_mm")), dec<double>(b.at("direction_l2")), {}};
        if (!b.at("evidence").is_null())
            v.declared_ray_bounds->evidence = content(b.at("evidence"));
    }
    v.max_declared_model_error_mm = dec<std::optional<double>>(j.at("max_declared_model_error_mm"));
    v.max_samples = dec<uint32_t>(j.at("max_samples"));
    v.max_source_bytes = dec<uint64_t>(j.at("max_source_bytes"));
    v.max_result_bytes = dec<uint64_t>(j.at("max_result_bytes"));
    v.max_geometry_bytes = dec<uint64_t>(j.at("max_geometry_bytes"));
    v.max_dependency_nodes = dec<uint32_t>(j.at("max_dependency_nodes"));
    v.producer = implementation(j.at("producer"));
    return v;
}
bool same(const data::ContentReference &a, const data::ContentReference &b) {
    return content(a) == content(b);
}
bool same(const data::ExactCalibrationReference &a, const data::ExactCalibrationReference &b) {
    return exact(a) == exact(b);
}
bool same(const spatial::CoordinateFrame &a, const spatial::CoordinateFrame &b) {
    return a.id == b.id && a.name == b.name;
}
void reference_shape(const data::ContentReference &r) {
    require(!r.id.value.empty() && r.id.value.size() <= 256 && !r.type.name.empty() &&
                r.type.name.size() <= 1024 && r.type.version > 0,
            "Invalid exact content identity");
    auto h = r.hash.get();
    require(h && !h->algorithm.empty() && h->algorithm.size() <= 1024 && !h->hex.empty() &&
                h->hex.size() <= 1024,
            "Resolved content hash required");
}
const data::ContentReference &resolved(const data::ExactCalibrationReference &r) {
    require(!r.calibration.id.value.empty() && r.calibration.id.value.size() <= 256 &&
                r.calibration.revision > 0 && r.calibration.schema_version == 1,
            "Resolved schema1 logical calibration required", Status::incompatible);
    auto c = r.content.get();
    require(c, "Resolved calibration content required", Status::incompatible);
    reference_shape(*c);
    require(c->revision == r.calibration.revision && c->type.version == 1,
            "Calibration reference version mismatch", Status::incompatible);
    return *c;
}
void producer_shape(const data::ImplementationIdentity &p) {
    require(!p.implementation.value.empty() && p.implementation.value.size() <= 256 && !p.build.empty() &&
                p.build.size() <= 1024,
            "Producer identity/build required");
    if (auto c = p.configuration.get())
        reference_shape(*c);
}
Json envelope(std::string_view kind, Json payload) {
    return {{"kind", kind}, {"schema_version", 1u}, {"payload", std::move(payload)}};
}
uint64_t geometry_charge(std::string_view bytes, uint64_t cap, const CancellationToken &token) {
    // Reserve for canonical buffers/serializer scratch and string/typed copies.
    // 512 bytes per lexical value OR key covers DOM nodes, map links, vector
    // growth, parser stack and the simultaneously retained fixed-schema copies.
    // Escapes can only shorten decoded strings. Count malformed input too: the
    // real parser still rejects it, but cannot allocate before this admission.
    uint64_t amount = 0;
    auto add = [&](uint64_t n) {
        require(n <= cap - amount, "Decoded geometry preallocation admission", Status::busy);
        amount += n;
    };
    require(bytes.size() <= cap / 16, "Geometry canonical working admission", Status::busy);
    add(16 * bytes.size());
    unsigned depth = 0;
    for (size_t i = 0; i < bytes.size();) {
        token.check();
        const char c = bytes[i++];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ',' || c == ':')
            continue;
        if (c == '}' || c == ']') {
            require(depth != 0, "Geometry JSON nesting");
            --depth;
            continue;
        }
        add(512);
        if (c == '{' || c == '[') {
            require(++depth <= 64, "Geometry JSON depth", Status::busy);
        } else if (c == '"') {
            while (i < bytes.size()) {
                token.check();
                const char next = bytes[i++];
                if (next == '"')
                    break;
                if (next == '\\' && i < bytes.size())
                    ++i;
            }
        } else {
            while (i < bytes.size() && bytes[i] != ',' && bytes[i] != '}' && bytes[i] != ']' &&
                   bytes[i] != ':' && bytes[i] != ' ' && bytes[i] != '\n' && bytes[i] != '\r' &&
                   bytes[i] != '\t' && bytes[i] != '"' && bytes[i] != '[' && bytes[i] != '{')
                ++i;
        }
    }
    return amount;
}
Json parse_document(std::string_view bytes, std::string_view kind) {
    require(!bytes.empty() && bytes.size() <= 262144, "ALG03 document size bound", Status::busy);
    (void)geometry_charge(bytes, max_geometry_bytes);
    auto j = Json::parse(bytes, [](int depth, Json::parse_event_t, Json &) {
        require(depth <= 64, "Document nesting bound");
        return true;
    });
    fields(j, {"kind", "schema_version", "payload"});
    require(j.at("kind") == kind && dec<uint32_t>(j.at("schema_version")) == 1, "Unsupported ALG03 schema",
            Status::incompatible);
    require(j.dump() == bytes, "Noncanonical ALG03 document");
    return j.at("payload");
}
data::Packet document_packet(std::string bytes) {
    require(!bytes.empty() && bytes.size() <= 262144, "ALG03 document byte bound", Status::busy);
    data::Packet p;
    p.type = schema::tensor;
    p.attributes.push_back(
        {{std::string(document_attribute), schema::ScalarType::u8, {bytes.size()}, {1}, "utf8-json"},
         memory::copy(std::as_bytes(std::span(bytes)))});
    return p;
}
std::string document_bytes(const data::Packet &p, std::string_view attribute,
                           const CancellationToken &token) {
    require(p.type == schema::tensor && p.frames.empty() && !p.attributes.empty(),
            "Expected flat Tensor1 document");
    const auto &a = p.attributes[0];
    require(a.descriptor.name == attribute && a.descriptor.scalar == schema::ScalarType::u8 &&
                a.descriptor.shape == std::vector<uint64_t>{a.buffer.size()} &&
                a.descriptor.stride == std::vector<uint64_t>{1} && a.descriptor.unit == "utf8-json" &&
                a.buffer.size() > 0 && a.buffer.size() <= 262144,
            "Invalid ALG03 manifest/document layout");
    auto b = a.buffer.map_read(token);
    if (!b)
        throw Failure(b.error());
    return {reinterpret_cast<const char *>(b->data()), b->size()};
}
artifact::ArtifactDescriptor descriptor(const artifact::Store &s, const data::ContentReference &r,
                                        const artifact::ArtifactType &type, const CancellationToken &t) {
    t.check();
    reference_shape(r);
    require(r.type.name == type.name && r.type.version == type.schema_version,
            "Wrong processing reference type/schema", Status::incompatible);
    auto d = s.get(r.id);
    require(d.type.name == type.name && d.type.schema_version == type.schema_version &&
                d.state == artifact::ArtifactState::finalized && d.hash == *r.hash.get(),
            "Processing artifact type/state/hash mismatch", Status::incompatible);
    return d;
}
static void add(std::vector<Id> &v, const data::ContentReference &r) {
    reference_shape(r);
    if (std::find(v.begin(), v.end(), r.id) == v.end())
        v.push_back(r.id);
}
std::vector<Id> inputs(const Plane &p) {
    std::vector<Id> v;
    add(v, resolved(p.rig));
    for (auto &c : p.cameras)
        add(v, resolved(c.calibration));
    if (p.parameter_bounds && p.parameter_bounds->evidence)
        add(v, *p.parameter_bounds->evidence);
    if (auto c = p.producer.configuration.get())
        add(v, *c);
    return v;
}
std::vector<Id> inputs(const Policy &p) {
    std::vector<Id> v;
    if (p.declared_ray_bounds && p.declared_ray_bounds->evidence)
        add(v, *p.declared_ray_bounds->evidence);
    if (auto c = p.producer.configuration.get())
        add(v, *c);
    return v;
}
std::vector<Id> inputs(const Bindings &b) {
    std::vector<Id> v;
    for (auto &r : {b.observation, resolved(b.camera), resolved(b.rig), b.plane, b.policy})
        add(v, r);
    return v;
}
} // namespace mantis::alg03::detail

namespace mantis::alg03 {
using namespace detail;
Result<void> validate(const Plane &p) {
    return result([&] {
        require(resolved(p.rig).type.name == "org.mantis.RigCalibration", "Plane requires rig",
                Status::incompatible);
        require(!p.frame.id.value.empty() && !p.frame.name.empty() && p.frame.name.size() <= 1024,
                "Plane frame required");
        double norm = std::hypot(p.normal_xyz[0], p.normal_xyz[1], p.normal_xyz[2]);
        require(std::isfinite(norm) && std::abs(norm - 1) <= 256 * std::numeric_limits<double>::epsilon() &&
                    std::isfinite(p.offset_mm) && std::abs(p.offset_mm) <= 1e6,
                "Invalid declared unit plane");
        require(p.side == Side::two_sided || p.side == Side::camera_on_positive_side, "Invalid plane side");
        (void)origin(p.origin);
        for (auto id : {p.line.emitter.id, p.line.pattern.id, p.line.local_line.id})
            require(!id.value.empty() && id.value.size() <= 256, "Invalid declared line identity");
        require(p.line.pattern_revision > 0 && !p.cameras.empty() && p.cameras.size() <= 2,
                "Plane applicability bound");
        std::string previous;
        for (auto &c : p.cameras) {
            require(!c.component.id.value.empty() && c.component.id.value.size() <= 256 &&
                        (previous.empty() || previous < c.component.id.value),
                    "Plane camera order/identity");
            previous = c.component.id.value;
            auto v = calibration::solve_detail::camera_valid(c.camera);
            if (!v)
                throw Failure(v.error());
            require(resolved(c.calibration).type.name == "org.mantis.CameraCalibration",
                    "Plane camera artifact type");
        }
        if (p.parameter_bounds) {
            auto &b = *p.parameter_bounds;
            require(std::isfinite(b.normal_l2) && b.normal_l2 >= 0 && b.normal_l2 < 1 &&
                        std::isfinite(b.offset_abs_mm) && b.offset_abs_mm >= 0,
                    "Invalid plane parameter bounds");
            if (b.evidence)
                reference_shape(*b.evidence);
        }
        producer_shape(p.producer);
    });
}
Result<void> validate(const Policy &p) {
    return result([&] {
        auto range = [](double x, double lo, double hi) { return std::isfinite(x) && x >= lo && x <= hi; };
        require(range(p.domain.a, std::numeric_limits<double>::min(), 16) &&
                    range(p.domain.b, std::numeric_limits<double>::min(), 16) &&
                    range(p.min_incidence_cosine, .01, 1) &&
                    range(p.z_min_mm, std::numeric_limits<double>::min(), 1e6) &&
                    range(p.z_max_mm, p.z_min_mm, 1e6) &&
                    range(p.max_abs_position_mm, std::numeric_limits<double>::min(), 1e6) &&
                    range(p.max_numeric_error_mm, std::numeric_limits<double>::min(), 1),
                "Invalid numerical policy");
        require((p.source_contract == SourceContract::alg02_subpixel_1 ||
                 p.source_contract == SourceContract::synthetic_reference_1) &&
                    (p.uncertainty_requirement == Uncertainty::numeric_only ||
                     p.uncertainty_requirement == Uncertainty::require_declared_bound),
                "Unknown policy enum");
        require(p.max_samples > 0 && p.max_samples <= alg03::max_samples && p.max_dependency_nodes > 0 &&
                    p.max_dependency_nodes <= 4096 && p.max_source_bytes > 0 &&
                    p.max_source_bytes <= alg03::max_source_bytes && p.max_result_bytes > 0 &&
                    p.max_result_bytes <= alg03::max_result_bytes && p.max_geometry_bytes > 0 &&
                    p.max_geometry_bytes <= alg03::max_geometry_bytes,
                "Invalid resource policy");
        if (p.declared_ray_bounds) {
            auto &b = *p.declared_ray_bounds;
            require(range(b.origin_l2_mm, 0, 1e308) && range(b.direction_l2, 0, std::nextafter(1., 0.)),
                    "Invalid ray parameter bounds");
            if (b.evidence)
                reference_shape(*b.evidence);
        }
        require(p.uncertainty_requirement == Uncertainty::numeric_only
                    ? !p.max_declared_model_error_mm
                    : p.max_declared_model_error_mm &&
                          range(*p.max_declared_model_error_mm, std::numeric_limits<double>::min(), 1e6),
                "Invalid declared-bound policy");
        producer_shape(p.producer);
    });
}
Result<std::string> encode_document(const Plane &p) {
    return result([&] {
        auto v = validate(p);
        if (!v)
            throw Failure(v.error());
        return envelope(plane_type.name, plane(p)).dump();
    });
}
Result<std::string> encode_document(const Policy &p) {
    return result([&] {
        auto v = validate(p);
        if (!v)
            throw Failure(v.error());
        return envelope(policy_type.name, policy(p)).dump();
    });
}
} // namespace mantis::alg03
