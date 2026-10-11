#include "fd-transport.hpp"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <sys/eventfd.h>
#include <system_error>
#include <unistd.h>

namespace x1::f2::posix {
namespace {
using Clock = std::chrono::steady_clock;
uint64_t now_us() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());
}
void add(uint64_t &v, uint64_t n = 1) {
    v += std::min(n, std::numeric_limits<uint64_t>::max() - v);
}
void close_fd(int &fd) noexcept {
    if (fd >= 0) {
        // Linux close releases the descriptor even on EINTR; never retry a reused FD.
        ::close(fd);
        fd = -1;
    }
}
} // namespace
FdTransport::FdTransport(int fd) : fd_(fd) {
    try {
        auto flags = ::fcntl(fd_, F_GETFL);
        auto descriptor_flags = ::fcntl(fd_, F_GETFD);
        if (flags < 0 || descriptor_flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0 ||
            ::fcntl(fd_, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0)
            throw std::system_error(errno, std::generic_category(), "F2 supplied FD flags");
        wake_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wake_fd_ < 0)
            throw std::system_error(errno, std::generic_category(), "F2 worker wake FD");
        worker_ = std::thread([this] { run(); });
    } catch (...) {
        close_fd(wake_fd_);
        close_fd(fd_);
        throw;
    }
}
FdTransport::~FdTransport() {
    close();
}
size_t FdTransport::lane(const Frame &f) {
    return f.message == 0x14 ? 2 : f.message == 0x12 ? 1 : 0;
}
void FdTransport::wake_locked() noexcept {
    if (wake_fd_ < 0)
        return;
    uint64_t one = 1;
    // Nonblocking eventfd's fixed-size writes do not wait for kernel/peer progress.
    // EAGAIN means a wake is already pending. No per-byte wake traffic.
    for (unsigned i = 0; i < 4; ++i) {
        if (::write(wake_fd_, &one, sizeof one) == sizeof one || errno == EAGAIN)
            return;
        if (errno != EINTR)
            break;
    }
    fail_locked(errno);
}
void FdTransport::clear_locked() noexcept {
    tx_.clear();
    replies_.clear();
    events_.clear();
    stop_tx_.reset();
    heartbeat_tx_.reset();
    active_.reset();
    stop_rx_.reset();
    heartbeat_rx_.reset();
    offset_ = 0;
}
void FdTransport::fail_locked(int error) noexcept {
    if (state_ == TransportStatus::ready) {
        state_ = TransportStatus::unavailable;
        metrics_.failure_errno = error;
        clear_locked();
    }
    changed_.notify_all();
}
TransportStatus FdTransport::submit(const Wire &w) {
    if (w.size < 3 || w.size > max_wire || w.bytes[0] || w.bytes[w.size - 1])
        return TransportStatus::invalid;
    auto q = decode(w.data().subspan(1, w.size - 2));
    if (!q || q->kind != Class::request)
        return TransportStatus::invalid;
    if ((q->message == 0x14 && (q->request || q->payload.size)) ||
        (q->message == 0x12 && (q->request || q->payload.size != 8)))
        return TransportStatus::invalid;
    std::lock_guard lock(mutex_);
    if (state_ != TransportStatus::ready)
        return state_;
    if (q->message == 0x14) {
        // Cancel everything not yet committed to its first kernel byte. A partial
        // frame must finish to keep the wire parseable; STOP is next, never spliced.
        add(metrics_.cancelled, tx_.count + (heartbeat_tx_ ? 1 : 0));
        tx_.clear();
        heartbeat_tx_.reset();
        if (active_ && !offset_) {
            active_.reset();
            add(metrics_.cancelled);
        }
        if (!stop_tx_) {
            stop_rx_.reset();
            overflow_[2] = false;
            stop_tx_ = w;
        } else if (*stop_tx_ != w) {
            add(metrics_.tx_rejected);
            return TransportStatus::unavailable;
        }
    } else {
        bool stopping = stop_tx_.has_value();
        if (active_) {
            auto f = decode(active_->data().subspan(1, active_->size - 2));
            stopping = stopping || (f && f->message == 0x14);
        }
        if (stopping || overflow_[lane(*q)]) {
            add(metrics_.tx_rejected);
            return TransportStatus::unavailable;
        }
        if (q->message == 0x12) {
            if (heartbeat_tx_) {
                add(metrics_.tx_rejected);
                return TransportStatus::unavailable;
            }
            heartbeat_rx_.reset();
            heartbeat_tx_ = w;
        } else if (!tx_.push(w)) {
            add(metrics_.tx_rejected);
            return TransportStatus::unavailable;
        }
        // A new dispatch starts a new wait epoch. An interrupt between this
        // admission and read registration remains visible; earlier cancellation
        // cannot poison fresh Host discovery/session work after quiescence.
        observed_interrupt_[lane(*q)] = interrupts_;
    }
    wake_locked();
    return state_;
}
void FdTransport::route_locked(const Frame &f) {
    if (f.kind == Class::event) {
        if (!events_.push(f)) {
            add(metrics_.event_overflow);
            overflow_[3] = true;
        }
    } else if (f.kind == Class::response) {
        auto l = lane(f);
        if (l) {
            auto &slot = l == 2 ? stop_rx_ : heartbeat_rx_;
            if (!slot)
                slot = f;
            else if (*slot != f) {
                add(metrics_.control_overflow);
                overflow_[l] = true;
            }
        } else if (!replies_.push(f)) {
            add(metrics_.response_overflow);
            overflow_[0] = true;
        }
    } else
        add(metrics_.discarded);
    changed_.notify_all();
}
FrameRead FdTransport::wait(const Frame *q, Deadline d) {
    std::unique_lock lock(mutex_);
    auto l = q ? lane(*q) : 3;
    add(metrics_.waits[l]);
    changed_.notify_all();
    for (;;) {
        if (state_ != TransportStatus::ready)
            return {state_, {}};
        // Sticky per-consumer epochs cover cancellation immediately before read
        // registration as well as a blocked wait. STOP is never interrupted.
        if (l != 2 && observed_interrupt_[l] != interrupts_) {
            observed_interrupt_[l] = interrupts_;
            return {TransportStatus::interrupted, {}};
        }
        if (overflow_[l])
            return {TransportStatus::unavailable, {}};
        auto take = [&]() -> std::optional<Frame> {
            if (l == 3)
                return events_.pop();
            if (!l)
                return replies_.pop();
            auto &slot = l == 2 ? stop_rx_ : heartbeat_rx_;
            auto f = slot;
            slot.reset();
            return f;
        };
        while (auto f = take()) {
            if (!q || matches_response(*f, *q))
                return {TransportStatus::ready, f};
            add(metrics_.discarded);
        }
        if (Clock::now() >= d)
            return {TransportStatus::timeout, {}};
        changed_.wait_until(lock, d);
    }
}
FrameRead FdTransport::receive(const Frame &q, Deadline d) {
    if (q.kind != Class::request)
        return {TransportStatus::invalid, {}};
    return wait(&q, d);
}
FrameRead FdTransport::event(Deadline d) {
    return wait(nullptr, d);
}
void FdTransport::interrupt() noexcept {
    std::lock_guard lock(mutex_);
    // Bounded epochs never wrap: exhaustion is a transport failure, not reuse.
    if (interrupts_ == UINT64_MAX)
        fail_locked(EOVERFLOW);
    else
        ++interrupts_;
    changed_.notify_all();
    wake_locked();
}
void FdTransport::close() noexcept {
    std::lock_guard lifecycle(lifecycle_);
    {
        std::lock_guard lock(mutex_);
        state_ = TransportStatus::closed;
        clear_locked();
        changed_.notify_all();
        wake_locked();
    }
    if (worker_.joinable())
        worker_.join();
    std::lock_guard lock(mutex_);
    close_fd(fd_);
    close_fd(wake_fd_);
}
FdTransport::Metrics FdTransport::metrics() const {
    std::lock_guard lock(mutex_);
    auto m = metrics_;
    m.tx_high = tx_.high;
    m.response_high = replies_.high;
    m.event_high = events_.high;
    m.active_bytes = offset_;
    m.active_size = active_ ? active_->size : 0;
    return m;
}
void FdTransport::run() noexcept {
    unsigned interrupted_io = 0;
    for (;;) {
        short interests = POLLIN;
        int timeout = -1;
        {
            std::lock_guard lock(mutex_);
            if (state_ != TransportStatus::ready)
                return;
            if (!active_) {
                if (stop_tx_) {
                    active_ = stop_tx_;
                    stop_tx_.reset();
                } else if (heartbeat_tx_) {
                    active_ = heartbeat_tx_;
                    heartbeat_tx_.reset();
                } else
                    active_ = tx_.pop();
                offset_ = 0;
#ifdef MANTIS_F2_POSIX_TESTING
                if (active_ && pause_message_) {
                    auto q = decode(active_->data().subspan(1, active_->size - 2));
                    if (q && q->message == pause_message_) {
                        pause_message_ = 0;
                        message_paused_ = true;
                        hold_write_ = !pause_bytes_;
                        if (pause_bytes_) {
                            write_limit_ = pause_bytes_;
                            pause_after_write_ = true;
                        }
                    }
                }
#endif
                changed_.notify_all();
            }
            bool writable = active_.has_value();
#ifdef MANTIS_F2_POSIX_TESTING
            writable = writable && !hold_write_;
#endif
            if (writable)
                interests |= POLLOUT;
            if (parser_.buffered()) {
                auto elapsed = now_us() - last_byte_;
                timeout = elapsed >= Parser::idle_us
                              ? 0
                              : static_cast<int>((Parser::idle_us - elapsed + 999) / 1000);
            }
            add(metrics_.polls);
        }
        pollfd fds[2]{{fd_, interests, 0}, {wake_fd_, POLLIN, 0}};
        auto result = ::poll(fds, 2, timeout);
        if (result < 0) {
            const int error = errno; // Capture before synchronization/library calls.
            std::lock_guard lock(mutex_);
            if (error == EINTR && ++interrupted_io < 64) {
                add(metrics_.eintr);
                continue;
            }
            fail_locked(error);
            return;
        }
        if (fds[1].revents & POLLIN) {
            uint64_t count;
            ::read(wake_fd_, &count, sizeof count);
        }
        {
            std::lock_guard lock(mutex_);
            if (state_ != TransportStatus::ready)
                return;
            add(metrics_.wakes);
            if ((fds[0].revents | fds[1].revents) & (POLLHUP | POLLERR | POLLNVAL)) {
                fail_locked(EIO);
                return;
            }
            parser_.idle(now_us());
            if (parser_.rejected)
                changed_.notify_all();
            add(metrics_.malformed, parser_.rejected);
            parser_.rejected = 0;
            if ((fds[0].revents & POLLOUT) && active_) {
                size_t count = active_->size - offset_;
                ssize_t sent;
#ifdef MANTIS_F2_POSIX_TESTING
                if (hold_write_)
                    continue;
                count = std::min(count, write_limit_);
                if (write_eintr_) {
                    --write_eintr_;
                    errno = EINTR;
                    sent = -1;
                } else if (write_eagain_) {
                    --write_eagain_;
                    hold_write_ = true;
                    errno = EAGAIN;
                    sent = -1;
                } else
#endif
                    sent = ::write(fd_, active_->bytes.data() + offset_, count);
                add(metrics_.writes);
                if (sent > 0) {
                    interrupted_io = 0;
                    add(metrics_.tx_bytes, static_cast<uint64_t>(sent));
                    if (static_cast<size_t>(sent) < active_->size - offset_)
                        add(metrics_.short_writes);
                    offset_ += static_cast<size_t>(sent);
#ifdef MANTIS_F2_POSIX_TESTING
                    if (pause_after_write_) {
                        hold_write_ = true;
                        pause_after_write_ = false;
                    }
#endif
                    if (offset_ == active_->size) {
                        active_.reset();
                        offset_ = 0;
                    }
                } else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    add(metrics_.write_eagain);
                else if (sent < 0 && errno == EINTR && ++interrupted_io < 64)
                    add(metrics_.eintr);
                else {
                    fail_locked(sent == 0 ? EIO : errno);
                    return;
                }
                changed_.notify_all();
            }
        }
        if (fds[0].revents & POLLIN) {
            std::array<uint8_t, 1024> bytes;
            size_t limit = bytes.size();
            bool simulate_eintr = false;
#ifdef MANTIS_F2_POSIX_TESTING
            {
                std::lock_guard lock(mutex_);
                limit = std::min(limit, read_limit_);
                if (read_eintr_) {
                    --read_eintr_;
                    simulate_eintr = true;
                }
            }
#endif
            ssize_t got;
            if (simulate_eintr) {
                errno = EINTR;
                got = -1;
            } else
                got = ::read(fd_, bytes.data(), limit);
            const int error = got < 0 ? errno : 0;
            std::lock_guard lock(mutex_);
            if (state_ != TransportStatus::ready)
                return;
            add(metrics_.reads);
            if (got > 0) {
                interrupted_io = 0;
                add(metrics_.rx_bytes, static_cast<uint64_t>(got));
                last_byte_ = now_us();
                for (ssize_t i = 0; i < got; ++i) {
                    auto f = parser_.feed(bytes[static_cast<size_t>(i)], last_byte_);
                    add(metrics_.malformed, parser_.rejected);
                    parser_.rejected = parser_.accepted = 0; // Drain into saturating diagnostics.
                    if (f)
                        route_locked(*f);
                }
                metrics_.parser_high = parser_.high_water;
            } else if (got < 0 && (error == EAGAIN || error == EWOULDBLOCK))
                add(metrics_.read_eagain);
            else if (got < 0 && error == EINTR && ++interrupted_io < 64)
                add(metrics_.eintr);
            else {
                fail_locked(got == 0 ? ENODEV : error);
                return;
            }
            changed_.notify_all();
        }
    }
}
} // namespace x1::f2::posix
