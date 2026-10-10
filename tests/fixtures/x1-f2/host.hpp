#pragma once
#include "controller.hpp"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace x1::f2::simulation {
uint64_t fresh_identity();
struct Injection {
    uint16_t drop_message{};
    unsigned drops{};
    uint16_t block_message{}, reject_message{};
    Result rejection = Result::hardware_fault;
    uint64_t delay_us{}, terminal_delay_us{};
    uint16_t snapshot_after_message{}, reboot_after_message{};
    bool duplicate{}, corrupt{}, lose_terminal{}, fail_link{}, fail_stop{}, cleanup_error{},
        disable_heartbeat{}, block_next{};
};
// One bounded byte link, no physical selector and no operating-system device API.
class Link {
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Controller controller_;
    Parser endpoint_, receiver_;
    Ring<Frame, 8> replies_, events_;
    std::optional<Frame> stop_reply_, heartbeat_reply_, delayed_, delayed_event_;
    std::array<uint64_t, 19> requests_{};
    uint64_t delayed_event_at_{}, delayed_at_{}, manual_now_{}, interrupts_{}, event_waits_{};
    bool manual_{};
    std::chrono::steady_clock::time_point epoch_ = std::chrono::steady_clock::now();
    Injection injection_;
    uint64_t now_locked() const;
    void deliver_locked(const Frame &);
    void progress_locked();

  public:
    explicit Link(bool manual = false, uint64_t boot = 0)
        : controller_(boot     ? boot
                      : manual ? 0x8266000000000001
                               : fresh_identity()),
          manual_(manual) {}
    bool send(const Wire &);
    std::optional<Frame> receive(const Frame &request, std::chrono::steady_clock::time_point deadline);
    std::optional<Frame> event(std::chrono::steady_clock::time_point deadline);
    void advance(uint64_t now);
    void inject(const Injection &);
    Injection injection() const;
    bool wait_requests(Message, uint64_t count, std::chrono::steady_clock::time_point);
    bool wait_events(uint64_t count, std::chrono::steady_clock::time_point);
    void saturate();
    void inject_event(const Frame &);
    void mutate(const std::function<void(Controller &)> &);
    struct Metrics {
        uint64_t frames{}, malformed{}, runs{}, pulses{}, stops{};
        size_t collector{}, reply_high_water{}, event_high_water{};
    };
    Metrics metrics() const;
    void interrupt();
};
class ProtocolError : public std::runtime_error {
  public:
    Result result;
    bool consumed{};
    explicit ProtocolError(Result r, const char *text, bool admitted = false)
        : std::runtime_error(std::string(text) + " [F2 result=" + std::to_string(static_cast<uint16_t>(r)) +
                             "]"),
          result(r), consumed(admitted) {}
};
struct Snapshot {
    uint64_t boot{}, generation{}, calibration{}, uid{};
    Frame global, channel, status, calibration_status, sensor;
};
bool configuration_fits(const Snapshot &, const BenchConfig &);
struct Association {
    uint64_t boot{}, session{}, execution{};
    uint32_t arm{};
    bool operator==(const Association &) const = default;
};
class Host {
    Link &link_;
    std::timed_mutex ordinary_, lifetime_;
    std::mutex control_, heartbeat_gate_;
    std::condition_variable changed_;
    Association association_;
    uint32_t next_id_ = 2, counter_{};
    uint64_t fence_{};
    int64_t command_dispatch_ns_{}, stop_dispatch_ns_{};
    Result stop_error_ = Result::ok;
    bool inhibited_ = true, exited_{}, lease_active_{};
    std::jthread heartbeat_;
    std::function<uint64_t()> identity_;
    Frame exchange(const Frame &, std::chrono::steady_clock::time_point deadline, bool enabling = false,
                   uint64_t fence = 0);
    Frame ordinary(Message, const Payload &, std::chrono::steady_clock::time_point, bool enabling = false);
    void heartbeat(std::stop_token);
    void resume_heartbeat(std::chrono::steady_clock::time_point);

  public:
    static constexpr unsigned max_attempts = 3;
    static constexpr auto retry_interval = std::chrono::milliseconds(20);
    explicit Host(Link &, std::function<uint64_t()> identity = {});
    ~Host();
    Host(const Host &) = delete;
    Host &operator=(const Host &) = delete;
    Snapshot discover(std::chrono::steady_clock::time_point);
    // begin fences old work. The caller separately binds this association to immutable Studio identities.
    Association start(const Snapshot &, const BenchConfig &, bool on, std::chrono::steady_clock::time_point,
                      uint64_t expected_fence);
    std::optional<Payload> terminal(std::chrono::steady_clock::time_point);
    bool stop(std::chrono::steady_clock::time_point);
    bool shutdown(std::chrono::steady_clock::time_point);
    bool retire_heartbeat(std::chrono::steady_clock::time_point);
    Result stop_error() {
        std::lock_guard lock(control_);
        return stop_error_;
    }
    Association association();
    int64_t dispatched(bool on) {
        std::lock_guard lock(control_);
        return on ? command_dispatch_ns_ : stop_dispatch_ns_;
    }
    void service_heartbeat();
    uint64_t fence() {
        std::lock_guard lock(control_);
        return fence_;
    }
};
} // namespace x1::f2::simulation
