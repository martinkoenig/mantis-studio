#include <cmath>
#include <cstring>
#include <limits>
#include <mantis/semantic_views.hpp>
#include <set>
#include <type_traits>

namespace mantis::plugins::semantic {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        fail(Status::incompatible, message, "semantic-abi");
}
template <class T> void init(T &v) {
    v.struct_size = sizeof(T);
    v.abi_version = MANTIS_ABI_V1;
}
uint32_t count(size_t n) {
    require(n <= data::max_semantic_entries, "Excessive semantic count");
    return static_cast<uint32_t>(n);
}
struct Reader {
    const MantisHostV1 *host{};
    size_t entries{}, text_bytes{}, payload_bytes{};
    template <class T> void prefix(const T &v) {
        require(sdk::compatible_table(&v), "Invalid semantic size/version");
        require(++entries <= 65536, "Semantic view entry budget exceeded");
    }
    void array(const void *p, uint32_t n, uint32_t maximum = MANTIS_MAX_SEMANTIC_ENTRIES) {
        require(n <= maximum && ((p != nullptr) == (n != 0)), "Invalid semantic array pointer/count");
        require(n <= 65536 - entries, "Semantic array budget exceeded");
        entries += n;
    }
    std::string text(const char *p) {
        require(p, "Null required semantic string");
        size_t n = 0;
        while (n <= data::max_semantic_string && p[n])
            ++n;
        require(n <= data::max_semantic_string, "Semantic string too long");
        text_bytes += n;
        require(text_bytes <= 4 * 1024 * 1024, "Semantic text budget exceeded");
        return {p, n};
    }
};
struct Writer {
    const MantisHostV1 *host{};
    std::vector<std::shared_ptr<void>> storage;
    std::vector<MantisBuffer *> buffers;
    ~Writer() {
        for (auto *b : buffers)
            host->release(b);
    }
    template <class T> T *allocate(size_t n = 1) {
        auto p = std::shared_ptr<T[]>(new T[n]{});
        auto *raw = p.get();
        storage.emplace_back(p, raw);
        return raw;
    }
    const char *text(const std::string &s) {
        require(s.size() <= data::max_semantic_string, "Semantic string too long");
        auto *p = allocate<char>(s.size() + 1);
        std::memcpy(p, s.c_str(), s.size() + 1);
        return p;
    }
};
#include "semantic_fields_forward.inc"
data::Attribute decode(const MantisAttributeV1 &, Reader &);
MantisAttributeV1 encode(const data::Attribute &, Writer &);
data::Metadata decode(const MantisMetadataV1 &, Reader &);
MantisMetadataV1 encode(const data::Metadata &, Writer &);
std::vector<data::ComponentId> decode(const MantisComponentListV1 &, Reader &);
MantisComponentListV1 encode(const std::vector<data::ComponentId> &, Writer &);
template <class E> constexpr uint32_t enum_max = 0;
#include "semantic_enums.inc"
template <class T> inline constexpr bool semantic_id = false;
template <class Tag> inline constexpr bool semantic_id<data::SemanticId<Tag>> = true;
template <class T> inline constexpr bool semantic_sequence = false;
template <class Tag> inline constexpr bool semantic_sequence<data::SemanticSequence<Tag>> = true;
template <> inline constexpr bool semantic_sequence<time::SequenceNumber> = true;
template <class T, class C> T decode_value(const C &v, Reader &r) {
    if constexpr (std::is_same_v<T, std::string>)
        return r.text(v);
    else if constexpr (std::is_same_v<T, Id>) {
        auto s = r.text(v);
        require(s.size() <= data::max_semantic_id, "Identity too long");
        return {std::move(s)};
    } else if constexpr (semantic_id<T>) {
        T o;
        o.id = decode_value<Id>(v, r);
        return o;
    } else if constexpr (std::is_enum_v<T>) {
        require(v <= enum_max<T>, "Invalid semantic enum");
        return static_cast<T>(v);
    } else if constexpr (std::is_arithmetic_v<T>) {
        if constexpr (std::is_floating_point_v<T>)
            require(std::isfinite(v), "Nonfinite semantic number");
        return v;
    } else if constexpr (std::is_same_v<T, data::Duration>)
        return data::Duration{v};
    else if constexpr (semantic_sequence<T>)
        return T{v};
    else if constexpr (std::is_same_v<T, time::MonotonicTimestamp>)
        return {v};
    else
        return decode(v, r);
}
template <class T, class C> data::Evidence<T> decode_evidence(const C &v, Reader &r) {
    r.prefix(v);
    require(v.presence <= MANTIS_PRESENCE_UNAVAILABLE, "Invalid evidence presence");
    require((v.presence == MANTIS_PRESENCE_ESTABLISHED) == (v.value != nullptr),
            "Evidence pointer/presence mismatch");
    if (v.presence == MANTIS_PRESENCE_UNKNOWN)
        return data::Unknown{};
    if (v.presence == MANTIS_PRESENCE_UNAVAILABLE)
        return data::Unavailable{};
    return decode_value<T>(*v.value, r);
}
template <class T, class C>
std::vector<T> decode_array(const C *p, uint32_t n, Reader &r,
                            uint32_t maximum = MANTIS_MAX_SEMANTIC_ENTRIES) {
    r.array(p, n, maximum);
    std::vector<T> out;
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i)
        out.push_back(decode_value<T>(p[i], r));
    return out;
}
template <class T, class C> std::optional<T> decode_required(const C &v, Reader &r) {
    auto value = decode_evidence<T>(v, r);
    require(value.get(), "Missing required semantic value; absence is not enum zero");
    return *value.get();
}
template <class T, class C> std::optional<T> decode_optional(const C *p, Reader &r) {
    if (!p)
        return {};
    return decode_value<T>(*p, r);
}
std::array<double, 9> decode_matrix(const double *v) {
    std::array<double, 9> out;
    for (size_t i = 0; i < 9; ++i) {
        require(std::isfinite(v[i]), "Nonfinite matrix");
        out[i] = v[i];
    }
    return out;
}
template <class T> auto encode_value(const T &v, Writer &w) {
    if constexpr (std::is_same_v<T, std::string>)
        return w.text(v);
    else if constexpr (std::is_same_v<T, Id>)
        return w.text(v.value);
    else if constexpr (semantic_id<T>)
        return w.text(v.id.value);
    else if constexpr (std::is_enum_v<T>)
        return static_cast<uint32_t>(v);
    else if constexpr (std::is_arithmetic_v<T>)
        return v;
    else if constexpr (std::is_same_v<T, data::Duration>)
        return v.count();
    else if constexpr (semantic_sequence<T>)
        return v.value;
    else if constexpr (std::is_same_v<T, time::MonotonicTimestamp>)
        return v.nanoseconds;
    else
        return encode(v, w);
}
template <class C, class T> C encode_evidence(const data::Evidence<T> &v, Writer &w) {
    C out{};
    init(out);
    out.presence = static_cast<uint32_t>(v.presence());
    if (v.get()) {
        auto converted = encode_value(*v.get(), w);
        auto *p = w.allocate<decltype(converted)>();
        *p = converted;
        out.value = p;
    }
    return out;
}
template <class C, class T> const C *encode_array(const std::vector<T> &v, Writer &w) {
    if (v.empty())
        return nullptr;
    count(v.size());
    auto *p = w.allocate<C>(v.size());
    for (size_t i = 0; i < v.size(); ++i)
        p[i] = encode_value(v[i], w);
    return p;
}
template <class C, class T> const C *encode_optional(const std::optional<T> &v, Writer &w) {
    if (!v)
        return nullptr;
    auto *p = w.allocate<C>();
    *p = encode_value(*v, w);
    return p;
}
template <class C, class T> C encode_required(const std::optional<T> &v, Writer &w) {
    require(v.has_value(), "Missing required semantic value");
    return encode_evidence<C>(data::Evidence<T>{*v}, w);
}
std::vector<data::ComponentId> decode(const MantisComponentListV1 &v, Reader &r) {
    r.prefix(v);
    return decode_array<data::ComponentId>(v.items, v.count, r, 64);
}
MantisComponentListV1 encode(const std::vector<data::ComponentId> &v, Writer &w) {
    MantisComponentListV1 out{};
    init(out);
    out.items = encode_array<const char *>(v, w);
    out.count = count(v.size());
    return out;
}
data::Metadata decode(const MantisMetadataV1 &v, Reader &r) {
    r.prefix(v);
    r.array(v.items, v.count, 256);
    data::Metadata out;
    for (uint32_t i = 0; i < v.count; ++i) {
        r.prefix(v.items[i]);
        require(out.emplace(r.text(v.items[i].key), r.text(v.items[i].value)).second,
                "Duplicate metadata key");
    }
    return out;
}
MantisMetadataV1 encode(const data::Metadata &v, Writer &w) {
    MantisMetadataV1 out{};
    init(out);
    out.count = count(v.size());
    if (v.empty())
        return out;
    auto *p = w.allocate<MantisMetadataEntryV1>(v.size());
    size_t i = 0;
    for (const auto &[k, val] : v) {
        init(p[i]);
        p[i].key = w.text(k);
        p[i].value = w.text(val);
        ++i;
    }
    out.items = p;
    return out;
}
data::Attribute decode(const MantisAttributeV1 &v, Reader &r) {
    r.prefix(v);
    require(v.rank > 0 && v.rank <= 4 && v.scalar_type >= 1 && v.scalar_type <= 5 && v.buffer,
            "Invalid attribute");
    require(r.host && r.host->retain && r.host->release && r.host->read_map, "Invalid buffer host");
    const void *bytes{};
    uint64_t size{};
    require(!r.host->read_map(v.buffer, &bytes, &size) && bytes, "Unpublished/unreadable buffer");
    require(v.offset <= size && v.bytes <= size - v.offset && size <= data::max_semantic_payload,
            "Attribute buffer range");
    require(v.bytes <= data::max_semantic_payload - r.payload_bytes, "Semantic payload budget");
    r.payload_bytes += static_cast<size_t>(v.bytes);
    data::Attribute out;
    out.descriptor = {r.text(v.name),
                      static_cast<schema::ScalarType>(v.scalar_type),
                      {v.shape, v.shape + v.rank},
                      {v.stride, v.stride + v.rank},
                      r.text(v.unit)};
    auto valid = schema::validate(out.descriptor, static_cast<size_t>(v.bytes));
    require(bool(valid), "Invalid attribute layout");
    auto storage = std::make_shared<memory::Storage>();
    r.host->retain(v.buffer);
    storage->owner = {v.buffer, [host = r.host](const void *p) {
                          host->release(static_cast<MantisBuffer *>(const_cast<void *>(p)));
                      }};
    storage->host = static_cast<const std::byte *>(bytes);
    storage->size = static_cast<size_t>(size);
    storage->alignment = 1;
    out.buffer = {std::move(storage), static_cast<size_t>(v.offset), static_cast<size_t>(v.bytes)};
    return out;
}
MantisAttributeV1 encode(const data::Attribute &v, Writer &w) {
    require(w.host && sdk::compatible(w.host) && w.host->allocate && w.host->retain && w.host->release &&
                w.host->write_map && w.host->read_map && w.host->publish,
            "Invalid buffer host");
    auto valid = schema::validate(v.descriptor, v.buffer.size());
    require(bool(valid), "Invalid attribute layout");
    auto bytes = v.buffer.map_read();
    require(bool(bytes), "Unmapped semantic buffer");
    auto *buffer = w.host->allocate(v.buffer.size(), 64);
    require(buffer, "Buffer allocation failed");
    try {
        w.buffers.push_back(buffer);
    } catch (...) {
        w.host->release(buffer);
        throw;
    }
    void *dest{};
    uint64_t size{};
    require(!w.host->write_map(buffer, &dest, &size) && size == v.buffer.size(), "Buffer write failed");
    std::memcpy(dest, bytes->data(), bytes->size());
    require(!w.host->publish(buffer), "Buffer publish failed");
    MantisAttributeV1 out{};
    init(out);
    out.name = w.text(v.descriptor.name);
    out.unit = w.text(v.descriptor.unit);
    out.scalar_type = static_cast<uint32_t>(v.descriptor.scalar);
    out.rank = static_cast<uint32_t>(v.descriptor.shape.size());
    std::copy(v.descriptor.shape.begin(), v.descriptor.shape.end(), out.shape);
    std::copy(v.descriptor.stride.begin(), v.descriptor.stride.end(), out.stride);
    out.buffer = buffer;
    out.bytes = v.buffer.size();
    return out;
}
#include "semantic_fields.inc"
void domain_valid(const auto &v) {
    auto result = data::validate(v);
    if (!result)
        fail(Status::incompatible, result.error().message, "semantic-abi");
}
data::Published decode_packet(const MantisDataPacketV1 &v, Reader &r, bool child = false) {
    r.prefix(v);
    auto type = decode(v.type, r);
    r.array(v.attributes, v.attribute_count, 128);
    r.array(v.frames, v.frame_count, 16);
    require(type.version > 0 && type.name.find('.') != std::string::npos, "Invalid packet schema");
    require(type.name != MANTIS_ACQUISITION_BUNDLE && type.name != MANTIS_ACQUISITION_EVIDENCE &&
                type.name != MANTIS_TRIGGER_EVENT && type.name != MANTIS_LASER_OBSERVATION &&
                type.name != MANTIS_ACQUISITION_PROGRAM,
            "Typed semantics cannot use flat data view");
    if (type.name == MANTIS_FRAMESET || type.name == MANTIS_IMAGE)
        require(type.version == 1, "Unsupported image/FrameSet schema");
    if (type == schema::frameset)
        require(!child && v.frame_count > 0 && !v.attribute_count, "Invalid FrameSet shape");
    else
        require(!v.frame_count && (!child || type == schema::image), "Invalid packet children");
    data::Packet out;
    out.type = std::move(type);
    out.header = decode(v.header, r);
    out.attributes = decode_array<data::Attribute>(v.attributes, v.attribute_count, r);
    for (uint32_t i = 0; i < v.frame_count; ++i)
        out.frames.push_back(decode_packet(v.frames[i], r, true));
    return data::publish(std::move(out));
}
MantisDataPacketV1 encode_packet(const data::Packet &v, Writer &w) {
    require(v.frames.size() <= 16 && v.attributes.size() <= 128, "Invalid packet bounds");
    require(v.type.version > 0 && v.type.name.find('.') != std::string::npos, "Invalid packet schema");
    require(v.type.name != MANTIS_ACQUISITION_BUNDLE && v.type.name != MANTIS_ACQUISITION_EVIDENCE &&
                v.type.name != MANTIS_TRIGGER_EVENT && v.type.name != MANTIS_LASER_OBSERVATION &&
                v.type.name != MANTIS_ACQUISITION_PROGRAM,
            "Typed semantics cannot use flat data view");
    if (v.type.name == MANTIS_FRAMESET || v.type.name == MANTIS_IMAGE)
        require(v.type.version == 1, "Unsupported image/FrameSet schema");
    if (v.type == schema::frameset) {
        require(!v.frames.empty() && v.attributes.empty(), "Invalid FrameSet");
        for (const auto &f : v.frames)
            require(f && f->type == schema::image && f->frames.empty(), "Invalid FrameSet child");
    } else
        require(v.frames.empty(), "Invalid composite packet");
    MantisDataPacketV1 out{};
    init(out);
    out.type = encode(v.type, w);
    out.header = encode(v.header, w);
    out.attributes = encode_array<MantisAttributeV1>(v.attributes, w);
    out.attribute_count = count(v.attributes.size());
    out.frame_count = count(v.frames.size());
    if (!v.frames.empty()) {
        auto *frames = w.allocate<MantisDataPacketV1>(v.frames.size());
        for (size_t i = 0; i < v.frames.size(); ++i)
            frames[i] = encode_packet(*v.frames[i], w);
        out.frames = frames;
    }
    return out;
}
data::AcquisitionBundle decode_bundle(const MantisAcquisitionBundleV1 &v, Reader &r) {
    r.prefix(v);
    require(v.trigger_count <= 63 && v.member_count <= 64 &&
                v.member_count == 1u + uint32_t(v.frameset != nullptr) + v.trigger_count,
            "Invalid bundle member count");
    data::AcquisitionBundle out;
    out.type = decode(v.type, r);
    out.key = decode(v.key, r);
    out.published = decode(v.published, r);
    out.evidence = decode(v.evidence, r);
    if (v.frameset)
        out.frameset = decode_packet(*v.frameset, r);
    out.triggers = decode_array<data::TriggerEvent>(v.triggers, v.trigger_count, r);
    domain_valid(out);
    return out;
}
MantisAcquisitionBundleV1 encode_bundle(const data::AcquisitionBundle &v, Writer &w) {
    domain_valid(v);
    MantisAcquisitionBundleV1 out{};
    init(out);
    out.type = encode(v.type, w);
    out.key = encode(v.key, w);
    out.published = encode(v.published, w);
    out.evidence = encode(v.evidence, w);
    if (v.frameset) {
        auto *p = w.allocate<MantisDataPacketV1>();
        *p = encode_packet(*v.frameset, w);
        out.frameset = p;
    }
    out.triggers = encode_array<MantisTriggerEventV1>(v.triggers, w);
    out.trigger_count = count(v.triggers.size());
    out.member_count = 1u + uint32_t(bool(v.frameset)) + out.trigger_count;
    return out;
}
} // namespace
ProgramView::ProgramView(const data::AcquisitionProgram &v) {
    domain_valid(v);
    auto writer = std::make_shared<Writer>();
    view_ = encode(v, *writer);
    owner_ = writer;
}
data::AcquisitionProgram program(const MantisAcquisitionProgramV1 *v) {
    require(v, "Null program");
    Reader r;
    auto out = decode(*v, r);
    domain_valid(out);
    return out;
}
PacketView::PacketView(const Packet &v, const MantisHostV1 *host) {
    auto w = std::make_shared<Writer>();
    w->host = host;
    init(view_);
    std::visit(
        [&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, data::Published>) {
                require(bool(value), "Null packet");
                auto *p = w->allocate<MantisDataPacketV1>();
                *p = encode_packet(*value, *w);
                view_.kind = MANTIS_SEMANTIC_DATA;
                view_.data = p;
            } else if constexpr (std::is_same_v<T, data::AcquisitionBundle>) {
                auto *p = w->allocate<MantisAcquisitionBundleV1>();
                *p = encode_bundle(value, *w);
                view_.kind = MANTIS_SEMANTIC_BUNDLE;
                view_.bundle = p;
            } else {
                domain_valid(value);
                if constexpr (std::is_same_v<T, data::AcquisitionEvidence>) {
                    auto *p = w->allocate<MantisAcquisitionEvidenceV1>();
                    *p = encode(value, *w);
                    view_.kind = MANTIS_SEMANTIC_EVIDENCE;
                    view_.evidence = p;
                }
                if constexpr (std::is_same_v<T, data::TriggerEvent>) {
                    auto *p = w->allocate<MantisTriggerEventV1>();
                    *p = encode(value, *w);
                    view_.kind = MANTIS_SEMANTIC_TRIGGER;
                    view_.trigger = p;
                }
                if constexpr (std::is_same_v<T, data::LaserObservation>) {
                    auto *p = w->allocate<MantisLaserObservationV1>();
                    *p = encode(value, *w);
                    view_.kind = MANTIS_SEMANTIC_LASER;
                    view_.laser = p;
                }
            }
        },
        v);
    owner_ = w;
}
Packet packet(const MantisSemanticPacketV1 *v, const MantisHostV1 *host) {
    require(v && sdk::compatible(host), "Null semantic packet/host");
    Reader r;
    r.host = host;
    r.prefix(*v);
    require(uint32_t(v->data != nullptr) + uint32_t(v->evidence != nullptr) +
                    uint32_t(v->trigger != nullptr) + uint32_t(v->bundle != nullptr) +
                    uint32_t(v->laser != nullptr) ==
                1,
            "Semantic packet requires exactly one typed value");
    switch (v->kind) {
    case MANTIS_SEMANTIC_DATA:
        require(v->data, "Wrong semantic member type");
        return decode_packet(*v->data, r);
    case MANTIS_SEMANTIC_EVIDENCE: {
        require(v->evidence, "Wrong semantic member type");
        auto out = decode(*v->evidence, r);
        domain_valid(out);
        return out;
    }
    case MANTIS_SEMANTIC_TRIGGER: {
        require(v->trigger, "Wrong semantic member type");
        auto out = decode(*v->trigger, r);
        domain_valid(out);
        return out;
    }
    case MANTIS_SEMANTIC_BUNDLE:
        require(v->bundle, "Wrong semantic member type");
        return decode_bundle(*v->bundle, r);
    case MANTIS_SEMANTIC_LASER: {
        require(v->laser, "Wrong semantic member type");
        auto out = decode(*v->laser, r);
        domain_valid(out);
        return out;
    }
    default:
        fail(Status::incompatible, "Invalid semantic packet kind");
    }
}
namespace {
template <class C> data::Evidence<bool> boolean(const C &v, Reader &r) {
    auto value = decode_evidence<uint32_t>(v, r);
    if (value.presence() == data::Presence::unknown)
        return data::Unknown{};
    if (value.presence() == data::Presence::unavailable)
        return data::Unavailable{};
    require(*value.get() <= 1, "Invalid evidence boolean");
    return *value.get() != 0;
}
device::ContractError error(const MantisContractErrorV1 &v, Reader &r) {
    r.prefix(v);
    require(v.category <= MANTIS_ERROR_CLEANUP, "Invalid error category");
    require((v.category == MANTIS_ERROR_NONE) == (v.code == 0), "Inconsistent error category/code");
    return {v.category, v.code};
}
void unique_ids(const std::vector<Id> &ids) {
    std::set<Id> seen;
    for (const auto &v : ids)
        require(!v.value.empty() && seen.insert(v).second, "Empty/duplicate relationship ID");
}
} // namespace
device::ProjectedGraph graph(const MantisProjectedGraphV1 *v) {
    require(v, "Null component graph");
    Reader r;
    r.prefix(*v);
    device::ProjectedGraph out;
    out.parent = decode_value<Id>(v->parent_id, r);
    const auto &l = v->limits;
    r.prefix(l);
    require(l.max_components > 0 && l.max_components <= 64 && l.max_steps > 0 && l.max_steps <= 256 &&
                l.max_bundle_members > 0 && l.max_bundle_members <= 64 && l.max_cameras <= 16 &&
                l.max_step_instances > 0 && l.max_step_instances <= data::max_executed_steps &&
                l.max_commands > 0 && l.max_events > 0 && l.max_bytes > 0 && l.max_in_flight_captures > 0 &&
                l.max_run_duration_ns > 0 && l.max_on_duration_ns > 0 && l.max_step_duration_ns > 0 &&
                l.max_pending_bundles > 0 && l.max_call_timeout_ms > 0 &&
                l.max_call_timeout_ms <= MANTIS_MAX_TIMEOUT_MS,
            "Invalid finite resource limits");
    out.limits = {l.max_components,
                  l.max_steps,
                  l.max_bundle_members,
                  l.max_cameras,
                  {data::Duration{l.max_run_duration_ns}, data::Duration{l.max_on_duration_ns},
                   l.max_step_instances, l.max_commands, l.max_events, l.max_bytes, l.max_in_flight_captures},
                  data::Duration{l.max_step_duration_ns},
                  l.max_pending_bundles,
                  l.max_call_timeout_ms,
                  boolean(l.watchdog, r),
                  boolean(l.interlock, r),
                  boolean(l.fail_off, r)};
    r.array(v->components, v->component_count, l.max_components);
    require(v->component_count > 0, "Empty component graph");
    std::set<Id> seen;
    for (uint32_t i = 0; i < v->component_count; ++i) {
        const auto &c = v->components[i];
        r.prefix(c);
        device::ProjectedComponent o;
        o.descriptor.id = decode_value<Id>(c.id, r);
        o.descriptor.parent = decode_value<Id>(c.parent_id, r);
        require(!o.descriptor.id.value.empty() && seen.insert(o.descriptor.id).second,
                "Duplicate/empty component ID");
        o.descriptor.name = r.text(c.name);
        o.role = r.text(c.role);
        require(!o.descriptor.name.empty() && c.participant_kind <= MANTIS_PARTICIPANT_CONTROLLER,
                "Invalid component kind/name");
        o.kind = static_cast<device::ParticipantKind>(c.participant_kind);
        o.descriptor.capabilities = decode_array<std::string>(c.capabilities, c.capability_count, r, 128);
        require(c.capability_count <= 128, "Too many capabilities");
        std::set<std::string> caps;
        for (const auto &cap : o.descriptor.capabilities)
            require(cap.find('.') != std::string::npos && caps.insert(cap).second,
                    "Invalid/duplicate capability");
        o.controls = decode_array<Id>(c.controls, c.control_count, r, 64);
        unique_ids(o.controls);
        o.participants = decode_array<Id>(c.participants, c.participant_count, r, 64);
        unique_ids(o.participants);
        o.trigger_endpoints = decode_array<Id>(c.trigger_endpoints, c.trigger_endpoint_count, r, 64);
        unique_ids(o.trigger_endpoints);
        o.emitter_states = decode_array<data::EmitterState>(c.emitter_states, c.emitter_state_count, r);
        o.capture_modes = decode_array<data::CaptureMode>(c.capture_modes, c.capture_mode_count, r);
        o.trigger_modes = decode_array<data::CaptureMode>(c.trigger_modes, c.trigger_mode_count, r);
        o.evidence_methods =
            decode_array<data::EvidenceMethod>(c.evidence_methods, c.evidence_method_count, r);
        o.evidence_scopes = decode_array<data::EvidenceScope>(c.evidence_scopes, c.evidence_scope_count, r);
        o.pattern = decode_evidence<data::PatternId>(c.pattern, r);
        o.pattern_revision = decode_evidence<uint64_t>(c.pattern_revision, r);
        o.lines = decode_array<data::LineIdentity>(c.lines, c.line_count, r);
        if (o.pattern.get())
            require(!o.pattern.get()->id.value.empty(), "Empty pattern ID");
        require(o.pattern.get() || !o.pattern_revision.get(), "Pattern revision without pattern");
        std::set<data::LineIdentity> lines;
        for (const auto &line : o.lines)
            require(line.emitter.id == o.descriptor.id && o.pattern.get() && o.pattern_revision.get() &&
                        line.pattern == *o.pattern.get() &&
                        line.pattern_revision == *o.pattern_revision.get() &&
                        !line.local_line.id.value.empty() && lines.insert(line).second,
                    "Invalid known line identity");
        auto cap = [&](const char *name) { return device::has_capability(o.descriptor, name); };
        if (o.kind == device::ParticipantKind::image)
            require(cap(MANTIS_IMAGE_STREAM_V1), "Image participant lacks image capability");
        else
            require(!cap(MANTIS_IMAGE_STREAM_V1), "Non-image participant advertises image capability");
        if (o.kind == device::ParticipantKind::emitter)
            require(cap(MANTIS_EMITTER_POWER_CONTROL_V1) && o.emitter_states.size() == 2 &&
                        o.emitter_states[0] != o.emitter_states[1],
                    "Emitter requires accessible OFF/ON");
        else
            require(o.emitter_states.empty() && !o.pattern.get() && o.lines.empty(),
                    "Non-emitter pattern/state advertisement");
        if (!o.capture_modes.empty())
            require(o.kind == device::ParticipantKind::image || o.kind == device::ParticipantKind::parent,
                    "Capture mode on non-image participant");
        if (!o.controls.empty())
            require((o.kind == device::ParticipantKind::controller ||
                     o.kind == device::ParticipantKind::parent) &&
                        cap(MANTIS_EMITTER_POWER_CONTROL_V1),
                    "Inaccessible control relationship");
        if (!o.trigger_endpoints.empty() || !o.trigger_modes.empty())
            require((o.kind == device::ParticipantKind::controller ||
                     o.kind == device::ParticipantKind::parent) &&
                        cap(MANTIS_HARDWARE_TRIGGER_V1),
                    "Inaccessible trigger relationship");
        if (!o.participants.empty())
            require(o.kind == device::ParticipantKind::parent, "Only selected parent declares participants");
        out.components.push_back(std::move(o));
    }
    auto find = [&](const Id &id) -> device::ProjectedComponent & {
        auto it = std::find_if(out.components.begin(), out.components.end(),
                               [&](const auto &c) { return c.descriptor.id == id; });
        require(it != out.components.end(), "Broken component reference");
        return *it;
    };
    auto &parent = find(out.parent);
    require(parent.kind == device::ParticipantKind::parent && parent.descriptor.parent.value.empty() &&
                device::has_capability(parent.descriptor, MANTIS_PROJECTED_LIGHT_ACQUISITION_V1),
            "Invalid projected-light parent");
    const bool integrated = std::find(parent.participants.begin(), parent.participants.end(), out.parent) !=
                            parent.participants.end();
    if (integrated)
        require(device::has_capability(parent.descriptor, MANTIS_HARDWARE_TRIGGER_V1) ||
                    device::has_capability(parent.descriptor, MANTIS_EMITTER_POWER_CONTROL_V1),
                "Parent self-participation requires controller capability");
    require(parent.participants.size() + 1 - uint32_t(integrated) == out.components.size(),
            "Parent must declare all owned participants");
    std::set<Id> control_targets, trigger_targets;
    for (auto &c : out.components) {
        if (c.descriptor.id != out.parent) {
            require(c.kind != device::ParticipantKind::parent &&
                        std::find(parent.participants.begin(), parent.participants.end(), c.descriptor.id) !=
                            parent.participants.end(),
                    "Undeclared parent participant");
            std::set<Id> ancestry;
            auto current = c.descriptor.id;
            while (current != out.parent) {
                require(ancestry.insert(current).second, "Cyclic component parent graph");
                current = find(current).descriptor.parent;
            }
            find(c.descriptor.parent).descriptor.children.push_back(c.descriptor.id);
        }
        for (const auto &id : c.controls)
            require(find(id).kind == device::ParticipantKind::emitter && control_targets.insert(id).second,
                    "Invalid/duplicate emitter control authority");
        for (const auto &id : c.trigger_endpoints)
            require(find(id).kind == device::ParticipantKind::image &&
                        device::has_capability(find(id).descriptor, MANTIS_HARDWARE_TRIGGER_V1) &&
                        trigger_targets.insert(id).second,
                    "Unsupported/duplicate trigger authority");
    }
    require(out.image_participants().size() <= l.max_cameras, "Camera limit exceeded");
    return out;
}
device::ProgramValidation validation(const MantisProgramValidationV1 *v) {
    require(v, "Null validation result");
    Reader r;
    r.prefix(*v);
    require(v->accepted <= 1, "Invalid accepted flag");
    auto e = error(v->error, r);
    require((v->accepted != 0) == (e.category == MANTIS_ERROR_NONE), "Validation result/error mismatch");
    return {v->accepted != 0, e, v->diagnostic ? r.text(v->diagnostic) : std::string{}};
}
device::ProjectedStatus status(const MantisProjectedStatusV1 *v) {
    require(v, "Null run status");
    Reader r;
    r.prefix(*v);
    require(v->state <= MANTIS_RUN_FAILED, "Invalid run state");
    device::ProjectedStatus out{static_cast<device::ProjectedRunState>(v->state),
                                decode_evidence<data::RunId>(v->run, r),
                                decode_evidence<data::GenerationId>(v->generation, r),
                                decode_evidence<data::StepInstance>(v->step, r),
                                boolean(v->commands_available, r),
                                boolean(v->evidence_available, r),
                                error(v->error, r)};
    if (out.run.get())
        require(!out.run.get()->id.value.empty(), "Empty active run");
    if (out.generation.get())
        require(!out.generation.get()->id.value.empty(), "Empty active generation");
    if (out.state == device::ProjectedRunState::started)
        require(out.run.get() && out.generation.get(), "Started status lacks run/generation");
    if (out.step.get())
        require(out.run.get() && out.step.get()->run_id == *out.run.get(), "Status step/run mismatch");
    return out;
}
device::AbortOutcome abort_outcome(const MantisAbortOutcomeV1 *v) {
    require(v, "Null abort outcome");
    Reader r;
    r.prefix(*v);
    device::AbortOutcome out{decode_evidence<data::RunId>(v->run, r),
                             decode_evidence<data::GenerationId>(v->fenced_generation, r),
                             boolean(v->inhibited, r),
                             boolean(v->stale_work_fenced, r),
                             boolean(v->off_requested, r),
                             decode_array<data::EmitterEvidence>(v->emitters, v->emitter_count, r, 64),
                             error(v->error, r)};
    require(v->emitter_count <= 64, "Too many abort emitters");
    if (out.run.get())
        require(!out.run.get()->id.value.empty(), "Empty abort run");
    if (out.fenced_generation.get())
        require(!out.fenced_generation.get()->id.value.empty(), "Empty fenced generation");
    // Use L1's evidence validator, including method/scope rules. Abort has no frames.
    data::AcquisitionEvidence evidence;
    evidence.key = {out.run.get() ? *out.run.get() : data::RunId{{"unstarted-abort"}}, {0}};
    evidence.program.id = {{"abort-contract"}};
    evidence.implementations = {{{"abort-contract"}, {1, 0, 0}, "boundary-validation", data::Unavailable{}}};
    evidence.disposition = data::AcquisitionDisposition::stopped;
    evidence.emitters = out.emitters;
    for (const auto &em : out.emitters) {
        evidence.participants.emitters.push_back(em.emitter);
        if (em.commanded.get())
            require(em.commanded.get()->state == data::EmitterState::off, "Abort commanded ON");
    }
    if (!out.emitters.empty())
        domain_valid(evidence);
    return out;
}
} // namespace mantis::plugins::semantic
