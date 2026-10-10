#pragma once
#include "../../../plugins/first-party/devices/x1/f2/host.hpp"
#include "controller.hpp"

namespace x1::f2::simulation {
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
class Link final : public Transport {
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Controller controller_;
    Parser endpoint_, receiver_;
    Ring<Frame, 8> replies_, events_;
    std::optional<Frame> stop_reply_, heartbeat_reply_, delayed_, delayed_event_;
    std::array<uint64_t, 19> requests_{};
    std::array<uint64_t, 19> response_waits_{};
    uint64_t delayed_event_at_{}, delayed_at_{}, manual_now_{}, interrupts_{}, event_waits_{};
    bool manual_{};
    bool closed_{};
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
    TransportStatus submit(const Wire &) override;
    FrameRead receive(const Frame &request, Deadline) override;
    FrameRead event(Deadline) override;
    // Test peer byte ingress permits deliberate fragmentation/malformed framing.
    bool send(const Wire &wire) {
        return submit(wire) == TransportStatus::ready;
    }
    void close();
    void advance(uint64_t now);
    void inject(const Injection &);
    Injection injection() const;
    bool wait_requests(Message, uint64_t count, std::chrono::steady_clock::time_point);
    bool wait_receives(Message, uint64_t count, Deadline);
    bool wait_events(uint64_t count, std::chrono::steady_clock::time_point);
    void saturate();
    void inject_event(const Frame &);
    void mutate(const std::function<void(Controller &)> &);
    struct Metrics {
        uint64_t frames{}, malformed{}, runs{}, pulses{}, stops{};
        size_t collector{}, reply_high_water{}, event_high_water{};
    };
    Metrics metrics() const;
    void interrupt() noexcept override;
};
} // namespace x1::f2::simulation
