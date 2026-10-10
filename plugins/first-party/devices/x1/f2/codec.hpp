#pragma once
// Private F2 v1 codec. Authority: firmware 854220b1717ef1e299bbd840d4cb5005dbcc31ed.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>

namespace x1::f2 {
constexpr size_t max_payload = 132, max_decoded = 160, max_encoded = 161, max_wire = 163;
enum class Class : uint8_t { request = 1, response = 2, event = 3 };
enum class Message : uint16_t {
    hello = 1,
    claim = 2,
    status = 3,
    capabilities = 4,
    channel_capabilities = 5,
    channel_status = 6,
    calibration = 7,
    sensor = 8,
    configure = 0x10,
    arm = 0x11,
    keepalive = 0x12,
    run = 0x13,
    stop = 0x14,
    disarm = 0x15,
    clear_fault = 0x16,
    terminal = 0x17,
    run_terminal = 0x8001,
    fault = 0x8002,
    lease_expired = 0x8003
};
enum class Result : uint16_t {
    ok,
    invalid_argument,
    unsupported,
    bad_state,
    no_session,
    stale_boot,
    stale_session,
    stale_request,
    conflicting_duplicate,
    not_calibrated,
    lease_expired,
    busy,
    limit_exceeded,
    hardware_fault,
    timeout,
    internal_error,
    unavailable,
    snapshot_changed,
    calibration_invalid
};
enum class State : uint8_t { boot, inhibited, configured, armed, running, stopping, fault };
enum class Stage : uint8_t { accepted, applied, terminal };
struct Payload {
    std::array<uint8_t, max_payload> bytes{};
    size_t size{};
    std::span<const uint8_t> data() const {
        return {bytes.data(), size};
    }
    bool operator==(const Payload &) const = default;
};
struct Frame {
    Class kind = Class::request;
    uint64_t session{};
    uint32_t request{};
    uint16_t message{};
    Payload payload;
    bool operator==(const Frame &) const = default;
};
struct Wire {
    std::array<uint8_t, max_wire> bytes{};
    size_t size{};
    std::span<const uint8_t> data() const {
        return {bytes.data(), size};
    }
    bool operator==(const Wire &) const = default;
};
uint64_t read(std::span<const uint8_t>, size_t offset, size_t width);
void write(std::span<uint8_t>, size_t offset, size_t width, uint64_t value);
inline uint64_t get(const Payload &p, size_t offset, size_t width) {
    return read(p.data(), offset, width);
}
inline void put(Payload &p, size_t offset, size_t width, uint64_t v) {
    write({p.bytes.data(), p.size}, offset, width, v);
}
uint32_t crc32c(std::span<const uint8_t>);
std::optional<Wire> encode(const Frame &);
std::optional<Frame> decode(std::span<const uint8_t> cobs);
// A collector never rescans an arbitrary suffix. Timeout/overflow discard to delimiter.
class Parser {
    std::array<uint8_t, max_encoded> bytes_{};
    size_t size_{};
    uint64_t last_{};
    bool discard_{};

  public:
    uint64_t rejected{}, accepted{};
    size_t high_water{};
    static constexpr uint64_t idle_us = 50'000;
    std::optional<Frame> feed(uint8_t byte, uint64_t now_us);
    void idle(uint64_t now_us);
    size_t buffered() const {
        return size_;
    }
};
struct Layout {
    uint16_t id;
    uint8_t request, response, event;
};
inline constexpr std::array<Layout, 19> registry{{{1, 0, 24, 0},
                                                  {2, 8, 12, 0},
                                                  {3, 0, 51, 0},
                                                  {4, 0, 48, 0},
                                                  {5, 9, 68, 0},
                                                  {6, 9, 32, 0},
                                                  {7, 9, 46, 0},
                                                  {8, 10, 52, 0},
                                                  {0x10, 29, 8, 0},
                                                  {0x11, 8, 10, 0},
                                                  {0x12, 8, 10, 0},
                                                  {0x13, 16, 16, 0},
                                                  {0x14, 0, 5, 0},
                                                  {0x15, 0, 4, 0},
                                                  {0x16, 0, 4, 0},
                                                  {0x17, 24, 62, 0},
                                                  {0x8001, 0, 0, 58},
                                                  {0x8002, 0, 0, 24},
                                                  {0x8003, 0, 0, 16}}};
const Layout *layout(uint16_t);
bool valid_terminal(std::span<const uint8_t>);
// Typed response/event validation is separate from framing and direction admission.
bool valid_message(const Frame &);
Frame request(Message, uint64_t session = 0, uint32_t id = 0);
} // namespace x1::f2
