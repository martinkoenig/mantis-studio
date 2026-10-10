#include "codec.hpp"
#include <algorithm>
#include <bit>

namespace x1::f2 {
uint64_t read(std::span<const uint8_t> b, size_t o, size_t n) {
    if (!n || n > 8 || o > b.size() || n > b.size() - o)
        throw std::out_of_range("F2 field read");
    uint64_t v{};
    for (size_t i = 0; i < n; ++i)
        v |= uint64_t(b[o + i]) << (i * 8);
    return v;
}
void write(std::span<uint8_t> b, size_t o, size_t n, uint64_t v) {
    if (!n || n > 8 || o > b.size() || n > b.size() - o || (n < 8 && (v >> (n * 8))))
        throw std::out_of_range("F2 field write");
    for (size_t i = 0; i < n; ++i)
        b[o + i] = static_cast<uint8_t>(v >> (i * 8));
}
uint32_t crc32c(std::span<const uint8_t> b) {
    uint32_t crc = UINT32_MAX;
    for (auto v : b) {
        crc ^= v;
        for (unsigned i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0x82f63b78u & (0u - (crc & 1u)));
    }
    return crc ^ UINT32_MAX;
}
std::optional<Wire> encode(const Frame &f) {
    auto kind = static_cast<uint8_t>(f.kind);
    if (kind < 1 || kind > 3 || f.payload.size > max_payload)
        return {};
    std::array<uint8_t, max_decoded> raw{};
    raw[0] = 0x4d;
    raw[1] = 0x58;
    raw[2] = 1;
    raw[4] = 24;
    raw[5] = kind;
    write(raw, 8, 8, f.session);
    write(raw, 16, 4, f.request);
    write(raw, 20, 2, f.message);
    write(raw, 22, 2, f.payload.size);
    std::copy(f.payload.data().begin(), f.payload.data().end(), raw.begin() + 24);
    size_t n = 24 + f.payload.size;
    write(raw, n, 4, crc32c({raw.data(), n}));
    n += 4;
    Wire w;
    size_t code_pos = 1, pos = 2;
    uint8_t code = 1;
    for (size_t i = 0; i < n; ++i) {
        if (!raw[i]) {
            w.bytes[code_pos] = code;
            code_pos = pos++;
            code = 1;
        } else {
            w.bytes[pos++] = raw[i];
            ++code;
        }
    }
    // Frames are <254 bytes, so there is no full 0xff run.
    w.bytes[code_pos] = code;
    w.bytes[pos++] = 0;
    w.size = pos;
    return w;
}
std::optional<Frame> decode(std::span<const uint8_t> b) {
    if (b.empty() || b.size() > max_encoded)
        return {};
    std::array<uint8_t, max_decoded> raw{};
    size_t pos = 0, n = 0;
    while (pos < b.size()) {
        auto code = b[pos++];
        if (!code || size_t(code - 1) > b.size() - pos || size_t(code - 1) > raw.size() - n)
            return {};
        for (unsigned i = 1; i < code; ++i) {
            if (!b[pos])
                return {};
            raw[n++] = b[pos++];
        }
        if (code != 255 && pos < b.size()) {
            if (n == raw.size())
                return {};
            raw[n++] = 0;
        }
    }
    if (n < 28 || raw[0] != 0x4d || raw[1] != 0x58 || raw[2] != 1 || raw[3] || raw[4] != 24 || raw[5] < 1 ||
        raw[5] > 3 || raw[6] || raw[7])
        return {};
    auto length = read(raw, 22, 2);
    if (length > max_payload || n != 28 + length || read(raw, n - 4, 4) != crc32c({raw.data(), n - 4}))
        return {};
    Frame f;
    f.kind = static_cast<Class>(raw[5]);
    f.session = read(raw, 8, 8);
    f.request = static_cast<uint32_t>(read(raw, 16, 4));
    f.message = static_cast<uint16_t>(read(raw, 20, 2));
    f.payload.size = length;
    std::copy_n(raw.begin() + 24, length, f.payload.bytes.begin());
    return f;
}
void Parser::idle(uint64_t now) {
    if (size_ && now >= last_ && now - last_ >= idle_us) {
        size_ = 0;
        discard_ = true;
        ++rejected;
    }
}
std::optional<Frame> Parser::feed(uint8_t byte, uint64_t now) {
    idle(now);
    last_ = now;
    if (!byte) {
        if (discard_) {
            discard_ = false;
            size_ = 0;
            return {};
        }
        if (!size_)
            return {};
        auto f = decode({bytes_.data(), size_});
        size_ = 0;
        if (f)
            ++accepted;
        else
            ++rejected;
        return f;
    }
    if (discard_)
        return {};
    if (size_ == bytes_.size()) {
        size_ = 0;
        discard_ = true;
        ++rejected;
        return {};
    }
    bytes_[size_++] = byte;
    high_water = std::max(high_water, size_);
    return {};
}
const Layout *layout(uint16_t id) {
    auto i = std::find_if(registry.begin(), registry.end(), [&](auto l) { return l.id == id; });
    return i == registry.end() ? nullptr : &*i;
}
bool valid_terminal(std::span<const uint8_t> b) {
    if (b.size() != 58)
        return false;
    if (!read(b, 0, 8) || !read(b, 8, 8) || !read(b, 16, 8) || !read(b, 24, 4) || !read(b, 54, 4))
        return false;
    if (b[28] < 1 || b[28] > 7 || read(b, 29, 2) > 18 || b[35] > 2 || b[36] > 2 || b[49] > 2 ||
        read(b, 50, 2) > 18 || read(b, 52, 2) > 18)
        return false;
    if (b[35] != 2 && read(b, 31, 4))
        return false;
    return b[36] != 0 || (!read(b, 37, 8) && !read(b, 45, 4));
}
bool valid_message(const Frame &f) {
    auto l = layout(f.message);
    if (!l)
        return false;
    const auto &p = f.payload;
    if (f.kind == Class::request)
        return !l->event && p.size == l->request;
    if (f.kind == Class::event) {
        if (!l->event || p.size != l->event || f.request)
            return false;
        if (f.message == 0x8001)
            return valid_terminal(p.data()) && f.session == get(p, 8, 8);
        if (f.message == 0x8002)
            return get(p, 0, 2) <= 18 && p.bytes[2] <= 5 && p.bytes[3] <= 2;
        return get(p, 0, 4) != 0;
    }
    if (f.kind != Class::response || !l->response || p.size != l->response || get(p, 0, 2) > 18 ||
        p.bytes[2] > 2 || p.bytes[3] > 6)
        return false;
    if (get(p, 0, 2))
        return p.bytes[2] == 2 &&
               std::all_of(p.bytes.begin() + 4, p.bytes.begin() + p.size, [](auto b) { return b == 0; });
    switch (static_cast<Message>(f.message)) {
    case Message::hello:
        return get(p, 4, 8) && !get(p, 20, 4);
    case Message::claim:
        return get(p, 4, 8) != 0;
    case Message::status:
        return p.bytes[12] <= 1 && p.bytes[33] <= 7 && get(p, 34, 2) <= 18 && !(p.bytes[36] & 0xfc) &&
               ((p.bytes[36] & 1) || (!get(p, 25, 8) && !p.bytes[33] && !get(p, 34, 2) && !get(p, 39, 8)));
    case Message::capabilities:
        return get(p, 4, 2) == 1 && p.bytes[6] >= 1 && p.bytes[6] <= 8 && !(p.bytes[7] & 0x80) &&
               get(p, 40, 8);
    case Message::channel_capabilities:
        return get(p, 4, 8) && get(p, 12, 8) && !(get(p, 21, 2) & 0xffc0) && p.bytes[63] <= 8 &&
               get(p, 64, 4);
    case Message::channel_status:
        return get(p, 4, 8) && get(p, 12, 8) && p.bytes[21] <= 4 && p.bytes[22] <= 2 && p.bytes[23] <= 3 &&
               get(p, 24, 2) <= 18 && !(get(p, 30, 2) & 0xffe0) && !(get(p, 30, 2) & 16);
    case Message::calibration:
        return get(p, 4, 8) && get(p, 12, 8) && p.bytes[21] <= 3 && p.bytes[22] <= 2 && p.bytes[23] <= 3 &&
               (p.bytes[21] != 2 || get(p, 24, 8));
    case Message::sensor:
        return get(p, 4, 8) && get(p, 12, 8) && get(p, 21, 8) && p.bytes[29] >= 1 && p.bytes[29] <= 4 &&
               p.bytes[30] >= 1 && p.bytes[30] <= 3 && p.bytes[31] >= 1 && p.bytes[31] <= 3 &&
               p.bytes[32] >= 1 && p.bytes[32] <= 4 && p.bytes[33] <= 1 && get(p, 42, 4) &&
               get(p, 50, 2) == 1 &&
               std::bit_cast<int32_t>(static_cast<uint32_t>(get(p, 34, 4))) <=
                   std::bit_cast<int32_t>(static_cast<uint32_t>(get(p, 38, 4)));
    case Message::configure:
        return get(p, 4, 4) != 0;
    case Message::arm:
        return get(p, 4, 4) && get(p, 8, 2);
    case Message::keepalive:
        return get(p, 4, 4) && get(p, 8, 2);
    case Message::run:
        return get(p, 4, 8) && get(p, 12, 4);
    case Message::stop:
        return p.bytes[4] <= 2;
    case Message::terminal:
        return valid_terminal(p.data().subspan(4));
    default:
        return true;
    }
}
Frame request(Message m, uint64_t session, uint32_t id) {
    auto l = layout(static_cast<uint16_t>(m));
    if (!l || l->event)
        throw std::invalid_argument("F2 request opcode");
    Frame f;
    f.message = l->id;
    f.session = session;
    f.request = id;
    f.payload.size = l->request;
    return f;
}
} // namespace x1::f2
