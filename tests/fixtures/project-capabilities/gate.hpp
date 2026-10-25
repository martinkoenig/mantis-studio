#pragma once
// Only test executables use this inherited, socketpair-owned synchronization.
#include <cerrno>
#include <chrono>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
namespace qualification {
inline void send(int fd, char value) {
    if (::send(fd, &value, 1, MSG_NOSIGNAL) != 1)
        throw std::runtime_error("Replay gate send failed");
}
inline char receive(int fd) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                .count();
        if (left <= 0)
            throw std::runtime_error("Replay gate deadline");
        pollfd p{fd, POLLIN, 0};
        const auto ready = ::poll(&p, 1, static_cast<int>(left));
        if (ready < 0 && errno == EINTR)
            continue;
        char value{};
        if (ready <= 0 || !(p.revents & POLLIN) || ::recv(fd, &value, 1, 0) != 1)
            throw std::runtime_error("Replay gate closed or timed out");
        return value;
    }
}
} // namespace qualification
