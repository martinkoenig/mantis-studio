#pragma once
#include "../../../plugins/first-party/devices/x1/f2-posix/fd-transport.hpp"
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <pthread.h>
#include <stdexcept>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
namespace x1::f2::posix {
struct FdTransportConformanceAccess {
    static clockid_t cpu_clock(FdTransport &t) {
        clockid_t clock;
        CHECK(::pthread_getcpuclockid(t.worker_.native_handle(), &clock) == 0);
        return clock;
    }
    template <class P> static bool await(FdTransport &t, P p, Deadline d) {
        std::unique_lock lock(t.mutex_);
        return t.changed_.wait_until(lock, d, [&] {
            auto m = t.metrics_;
            m.active_bytes = t.offset_;
            m.active_size = t.active_ ? t.active_->size : 0;
            m.tx_high = t.tx_.high;
            m.response_high = t.replies_.high;
            m.event_high = t.events_.high;
            return p(m);
        });
    }
    static void gate(FdTransport &t, bool hold, size_t write_limit = max_wire, bool pause_after = false) {
        std::lock_guard lock(t.mutex_);
        t.hold_write_ = hold;
        t.write_limit_ = write_limit;
        t.pause_after_write_ = pause_after;
        t.wake_locked();
    }
    static void faults(FdTransport &t, unsigned weintr, unsigned reintr, unsigned eagain,
                       size_t read_limit = 1024) {
        std::lock_guard lock(t.mutex_);
        t.write_eintr_ = weintr;
        t.read_eintr_ = reintr;
        t.write_eagain_ = eagain;
        t.read_limit_ = read_limit;
        t.wake_locked();
    }
    static void diagnostic_limit(FdTransport &t) {
        std::lock_guard lock(t.mutex_);
        t.metrics_.discarded = UINT64_MAX;
        t.metrics_.tx_rejected = UINT64_MAX;
    }
};
} // namespace x1::f2::posix
namespace ptytest {
using namespace x1::f2;
using x1::f2::posix::FdTransport;
using Access = x1::f2::posix::FdTransportConformanceAccess;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
inline Deadline deadline() {
    return Clock::now() + 2s;
}
struct Pty {
    int master = -1, slave = -1;
    Pty() {
        master = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        try {
            CHECK(master >= 0 && ::grantpt(master) == 0 && ::unlockpt(master) == 0);
            std::array<char, 128> name{};
            CHECK(::ptsname_r(master, name.data(), name.size()) == 0);
            slave = ::open(name.data(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
            CHECK(slave >= 0);
            termios mode{};
            CHECK(::tcgetattr(slave, &mode) == 0);
            ::cfmakeraw(&mode);
            mode.c_cc[VMIN] = 1;
            mode.c_cc[VTIME] = 0;
            CHECK(::tcsetattr(slave, TCSANOW, &mode) == 0);
        } catch (...) {
            close();
            throw;
        }
    }
    Pty(const Pty &) = delete;
    ~Pty() {
        close();
    }
    void close() {
        if (master >= 0) {
            ::close(master);
            master = -1;
        }
        if (slave >= 0) {
            ::close(slave);
            slave = -1;
        }
    }
    int take_slave() {
        auto f = slave;
        slave = -1;
        return f;
    }
    int take_master() {
        auto f = master;
        master = -1;
        return f;
    }
};
inline void write_bytes(int fd, std::span<const uint8_t> bytes, size_t fragment = max_wire) {
    auto d = deadline();
    while (!bytes.empty()) {
        auto sent = ::write(fd, bytes.data(), std::min(fragment, bytes.size()));
        if (sent > 0) {
            bytes = bytes.subspan(static_cast<size_t>(sent));
            continue;
        }
        CHECK(sent < 0 && (errno == EINTR || errno == EAGAIN));
        CHECK(Clock::now() < d);
        pollfd p{fd, POLLOUT, 0};
        auto ready = ::poll(&p, 1, 10);
        CHECK(ready >= 0 || errno == EINTR);
        CHECK(!(p.revents & (POLLHUP | POLLERR | POLLNVAL)));
    }
}
inline Wire read_wire(int fd) {
    Wire wire;
    auto d = deadline();
    bool started = false;
    while (Clock::now() < d) {
        uint8_t byte;
        auto got = ::read(fd, &byte, 1);
        if (got == 1) {
            if (!started) {
                CHECK(!byte);
                started = true;
            }
            CHECK(wire.size < max_wire);
            wire.bytes[wire.size++] = byte;
            if (!byte && wire.size > 1)
                return wire;
        } else {
            CHECK(got < 0 && (errno == EAGAIN || errno == EINTR));
            pollfd p{fd, POLLIN, 0};
            auto ready = ::poll(&p, 1, 10);
            CHECK(ready >= 0 || errno == EINTR);
        }
    }
    throw std::runtime_error("PTY frame read deadline");
}
inline Frame reply(const Frame &q, size_t length = 4) {
    auto r = q;
    r.kind = Class::response;
    r.payload = {};
    r.payload.size = length;
    put(r.payload, 0, 2, 2);
    r.payload.bytes[2] = 2;
    r.payload.bytes[3] = 1;
    return r;
}
inline Frame unknown(uint16_t op = 0x1234, size_t size = 0) {
    Frame f;
    f.message = op;
    f.payload.size = size;
    return f;
}
} // namespace ptytest
