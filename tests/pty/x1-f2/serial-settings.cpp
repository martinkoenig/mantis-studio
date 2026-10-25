#include "../../../plugins/first-party/devices/x1/f2-serial/serial-settings.hpp"
#include "peer.hpp"
#include <filesystem>
#include <future>

// Link-time, test-only syscall faults. Real PTYs and real termios remain in use;
// no hooks or global injection state enter the private configuration library.
namespace {
thread_local unsigned set_calls{}, get_calls{}, fail_set{}, fail_get{};
thread_local bool applied_failure{};
} // namespace
extern "C" int __real_tcsetattr(int, int, const termios *);
extern "C" int __real_tcgetattr(int, termios *);
extern "C" int __wrap_tcsetattr(int fd, int action, const termios *settings) {
    if (++set_calls == fail_set) {
        if (applied_failure)
            CHECK(__real_tcsetattr(fd, action, settings) == 0);
        errno = EIO;
        return -1;
    }
    return __real_tcsetattr(fd, action, settings);
}
extern "C" int __wrap_tcgetattr(int fd, termios *settings) {
    if (++get_calls == fail_get) {
        errno = ENODEV;
        return -1;
    }
    return __real_tcgetattr(fd, settings);
}
using namespace ptytest;
using namespace x1::f2::posix;
namespace {
void check_mode(int fd) {
    termios m{};
    CHECK(::tcgetattr(fd, &m) == 0);
    CHECK(::cfgetispeed(&m) == B115200 && ::cfgetospeed(&m) == B115200);
    CHECK((m.c_cflag & CSIZE) == CS8 && !(m.c_cflag & (PARENB | PARODD | CSTOPB | CRTSCTS)));
    CHECK((m.c_cflag & (CLOCAL | CREAD)) == (CLOCAL | CREAD));
    CHECK(!(m.c_iflag & (IXON | IXOFF | IXANY | ICRNL | INLCR | IGNCR | ISTRIP | PARMRK | INPCK)));
    CHECK(!(m.c_lflag & (ICANON | ECHO | ECHONL | ISIG | IEXTEN)) && !(m.c_oflag & OPOST));
    CHECK(m.c_cc[VMIN] == 1 && m.c_cc[VTIME] == 0);
}
void settings_and_bytes() {
    Pty p;
    termios original{};
    CHECK(::tcgetattr(p.slave, &original) == 0);
    auto dirty = original;
    dirty.c_iflag |= IXON | IXOFF | ICRNL | ISTRIP;
    dirty.c_oflag |= OPOST | ONLCR;
    dirty.c_lflag |= ECHO | ICANON | ISIG;
    dirty.c_cc[VMIN] = 7;
    dirty.c_cc[VTIME] = 3;
    CHECK(::cfsetispeed(&dirty, B9600) == 0 && ::cfsetospeed(&dirty, B9600) == 0);
    CHECK(::tcsetattr(p.slave, TCSANOW, &dirty) == 0);
    const auto flags = ::fcntl(p.slave, F_GETFL);
    CHECK(configure_serial(p.slave));
    check_mode(p.slave);
    CHECK(::fcntl(p.slave, F_GETFL) == flags); // Borrowed FD, no transport/flag ownership.
    std::array<uint8_t, 256> all{};
    for (size_t i = 0; i < all.size(); ++i)
        all[i] = static_cast<uint8_t>(i);
    for (bool inbound : {true, false}) {
        const int from = inbound ? p.master : p.slave, to = inbound ? p.slave : p.master;
        write_bytes(from, all, 1);
        std::array<uint8_t, 256> actual{};
        size_t count = 0;
        auto end = deadline();
        while (count < actual.size()) {
            CHECK(Clock::now() < end);
            pollfd ready{to, POLLIN, 0};
            CHECK(::poll(&ready, 1, 100) >= 0);
            auto n = ::read(to, actual.data() + count, actual.size() - count);
            if (n < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            CHECK(n > 0);
            count += static_cast<size_t>(n);
        }
        CHECK(actual == all);
        pollfd echo{from, POLLIN, 0};
        CHECK(::poll(&echo, 1, 0) == 0);
    }
    // Unsupported profiles leave the successfully established settings intact.
    for (unsigned variation = 0; variation < 6; ++variation) {
        SerialProfile bad;
        if (variation == 0)
            bad.baud = 460800;
        if (variation == 1)
            bad.data_bits = 7;
        if (variation == 2)
            bad.stop_bits = 2;
        if (variation == 3)
            bad.parity = true;
        if (variation == 4)
            bad.software_flow = true;
        if (variation == 5)
            bad.hardware_flow = true;
        const auto r = configure_serial(p.slave, bad);
        CHECK(!r && r.status == SerialStatus::unsupported && r.error == EINVAL);
        CHECK(!r.rollback_attempted && !r.rollback_error);
        check_mode(p.slave);
    }
    CHECK(configure_serial(-1).error == EBADF);
    int descriptors[2];
    CHECK(::pipe2(descriptors, O_NONBLOCK | O_CLOEXEC) == 0);
    const auto r = configure_serial(descriptors[0]);
    CHECK(!r && r.status == SerialStatus::inspect_failed && r.error == ENOTTY);
    CHECK(::fcntl(descriptors[0], F_GETFD) >= 0);
    ::close(descriptors[0]);
    ::close(descriptors[1]);
}
void rollback_paths() {
    Pty p;
    termios original{};
    CHECK(::tcgetattr(p.slave, &original) == 0);
    CHECK(::cfsetispeed(&original, B9600) == 0 && ::cfsetospeed(&original, B9600) == 0);
    CHECK(::tcsetattr(p.slave, TCSANOW, &original) == 0);
    // A driver may apply some settings and still fail. Restore, don't continue.
    set_calls = get_calls = 0;
    fail_set = 1;
    applied_failure = true;
    auto result = configure_serial(p.slave);
    fail_set = 0;
    applied_failure = false;
    CHECK(!result && result.status == SerialStatus::apply_failed && result.error == EIO &&
          result.rollback_attempted && !result.rollback_error);
    termios actual{};
    CHECK(::tcgetattr(p.slave, &actual) == 0 && ::cfgetospeed(&actual) == B9600);
    // Failed readback must roll back. Cleanup failure stays separate from cause.
    for (bool fail_restore : {false, true}) {
        set_calls = get_calls = 0;
        fail_get = 2;
        fail_set = fail_restore ? 2 : 0;
        result = configure_serial(p.slave);
        fail_get = fail_set = 0;
        CHECK(!result && result.status == SerialStatus::verify_failed && result.error == ENODEV &&
              result.rollback_attempted && result.rollback_error == (fail_restore ? EIO : 0));
        CHECK(::tcgetattr(p.slave, &actual) == 0);
        CHECK(::cfgetospeed(&actual) == (fail_restore ? B115200 : B9600));
    }
}
void host_and_lifetime() {
    using namespace x1::f2::simulation;
    const auto before = std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
                                      std::filesystem::directory_iterator{});
    for (unsigned i = 0; i < 12; ++i) {
        Pty p;
        CHECK(configure_serial(p.slave));
        const int supplied = p.slave;
        FdTransport transport(p.take_slave());
        check_mode(supplied); // O_NONBLOCK setup retains exact line settings.
        Access::gate(transport, false, 2);
        Access::faults(transport, 1, 1, 0, 3);
        Peer peer(p.take_master());
        Host host(transport, host_selection());
        const auto snapshot = host.discover(deadline());
        const auto run = host.start(snapshot, BenchConfig{}.execution(), true, deadline(), host.fence());
        CHECK(peer.wait(0x13, 1) && peer.metrics().runs == 1);
        peer.advance(200'000);
        const auto terminal = host.terminal(deadline());
        CHECK(terminal && get(*terminal, 16, 8) == run.execution && terminal->bytes[28] == 1);
        CHECK(host.stop(deadline()) && host.retire_heartbeat(deadline()));
        check_mode(supplied);
        peer.check();
        transport.close();
        transport.close();
        CHECK(::fcntl(supplied, F_GETFD) < 0 && errno == EBADF);
    }
    CHECK(std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
                        std::filesystem::directory_iterator{}) == before);
}
void configured_failure_paths() {
    Pty p;
    CHECK(configure_serial(p.slave));
    FdTransport transport(p.take_slave());
    auto q = unknown();
    CHECK(transport.receive(q, Clock::now()).status == TransportStatus::timeout);
    auto pending = std::async(std::launch::async, [&] { return transport.receive(q, deadline()); });
    CHECK(Access::await(transport, [](auto m) { return m.waits[0] == 2; }, deadline()));
    transport.interrupt();
    CHECK(pending.get().status == TransportStatus::interrupted);
    auto stop = request(Message::stop);
    CHECK(transport.submit(*encode(stop)) == TransportStatus::ready);
    CHECK(read_wire(p.master) == *encode(stop));
    write_bytes(p.master, encode(reply(stop, 5))->data(), 1);
    CHECK(transport.receive(stop, deadline())); // Interrupt never hides independent STOP ACK.
    CHECK(transport.event(Clock::now()).status == TransportStatus::interrupted);
    pending = std::async(std::launch::async, [&] { return transport.event(deadline()); });
    CHECK(Access::await(transport, [](auto m) { return m.waits[3] == 2; }, deadline()));
    ::close(p.master);
    p.master = -1;
    CHECK(pending.get().status == TransportStatus::unavailable);
    transport.close();
}
} // namespace
int main() {
    try {
        settings_and_bytes();
        rollback_paths();
        host_and_lifetime();
        configured_failure_paths();
        std::cout << "serial profile/readback, 512 exact bytes, invalid settings, 12 Host lifetimes, "
                     "partial I/O, timeout/cancel/STOP/HUP PASS; no physical device access\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
