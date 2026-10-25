#pragma once
#include "common.hpp"
#include <mantis_x1/controller/v1/controller.hpp>

namespace interop {
namespace fw = mantis_x1::uart::v1;
namespace mcu = mantis_x1::controller::v1;
// Test-only transport. The ONLY peer semantics are the external accepted F4a engine.
// Host borrows this object; quiesce Host/worker before transport destruction.
class FirmwareTransport final : public Transport {
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    mcu::Controller controller_;
    fw::StreamParser peer_;
    Parser receiver_;
    fw::Tick tick_{};
    Queue<Frame, 8> replies_, events_;
    std::optional<Frame> stop_, heartbeat_;
    std::array<uint64_t, 20> waits_{};
    uint64_t interrupts_{};
    bool closed_{};
    void output(const fw::Frame &);
    void accept_bytes(const Wire &);
    std::optional<Wire> last_event_;
    void progress();

  public:
    struct Injection {
        uint16_t block{}, drop{};
        unsigned drops{};
        bool lose_events{};
        unsigned mutation{};
    };

  private:
    Injection injection_{};

  public:
    struct Exchange {
        Wire request, response;
        bool replay{}, consumed{};
    };

  private:
    std::array<Exchange, 256> trace_{};
    std::array<Wire, 256> submitted_{};
    size_t traces_{}, submissions_{}, rejected_frames_{}, dispatched_{}, queue_drops_{}, diagnostic_drops_{};

  public:
    void inject(Injection);
    size_t trace_count() const;
    Exchange exchange(size_t) const;
    size_t submission_count() const;
    Wire submission(size_t) const;
    size_t rejected() const;
    size_t dispatched() const;
    size_t diagnostic_drops() const;
    std::optional<Wire> event_wire() const;
    explicit FirmwareTransport();
    TransportStatus submit(const Wire &) override;
    TransportStatus batch(std::span<const Wire>);
    FrameRead receive(const Frame &, Deadline) override;
    FrameRead event(Deadline) override;
    void interrupt() noexcept override;
    void close();
    void advance(uint32_t ms);
    void reboot(uint64_t boot_token);
    void invalidate_snapshot();
    bool wait_receive(uint16_t opcode, uint64_t count, Deadline);
    struct Facts {
        uint64_t boot, session, execution, snapshot;
        uint32_t next, config, arm, heartbeat, executions;
        uint16_t lease;
        fw::ControllerState state;
        bool operator==(const Facts &) const = default;
    };
    Facts facts() const;
    std::pair<size_t, size_t> high_water() const;
    void inject_bytes(const Wire &); // RX link mutation, still goes through the Studio parser.
};
} // namespace interop
