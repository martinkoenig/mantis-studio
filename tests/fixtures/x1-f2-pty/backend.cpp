#include "backend.hpp"
#include "../../../plugins/first-party/devices/x1/f2-serial/serial-settings.hpp"
#include <climits>
#include <cstdio>
#include <sys/eventfd.h>

namespace x1::f2::simulation::pty {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
class Backend::Peer {
    struct Descriptor {
        int value;
        explicit Descriptor(int fd) noexcept : value(fd) {}
        Descriptor(const Descriptor &) = delete;
        operator int() const noexcept {
            return value;
        }
        void close() noexcept {
            if (value >= 0) {
                ::close(value); // Linux releases the descriptor even on EINTR; never retry.
                value = -1;
            }
        }
        ~Descriptor() {
            close();
        }
    };
    Descriptor fd_, wake_; // Own before Link/random identity construction can throw.
    std::jthread worker_;
    Link link_; // Existing accepted real-time controller and byte-level endpoint.
    Parser parser_;
    mutable std::mutex mutex_;
    ByteMetrics metrics_;
    bool disconnect_on_run_{}, corrupt_run_{}, fragmented_{};
    std::optional<Wire> last_run_;
    Clock::time_point stop_received_{};
    void wake() noexcept {
        uint64_t one = 1;
        auto ignored = ::write(wake_, &one, sizeof one);
        (void)ignored;
    }
    void run(std::stop_token stop) noexcept {
        try {
            Ring<Wire, 16> output;
            std::optional<Wire> stop_reply, heartbeat_reply, active;
            std::array<std::optional<Frame>, 3> pending;
            size_t offset = 0;
            Wire ingress;
            auto epoch = Clock::now();
            auto now = [&] {
                return static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - epoch).count());
            };
            auto queue = [&](const Frame &f) {
                auto wire = encode(f);
                CHECK(wire);
                {
                    std::lock_guard lock(mutex_);
                    if (f.kind == Class::event)
                        ++metrics_.events;
                    else {
                        ++metrics_.responses;
                        if (f.message == 0x13 && corrupt_run_) {
                            wire->bytes[wire->size - 3] ^= 0x80;
                            corrupt_run_ = false;
                        }
                        if (f.message == 0x14) {
                            ++metrics_.stop_acks;
                            metrics_.stop_roundtrip_ns =
                                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                          Clock::now() - stop_received_)
                                                          .count());
                        }
                    }
                }
                if (f.kind == Class::response && f.message == 0x14) {
                    CHECK(!stop_reply || *stop_reply == *wire); // Coalesce only identical idempotent ACKs.
                    stop_reply = *wire;
                } else if (f.kind == Class::response && f.message == 0x12) {
                    CHECK(!heartbeat_reply || *heartbeat_reply == *wire);
                    heartbeat_reply = *wire;
                } else
                    CHECK(output.push(*wire)); // Explicit peer failure on overflow; never overwrite.
                std::lock_guard lock(mutex_);
                metrics_.pending_high = std::max(metrics_.pending_high, output.high_water);
            };
            auto progress = [&] {
                for (auto &q : pending)
                    if (q)
                        if (auto r = link_.receive(*q, Clock::now())) {
                            queue(*r);
                            q.reset();
                        }
                while (auto e = link_.event(Clock::now()))
                    queue(*e);
            };
            auto select = [&] {
                if (!active) {
                    if (stop_reply) {
                        active = stop_reply;
                        stop_reply.reset();
                    } else if (heartbeat_reply) {
                        active = heartbeat_reply;
                        heartbeat_reply.reset();
                    } else
                        active = output.pop();
                    offset = 0;
                }
            };
            while (!stop.stop_requested()) {
                progress();
                select();
                auto due = link_.activity_deadline();
                int timeout = -1;
                if (due != Deadline::max()) {
                    auto ms = std::chrono::ceil<std::chrono::milliseconds>(due - Clock::now()).count();
                    timeout = static_cast<int>(std::clamp<int64_t>(ms, 0, INT_MAX));
                }
                pollfd fds[2]{{fd_, static_cast<short>(POLLIN | (active ? POLLOUT : 0)), 0},
                              {wake_, POLLIN, 0}};
                auto ready = ::poll(fds, 2, timeout);
                if (ready < 0 && errno == EINTR)
                    continue;
                CHECK(ready >= 0);
                {
                    std::lock_guard lock(mutex_);
                    ++metrics_.polls;
                }
                if (stop.stop_requested())
                    break;
                CHECK(!(fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)));
                if (fds[1].revents & POLLIN) {
                    uint64_t v;
                    auto ignored = ::read(wake_, &v, sizeof v);
                    (void)ignored;
                }
                if (fds[0].revents & POLLIN) {
                    std::array<uint8_t, 1024> bytes{};
                    auto n = ::read(fd_, bytes.data(), bytes.size());
                    if (n < 0 && (errno == EINTR || errno == EAGAIN))
                        continue;
                    CHECK(n > 0);
                    {
                        std::lock_guard lock(mutex_);
                        metrics_.bytes_read += static_cast<uint64_t>(n);
                    }
                    for (ssize_t i = 0; i < n; ++i) {
                        auto byte = bytes[static_cast<size_t>(i)];
                        if (!byte && ingress.size == 1) // Consecutive leading delimiter.
                            ingress.size = 0;
                        if (ingress.size < max_wire && (ingress.size || !byte))
                            ingress.bytes[ingress.size++] = byte;
                        if (auto f = parser_.feed(byte, now())) {
                            // Framing is checked at PTY ingress, then the accepted Link
                            // validates these identical canonical bytes independently.
                            auto w = encode(*f);
                            CHECK(w && f->kind == Class::request && *w == ingress);
                            {
                                std::lock_guard lock(mutex_);
                                ++metrics_.requests;
                                if (f->message == 0x13) {
                                    ++metrics_.run_requests;
                                    if (last_run_ && *last_run_ == *w)
                                        ++metrics_.identical_run_retries;
                                    last_run_ = *w;
                                    CHECK(!disconnect_on_run_);
                                }
                                if (f->message == 0x14)
                                    stop_received_ = Clock::now();
                            }
                            CHECK(link_.submit(ingress) == TransportStatus::ready);
                            auto lane = f->message == 0x14 ? 2 : f->message == 0x12 ? 1 : 0;
                            pending[lane] = *f;
                            progress();
                        }
                        if (!byte && ingress.size > 1)
                            ingress = {};
                    }
                    std::lock_guard lock(mutex_);
                    metrics_.malformed = parser_.rejected;
                }
                if (active && (fds[0].revents & POLLOUT)) {
                    size_t chunk;
                    {
                        std::lock_guard lock(mutex_);
                        chunk = fragmented_ ? 1 : max_wire;
                    }
                    auto n =
                        ::write(fd_, active->bytes.data() + offset, std::min(chunk, active->size - offset));
                    if (n < 0 && (errno == EINTR || errno == EAGAIN))
                        continue;
                    CHECK(n > 0);
                    offset += static_cast<size_t>(n);
                    {
                        std::lock_guard lock(mutex_);
                        metrics_.bytes_written += static_cast<uint64_t>(n);
                    }
                    if (offset == active->size)
                        active.reset();
                }
            }
        } catch (const std::exception &e) {
            std::lock_guard lock(mutex_);
            metrics_.peer_failed = true;
            std::snprintf(metrics_.error.data(), metrics_.error.size(), "%.120s", e.what());
        } catch (...) {
            std::lock_guard lock(mutex_);
            metrics_.peer_failed = true;
        }
        // HUP propagates through FdTransport. No reconnect and no peer replacement.
        link_.close();
        fd_.close(); // Worker alone closes the master, exactly once.
    }

  public:
    explicit Peer(int transferred_fd) : fd_(transferred_fd), wake_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {
        CHECK(wake_ >= 0);
        worker_ = std::jthread([this](std::stop_token s) { run(s); });
    }
    ~Peer() {
        worker_.request_stop();
        wake();
        worker_.join();
    }
    void inject(const Injection &i) {
        link_.inject(i);
        wake();
    }
    bool fault(std::string_view v) {
        std::lock_guard lock(mutex_);
        if (v == "disconnect")
            disconnect_on_run_ = true;
        else if (v == "corrupt-run")
            corrupt_run_ = true;
        else if (v == "fragmented")
            fragmented_ = true;
        else
            return false;
        return true;
    }
    bool wait_requests(Message m, uint64_t count, Deadline d) {
        return link_.wait_requests(m, count, d);
    }
    Link::Metrics metrics() const {
        return link_.metrics();
    }
    ByteMetrics bytes() const {
        std::lock_guard lock(mutex_);
        return metrics_;
    }
};
Backend::Backend() {
    ptytest::Pty pair; // Opens only the slave name just allocated by posix_openpt.
    CHECK(posix::configure_serial(pair.slave));
    peer_ = std::unique_ptr<Peer>(new Peer(pair.take_master()));
    fd_ = std::unique_ptr<posix::FdTransport>(new posix::FdTransport(pair.take_slave()));
}
Backend::~Backend() = default;
TransportStatus Backend::submit(const Wire &w) {
    auto status = fd_->submit(w);
    if (release_on_stop_ && w.size >= 2) {
        auto f = decode(w.data().subspan(1, w.size - 2));
        if (f && f->message == 0x14)
            ptytest::Access::gate(*fd_, false);
    } // STOP first fences queued TX, then partial bytes may retire.
    return status;
}
FrameRead Backend::receive(const Frame &q, Deadline d) {
    return fd_->receive(q, d);
}
FrameRead Backend::event(Deadline d) {
    auto r = fd_->event(d);
    if (blocked_event_ && r.status == TransportStatus::timeout)
        r.status = TransportStatus::interrupted; // Same bounded blocked-read fault as accepted Link.
    return r;
}
void Backend::interrupt() noexcept {
    fd_->interrupt();
}
void Backend::inject(const Injection &i) {
    blocked_event_ = i.block_next;
    peer_->inject(i);
}
bool Backend::fault(std::string_view v) {
    if (v == "congestion" || v == "partial-run" || v == "queued-claim") {
        ptytest::Access::pause_message(*fd_, v == "queued-claim" ? Message::claim : Message::run,
                                       v == "partial-run" ? 3 : 0);
        release_on_stop_ = true; // Fixed per-instance test policy, never a production callback.
        return true;
    }
    return peer_->fault(v);
}
bool Backend::wait_requests(Message m, uint64_t count, Deadline d) {
    return peer_->wait_requests(m, count, d);
}
bool Backend::wait_receives(Message m, uint64_t count, Deadline d) {
    auto lane = m == Message::stop ? 2 : m == Message::keepalive ? 1 : 0;
    return ptytest::Access::await(*fd_, [=](auto metrics) { return metrics.waits[lane] >= count; }, d);
}
Link::Metrics Backend::metrics() const {
    return peer_->metrics();
}
size_t Backend::peer_storage_bytes() noexcept {
    return sizeof(Peer);
}
Backend::ByteMetrics Backend::byte_metrics() const {
    return peer_->bytes();
}
posix::FdTransport::Metrics Backend::transport_metrics() const {
    return fd_->metrics();
}
} // namespace x1::f2::simulation::pty
