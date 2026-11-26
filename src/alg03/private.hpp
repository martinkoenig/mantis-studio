#pragma once
#include "../calibration-artifacts/codecs.hpp"
#include <bit>
#include <filesystem>
#include <mantis/data_io.hpp>
#include <mantis/l1_ray_plane.hpp>
#include <mantis/laser_observation_io.hpp>
#include <sstream>

namespace mantis::alg03::detail {
using Json = nlohmann::json;
namespace m4 = calibration::artifacts::codec;
inline void require(bool ok, std::string_view message, Status code = Status::corrupt) {
    if (!ok)
        fail(code, std::string(message), "alg03");
}
inline Error filesystem_error(const std::filesystem::filesystem_error &error, std::string_view phase) {
    // Never copy what(), paths, or custom category messages/names.
    const auto &category = error.code().category();
    const char *name = &category == &std::generic_category()  ? "generic"
                       : &category == &std::system_category() ? "system"
                                                              : "other";
    return {Status::io,
            "ALG03 filesystem phase=" + std::string(phase) + "; category=" + name +
                "; code=" + std::to_string(error.code().value()),
            "alg03"};
}
template <class F> auto result(F &&f) -> Result<decltype(f())> {
    try {
        if constexpr (std::is_void_v<decltype(f())>) {
            f();
            return {};
        } else
            return f();
    } catch (const Failure &e) {
        return std::unexpected(Error{e.error.code, e.error.message.substr(0, 1024), "alg03"});
    } catch (const std::bad_alloc &) {
        return std::unexpected(Error{Status::busy, "ALG03 allocation failed", "alg03"});
    } catch (const std::filesystem::filesystem_error &error) {
        return std::unexpected(filesystem_error(error, "unreserved operation"));
    } catch (const std::exception &) {
        return std::unexpected(Error{Status::corrupt, "Malformed ALG03 document/data", "alg03"});
    }
}
inline void fields(const Json &j, std::initializer_list<std::string_view> names) {
    require(j.is_object() && j.size() == names.size(), "ALG03 object field set mismatch");
    for (auto n : names)
        require(j.contains(n), "Missing ALG03 field");
}
template <class T> Json enc(const T &v) {
    return m4::encode(v);
}
template <class T> T dec(const Json &j) {
    return m4::decode<T>(j);
}
template <class T> Json semantic_id(const data::SemanticId<T> &v) {
    return {{"id", enc(v.id)}};
}
template <class T> data::SemanticId<T> semantic_id(const Json &j) {
    fields(j, {"id"});
    return {dec<Id>(j.at("id"))};
}
template <class T, class F> Json evidence(const data::Evidence<T> &v, F f) {
    if (auto p = v.get())
        return {{"presence", "established"}, {"value", f(*p)}};
    return {{"presence", v.presence() == data::Presence::unknown ? "unknown" : "unavailable"}};
}
template <class T, class F> data::Evidence<T> evidence(const Json &j, F f) {
    require(j.is_object() && j.contains("presence"), "Invalid Evidence");
    const auto presence = dec<std::string>(j.at("presence"));
    if (presence == "established") {
        fields(j, {"presence", "value"});
        return f(j.at("value"));
    }
    fields(j, {"presence"});
    if (presence == "unknown")
        return data::Unknown{};
    require(presence == "unavailable", "Invalid Evidence presence");
    return data::Unavailable{};
}
Json content(const data::ContentReference &);
data::ContentReference content(const Json &);
Json exact(const data::ExactCalibrationReference &);
data::ExactCalibrationReference exact(const Json &);
Json implementation(const data::ImplementationIdentity &);
data::ImplementationIdentity implementation(const Json &);
Json observation_key(const data::ObservationKey &);
Json frame_key(const data::SourceFrameKey &);
Json plane(const Plane &);
Plane plane(const Json &);
Json policy(const Policy &);
Policy policy(const Json &);
Json bindings(const Bindings &);
Bindings bindings(const Json &);
std::string origin(data::ObservationOrigin);
data::ObservationOrigin origin(const Json &);
std::string selection(Selection);
bool same(const data::ContentReference &, const data::ContentReference &);
bool same(const data::ExactCalibrationReference &, const data::ExactCalibrationReference &);
bool same(const spatial::CoordinateFrame &, const spatial::CoordinateFrame &);
const data::ContentReference &resolved(const data::ExactCalibrationReference &);
void reference_shape(const data::ContentReference &);
void producer_shape(const data::ImplementationIdentity &);
Json envelope(std::string_view kind, Json payload);
// Nonallocating lexical admission, before any JSON DOM or typed-field copies.
uint64_t geometry_charge(std::string_view, uint64_t cap, const CancellationToken & = {});
Json parse_document(std::string_view, std::string_view kind);
data::Packet document_packet(std::string);
std::string document_bytes(const data::Packet &, std::string_view attribute = document_attribute,
                           const CancellationToken & = {});
artifact::ArtifactDescriptor descriptor(const artifact::Store &, const data::ContentReference &,
                                        const artifact::ArtifactType &, const CancellationToken &,
                                        uint64_t cap = max_geometry_bytes);
std::vector<Id> inputs(const Plane &);
std::vector<Id> inputs(const Policy &);
std::vector<Id> inputs(const Bindings &);
void evidence_dependencies(const artifact::Store &, const Plane &, const Policy &, const CancellationToken &);
// Bounded streaming byte admission: skip bulk spans without reading/copying
// them. Exact source/result comparisons below are independent of content hashes.
class ByteCounter : public std::streambuf {
    uint64_t count_{}, limit_;
    CancellationToken token_;
    std::streamsize xsputn(const char *, std::streamsize n) override {
        token_.check();
        require(n >= 0 && uint64_t(n) <= limit_ - count_, "ALG03 record exceeds byte budget", Status::busy);
        count_ += uint64_t(n);
        return n;
    }
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof()))
            return traits_type::not_eof(c);
        const auto ch = traits_type::to_char_type(c);
        xsputn(&ch, 1);
        return c;
    }

  public:
    explicit ByteCounter(uint64_t limit, CancellationToken token = {})
        : limit_(limit), token_(std::move(token)) {}
    uint64_t size() const {
        return count_;
    }
};
template <class T, class F>
uint64_t encoded_size(const T &v, uint64_t cap, const CancellationToken &token, F writer) {
    ByteCounter sink(cap, token);
    std::ostream out(&sink);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    writer(out, v);
    return sink.size();
}
inline auto encoded_size(const data::LaserObservation &o, uint64_t cap, const CancellationToken &token) {
    return encoded_size(o, cap, token,
                        [](auto &out, const auto &v) { data::write_laser_observation(out, v); });
}
inline auto encoded_size(const data::Packet &p, uint64_t cap, const CancellationToken &token) {
    return encoded_size(p, cap, token, [](auto &out, const auto &v) { data::write_packet(out, v); });
}
} // namespace mantis::alg03::detail

