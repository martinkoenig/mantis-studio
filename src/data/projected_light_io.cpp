#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <mantis/data_io.hpp>
#include <mantis/laser_observation_io.hpp>
#include <mantis/platform.hpp>
#include <mantis/projected_light_io.hpp>
#include <sstream>
#include <streambuf>
namespace mantis::data {
namespace {
constexpr uint64_t limit = 128 * 1024 * 1024;
void require(bool ok, const char *message) {
    if (!ok)
        fail(Status::corrupt, message, "MANTIS03");
}
uint64_t add(uint64_t a, uint64_t b) {
    require(b <= limit && a <= limit - b, "Encoded size exceeds bound");
    return a + b;
}
struct Writer {
    std::ostream &out;
    void byte(uint8_t n) { out.put(static_cast<char>(n)); }
    void number(uint64_t n) {
        for (unsigned i = 0; i < 8; ++i)
            byte(static_cast<uint8_t>(n >> (8 * i)));
    }
    void raw(std::string_view s) { out.write(s.data(), static_cast<std::streamsize>(s.size())); }
};
struct Reader {
    memory::BufferView storage;
    std::span<const std::byte> bytes;
    size_t pos{};
    bool observation{};
    explicit Reader(memory::BufferView v, bool obs = false) : storage(std::move(v)), observation(obs) {
        auto m = storage.map_read();
        if (!m)
            throw Failure(m.error());
        bytes = *m;
        require(bytes.size() <= limit, "Payload exceeds bound");
    }
    uint8_t byte() {
        require(pos < bytes.size(), "Truncated semantic field");
        return std::to_integer<uint8_t>(bytes[pos++]);
    }
    uint64_t number() {
        uint64_t n{};
        for (unsigned i = 0; i < 8; ++i)
            n |= uint64_t(byte()) << (8 * i);
        return n;
    }
    memory::BufferView take(uint64_t n) {
        require(n <= bytes.size() - pos, "Invalid member length");
        auto v = storage.slice(pos, static_cast<size_t>(n));
        pos += static_cast<size_t>(n);
        return v;
    }
    void magic(std::string_view s) {
        require(s.size() <= bytes.size() - pos && !std::memcmp(bytes.data() + pos, s.data(), s.size()),
                "Invalid magic");
        pos += s.size();
    }
    void end() { require(pos == bytes.size(), "Trailing data"); }
};
// Counts metadata only. Pixel sizes are computed separately from BufferView extents.
class Counter : public std::streambuf {
    bool checksum;

  public:
    explicit Counter(bool integrity = true) : checksum(integrity) {}
    uint64_t size{}, hash{14695981039346656037ull};

  protected:
    std::streamsize xsputn(const char *p, std::streamsize n) override {
        require(n >= 0, "Invalid write size");
        size = add(size, static_cast<uint64_t>(n));
        if (checksum)
            for (std::streamsize i = 0; i < n; ++i)
                hash = (hash ^ static_cast<unsigned char>(p[i])) * 1099511628211ull;
        return n;
    }
    int_type overflow(int_type c) override {
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            char b = traits_type::to_char_type(c);
            xsputn(&b, 1);
        }
        return traits_type::not_eof(c);
    }
};
// Supplies tellp for the unchanged legacy encoder even on a non-seekable ostream.
// This forwards bytes directly and stores only a position counter.
class SequentialPosition final : public std::streambuf {
    std::streambuf *target;
    uint64_t position{};

  public:
    explicit SequentialPosition(std::streambuf *t) : target(t) {}

