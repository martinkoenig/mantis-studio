#pragma once
#include "../../pty/x1-f2/common.hpp"
#include "../x1-f2/link.hpp"
#include <memory>

namespace x1::f2::simulation::pty {
// TEST ONLY. Owns an internally allocated PTY pair, never an external FD/path.
// Peer stays alive until after FD transport retirement; Host borrows this object
// and must retire first. Both workers are joined, with eventfd shutdown wakeups.
class Backend final : public Transport {
    class Peer;
    std::unique_ptr<Peer> peer_;
    std::unique_ptr<posix::FdTransport> fd_;
    bool release_on_stop_{}, blocked_event_{};

  public:
    Backend();
    ~Backend() override;
    Backend(const Backend &) = delete;
    TransportStatus submit(const Wire &) override;
    FrameRead receive(const Frame &, Deadline) override;
    FrameRead event(Deadline) override;
    void interrupt() noexcept override;
    // Fixture controls are set with Host callers quiescent, before execution.
    // Peer injection itself remains protected by the accepted Link mutex.
    void inject(const Injection &);
    bool fault(std::string_view);
    bool wait_requests(Message, uint64_t count, Deadline);
    bool wait_receives(Message, uint64_t count, Deadline);
    Link::Metrics metrics() const;
    struct ByteMetrics {
        uint64_t requests{}, responses{}, events{}, malformed{}, polls{}, bytes_read{}, bytes_written{};
        uint64_t run_requests{}, identical_run_retries{}, stop_acks{}, stop_roundtrip_ns{};
        bool peer_failed{};
        std::array<char, 128> error{};
        size_t pending_high{};
    };
    ByteMetrics byte_metrics() const;
    static size_t peer_storage_bytes() noexcept;
    posix::FdTransport::Metrics transport_metrics() const;
};
} // namespace x1::f2::simulation::pty
