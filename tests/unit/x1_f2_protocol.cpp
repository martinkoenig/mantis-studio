#include "../../plugins/first-party/devices/x1/f2/codec.hpp"
#include "../fixtures/x1-f2/golden.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
namespace {
std::atomic<uint64_t> heap_allocations{};
}
void *operator new(std::size_t n) {
    ++heap_allocations;
    if (auto p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void *p) noexcept {
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void *p) noexcept {
    std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept {
    std::free(p);
}
using namespace x1::f2;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
std::vector<uint8_t> hex(std::string_view s) {
    std::vector<uint8_t> b;
    for (size_t i = 0; i < s.size(); i += 2)
        b.push_back(static_cast<uint8_t>(std::stoul(std::string(s.substr(i, 2)), nullptr, 16)));
    return b;
}
// Independent test COBS construction is used only to create deliberate raw header mutations.
std::vector<uint8_t> cobs(std::vector<uint8_t> b) {
    std::vector<uint8_t> out{0, 0};
    size_t mark = 1;
    unsigned code = 1;
    for (auto v : b) {
        if (!v) {
            out[mark] = static_cast<uint8_t>(code);
            mark = out.size();
            out.push_back(0);
            code = 1;
        } else {
            out.push_back(v);
            ++code;
        }
    }
    out[mark] = static_cast<uint8_t>(code);
    out.push_back(0);
    return out;
}
std::optional<Frame> parse(const std::vector<uint8_t> &b) {
    Parser p;
    std::optional<Frame> f;
    for (auto v : b)
        if (auto r = p.feed(v, 0))
            f = r;
    return f;
}
int main() {
    try {
        auto check = hex("313233343536373839");
        CHECK(crc32c(check) == 0xe3069283);
        CHECK(crc32c({}) == 0);
        for (size_t i = 0; i < f2_golden.size(); ++i) {
            auto bytes = hex(f2_golden[i]);
            auto f = parse(bytes);
            CHECK(f);
            auto wire = encode(*f);
            CHECK(wire && wire->size == bytes.size() &&
                  std::equal(bytes.begin(), bytes.end(), wire->bytes.begin()));
            Parser fragmented;
            unsigned received{};
            for (size_t j = 0; j < bytes.size(); ++j)
                if (fragmented.feed(bytes[j], j * 100))
                    ++received;
            CHECK(received == 1 && fragmented.high_water <= 161);
            if (i >= 3)
                CHECK(valid_message(*f));
            auto corrupt = bytes;
            corrupt[corrupt.size() - 3] ^= 0x10;
            CHECK(!parse(corrupt));
            CHECK(!decode(std::span(bytes).subspan(1, bytes.size() - 3)));
        }
        auto configure = parse(hex(f2_golden[11]));
        CHECK(configure->payload.size == 29);
        CHECK(get(configure->payload, 1, 4) == 50'000 && get(configure->payload, 5, 4) == 100'000 &&
              get(configure->payload, 9, 4) == 2'000);
        CHECK(get(configure->payload, 13, 8) == 0xaabbccddeeff0011 &&
              get(configure->payload, 21, 8) == 0x1122334455667788);
        auto sensor = parse(hex(f2_golden[12]));
        CHECK(!sensor->session && !sensor->request && sensor->payload.size == 10 &&
              get(sensor->payload, 1, 8) == 0x1122334455667788);
        auto maximum = parse(hex(f2_golden[2]));
        CHECK(maximum->payload.size == 132);
        for (size_t i = 0; i < 132; ++i)
            CHECK(maximum->payload.bytes[i] == i);
        CHECK(hex(f2_golden[2]).size() == 163);
        maximum->payload.size = 133;
        CHECK(!encode(*maximum));
        maximum->payload.size = 132;
        maximum->payload.bytes.fill(0);
        auto zero_wire = *encode(*maximum);
        CHECK(parse(std::vector(zero_wire.data().begin(), zero_wire.data().end())));
        std::vector<uint8_t> raw = hex("4d5801001801000000000000000000000000000001000000e98d830f");
        // Recompute CRC independently in test code after each envelope mutation, isolating header checks.
        auto crc = [](std::span<const uint8_t> b) {
            uint32_t c = 0xffffffff;
            for (auto v : b) {
                c ^= v;
                for (unsigned k = 0; k < 8; ++k)
                    c = (c & 1) ? (c >> 1) ^ 0x82f63b78 : c >> 1;
            }
            return c ^ 0xffffffff;
        };
        for (auto [offset, value] : std::array<std::pair<size_t, uint8_t>, 9>{
                 {{0, 0x51}, {2, 2}, {3, 1}, {4, 23}, {5, 4}, {6, 1}, {7, 1}, {22, 1}, {23, 1}}}) {
            auto b = raw;
            b[offset] = value;
            auto c = crc(std::span(b).first(24));
            for (unsigned k = 0; k < 4; ++k)
                b[24 + k] = static_cast<uint8_t>(c >> (8 * k));
            CHECK(!parse(cobs(b)));
        }
        Parser p;
        auto good = hex(f2_golden[3]);
        for (auto v : std::string("ESP ROM chatter"))
            CHECK(!p.feed(static_cast<uint8_t>(v), 0));
        CHECK(!p.feed(0, 0));
        for (unsigned i = 0; i < 200; ++i)
            CHECK(!p.feed(1, 0));
        // A suffix lacking a new delimiter must not be recovered.
        for (size_t i = 1; i < good.size() - 1; ++i)
            CHECK(!p.feed(good[i], 0));
        CHECK(!p.feed(0, 0));
        unsigned received{};
        for (auto v : good)
            if (p.feed(v, 0))
                ++received;
        CHECK(received == 1 && p.rejected == 2);
        CHECK(!p.feed(2, 1));
        p.idle(50'001);
        CHECK(p.buffered() == 0);
        for (size_t i = 1; i < good.size() - 1; ++i)
            CHECK(!p.feed(good[i], 50'002));
        CHECK(!p.feed(0, 50'002));
        for (auto v : good)
            if (p.feed(v, 50'002))
                ++received;
        CHECK(received == 2);
        CHECK(!decode(hex("ff010203")));
        CHECK(!decode(std::vector<uint8_t>(162, 1)));
        CHECK(!decode(std::vector<uint8_t>(161, 1))); // decoded output overflow
        // Registry/layout checks use independent accepted numeric lengths.
        constexpr std::array<Layout, 19> expected{{{1, 0, 24, 0},
                                                   {2, 8, 12, 0},
                                                   {3, 0, 51, 0},
                                                   {4, 0, 48, 0},
                                                   {5, 9, 68, 0},
                                                   {6, 9, 32, 0},
                                                   {7, 9, 46, 0},
                                                   {8, 10, 52, 0},
                                                   {16, 29, 8, 0},
                                                   {17, 8, 10, 0},
                                                   {18, 8, 10, 0},
                                                   {19, 16, 16, 0},
                                                   {20, 0, 5, 0},
                                                   {21, 0, 4, 0},
                                                   {22, 0, 4, 0},
                                                   {23, 24, 62, 0},
                                                   {32769, 0, 0, 58},
                                                   {32770, 0, 0, 24},
                                                   {32771, 0, 0, 16}}};
        for (auto l : expected) {
            auto actual = layout(l.id);
            CHECK(actual && actual->request == l.request && actual->response == l.response &&
                  actual->event == l.event);
        }
        CHECK(!layout(0x20));
        Frame response;
        response.kind = Class::response;
        response.message = 0x15;
        response.payload.size = 4;
        CHECK(valid_message(response));
        for (size_t offset : {size_t(0), size_t(2), size_t(3)}) {
            auto bad = response;
            bad.payload.bytes[offset] = 255;
            CHECK(!valid_message(bad));
        }
        response.message = 0x8002;
        CHECK(!valid_message(response));
        auto begin = std::chrono::steady_clock::now();
        constexpr unsigned iterations = 100'000;
        auto allocations_before = heap_allocations.load();
        Parser stream;
        for (unsigned i = 0; i < iterations; ++i) {
            auto encoded = encode(*sensor);
            auto decoded = decode(encoded->data().subspan(1, encoded->size - 2));
            CHECK(decoded && *decoded == *sensor);
            for (auto b : encoded->data())
                stream.feed(b, 0);
        }
        auto allocations = heap_allocations.load() - allocations_before;
        CHECK(allocations == 0 && stream.accepted == iterations);
        auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        std::cout << "F2 V1-V13, mutations/layouts/parser passed; fixed parser=" << sizeof(Parser)
                  << " bytes, collector=161, measured zero encode/decode/parser heap allocations; "
                  << iterations / seconds << " encode+decode/s (Debug synthetic)\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