  protected:
    std::streamsize xsputn(const char *p, std::streamsize n) override {
        auto written = target->sputn(p, n);
        position = add(position, static_cast<uint64_t>(written));
        return written;
    }
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof()))
            return traits_type::not_eof(c);
        auto result = target->sputc(traits_type::to_char_type(c));
        if (!traits_type::eq_int_type(result, traits_type::eof()))
            position = add(position, 1);
        return result;
    }
    pos_type seekoff(off_type offset, std::ios_base::seekdir dir, std::ios_base::openmode) override {
        if (offset == 0 && dir == std::ios_base::cur)
            return pos_type(position);
        return pos_type(off_type(-1));
    }
};
template <class T> struct Fields;
template <class T> struct Tags;
#include "laser_observation_fields.inc"
#include "projected_light_fields.inc"
template <class T> struct Fields<SemanticId<T>> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.id); }
};
template <class T> struct Fields<SemanticSequence<T>> {
    template <class A, class V> static void apply(A &a, V &v) { a(v.value); }
};
template <class T> struct IsVector : std::false_type {};
template <class T> struct IsVector<std::vector<T>> : std::true_type {
    using value_type = T;
};
template <class T> struct IsOptional : std::false_type {};
template <class T> struct IsOptional<std::optional<T>> : std::true_type {
    using value_type = T;
};
template <class T> struct IsEvidence : std::false_type {};
template <class T> struct IsEvidence<Evidence<T>> : std::true_type {
    using value_type = T;
};
template <class T> void put(Writer &, const T &);
template <class T> T get(Reader &);
struct Output {
    Writer &w;
    template <class... T> void operator()(const T &...v) { (put(w, v), ...); }
};
struct Input {
    Reader &r;
    template <class... T> void operator()(T &...v) { ((v = get<T>(r)), ...); }
};
template <class T> void put(Writer &w, const T &v) {
    if constexpr (std::is_same_v<T, Id>)
        require(!v.value.empty() && v.value.size() <= max_semantic_id, "Invalid semantic ID");
    if constexpr (std::is_same_v<T, std::string>) {
        require(v.size() <= max_semantic_string, "String too long");
        w.number(v.size());
        w.raw(v);
    } else if constexpr (std::is_same_v<T, Duration>)
        w.number(std::bit_cast<uint64_t>(v.count()));
    else if constexpr (std::is_same_v<T, double>) {
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
        require(std::isfinite(v), "Nonfinite semantic number");
        w.number(std::bit_cast<uint64_t>(v));
    } else if constexpr (std::is_same_v<T, memory::BufferView>) {
        require(std::endian::native == std::endian::little, "Bulk scalar codec requires little-endian host");
        auto b = v.map_read();
        if (!b)
            throw Failure(b.error());
        require(b->size() <= max_semantic_payload, "Excessive attribute bytes");
        w.number(b->size());
        w.raw({reinterpret_cast<const char *>(b->data()), b->size()});
    } else if constexpr (std::is_same_v<T, bool>) {
        w.byte(v ? 1 : 0);
    } else if constexpr (std::is_integral_v<T>) {
        if constexpr (std::is_signed_v<T>)
            w.number(std::bit_cast<uint64_t>(static_cast<int64_t>(v)));
        else
            w.number(v);
    } else if constexpr (std::is_enum_v<T>) {
        auto it = std::find(Tags<T>::values.begin(), Tags<T>::values.end(), v);
        require(it != Tags<T>::values.end(), "Invalid enum value");
        w.byte(static_cast<uint8_t>(it - Tags<T>::values.begin()));
    } else if constexpr (IsVector<T>::value) {
        require(v.size() <= max_semantic_entries, "Array too large");
        w.number(v.size());
        for (const auto &x : v)
            put(w, x);
    } else if constexpr (IsOptional<T>::value) {
        w.byte(v ? 1 : 0);
        if (v)
            put(w, *v);
    } else if constexpr (IsEvidence<T>::value) {
        w.byte(v.get() ? 2 : v.presence() == Presence::unknown ? 0 : 1);
        if (v.get())
            put(w, *v.get());
    } else {
        Output a{w};
        Fields<T>::apply(a, v);
    }
    if constexpr (std::is_same_v<T, AcquisitionProgram>)
        w.byte(0); // Frozen terminal policy.
}
template <class T> T get(Reader &r) {
    T v{};
    if constexpr (std::is_same_v<T, std::string>) {
        auto n = r.number();
        require(n <= max_semantic_string, "String too long");
        auto b = r.take(n).map_read();
        if (!b)
            throw Failure(b.error());
        v.assign(reinterpret_cast<const char *>(b->data()), b->size());
    } else if constexpr (std::is_same_v<T, Duration>)
        v = Duration{std::bit_cast<int64_t>(r.number())};
    else if constexpr (std::is_same_v<T, double>)
        v = std::bit_cast<double>(r.number());
    else if constexpr (std::is_same_v<T, memory::BufferView>) {
        require(std::endian::native == std::endian::little, "Bulk scalar codec requires little-endian host");
        auto n = r.number();
        require(n <= max_semantic_payload, "Excessive attribute bytes");
        v = r.take(n);
    } else if constexpr (std::is_same_v<T, bool>) {
        auto tag = r.byte();
        require(tag <= 1, "Invalid boolean tag");
        v = tag == 1;
    } else if constexpr (std::is_integral_v<T>) {
        auto n = r.number();
        if constexpr (std::is_signed_v<T>)
            v = std::bit_cast<int64_t>(n);
        else {
            require(n <= std::numeric_limits<T>::max(), "Integer overflow");
            v = static_cast<T>(n);
        }
    } else if constexpr (std::is_enum_v<T>) {
        auto n = r.byte();
        require(n < Tags<T>::values.size(), "Invalid enum tag");
        v = Tags<T>::values[n];
    } else if constexpr (IsVector<T>::value) {
        auto n = r.number();
        require(n <= max_semantic_entries && n <= r.bytes.size() - r.pos, "Array too large or truncated");
        if (r.observation) {
            using Element = typename IsVector<T>::value_type;
            if constexpr (std::is_same_v<Element, Attribute>)
                require(n <= 128, "Too many observation attributes");
            if constexpr (std::is_same_v<Element, uint64_t>)
                require(n <= 4, "Invalid attribute rank");
            if constexpr (std::is_same_v<Element, EmitterEvidence> ||
                          std::is_same_v<Element, ClockMappingEvidence>)
                require(n <= 64, "Too many observation participants/mappings");
            if constexpr (std::is_same_v<Element, CameraEffectiveState>)
                require(n <= 1, "Observation is scoped to one source image");
        }
        v.reserve(static_cast<size_t>(n));
        for (uint64_t i = 0; i < n; ++i)
            v.push_back(get<typename IsVector<T>::value_type>(r));
    } else if constexpr (IsOptional<T>::value) {
        auto n = r.byte();
        require(n <= 1, "Invalid optional tag");
        if (n)
            v = get<typename IsOptional<T>::value_type>(r);
    } else if constexpr (IsEvidence<T>::value) {
        auto n = r.byte();
        require(n <= 2, "Invalid presence tag");
        if (n == 1)
            v = Unavailable{};
        if (n == 2)
            v = get<typename IsEvidence<T>::value_type>(r);
    } else {
        Input a{r};
        Fields<T>::apply(a, v);
    }
    if constexpr (std::is_same_v<T, Id>)
        require(v.value.size() <= max_semantic_id, "ID too long");
    if constexpr (std::is_same_v<T, AcquisitionProgram>)
        require(r.byte() == 0, "Invalid terminal policy");
    return v;
}
template <class T> Counter measure(const T &v) {
    Counter c;
    std::ostream o(&c);
    Writer w{o};
    put(w, v);
    return c;
}
template <class T> uint64_t semantic_size(const T &v) { return add(16, measure(v).size); }
template <class T> void semantic(Writer &w, std::string_view magic, const T &v) {
    w.raw(magic);
    w.number(1);
    put(w, v);
}
template <class T> T semantic(memory::BufferView b, std::string_view magic) {
    Reader r(std::move(b));
    r.magic(magic);
    require(r.number() == 1, "Unsupported semantic version");
    auto v = get<T>(r);
    r.end();
    return v;
}
uint64_t string_size(std::string_view s) { return add(8, s.size()); }
// Mirrors the unchanged MANTIS01/02 structural field order. Never maps payloads.
uint64_t packet_size(const Packet &p) {
    uint64_t n = 8;
    require(p.header.metadata.size() <= 256 && p.attributes.size() <= 128,
            "Embedded metadata exceeds legacy bound");
    require(p.header.sync_quality == time::SyncQuality::unknown ||
                p.header.sync_quality == time::SyncQuality::software ||
                p.header.sync_quality == time::SyncQuality::hardware,
            "Invalid embedded SyncQuality");
    auto str = [&](std::string_view s) {
        require(s.size() <= 1024 * 1024, "Embedded string exceeds legacy bound");
        n = add(n, string_size(s));
    };
    str(p.type.name);
    n = add(n, 24);
    str(p.header.timestamp.domain.id.value);
    str(p.header.timestamp.domain.name);
    n = add(n, 8);
    str(p.header.sync.id.value);
    n = add(n, 16);
    str(p.header.calibration.id.value);
    n = add(n, 16);
    str(p.header.frame.id.value);
    str(p.header.frame.name);
    n = add(n, 8);
    for (const auto &[k, v] : p.header.metadata) {
        str(k);
        str(v);
    }
    n = add(n, 8);
    for (const auto &a : p.attributes) {
        str(a.descriptor.name);
        str(a.descriptor.unit);
        n = add(n, 16);
        n = add(n, 16 * a.descriptor.shape.size());
        n = add(n, 8);
        n = add(n, (64 - n % 64) % 64);
        n = add(n, a.buffer.size());
    }
    if (!p.frames.empty()) {
        n = add(n, 8);
        for (const auto &f : p.frames)
            n = add(n, packet_size(*f));
    }
    return n;
}
// Strict schema-3 embedding guard. Legacy readers/writers and bytes remain untouched.
// Only metadata and padding are traversed; bulk pixel spans are skipped.
void inspect_frameset(Reader &r, unsigned depth = 0) {
    const auto origin = r.pos;
    r.magic(depth == 0 ? "MANTIS02" : "MANTIS01");
    auto string = [&]() {
        auto n = r.number();
        require(n <= 1024 * 1024, "Embedded string exceeds legacy bound");
        auto v = r.take(n).map_read();
        if (!v)
            throw Failure(v.error());
        return std::string(reinterpret_cast<const char *>(v->data()), v->size());
    };
    auto name = string();
    require(name == (depth == 0 ? schema::frameset.name : schema::image.name) && r.number() == 1,
            "Invalid embedded schema");
    (void)r.number();
    (void)r.number();
    (void)string();
    (void)string();
    (void)r.number();
    (void)string();
    (void)r.number();
    require(r.number() <= 2, "Invalid embedded SyncQuality");
    (void)string();
    require(r.number() <= UINT32_MAX, "Embedded calibration schema overflow");
    (void)r.number();
    (void)string();
    (void)string();
    auto metadata = r.number();
    require(metadata <= 256, "Too much embedded metadata");
    std::optional<std::string> previous;
    for (uint64_t i = 0; i < metadata; ++i) {
        auto key = string();
        require(!previous || *previous < key, "Duplicate/unordered embedded metadata");
        previous = std::move(key);
        (void)string();
    }
    auto attributes = r.number();
    require(attributes <= 128 && (depth != 0 || attributes == 0), "Invalid embedded attributes");
    for (uint64_t i = 0; i < attributes; ++i) {
        (void)string();
        (void)string();
        auto scalar = r.number();
        require(scalar >= 1 && scalar <= 5, "Invalid embedded scalar");
        auto rank = r.number();
        require(rank >= 1 && rank <= 4, "Invalid embedded rank");
        for (uint64_t j = 0; j < rank * 2; ++j)
            (void)r.number();
        auto n = r.number();
        auto padding = (64 - (r.pos - origin) % 64) % 64;
        for (size_t j = 0; j < padding; ++j)
            require(r.byte() == 0, "Noncanonical embedded padding");
        (void)r.take(n); // No application-level pixel read/copy for structural validation.
    }
    if (depth == 0) {
        auto children = r.number();
        require(children > 0 && children <= 16, "Invalid FrameSet children");
        for (uint64_t i = 0; i < children; ++i)
            inspect_frameset(r, 1);
    }
}
void valid(const AcquisitionBundle &b) {
    auto v = validate(b);
    if (!v)
        fail(Status::corrupt, v.error().message, "MANTIS03");
}
void check_write(std::ostream &o) {
    if (!o)
        fail(Status::io, "Projected capture write failed");
}
} // namespace
uint64_t bundle_encoded_size(const AcquisitionBundle &b) {
    valid(b);
    uint64_t n = 24;
    n = add(n, measure(b.type).size);
    n = add(n, measure(b.key).size);
    n = add(n, measure(b.published).size);
    n = add(n, add(16, semantic_size(b.evidence)));
    if (b.frameset)
        n = add(n, add(16, packet_size(*b.frameset)));
    for (const auto &t : b.triggers)
        n = add(n, add(16, semantic_size(t)));
    return n;
}
void write_bundle(std::ostream &out, const AcquisitionBundle &b) {
    (void)bundle_encoded_size(b);
    Writer w{out};
    w.raw("MANTIS03");
    w.number(1);
    put(w, b.type);
    put(w, b.key);
    put(w, b.published);
    w.number(1 + (b.frameset ? 1 : 0) + b.triggers.size());
    w.number(1);
    w.number(semantic_size(b.evidence));
    semantic(w, "MEVID001", b.evidence);
    if (b.frameset) {
        w.number(2);
        w.number(packet_size(*b.frameset));
        SequentialPosition position(out.rdbuf());
        std::ostream sequential(&position);
        write_packet(sequential, *b.frameset);
        check_write(sequential);
    }
    for (const auto &t : b.triggers) {
        w.number(3);
        w.number(semantic_size(t));
        semantic(w, "MTRIG001", t);
    }
    check_write(out);
}
AcquisitionBundle read_bundle(memory::BufferView b) {
    Reader r(std::move(b));
    r.magic("MANTIS03");
    require(r.number() == 1, "Unsupported bundle version");
    AcquisitionBundle v;
    v.type = get<schema::DataTypeId>(r);
    v.key = get<BundleKey>(r);
    v.published = get<RuntimeTimestamp>(r);
    auto n = r.number();
    require(n > 0 && n <= max_bundle_members, "Invalid member count");
    bool evidence = false, frames = false, triggers = false;
    for (uint64_t i = 0; i < n; ++i) {
        auto kind = r.number();
        auto payload = r.take(r.number());
        if (kind == 1) {
            require(!evidence && i == 0, "Duplicate/out-of-order evidence");
            evidence = true;
            v.evidence = semantic<AcquisitionEvidence>(payload, "MEVID001");
        } else if (kind == 2) {
            require(evidence && !frames && !triggers, "Duplicate/out-of-order FrameSet");
            frames = true;
            Reader f(payload);
            inspect_frameset(f);
            f.end();
            v.frameset = read_packet(payload);
            require(v.frameset->type == schema::frameset, "Invalid FrameSet member");
        } else if (kind == 3) {
            require(evidence, "Missing required evidence");
            triggers = true;
            v.triggers.push_back(semantic<TriggerEvent>(payload, "MTRIG001"));
        } else
            fail(Status::corrupt, "Unknown member kind", "MANTIS03");
    }
    require(evidence, "Missing required evidence");
    r.end();
    valid(v);
    return v;
}
void validate_capture_header(const ProjectedCaptureHeader &h) {
    auto v = validate(h.program);
    if (!v)
        throw Failure(v.error());
    require(!h.run.id.value.empty() && h.run.id.value.size() <= max_semantic_id &&
                !h.generation.id.value.empty() && h.generation.id.value.size() <= max_semantic_id,
            "Invalid run identity");
    const auto &c = h.config;
    require(c.queue_capacity > 0 && c.queue_capacity <= max_executed_steps && c.max_correlation_entries > 0 &&
                c.max_correlation_entries <= max_executed_steps,
            "Invalid recorded capacities");
    for (auto ms :
         {c.operation_timeout_ms, c.abort_timeout_ms, c.cleanup_timeout_ms, c.publication_timeout_ms})
        require(ms > 0 && ms <= 60000, "Invalid recorded timeout");
}
void write_capture_header(std::ostream &out, const ProjectedCaptureHeader &h) {
    validate_capture_header(h);
    auto c = measure(h);
    Writer w{out};
    w.raw("MRUNHDR3");
    w.number(1);
    w.number(c.size);
    w.number(c.hash);
    put(w, h);
    w.raw("MRUNEND3");
    w.number(~c.size);
    check_write(out);
}
ProjectedCaptureHeader read_capture_header(memory::BufferView b) {
    Reader r(std::move(b));
    r.magic("MRUNHDR3");
    require(r.number() == 1, "Unsupported capture header version");
    auto n = r.number(), hash = r.number();
    auto body = r.take(n);
    r.magic("MRUNEND3");
    require(r.number() == ~n, "Invalid capture header footer");
    r.end();
    uint64_t actual = 14695981039346656037ull;
    auto bytes = body.map_read();
    if (!bytes)
        throw Failure(bytes.error());
    for (auto x : *bytes)
        actual = (actual ^ std::to_integer<uint8_t>(x)) * 1099511628211ull;
    require(actual == hash, "Capture header integrity mismatch");
    Reader fields(body);
    auto h = get<ProjectedCaptureHeader>(fields);
    fields.end();
    validate_capture_header(h);
    return h;
}
void validate_run_outcome(const ProjectedRunOutcome &v) {
    require(v.run.id.value.size() <= max_semantic_id && !v.run.id.value.empty() &&
                v.generation.id.value.size() <= max_semantic_id && !v.generation.id.value.empty(),
            "Invalid outcome identity");
    bool fault = false;
    for (const auto *e : {&v.initiating_error, &v.abort_error, &v.stop_error, &v.close_error})
        if (*e) {
            require((*e)->code != Status::ok, "A present error cannot report OK");
            fault = true;
        }
    if (const auto &a = v.abort_outcome) {
        if (a->run.get())
            require(*a->run.get() == v.run, "Abort RunId mismatch");
        if (a->fenced_generation.get())
            require(*a->fenced_generation.get() == v.generation, "Abort generation mismatch");
        // Frozen L2 ContractError categories: NONE=0 through CLEANUP=7.
        require(a->error.category <= 7 && ((a->error.category == 0) == (a->error.code == 0)),
                "Invalid recorded executor error category/code");
        fault |= a->error.category != 0;
        for (const auto *flag : {&a->inhibited, &a->stale_work_fenced, &a->off_requested})
            fault |= flag->get() && !*flag->get();
        require(a->emitters.size() <= max_participants, "Too many abort emitters");
        std::vector<ComponentId> emitters;
        for (const auto &e : a->emitters) {
            auto valid = validate(e);
            if (!valid)
                throw Failure(valid.error());
            require(std::find(emitters.begin(), emitters.end(), e.emitter) == emitters.end(),
                    "Duplicate abort emitter");
            emitters.push_back(e.emitter);
            if (e.commanded.get())
                require(e.commanded.get()->state == EmitterState::off, "Abort command must request OFF");
        }
    }
    require(!fault || v.disposition == RecordedRunDisposition::failed,
            "Recorded faults require the daemon's FAILED disposition");
    // Applies the explicit enum/string/ID/vector bounds without creating a large buffer.
    require(measure(v).size <= max_run_outcome_bytes - 56, "Outcome exceeds bound");
}
void write_run_outcome(std::ostream &out, const ProjectedCaptureOutcome &v) {
    validate_run_outcome(v.outcome);
    auto c = measure(v);
    require(c.size <= max_run_outcome_bytes - 48, "Outcome exceeds bound");
    Writer w{out};
    w.raw("MRUNOUT3");
    w.number(1);
    w.number(c.size);
    w.number(c.hash);
    put(w, v);
    w.raw("MOUTEND3");
    w.number(~c.size);
    check_write(out);
}
std::optional<ProjectedCaptureOutcome> read_run_outcome(memory::BufferView b, bool allow_incomplete) {
    Reader r(std::move(b));
    require(r.bytes.size() <= max_run_outcome_bytes, "Outcome exceeds bound");
    constexpr std::string_view magic = "MRUNOUT3";
    const auto prefix = std::min(r.bytes.size(), magic.size());
    require(prefix == 0 || !std::memcmp(r.bytes.data(), magic.data(), prefix), "Invalid outcome magic");
    if (r.bytes.size() >= 16) {
        r.pos = 8;
        require(r.number() == 1, "Unsupported outcome version");
    }
    std::optional<uint64_t> size;
    if (r.bytes.size() >= 24) {
        r.pos = 16;
        size = r.number();
        require(*size > 0 && *size <= max_run_outcome_bytes - 48, "Invalid outcome length");
    }
    if (r.bytes.size() < 32 || (size && r.bytes.size() < *size + 48)) {
        // A complete footer at EOF is evidence of completion even if length is corrupt.
        // Never turn such a complete provisional outcome into absence.
        if (r.bytes.size() >= 48)
            require(std::memcmp(r.bytes.data() + r.bytes.size() - 16, "MOUTEND3", 8) != 0,
                    "Complete outcome footer conflicts with envelope length");
        require(allow_incomplete, "Incomplete published outcome");
        return {};
    }
    r.pos = 24;
    auto expected = r.number();
    auto body = r.take(*size);
    r.magic("MOUTEND3");
    require(r.number() == ~*size, "Invalid outcome footer");
    r.end();
    uint64_t actual = 14695981039346656037ull;
    auto bytes = body.map_read();
    if (!bytes)
        throw Failure(bytes.error());
    for (auto x : *bytes)
        actual = (actual ^ std::to_integer<uint8_t>(x)) * 1099511628211ull;
    require(actual == expected, "Outcome integrity mismatch");
    Reader fields(body);
    auto v = get<ProjectedCaptureOutcome>(fields);
    fields.end();
    validate_run_outcome(v.outcome);
    return v;
}
bool same_program_reference(const ProgramReference &a, const ProgramReference &b) {
    std::ostringstream x, y;
    Writer wx{x}, wy{y};
    put(wx, a);
    put(wy, b);
    return x.str() == y.str();
}
bool same_emitter_command(const EmitterCommand &a, const EmitterCommand &b) {
    std::ostringstream x, y;
    Writer wx{x}, wy{y};
    put(wx, a);
    put(wy, b);
    return x.str() == y.str();
}
bool same_participants(const Participants &a, const Participants &b) {
    std::ostringstream x, y;
    Writer wx{x}, wy{y};
    put(wx, a);
    put(wy, b);
    return x.str() == y.str();
}
namespace {
memory::BufferView map_semantic_file(const std::filesystem::path &p) {
    require(std::filesystem::file_size(p) <= limit, "Semantic file exceeds bound");
    auto m = platform::map_read(p);
    require(m.size <= limit, "Semantic file exceeds bound");
    auto storage = std::make_shared<memory::Storage>();
    storage->owner = m.owner;
    storage->host = m.data;
    storage->size = m.size;
    storage->alignment = 1;
    storage->domain = memory::MemoryDomain::shared_memory;
    return {std::move(storage), 0, m.size};
}
void write_file(const std::filesystem::path &p, const auto &value, auto encode) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out)
        fail(Status::io, "Cannot write semantic file");
    encode(out, value);
    out.flush();
    check_write(out);
}
void envelope(std::ostream &out, std::string_view magic, std::string_view footer, const auto &encode) {
    Counter c;
    std::ostream pass(&c);
    pass.exceptions(std::ios::badbit | std::ios::failbit);
    encode(pass);
    require(c.size <= limit - 48, "Semantic record exceeds bound");
    Writer w{out};
    w.raw(magic);
    w.number(1);
    w.number(c.size);
    w.number(c.hash);
    encode(out);
    w.raw(footer);
    w.number(~c.size);
    check_write(out);
}
memory::BufferView envelope(memory::BufferView b, std::string_view magic, std::string_view footer) {
    Reader r(std::move(b));
    r.magic(magic);
    require(r.number() == 1, "Unsupported semantic record version");
    auto n = r.number(), hash = r.number();
    require(n <= limit - 48, "Semantic record exceeds bound");
    auto body = r.take(n);
    r.magic(footer);
    require(r.number() == ~n, "Invalid semantic completion footer");
    r.end();
    auto bytes = body.map_read();
    if (!bytes)
        throw Failure(bytes.error());
    uint64_t actual = 14695981039346656037ull;
    for (auto x : *bytes)
        actual = (actual ^ std::to_integer<uint8_t>(x)) * 1099511628211ull;
    require(actual == hash, "Semantic record checksum mismatch");
    return body;
}
void observation_payload(std::ostream &out, const LaserObservation &v, const Counter &body) {
    Writer w{out};
    w.raw("MLOBS001");
    w.number(1);
    w.number(body.size);
    w.number(body.hash);
    put(w, v);
    w.raw("MLOBEND1");
    w.number(~body.size);
    check_write(out);
}
void semantic_payload(std::ostream &out, const SemanticPacket &p, const Counter *observation_body) {
    Writer w{out};
    const auto tag = std::visit(
        [](const auto &value) -> uint64_t {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Published>)
                return 0;
            else if constexpr (std::is_same_v<T, AcquisitionEvidence>)
                return 1;
            else if constexpr (std::is_same_v<T, TriggerEvent>)
                return 2;
            else if constexpr (std::is_same_v<T, AcquisitionBundle>)
                return 3;
            else
                return 4;
        },
        p);
    w.number(tag);
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Published>) {
                SequentialPosition position(out.rdbuf());
                std::ostream sequential(&position);
                write_packet(sequential, *v);
                check_write(sequential);
            } else if constexpr (std::is_same_v<T, AcquisitionBundle>)
                write_bundle(out, v);
            else if constexpr (std::is_same_v<T, LaserObservation>)
                observation_payload(out, v, *observation_body);
            else if constexpr (std::is_same_v<T, AcquisitionEvidence>)
                semantic(w, "MEVID001", v);
            else
                semantic(w, "MTRIG001", v);
        },
        p);
}
} // namespace
schema::DataTypeId semantic_type(const SemanticPacket &p) {
    return std::visit(
        [](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Published>) {
                require(bool(v), "Null semantic data packet");
                return v->type;
            } else
                return v.type;
        },
        p);
}
Result<void> validate(const SemanticPacket &p) {
    try {
        std::visit(
            [](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, Published>) {
                    require(bool(v), "Null semantic packet");
                    require(v->type.version > 0 && v->type.name.find('.') != std::string::npos,
                            "Invalid semantic data type");
                    require(v->type.name != schema::laser_observation.name &&
                                v->type.name != schema::acquisition_bundle.name &&
                                v->type.name != schema::acquisition_evidence.name &&
                                v->type.name != schema::trigger_event.name &&
                                v->type.name != schema::acquisition_program.name,
                            "Typed semantics cannot masquerade as a flat packet");
                    require((v->type.name != schema::image.name && v->type.name != schema::frameset.name) ||
                                v->type.version == 1,
                            "Unsupported image schema");
                    (void)publish(*v);
                    (void)packet_size(*v);
                } else {
                    auto r = validate(v);
                    if (!r)
                        throw Failure(r.error());
                }
            },
            p);
        return {};
    } catch (const Failure &e) {
        return std::unexpected(e.error);
    }
}
SemanticPublished publish(SemanticPacket p) {
    auto r = validate(p);
    if (!r)
        throw Failure(r.error());
    return std::make_shared<const SemanticPacket>(std::move(p));
}
void write_laser_observation(std::ostream &out, const LaserObservation &v) {
    auto r = validate(v);
    if (!r)
        throw Failure(r.error());
    Counter size(false);
    std::ostream structural(&size);
    structural.exceptions(std::ios::badbit | std::ios::failbit);
    Writer sizing{structural};
    put(sizing, v);
    require(size.size <= limit - 48, "Observation record exceeds bound");
    envelope(out, "MLOBS001", "MLOBEND1", [&](std::ostream &o) {
        Writer w{o};
        put(w, v);
    });
}
LaserObservation read_laser_observation(memory::BufferView b) {
    Reader fields(envelope(std::move(b), "MLOBS001", "MLOBEND1"), true);
    auto v = get<LaserObservation>(fields);
    fields.end();
    auto r = validate(v);
    if (!r)
        fail(Status::corrupt, r.error().message, "MLOBS001");
    return v;
}
void write_laser_observation(const std::filesystem::path &p, const LaserObservation &v) {
    write_file(p, v, [](auto &o, const auto &x) { write_laser_observation(o, x); });
}
LaserObservation read_laser_observation(const std::filesystem::path &p) {
    return read_laser_observation(map_semantic_file(p));
}
void write_semantic_packet(std::ostream &out, const SemanticPacket &v) {
    auto r = validate(v);
    if (!r)
        throw Failure(r.error());
    std::optional<Counter> observation_body;
    if (const auto *obs = std::get_if<LaserObservation>(&v)) {
        Counter size(false);
        std::ostream structural(&size);
        structural.exceptions(std::ios::badbit | std::ios::failbit);
        Writer sizing{structural};
        put(sizing, *obs);
        require(size.size <= limit - 104, "Observation transport exceeds bound");
        observation_body.emplace();
        std::ostream integrity(&*observation_body);
        integrity.exceptions(std::ios::badbit | std::ios::failbit);
        Writer fields{integrity};
        put(fields, *obs);
    }
    envelope(out, "MSEM0001", "MSEMEND1", [&](std::ostream &o) {
        semantic_payload(o, v, observation_body ? &*observation_body : nullptr);
    });
}
SemanticPacket read_semantic_packet(memory::BufferView b) {
    Reader r(envelope(std::move(b), "MSEM0001", "MSEMEND1"));
    auto kind = r.number();
    auto payload = r.take(r.bytes.size() - r.pos);
    r.end();
    SemanticPacket result;
    switch (kind) {
    case 0: {
        auto bytes = payload.map_read();
        if (!bytes)
            throw Failure(bytes.error());
        if (bytes->size() >= 8 && !std::memcmp(bytes->data(), "MANTIS02", 8)) {
            Reader f(payload);
            inspect_frameset(f);
            f.end();
        }
        result = read_packet(payload);
        break;
    }
    case 1:
        result = semantic<AcquisitionEvidence>(payload, "MEVID001");
        break;
    case 2:
        result = semantic<TriggerEvent>(payload, "MTRIG001");
        break;
    case 3:
        result = read_bundle(payload);
        break;
    case 4:
        result = read_laser_observation(payload);
        break;
    default:
        fail(Status::corrupt, "Unknown semantic transport kind");
    }
    auto valid = validate(result);
    if (!valid)
        fail(Status::corrupt, valid.error().message);
    return result;
}
void write_semantic_packet(const std::filesystem::path &p, const SemanticPacket &v) {
    write_file(p, v, [](auto &o, const auto &x) { write_semantic_packet(o, x); });
}
SemanticPacket read_semantic_packet(const std::filesystem::path &p) {
    return read_semantic_packet(map_semantic_file(p));
}
} // namespace mantis::data
