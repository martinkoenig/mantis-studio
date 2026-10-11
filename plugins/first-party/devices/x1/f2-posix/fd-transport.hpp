#pragma once
#include "../f2/transport.hpp"
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace x1::f2::posix {
// Linux-only, private and uninstalled. Ownership transfers on constructor entry,
// including failure. Caller supplies a configured, duplex byte FD (PTY in tests).
// Ownership includes exclusive I/O and flag control of its open-file description.
// No opening, termios policy, discovery, reconnect or physical activation here.
// Host and all callers must quiesce before destruction. close() may run with reads.
class FdTransport final : public Transport {
    friend struct FdTransportConformanceAccess;
    template <class T, size_t N> struct Queue {
        std::array<T, N> data{};
        size_t head{}, count{}, high{};
        bool push(const T &v) {
            if (count == N)
                return false;
            data[(head + count++) % N] = v;
            high = std::max(high, count);
            return true;
        }
        std::optional<T> pop() {
            if (!count)
                return {};
            auto v = data[head];
            head = (head + 1) % N;
            --count;
            return v;
        }
        void clear() {
            head = count = 0;
        }
    };

  public:
    static constexpr size_t tx_capacity = 8, response_capacity = 8, event_capacity = 8;
    struct Metrics {
        uint64_t polls{}, wakes{}, reads{}, writes{}, rx_bytes{}, tx_bytes{}, short_writes{}, eintr{},
            read_eagain{}, write_eagain{}, malformed{}, discarded{}, tx_rejected{}, cancelled{},
            response_overflow{}, event_overflow{}, control_overflow{};
        std::array<uint64_t, 4> waits{};
        size_t tx_high{}, response_high{}, event_high{}, parser_high{}, active_bytes{}, active_size{};
        int failure_errno{};
    };
    explicit FdTransport(int transferred_fd);
    ~FdTransport() override;
    FdTransport(const FdTransport &) = delete;
    FdTransport &operator=(const FdTransport &) = delete;
    TransportStatus submit(const Wire &) override;
    FrameRead receive(const Frame &, Deadline) override;
    FrameRead event(Deadline) override;
    void interrupt() noexcept override;
    void close() noexcept;
    Metrics metrics() const;

  private:
    // lifecycle_ serializes joins; never acquired by worker or ordinary operations.
    // mutex_ guards all shared state. No poll/wait/join under mutex_. A single
    // nonblocking <=163-byte write under mutex_ is the first-byte admission fence.
    mutable std::mutex mutex_;
    std::mutex lifecycle_;
    std::condition_variable changed_;
    int fd_ = -1, wake_fd_ = -1;
    std::thread worker_;
    TransportStatus state_ = TransportStatus::ready;
    Queue<Wire, tx_capacity> tx_;
    Queue<Frame, response_capacity> replies_;
    Queue<Frame, event_capacity> events_;
    std::optional<Wire> stop_tx_, heartbeat_tx_, active_;
    size_t offset_{};
    std::optional<Frame> stop_rx_, heartbeat_rx_;
    std::array<bool, 4> overflow_{};
    std::array<uint64_t, 4> observed_interrupt_{};
    uint64_t interrupts_{};
    Metrics metrics_;
    Parser parser_; // Exclusively owned by worker; diagnostics published under mutex_.
    uint64_t last_byte_{};
#ifdef MANTIS_F2_POSIX_TESTING
    // Narrow per-instance syscall instrumentation, no arbitrary callbacks. Tests
    // alter this only under mutex_ and wake the worker. Real PTY syscalls remain.
    bool hold_write_{}, pause_after_write_{};
    size_t write_limit_ = max_wire, read_limit_ = 1024;
    unsigned write_eintr_{}, read_eintr_{}, write_eagain_{};
#endif
    void run() noexcept;
    void wake_locked() noexcept;
    void fail_locked(int error) noexcept;
    void clear_locked() noexcept;
    void route_locked(const Frame &);
    FrameRead wait(const Frame *request, Deadline);
    static size_t lane(const Frame &);
};
} // namespace x1::f2::posix