namespace mantis::alg03::detail {
struct Geometry {
    calibration::artifacts::CameraArtifact camera;
    calibration::artifacts::RigArtifact rig;
    uint64_t retained_charge{};
};
Plane plane_snapshot(const artifact::Store &, const data::ContentReference &, const CancellationToken &,
                     uint64_t cap);
Geometry geometry(const artifact::Store &, const Bindings &, const Plane &, const Policy &,
                  const CancellationToken &);
} // namespace mantis::alg03::detail

namespace mantis::alg03::detail {
inline Error reserved_error(const artifact::Store &s, const Id &id, Error e) {
    std::string state = "UNKNOWN";
    try {
        auto cap = max_geometry_bytes;
        state = artifact::state_name(s.get_bounded(id, cap).state);
    } catch (...) { /* Do not replace the first failure. */
    }
    e.message = e.message.substr(0, 800) + "; ALG03 artifact " + id.value + "; observed state " + state;
    return e;
}
class MetadataSink final : public std::streambuf {
    std::vector<std::span<const std::byte>> ranges_;
    std::string data_;
    const std::string *expected_{};
    uint64_t count_{}, limit_;
    CancellationToken token_;
    std::streamsize xsputn(const char *s, std::streamsize n) override {
        token_.check();
        require(n >= 0, "Negative serialized extent");
        const auto begin = reinterpret_cast<uintptr_t>(s);
        for (auto range : ranges_) {
            auto base = reinterpret_cast<uintptr_t>(range.data());
            if (begin >= base && begin - base <= range.size() && uint64_t(n) <= range.size() - (begin - base))
                return n;
        }
        require(uint64_t(n) <= limit_ - count_, "Canonical metadata budget", Status::busy);
        if (expected_)
            require(count_ <= expected_->size() && uint64_t(n) <= expected_->size() - count_ &&
                        std::memcmp(s, expected_->data() + count_, static_cast<size_t>(n)) == 0,
                    "Canonical metadata mismatch", Status::incompatible);
        else
            data_.append(s, static_cast<size_t>(n));
        count_ += static_cast<uint64_t>(n);
        return n;
    }
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof()))
            return traits_type::not_eof(c);
        const char value = traits_type::to_char_type(c);
        xsputn(&value, 1);
        return c;
    }

  public:
    MetadataSink(const std::vector<data::Attribute> &attributes, uint64_t cap, const CancellationToken &token,
                 const std::string *expected = nullptr)
        : expected_(expected), limit_(cap), token_(token) {
        ranges_.reserve(attributes.size());
        for (const auto &a : attributes) {
            auto v = a.buffer.map_read(token);
            if (!v)
                throw Failure(v.error());
            ranges_.push_back(*v);
        }
    }
    std::string finish() {
        if (expected_)
            require(count_ == expected_->size(), "Canonical metadata length mismatch", Status::incompatible);
        return std::move(data_);
    }
};
// Keep writer dispatch separate from the expected-metadata comparison overloads.
template <class T, class F>
std::string serialize_metadata(const T &v, uint64_t cap, const CancellationToken &t, F writer,
                               const std::string *expected = nullptr) {
    MetadataSink sink(v.attributes, cap, t, expected);
    std::ostream stream(&sink);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    writer(stream, v);
    return sink.finish();
}
inline std::string metadata(const data::LaserObservation &v, uint64_t cap, const CancellationToken &t,
                            const std::string *expected = nullptr) {
    return serialize_metadata(
        v, cap, t, [](auto &out, const auto &o) { data::write_laser_observation(out, o); }, expected);
}
inline std::string metadata(const data::Packet &v, uint64_t cap, const CancellationToken &t,
                            const std::string *expected = nullptr) {
    return serialize_metadata(
        v, cap, t, [](auto &out, const auto &p) { data::write_packet(out, p); }, expected);
}
inline void equal_columns(const std::vector<data::Attribute> &a, const std::vector<data::Attribute> &b,
                          const CancellationToken &t) {
    require(a.size() == b.size(), "Attribute count mismatch", Status::incompatible);
    for (size_t i = 0; i < a.size(); ++i) {
        t.check();
        auto &x = a[i].descriptor;
        auto &y = b[i].descriptor;
        require(x.name == y.name && x.scalar == y.scalar && x.shape == y.shape && x.stride == y.stride &&
                    x.unit == y.unit && a[i].buffer.size() == b[i].buffer.size(),
                "Attribute descriptor/extent mismatch", Status::incompatible);
        auto xmap = a[i].buffer.map_read(t), ymap = b[i].buffer.map_read(t);
        if (!xmap)
            throw Failure(xmap.error());
        if (!ymap)
            throw Failure(ymap.error());
        for (size_t offset = 0; offset < xmap->size();) {
            t.check();
            size_t n = std::min(size_t(65536), xmap->size() - offset);
            require(std::memcmp(xmap->data() + offset, ymap->data() + offset, n) == 0,
                    "Attribute byte mismatch", Status::incompatible);
            offset += n;
        }
    }
}
inline void equal_packet(const data::Packet &a, const data::Packet &b, uint64_t cap,
                         const CancellationToken &t) {
    require(a.frames.empty() && b.frames.empty(), "ALG03 packets are flat");
    equal_columns(a.attributes, b.attributes, t);
    auto meta = metadata(a, cap, t);
    metadata(b, cap, t, &meta);
}
} // namespace mantis::alg03::detail
