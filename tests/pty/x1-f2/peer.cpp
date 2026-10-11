#include "peer.hpp"
namespace ptytest {
Peer::Peer(int fd) : fd_(fd), wake_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {
    try {
        CHECK(wake_ >= 0);
        thread_ = std::jthread([this](std::stop_token s) { run(s); });
    } catch (...) {
        ::close(fd_);
        if (wake_ >= 0)
            ::close(wake_);
        throw;
    }
}
Peer::~Peer() {
    thread_.request_stop();
    uint64_t one = 1;
    ::write(wake_, &one, sizeof one);
    thread_.join();
    ::close(fd_);
    ::close(wake_);
}
void Peer::advance(uint64_t us) {
    now_ = us;
    uint64_t one = 1;
    CHECK(::write(wake_, &one, sizeof one) == sizeof one);
}
void Peer::send(Frame f) {
    {
        std::lock_guard lock(mutex_);
        if (f.kind == Class::response) {
            if (mutation_ == 1)
                f.payload.size = 4;
            if (mutation_ == 2)
                f.payload.bytes[2] = 3;
            if (mutation_ == 3)
                f.payload.bytes[3] = 7;
            if (mutation_ == 4 && f.message == static_cast<uint16_t>(Message::claim))
                put(f.payload, 4, 8, get(f.payload, 4, 8) + 1);
        }
    }
    auto w = encode(f);
    CHECK(w);
    write_bytes(fd_, w->data(), 3);
}
void Peer::run(std::stop_token stop) {
    try {
        Wire collected;
        while (!stop.stop_requested()) {
            pollfd fds[2]{{fd_, POLLIN, 0}, {wake_, POLLIN, 0}};
            auto ready = ::poll(fds, 2, -1);
            if (ready < 0 && errno == EINTR)
                continue;
            CHECK(ready >= 0);
            if (stop.stop_requested())
                break;
            CHECK(!(fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)));
            if (fds[1].revents & POLLIN) {
                uint64_t v;
                ::read(wake_, &v, sizeof v);
            }
            link_.advance(now_.load());
            if (fds[0].revents & POLLIN) {
                std::array<uint8_t, 1024> bytes{};
                auto n = ::read(fd_, bytes.data(), bytes.size());
                if (n < 0 && (errno == EINTR || errno == EAGAIN))
                    continue;
                CHECK(n > 0);
                for (ssize_t i = 0; i < n; ++i) {
                    auto b = bytes[static_cast<size_t>(i)];
                    if (collected.size == 0 && b)
                        continue;
                    CHECK(collected.size < max_wire);
                    collected.bytes[collected.size++] = b;
                    auto f = parser_.feed(b, now_.load());
                    if (!b && collected.size > 1) {
                        if (f) {
                            {
                                std::lock_guard lock(mutex_);
                                CHECK(count_ < requests_.size());
                                requests_[count_++] = collected;
                                changed_.notify_all();
                            }
                            CHECK(link_.submit(collected) == TransportStatus::ready);
                            auto response = link_.receive(*f, Clock::now());
                            if (response)
                                send(*response);
                        }
                        collected = {};
                    } else if (!b && collected.size == 1) {
                        // Adjacent leading delimiters are legal; retain one.
                    }
                }
            }
            while (auto e = link_.event(Clock::now()))
                send(*e);
        }
    } catch (...) {
        std::lock_guard lock(mutex_);
        error_ = std::current_exception();
        changed_.notify_all();
    }
}
void Peer::check() {
    std::lock_guard lock(mutex_);
    if (error_)
        std::rethrow_exception(error_);
}
size_t Peer::request_count(uint16_t opcode) {
    std::lock_guard lock(mutex_);
    size_t count = 0;
    for (size_t i = 0; i < count_; ++i) {
        auto f = decode(requests_[i].data().subspan(1, requests_[i].size - 2));
        if (f && f->message == opcode)
            ++count;
    }
    return count;
}
bool Peer::wait(uint16_t op, size_t count) {
    std::unique_lock lock(mutex_);
    return changed_.wait_until(lock, deadline(), [&] {
        if (error_)
            std::rethrow_exception(error_);
        size_t matches = 0;
        for (size_t i = 0; i < count_; ++i) {
            auto f = decode(requests_[i].data().subspan(1, requests_[i].size - 2));
            if (f && f->message == op)
                ++matches;
        }
        return matches >= count;
    });
}
Wire Peer::request_at(size_t i) {
    std::lock_guard lock(mutex_);
    CHECK(i < count_);
    return requests_[i];
}
} // namespace ptytest
