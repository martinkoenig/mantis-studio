#include <bit>
#include <fstream>
#include <mantis/data_io.hpp>
#include <mantis/platform.hpp>
namespace mantis::data {
namespace {
void u64(std::ostream &out, uint64_t n) {
    for (int i = 0; i < 8; ++i)
        out.put(static_cast<char>(n >> (i * 8)));
}
void str(std::ostream &out, std::string_view s) {
    u64(out, s.size());
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}
struct Reader {
    std::span<const std::byte> bytes;
    size_t pos{};
    uint64_t number() {
        if (bytes.size() - pos < 8)
            fail(Status::corrupt, "Truncated packet");
        uint64_t n = 0;
        for (int i = 0; i < 8; ++i)
            n |= uint64_t(std::to_integer<uint8_t>(bytes[pos++])) << (i * 8);
        return n;
    }
    std::string string() {
        auto size = number();
        if (size > 1024 * 1024 || size > bytes.size() - pos)
            fail(Status::corrupt, "Invalid string length");
        std::string s(reinterpret_cast<const char *>(bytes.data() + pos), static_cast<size_t>(size));
        pos += size;
        return s;
    }
};
} // namespace
void write_packet(std::ostream &o, const Packet &p) {
    const auto origin = o.tellp();
    static_assert(std::endian::native == std::endian::little,
                  "Data codec requires a little-endian adapter on this target");
    o.write(p.frames.empty() ? "MANTIS01" : "MANTIS02", 8);
    str(o, p.type.name);
    u64(o, p.type.version);
    u64(o, p.header.sequence.value);
    u64(o, static_cast<uint64_t>(p.header.timestamp.nanoseconds));
    str(o, p.header.timestamp.domain.id.value);
    str(o, p.header.timestamp.domain.name);
    u64(o, static_cast<uint64_t>(p.header.received.nanoseconds));
    str(o, p.header.sync.id.value);
    u64(o, p.header.sync.trigger);
    u64(o, static_cast<uint64_t>(p.header.sync_quality));
    str(o, p.header.calibration.id.value);
    u64(o, p.header.calibration.schema_version);
    u64(o, p.header.calibration.revision);
    str(o, p.header.frame.id.value);
    str(o, p.header.frame.name);
    u64(o, p.header.metadata.size());
    for (auto &[k, v] : p.header.metadata) {
        str(o, k);
        str(o, v);
    }
    u64(o, p.attributes.size());
    for (const auto &a : p.attributes) {
        str(o, a.descriptor.name);
        str(o, a.descriptor.unit);
        u64(o, static_cast<uint64_t>(a.descriptor.scalar));
        u64(o, a.descriptor.shape.size());
        for (auto n : a.descriptor.shape)
            u64(o, n);
        for (auto n : a.descriptor.stride)
            u64(o, n);
        auto mapped = a.buffer.map_read();
        if (!mapped)
            throw Failure(mapped.error());
        u64(o, mapped->size());
        auto pos = static_cast<uint64_t>(o.tellp() - origin);
        while (pos++ % 64)
            o.put(0);
        o.write(reinterpret_cast<const char *>(mapped->data()), static_cast<std::streamsize>(mapped->size()));
    }
    if (!p.frames.empty()) {
        u64(o, p.frames.size());
        for (const auto &frame : p.frames)
            write_packet(o, *frame);
    }
    if (!o)
        fail(Status::io, "Packet write failed (disk full?)");
}
namespace {
Published decode(Reader &r, const memory::BufferView &storage, unsigned depth = 0) {
    const auto origin = r.pos;
    if (r.bytes.size() - r.pos < 8)
        fail(Status::corrupt, "Truncated packet magic");
    bool composite = std::memcmp(r.bytes.data() + r.pos, "MANTIS02", 8) == 0;
    if ((!composite && std::memcmp(r.bytes.data() + r.pos, "MANTIS01", 8)) || depth > 1)
        fail(Status::corrupt, "Invalid packet format or nesting");
    r.pos += 8;
    Packet p;
    p.type.name = r.string();
    p.type.version = static_cast<uint32_t>(r.number());
    p.header.sequence.value = r.number();
    p.header.timestamp.nanoseconds = static_cast<int64_t>(r.number());
    p.header.timestamp.domain.id.value = r.string();
    p.header.timestamp.domain.name = r.string();
    p.header.received.nanoseconds = static_cast<int64_t>(r.number());
    p.header.sync.id.value = r.string();
    p.header.sync.trigger = r.number();
    p.header.sync_quality = static_cast<time::SyncQuality>(r.number());
    p.header.calibration.id.value = r.string();
    p.header.calibration.schema_version = static_cast<uint32_t>(r.number());
    p.header.calibration.revision = r.number();
    p.header.frame.id.value = r.string();
    p.header.frame.name = r.string();
    auto metadata = r.number();
    if (metadata > 256)
        fail(Status::corrupt, "Too much metadata");
    for (uint64_t i = 0; i < metadata; ++i) {
        auto k = r.string();
        auto v = r.string();
        p.header.metadata.emplace(std::move(k), std::move(v));
    }
    auto count = r.number();
    if (count > 128)
        fail(Status::corrupt, "Too many attributes");
    for (uint64_t i = 0; i < count; ++i) {
        Attribute a;
        a.descriptor.name = r.string();
        a.descriptor.unit = r.string();
        a.descriptor.scalar = static_cast<schema::ScalarType>(r.number());
        auto rank = r.number();
        if (rank < 1 || rank > 4)
            fail(Status::corrupt, "Invalid rank");
        for (uint64_t j = 0; j < rank; ++j)
            a.descriptor.shape.push_back(r.number());
        for (uint64_t j = 0; j < rank; ++j)
            a.descriptor.stride.push_back(r.number());
        auto size = r.number();
        r.pos = origin + ((r.pos - origin + 63) & ~size_t(63));
        if (r.pos > r.bytes.size() || size > r.bytes.size() - r.pos)
            fail(Status::corrupt, "Truncated attribute payload");
        a.buffer = storage.slice(r.pos, static_cast<size_t>(size));
        r.pos += size;
        p.attributes.push_back(std::move(a));
    }
    if (composite) {
        auto child_count = r.number();
        if (child_count == 0 || child_count > 16 || p.type != schema::frameset)
            fail(Status::corrupt, "Invalid FrameSet children");
        for (uint64_t i = 0; i < child_count; ++i)
            p.frames.push_back(decode(r, storage, depth + 1));
    }
    return publish(std::move(p));
}
} // namespace
Published read_packet(memory::BufferView storage) {
    auto bytes = storage.map_read();
    if (!bytes) throw Failure(bytes.error());
    if (bytes->size() > 256 * 1024 * 1024)
        fail(Status::corrupt, "Packet exceeds size limit");
    Reader r{*bytes};
    auto packet = decode(r, storage);
    if (r.pos != bytes->size()) fail(Status::corrupt, "Trailing packet data");
    return packet;
}
Published read_packet(const std::filesystem::path &path) {
    auto m = platform::map_read(path);
    auto storage = std::make_shared<memory::Storage>();
    storage->owner = m.owner;
    storage->host = m.data;
    storage->size = m.size;
    storage->alignment = 64;
    storage->domain = memory::MemoryDomain::shared_memory;
    return read_packet(memory::BufferView{storage, 0, m.size});
}
void write_packet(const std::filesystem::path &path, const Packet &p) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) fail(Status::io, "Cannot write packet");
    write_packet(out, p);
    out.flush();
    if (!out) fail(Status::io, "Packet flush failed");
}
} // namespace mantis::data
