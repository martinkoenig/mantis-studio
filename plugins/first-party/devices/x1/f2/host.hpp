#pragma once
#include "configuration.hpp"
#include "transport.hpp"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace x1::f2 {
uint64_t fresh_identity();
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
    Selection selection;
    uint64_t boot{}, generation{}, calibration{}, uid{};
    Frame global, channel, status, calibration_status, sensor;
};
bool configuration_fits(const Snapshot &, const Selection &, const FiniteExecution &);
struct Association {
    uint64_t boot{}, session{}, execution{};
    uint32_t arm{};
    bool operator==(const Association &) const = default;
};
// Ordinary public operations are caller-serialized; STOP, heartbeat service and
// heartbeat retirement are independent side paths. Quiesce public calls before
// destruction; the destructor interrupts and joins the worker before releasing
// its borrowed Transport reference (see transport.hpp lifetime contract).
class Host {
    Transport &transport_;
    const Selection selection_;
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
    explicit Host(Transport &, Selection, std::function<uint64_t()> identity = {});
    ~Host();
    Host(const Host &) = delete;
    Host &operator=(const Host &) = delete;
    Snapshot discover(std::chrono::steady_clock::time_point);
    // begin fences old work. The caller separately binds this association to immutable Studio identities.
    Association start(const Snapshot &, const FiniteExecution &, bool on,
                      std::chrono::steady_clock::time_point, uint64_t expected_fence);
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
} // namespace x1::f2
