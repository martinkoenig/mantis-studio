#include "peer.hpp"
#include <filesystem>
#include <future>

namespace x1::f2 {
struct HostConformanceAccess {
    static Frame transaction(Host &h, const Frame &q) {
        std::unique_lock lock(h.ordinary_);
        return h.exchange(q, std::chrono::steady_clock::now() + std::chrono::seconds(1));
    }
};
} // namespace x1::f2
using namespace ptytest;
namespace {
template <class F> void error(F f, Result result) {
    try {
        f();
    } catch (const ProtocolError &e) {
        CHECK(e.result == result);
        return;
    }
    throw std::runtime_error("Expected Host error absent");
}
size_t fd_count() {
    return static_cast<size_t>(std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
                                             std::filesystem::directory_iterator{}));
}
void framing() {
    Pty p;
    int transferred = p.slave;
    FdTransport t(p.take_slave());
    CHECK(::fcntl(transferred, F_GETFL) & O_NONBLOCK);
    CHECK(::fcntl(transferred, F_GETFD) & FD_CLOEXEC);
    Access::faults(t, 1, 1, 0, 1);
    Access::gate(t, false, 3);
    for (auto op : {uint16_t(0x1234), uint16_t(0x8001), uint16_t(0x8002), uint16_t(0x8003)}) {
        auto q = unknown(op, 132);
        auto w = *encode(q);
        CHECK(w.size == 163 && t.submit(w) == TransportStatus::ready);
        CHECK(read_wire(p.master) == w);
        auto r = reply(q, 132);
        r.payload.bytes[131] = 0x42;
        auto response = *encode(r);
        write_bytes(p.master, response.data(), 1);
        CHECK(t.receive(q, deadline()).frame == r);
    }
    CHECK(t.metrics().short_writes && t.metrics().eintr >= 2);
    // Multiple adjacent frames, irregular chunks, and response/event separation.
    Access::faults(t, 0, 0, 0, 1024);
    auto q = unknown();
    auto r = reply(q);
    auto event = r;
    event.kind = Class::event;
    event.message = 0x8002;
    std::array<uint8_t, 2 * max_wire> joined{};
    auto rw = *encode(r), ew = *encode(event);
    std::copy(rw.data().begin(), rw.data().end(), joined.begin());
    std::copy(ew.data().begin(), ew.data().end(), joined.begin() + static_cast<ptrdiff_t>(rw.size));
    write_bytes(p.master, {joined.data(), rw.size + ew.size}, 7);
    CHECK(t.receive(q, deadline()).frame == r && t.event(deadline()).frame == event);
    CHECK(t.submit(Wire{}) == TransportStatus::invalid);
    auto wrong = rw;
    wrong.bytes[wrong.size - 3] ^= 1;
    CHECK(t.submit(wrong) == TransportStatus::invalid);
    CHECK(t.submit(rw) == TransportStatus::invalid); // Wrong outbound direction.
    t.close();
    t.close();
    CHECK(::fcntl(transferred, F_GETFD) < 0 && errno == EBADF);
    CHECK(t.submit(*encode(q)) == TransportStatus::closed);
    CHECK(t.receive(q, deadline()).status == TransportStatus::closed);
}
void recovery_and_correlation() {
    Pty p;
    FdTransport t(p.take_slave());
    auto q = unknown(), r = reply(q);
    auto w = *encode(r);
    const std::array<uint8_t, 12> noise{'b', 'o', 'o', 't', '\r', '\n', 0, 0, 255, 2, 0, 0};
    write_bytes(p.master, noise);
    auto bad = w;
    bad.bytes[bad.size - 3] ^= 0x80;
    write_bytes(p.master, bad.data());
    std::array<uint8_t, 300> overflow{};
    overflow.fill(1);
    write_bytes(p.master, overflow);
    const std::array<uint8_t, 2> delimiters{};
    write_bytes(p.master, delimiters);
    write_bytes(p.master, w.data());
    CHECK(t.receive(q, deadline()).frame == r && t.metrics().malformed >= 3);
    auto received = t.metrics().rx_bytes;
    write_bytes(p.master, w.data().first(6));
    CHECK(Access::await(t, [&](auto m) { return m.rx_bytes >= received + 6; }, deadline()));
    auto rejected = t.metrics().malformed;
    CHECK(Access::await(t, [&](auto m) { return m.malformed > rejected; }, deadline())); // Real idle timer.
    // A suffix after idle is discarded, never reinterpreted as a command.
    received = t.metrics().rx_bytes;
    write_bytes(p.master, w.data().subspan(6));
    CHECK(Access::await(t, [&](auto m) { return m.rx_bytes > received; }, deadline()));
    CHECK(t.receive(q, Clock::now()).status == TransportStatus::timeout);
    write_bytes(p.master, w.data());
    CHECK(t.receive(q, deadline()).frame == r);
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
        auto unrelated = r;
        if (!mutation)
            ++unrelated.message;
        if (mutation == 1)
            ++unrelated.session;
        if (mutation == 2)
            ++unrelated.request;
        if (mutation == 3)
            unrelated.kind = Class::request;
        received = t.metrics().rx_bytes;
        write_bytes(p.master, encode(unrelated)->data());
        CHECK(Access::await(t, [&](auto m) { return m.rx_bytes > received; }, deadline()));
        CHECK(t.receive(q, Clock::now()).status == TransportStatus::timeout);
    }
    write_bytes(p.master, w.data());
    CHECK(t.receive(q, deadline()).frame == r);
    auto start = Clock::now();
    CHECK(t.receive(q, start - 1s).status == TransportStatus::timeout);
    CHECK(t.receive(q, Clock::now() + 10ms).status == TransportStatus::timeout);
    CHECK(Clock::now() - start < 500ms);
}
void priority(bool partial) {
    Pty p;
    FdTransport t(p.take_slave());
    auto q = request(Message::run, 99, 4);
    put(q.payload, 0, 4, 1);
    put(q.payload, 4, 8, 7);
    put(q.payload, 12, 4, 10);
    auto wire = *encode(q);
    Access::gate(t, !partial, 3, partial);
    CHECK(t.submit(wire) == TransportStatus::ready);
    CHECK(Access::await(
        t, [&](auto m) { return partial ? m.active_bytes == 3 : m.active_size != 0; }, deadline()));
    for (size_t i = 0; i < FdTransport::tx_capacity; ++i)
        CHECK(t.submit(wire) == TransportStatus::ready);
    CHECK(t.submit(wire) == TransportStatus::unavailable);
    auto hb = request(Message::keepalive, 99);
    put(hb.payload, 0, 4, 1);
    put(hb.payload, 4, 4, 1);
    CHECK(t.submit(*encode(hb)) == TransportStatus::ready);
    auto stop = request(Message::stop);
    CHECK(t.submit(*encode(stop)) == TransportStatus::ready);
    CHECK(t.submit(wire) == TransportStatus::unavailable);
    CHECK(t.submit(*encode(hb)) == TransportStatus::unavailable);
    CHECK(t.metrics().cancelled >= 9 && t.metrics().tx_high == 8);
    Access::gate(t, false, 3);
    if (partial)
        CHECK(read_wire(p.master) == wire);
    CHECK(read_wire(p.master) == *encode(stop));
    CHECK(Access::await(t, [](auto m) { return !m.active_size; }, deadline()));
    pollfd f{p.master, POLLIN, 0};
    CHECK(::poll(&f, 1, 0) == 0); // No canceled RUN/HB bytes after STOP.
    CHECK(t.receive(stop, Clock::now()).status == TransportStatus::timeout); // Transmission is not ACK.
    auto ack = reply(stop, 5);
    ack.payload.bytes[0] = 0;
    ack.payload.bytes[4] = 2;
    write_bytes(p.master, encode(ack)->data());
    CHECK(t.receive(stop, deadline()).frame == ack);
}
void overflow_and_heartbeat() {
    Pty p;
    FdTransport t(p.take_slave());
    auto q = unknown(), r = reply(q);
    std::array<uint8_t, 10 * max_wire> input{};
    auto rw = *encode(r);
    for (size_t i = 0; i < 10; ++i)
        std::copy(rw.data().begin(), rw.data().end(), input.begin() + static_cast<ptrdiff_t>(i * rw.size));
    write_bytes(p.master, {input.data(), 10 * rw.size});
    CHECK(Access::await(t, [](auto m) { return m.response_overflow >= 2; }, deadline()));
    CHECK(t.receive(q, deadline()).status == TransportStatus::unavailable);
    CHECK(t.submit(*encode(q)) == TransportStatus::unavailable);
    auto e = r;
    e.kind = Class::event;
    auto ew = *encode(e);
    for (size_t i = 0; i < 10; ++i)
        std::copy(ew.data().begin(), ew.data().end(), input.begin() + static_cast<ptrdiff_t>(i * ew.size));
    write_bytes(p.master, {input.data(), 10 * ew.size});
    CHECK(Access::await(t, [](auto m) { return m.event_overflow >= 2; }, deadline()));
    CHECK(t.event(deadline()).status == TransportStatus::unavailable);
    // Reserved heartbeat and STOP paths survive ordinary/event saturation.
    auto hb = request(Message::keepalive, 9);
    put(hb.payload, 0, 4, 1);
    put(hb.payload, 4, 4, 1);
    CHECK(t.submit(*encode(hb)) == TransportStatus::ready && read_wire(p.master) == *encode(hb));
    auto hbr = reply(hb, 10);
    write_bytes(p.master, encode(hbr)->data());
    CHECK(t.receive(hb, deadline()).frame == hbr);
    auto stop = request(Message::stop);
    CHECK(t.submit(*encode(stop)) == TransportStatus::ready && read_wire(p.master) == *encode(stop));
    auto sr = reply(stop, 5);
    write_bytes(p.master, encode(sr)->data());
    t.interrupt();
    CHECK(t.receive(stop, deadline()).frame == sr);
    CHECK(t.metrics().response_high == 8 && t.metrics().event_high == 8);
    Access::diagnostic_limit(t);
    CHECK(t.submit(*encode(q)) == TransportStatus::unavailable && t.metrics().tx_rejected == UINT64_MAX);
}
void heartbeat_scheduling() {
    Pty p;
    FdTransport t(p.take_slave());
    auto q = unknown();
    auto wire = *encode(q);
    Access::gate(t, false, 1, true);
    CHECK(t.submit(wire) == TransportStatus::ready);
    CHECK(Access::await(t, [](auto m) { return m.active_bytes == 1; }, deadline()));
    for (size_t i = 0; i < FdTransport::tx_capacity; ++i)
        CHECK(t.submit(wire) == TransportStatus::ready);
    CHECK(t.submit(wire) == TransportStatus::unavailable);
    auto hb = request(Message::keepalive, 9);
    put(hb.payload, 0, 4, 1);
    put(hb.payload, 4, 4, 1);
    CHECK(t.submit(*encode(hb)) == TransportStatus::ready);
    auto pending = std::async(std::launch::async, [&] { return t.receive(q, deadline()); });
    CHECK(Access::await(t, [](auto m) { return m.waits[0] == 1; }, deadline()));
    Access::gate(t, false);
    CHECK(read_wire(p.master) == wire);
    CHECK(read_wire(p.master) == *encode(hb)); // Before the saturated ordinary backlog.
    auto r = reply(hb, 10);
    write_bytes(p.master, encode(r)->data());
    CHECK(t.receive(hb, deadline()).frame == r);
    t.interrupt();
    CHECK(pending.get().status == TransportStatus::interrupted);
    for (size_t i = 0; i < FdTransport::tx_capacity; ++i)
        CHECK(read_wire(p.master) == wire);
}
void kernel_backpressure() {
    Pty p;
    int owned = p.slave; // Borrow only for a zero-budget readiness observation, never I/O/close.
    FdTransport t(p.take_slave());
    auto w = *encode(unknown(0x1234, 132));
    // The peer does not read: exercise real nonblocking kernel EAGAIN, supplementing
    // the deterministic short-write seam. Bound both offered frames and total time.
    size_t submitted = 0;
    bool blocked = false;
    auto d = deadline();
    for (unsigned i = 0; i < 512 && !t.metrics().write_eagain; ++i) {
        auto before = t.metrics().writes;
        auto status = t.submit(w);
        CHECK(status == TransportStatus::ready || status == TransportStatus::unavailable);
        if (status == TransportStatus::ready)
            ++submitted;
        pollfd observed{owned, POLLOUT, 0};
        CHECK(::poll(&observed, 1, 0) >= 0);
        CHECK(!(observed.revents & (POLLHUP | POLLERR | POLLNVAL)));
        if (!(observed.revents & POLLOUT) && submitted > 8) {
            blocked = true;
            break;
        }
        if (!Access::await(t, [&](auto m) { return m.write_eagain || m.writes > before; }, d)) {
            CHECK(::poll(&observed, 1, 0) == 0); // Linux may suppress POLLOUT before write would EAGAIN.
            blocked = true;
            break;
        }
    }
    CHECK((blocked || t.metrics().write_eagain) && submitted > 8);
    auto stop = request(Message::stop);
    CHECK(t.submit(*encode(stop)) == TransportStatus::ready);
    CHECK(t.receive(stop, Clock::now()).status == TransportStatus::timeout);
    // Disconnect while the kernel blocks TX, with independent STOP still pending.
    ::close(p.master);
    p.master = -1;
    CHECK(t.receive(stop, deadline()).status == TransportStatus::unavailable);
}
void interrupts_and_disconnect() {
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        Pty p;
        FdTransport t(p.take_slave());
        auto q = scenario == 2   ? request(Message::keepalive, 1)
                 : scenario == 3 ? request(Message::stop)
                                 : unknown();
        bool event = scenario >= 4;
        if (scenario == 0) {
            t.interrupt();
            CHECK(t.receive(q, deadline()).status == TransportStatus::interrupted);
        }
        if (scenario == 2) {
            put(q.payload, 0, 4, 1);
            put(q.payload, 4, 4, 1);
        }
        if (scenario == 2 || scenario == 3)
            CHECK(t.submit(*encode(q)) == TransportStatus::ready);
        auto before = t.metrics().waits[event ? 3 : scenario == 3 ? 2 : scenario == 2 ? 1 : 0];
        auto pending = std::async(std::launch::async,
                                  [&] { return event ? t.event(deadline()) : t.receive(q, deadline()); });
        auto lane = event ? 3 : scenario == 3 ? 2 : scenario == 2 ? 1 : 0;
        CHECK(Access::await(
            t, [&](auto m) { return m.waits[static_cast<size_t>(lane)] > before; }, deadline()));
        if (scenario == 1) {
            t.interrupt();
            CHECK(pending.get().status == TransportStatus::interrupted);
        } else if (scenario == 4) {
            t.close();
            t.close();
            CHECK(pending.get().status == TransportStatus::closed);
        } else {
            ::close(p.master);
            p.master = -1;
            CHECK(pending.get().status == TransportStatus::unavailable);
        }
    }
    // RESPONSE/EVENT already arrived, then fence before callback/read admission.
    Pty p;
    FdTransport t(p.take_slave());
    auto q = unknown(), r = reply(q);
    write_bytes(p.master, encode(r)->data());
    CHECK(Access::await(t, [](auto m) { return m.response_high == 1; }, deadline()));
    t.interrupt();
    CHECK(t.receive(q, deadline()).status == TransportStatus::interrupted);
    auto e = r;
    e.kind = Class::event;
    write_bytes(p.master, encode(e)->data());
    CHECK(Access::await(t, [](auto m) { return m.event_high == 1; }, deadline()));
    CHECK(t.event(deadline()).status == TransportStatus::interrupted);
    // Interrupted epochs do not contaminate new submitted work.
    q.request = 1;
    CHECK(t.submit(*encode(q)) == TransportStatus::ready && read_wire(p.master) == *encode(q));
    auto fresh = reply(q);
    write_bytes(p.master, encode(fresh)->data());
    CHECK(t.receive(q, deadline()).frame == fresh);
}
void syscall_failures_and_close() {
    Pty p;
    FdTransport t(p.take_slave());
    auto q = unknown();
    Access::faults(t, 1, 1, 1);
    CHECK(t.submit(*encode(q)) == TransportStatus::ready);
    CHECK(Access::await(t, [](auto m) { return m.write_eagain == 1; }, deadline()));
    auto before = t.metrics();
    auto stop = request(Message::stop);
    CHECK(t.submit(*encode(stop)) == TransportStatus::ready);
    Access::gate(t, false, 1);
    CHECK(read_wire(p.master) == *encode(stop)); // Unstarted frame was canceled after EAGAIN.
    write_bytes(p.master, encode(reply(stop, 5))->data());
    CHECK(t.receive(stop, deadline()) && t.metrics().eintr >= 2 && before.eintr >= 1);
    Access::gate(t, true);
    CHECK(t.submit(*encode(q)) == TransportStatus::ready);
    auto pending = std::async(std::launch::async, [&] { return t.receive(q, deadline()); });
    CHECK(Access::await(t, [](auto m) { return m.waits[0] == 1; }, deadline()));
    auto closing = std::async(std::launch::async, [&] { t.close(); });
    t.close();
    closing.get();
    CHECK(pending.get().status == TransportStatus::closed);
}
void host_workflows() {
    using namespace x1::f2::simulation;
    for (bool lose_ack : {false, true}) {
        Pty p;
        FdTransport t(p.take_slave());
        Peer peer(p.take_master());
        Host host(t, host_selection(), [id = uint64_t(100)]() mutable { return ++id; });
        auto snapshot = host.discover(deadline());
        Injection inject;
        if (lose_ack) {
            inject.drop_message = 0x13;
            inject.drops = 1;
            inject.lose_terminal = true;
        }
        peer.inject(inject);
        auto a = host.start(snapshot, BenchConfig{}.execution(), true, deadline(), host.fence());
        CHECK(peer.wait(0x13, lose_ack ? 2 : 1));
        CHECK(peer.metrics().runs == 1);
        if (lose_ack) {
            Wire first{};
            size_t count = 0;
            // Discovery has six fixed queries plus final rechecks, then claim/config/arm/run.
            for (size_t i = 0; i < 32; ++i) {
                if (count == 2)
                    break;
                auto w = peer.request_at(i);
                auto f = decode(w.data().subspan(1, w.size - 2));
                if (f && f->message == 0x13) {
                    if (!count)
                        first = w;
                    else
                        CHECK(first == w);
                    ++count;
                }
            }
            CHECK(count == 2);
        }
        peer.advance(200'000);
        auto terminal = host.terminal(deadline());
        CHECK(terminal && terminal->bytes[28] == 1 && get(*terminal, 16, 8) == a.execution);
        CHECK(host.stop(deadline()) && host.retire_heartbeat(deadline()));
        // Same FD supports fresh read-only discovery after fenced cleanup.
        CHECK(host.discover(deadline()).boot == snapshot.boot);
        peer.check();
    }
    for (unsigned mutation = 1; mutation <= 3; ++mutation) {
        Pty p;
        FdTransport t(p.take_slave());
        Peer peer(p.take_master());
        Host host(t, host_selection());
        peer.mutation(mutation);
        error([&] { host.discover(deadline()); }, Result::invalid_argument);
        peer.check();
    }
    Pty p;
    FdTransport t(p.take_slave());
    Peer peer(p.take_master());
    Host host(t, host_selection());
    for (auto op : {0x1234, 0x8001, 0x8002, 0x8003})
        error([&] { HostConformanceAccess::transaction(host, unknown(static_cast<uint16_t>(op), 132)); },
              Result::unsupported);
    peer.check();
}
void host_priority() {
    using namespace x1::f2::simulation;
    Pty p;
    FdTransport t(p.take_slave());
    Peer peer(p.take_master());
    Host h(t, host_selection(), [id = uint64_t(200)]() mutable { return ++id; });
    auto s = h.discover(deadline());
    Injection inject;
    inject.block_message = 0x13;
    peer.inject(inject);
    auto pending = std::async(std::launch::async, [&] {
        error([&] { h.start(s, BenchConfig{}.execution(), true, deadline(), h.fence()); }, Result::bad_state);
    });
    CHECK(peer.wait(0x13, 1));
    peer.advance(100'000);
    h.service_heartbeat();
    CHECK(peer.wait(0x12, 1));
    CHECK(h.stop(deadline()));
    pending.get();
    CHECK(h.retire_heartbeat(deadline()) && peer.metrics().runs == 1 && peer.metrics().stops >= 1);
    peer.advance(1'000'000);
    CHECK(peer.metrics().pulses < BenchConfig{}.pulses);
    peer.check();
}
void lifetimes() {
    auto baseline = fd_count();
    try {
        FdTransport invalid(-1);
        CHECK(false);
    } catch (const std::system_error &) {
    }
    for (unsigned i = 0; i < 30; ++i) {
        Pty p;
        FdTransport t(p.take_slave());
        Access::gate(t, true);
        auto q = unknown();
        CHECK(t.submit(*encode(q)) == TransportStatus::ready);
        if (i & 1) {
            ::close(p.master);
            p.master = -1;
            CHECK(t.receive(q, deadline()).status == TransportStatus::unavailable);
        }
        t.close();
        t.close();
    }
    CHECK(fd_count() == baseline);
    // New transport has neither old buffers nor stale authority; real Host starts fresh.
    host_workflows();
    CHECK(fd_count() == baseline);
}
} // namespace
int main() {
    try {
        framing();
        recovery_and_correlation();
        priority(false);
        priority(true);
        overflow_and_heartbeat();
        heartbeat_scheduling();
        kernel_backpressure();
        interrupts_and_disconnect();
        syscall_failures_and_close();
        host_workflows();
        host_priority();
        lifetimes();
        std::cout << "PTY ownership/framing/Host/retry/STOP/heartbeat/overflow/cancellation/lifetime PASS\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
