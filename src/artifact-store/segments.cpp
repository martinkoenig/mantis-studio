#include "segments.hpp"
#include <mantis/platform.hpp>
#include <streambuf>
namespace mantis::artifact::segments {
namespace {
constexpr uint64_t offset_basis = 14695981039346656037ull, prime = 1099511628211ull;
uint64_t hash(std::span<const std::byte> bytes) {
    uint64_t result = offset_basis;
    for (auto byte : bytes) result = (result ^ std::to_integer<uint8_t>(byte)) * prime;
    return result;
}
void number(std::ostream &out, uint64_t n) {
    char bytes[8];
    for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<char>(n >> (i * 8));
    out.write(bytes, 8);
}
uint64_t number(std::span<const std::byte> bytes, size_t offset) {
    uint64_t result{};
    for (unsigned i = 0; i < 8; ++i) result |= uint64_t(std::to_integer<uint8_t>(bytes[offset + i])) << (i * 8);
    return result;
}
// Counts/hashes serialization without allocating a packet-sized staging buffer.
// One read pass for integrity, followed by direct sequential writes of the spans.
class Probe final : public std::streambuf {
  public:
    uint64_t bytes{}, checksum{offset_basis};
  protected:
    std::streamsize xsputn(const char *data, std::streamsize count) override {
        for (std::streamsize i = 0; i < count; ++i)
            checksum = (checksum ^ static_cast<unsigned char>(data[i])) * prime;
        bytes += static_cast<uint64_t>(count); return count;
    }
    int_type overflow(int_type c) override {
        if (!traits_type::eq_int_type(c, traits_type::eof())) {
            auto byte = traits_type::to_char_type(c); xsputn(&byte, 1);
        }
        return traits_type::not_eof(c);
    }
    pos_type seekoff(off_type offset, std::ios_base::seekdir dir, std::ios_base::openmode) override {
        if (offset == 0 && dir == std::ios_base::cur) return pos_type(bytes);
        return pos_type(off_type(-1));
    }
};
}
void append(std::ostream &out, const data::Packet &p) {
    Probe probe; std::ostream counter(&probe);
    data::write_packet(counter, p);
    if (!probe.bytes || probe.bytes > 128 * 1024 * 1024) fail(Status::invalid_argument, "Capture record too large");
    out.write("MRAWREC2", 8); number(out, probe.bytes); number(out, probe.checksum); number(out, ~probe.bytes);
    data::write_packet(out, p);
    number(out, probe.bytes ^ 0x4d414e5449533032ull);
    out.flush(); // OS visibility for process-kill recovery; durability is segment-batched.
    if (!out) fail(Status::io, "RawCapture sequential write failed (disk full?)", "raw-writer");
}
void append(std::ostream &out, const data::AcquisitionBundle &b) {
    Probe probe;
    std::ostream counter(&probe);
    data::write_bundle(counter, b);
    if (!probe.bytes || probe.bytes > 128 * 1024 * 1024)
        fail(Status::invalid_argument, "Capture record too large");
    out.write("MRAWREC3", 8);
    number(out, probe.bytes);
    number(out, probe.checksum);
    number(out, ~probe.bytes);
    data::write_bundle(out, b);
    number(out, probe.bytes ^ 0x4d414e5449533033ull);
    out.flush();
    if (!out)
        fail(Status::io, "RawCapture bundle write failed", "raw-writer");
}
data::AcquisitionBundle bundle(const Scan &scan, size_t index) {
    const auto &r = scan.records.at(index);
    return data::read_bundle(scan.mapping.slice(r.offset, r.bytes));
}
Scan scan(const std::filesystem::path &path, uint32_t schema, const CancellationToken &token) {
    if (schema != 2 && schema != 3)
        fail(Status::incompatible, "Unsupported record schema");
    const auto magic = schema == 2 ? "MRAWREC2" : "MRAWREC3";
    const uint64_t footer = schema == 2 ? 0x4d414e5449533032ull : 0x4d414e5449533033ull;
    Scan result;
    token.check();
    const auto bytes_on_disk = std::filesystem::file_size(path);
    if (!bytes_on_disk) {
        result.incomplete = true;
        return result;
    }
    const uint64_t segment_bound = max_segment_bytes + 128 * 1024 * 1024 + (schema == 3 ? 40 : 0);
    if (bytes_on_disk > segment_bound)
        fail(Status::corrupt, "RawCapture segment exceeds bound");
    auto map = platform::map_read(path);
    if (map.size > segment_bound)
        fail(Status::corrupt, "RawCapture segment exceeds bound");
    auto storage = std::make_shared<memory::Storage>(); storage->owner = map.owner;
    storage->host = map.data; storage->size = map.size; storage->alignment = 1;
    storage->domain = memory::MemoryDomain::shared_memory;
    result.mapping = {storage, 0, map.size};
    std::span<const std::byte> bytes{map.data, map.size};
    size_t pos{};
    while (pos < bytes.size()) {
        token.check();
        if (bytes.size() - pos < 32) { result.incomplete = true; break; }
        if (std::memcmp(bytes.data() + pos, magic, 8)) {
            result.corruption = "Invalid RawCapture record magic";
            break;
        }
        auto size = number(bytes, pos + 8), expected_hash = number(bytes, pos + 16);
        if (!size || size > 128 * 1024 * 1024 || number(bytes, pos + 24) != ~size) { result.corruption = "Invalid RawCapture record length"; break; }
        if (size + 40 > bytes.size() - pos) { result.incomplete = true; break; }
        auto payload = bytes.subspan(pos + 32, static_cast<size_t>(size));
        if (hash(payload) != expected_hash ||
            number(bytes, pos + 32 + static_cast<size_t>(size)) != (size ^ footer)) {
            result.corruption = "RawCapture record integrity mismatch"; break;
        }
        try {
            auto payload_view = result.mapping.slice(pos + 32, static_cast<size_t>(size));
            if (schema == 2)
                (void)data::read_packet(payload_view);
            else
                (void)data::read_bundle(payload_view);
        } catch (const std::exception &e) {
            result.corruption = e.what();
            break;
        }
        result.records.push_back({pos + 32, static_cast<size_t>(size)});
        if (result.records.size() > 100000) { result.corruption = "Too many records in segment"; break; }
        pos += static_cast<size_t>(size) + 40; result.complete_bytes = pos;
    }
    return result;
}
data::Published packet(const Scan &scan, size_t index) {
    const auto &record = scan.records.at(index);
    return data::read_packet(scan.mapping.slice(record.offset, record.bytes));
}
} // namespace mantis::artifact::segments
