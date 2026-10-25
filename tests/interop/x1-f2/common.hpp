#pragma once
#include "../../../plugins/first-party/devices/x1/f2/host.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>

#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
namespace x1::f2 {
struct HostConformanceAccess {
    static Frame transaction(Host &h, const Frame &q, Deadline d) {
        std::unique_lock lock(h.ordinary_, std::defer_lock);
        CHECK(lock.try_lock_until(d));
        return h.exchange(q, d);
    }
};
} // namespace x1::f2
namespace interop {
using namespace x1::f2;
using namespace std::chrono_literals;
inline constexpr std::string_view firmware_sha = "92197ce99da9b6b86a3a6139c4bdf2ab696ec3d8";
inline constexpr uint64_t boot = 0x0102030405060708, session = 0x1112131415161718, token = 999;
inline Selection selection() {
    return {1, 1, 1, 0, ExecutionMode::simulation_only, 1, 0};
}
inline FiniteExecution execution(uint32_t pulses = 20) {
    return {500, 10'000, 100, pulses};
}
inline Deadline deadline() {
    return std::chrono::steady_clock::now() + 1s;
}
inline Wire unhex(std::string_view s) {
    CHECK(s.size() % 2 == 0 && s.size() / 2 <= max_wire);
    Wire w;
    w.size = s.size() / 2;
    auto digit = [](char c) -> uint8_t {
        CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    };
    for (size_t i = 0; i < w.size; ++i)
        w.bytes[i] = static_cast<uint8_t>((digit(s[2 * i]) << 4) | digit(s[2 * i + 1]));
    return w;
}
inline std::string hex(const Wire &w) {
    std::string s;
    s.reserve(w.size * 2);
    for (auto b : w.data()) {
        s += "0123456789abcdef"[b >> 4];
        s += "0123456789abcdef"[b & 15];
    }
    return s;
}
template <class T, size_t N> struct Queue {
    std::array<T, N> items{};
    size_t head{}, count{}, high{}, rejected{};
    bool push(const T &v) {
        if (count == N) {
            ++rejected;
            return false;
        }
        items[(head + count) % N] = v;
        ++count;
        high = std::max(high, count);
        return true;
    }
    std::optional<T> pop() {
        if (!count)
            return {};
        auto v = items[head];
        head = (head + 1) % N;
        --count;
        return v;
    }
};
template <class F> void error(F f, Result r, bool consumed = false) {
    try {
        f();
    } catch (const ProtocolError &e) {
        CHECK(e.result == r && e.consumed == consumed);
        return;
    }
    throw std::runtime_error("Expected Host error absent");
}
inline Frame opaque(uint16_t opcode, uint64_t s = 0, uint32_t id = 0, size_t size = 0) {
    Frame q;
    q.message = opcode;
    q.session = s;
    q.request = id;
    q.payload.size = size;
    CHECK(size <= max_payload);
    for (size_t i = 0; i < size; ++i)
        q.payload.bytes[i] = static_cast<uint8_t>(i);
    return q;
}
} // namespace interop
