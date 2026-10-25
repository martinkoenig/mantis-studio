#include "peer.hpp"
#include <future>
#include <memory>

namespace x1::f2 {
struct HostConformanceAccess {
    static void enabling(Host &h, Message m) {
        h.ordinary(m, request(m).payload, std::chrono::steady_clock::now() + std::chrono::seconds(1), true);
    }
};
} // namespace x1::f2
using namespace ptytest;
using namespace x1::f2::simulation;
namespace {
template <class F> void error(F f, Result code) {
    try {
        f();
    } catch (const ProtocolError &e) {
        CHECK(e.result == code);
        return;
    }
    throw std::runtime_error("Missing expected Host failure");
}
Frame status(FdTransport &t) {
    auto q = request(Message::status);
    CHECK(t.submit(*encode(q)) == TransportStatus::ready);
    auto r = t.receive(q, deadline());
    CHECK(r && valid_message(*r) && get(r->payload, 0, 2) == 0);
    return *r;
}
void timeout(Message message, size_t prefix, bool disconnect = false) {
    Pty p;
    FdTransport t(p.take_slave());
    auto peer = std::make_unique<Peer>(p.take_master());
    Host h(t, host_selection(), [id = uint64_t(100)]() mutable { return ++id; });
    auto snapshot = h.discover(deadline());
    auto old_fence = h.fence();
    Access::pause_message(t, message, prefix);
    auto pending = std::async(std::launch::async, [&] {
        error([&] { h.start(snapshot, BenchConfig{}.execution(), true, deadline(), old_fence); },
              Result::timeout);
    });
    CHECK(Access::await_message(t, prefix));
    CHECK(peer->request_count(static_cast<uint16_t>(message)) == 0);
    auto previous_heartbeats = peer->request_count(0x12);
    pending.get(); // Exhaust the real Host's three identical, admitted retries.
    CHECK(h.fence() != old_fence && h.stop_error() == Result::timeout);
    CHECK(t.metrics().cancelled >= Host::max_attempts - (prefix ? 1 : 0));
    h.service_heartbeat();
    CHECK(peer->request_count(0x12) == previous_heartbeats); // No lease revival after failed authority.
    auto failed = h.association();
    CHECK(failed.boot == snapshot.boot);
    if (message == Message::claim)
        CHECK(!failed.session && !failed.arm && !failed.execution);
    else
        CHECK(failed.session && failed.arm && failed.execution);
    if (disconnect) {
        peer.reset(); // Actual PTY HUP while the containment STOP cannot progress.
        CHECK(t.receive(request(Message::stop), deadline()).status == TransportStatus::unavailable);
        CHECK(!h.stop(deadline()) && h.stop_error() == Result::unavailable);
        return;
    }
    Access::gate(t, false);
    CHECK(peer->wait(0x14, 1));
    auto s = status(t); // Peer barrier: STOP has actually been processed, not just queued.
    CHECK(s.payload.bytes[3] == static_cast<uint8_t>(State::inhibited));
    CHECK(peer->request_count(static_cast<uint16_t>(message)) == (prefix ? 1 : 0));
    CHECK(peer->metrics().runs == (message == Message::run && prefix ? 1 : 0));
    if (message == Message::claim && !prefix)
        CHECK(!s.payload.bytes[12]); // No late CLAIM acquired any controller session.
    if (message == Message::run && prefix) {
        // Exactly the started original frame, then STOP: no interleaved or queued retry.
        size_t run_index = 0;
        while (decode(peer->request_at(run_index).data().subspan(1, peer->request_at(run_index).size - 2))
                   ->message != 0x13)
            ++run_index;
        auto next = peer->request_at(run_index + 1);
        CHECK(next == *encode(request(Message::stop)));
        auto e = t.event(deadline());
        if (e.status == TransportStatus::interrupted)
            e = t.event(deadline());
        CHECK(e && valid_message(*e) && e->message == 0x8001);
        const auto &record = e->payload;
        CHECK(get(record, 0, 8) == failed.boot && get(record, 8, 8) == failed.session &&
              get(record, 16, 8) == failed.execution && get(record, 24, 4) == failed.arm);
        CHECK(record.bytes[28] == 2 && get(record, 31, 4) == 0 && record.bytes[49] == 2);
        CHECK(get(record, 50, 2) == 0 && get(record, 52, 2) == 0); // Software STOP, no optical evidence.
    }
    peer->advance(1'000'000);
    CHECK(peer->metrics().pulses == 0);
    if (message == Message::run) {
        auto count = peer->request_count(0x13);
        error([&] { HostConformanceAccess::enabling(h, Message::run); }, Result::bad_state);
        CHECK(peer->request_count(0x13) == count);
    }
    CHECK(h.stop(deadline()) && h.stop_error() == Result::ok);
    error([&] { h.start(snapshot, BenchConfig{}.execution(), true, deadline(), old_fence); },
          Result::bad_state);
    // Recovery requires fresh discovery/session/CONFIGURE/ARM, never the abandoned ID.
    auto fresh = h.start(snapshot, BenchConfig{}.execution(), true, deadline(), h.fence());
    CHECK(fresh.session && fresh.session != failed.session && fresh.execution != failed.execution);
    CHECK(h.stop(deadline()) && h.retire_heartbeat(deadline()));
    peer->check();
}
// A narrow receive-registration/ACK-return barrier wraps the real PTY transport.
// All requests, replies and MCU semantics still traverse the unchanged byte path.
class ReceiveBarrier final : public Transport {
    FdTransport &fd_;
    Message message_;
    bool after_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_{}, released_{};
    void arrive() {
        std::unique_lock lock(mutex_);
        entered_ = true;
        changed_.notify_all();
        CHECK(changed_.wait_until(lock, deadline(), [&] { return released_; }));
    }

  public:
    ReceiveBarrier(FdTransport &fd, Message m, bool after) : fd_(fd), message_(m), after_(after) {}
    TransportStatus submit(const Wire &w) override {
        return fd_.submit(w);
    }
    FrameRead receive(const Frame &q, Deadline d) override {
        if (q.message == static_cast<uint16_t>(message_) && !after_)
            arrive();
        auto r = fd_.receive(q, d);
        if (q.message == static_cast<uint16_t>(message_) && after_)
            arrive();
        return r;
    }
    FrameRead event(Deadline d) override {
        return fd_.event(d);
    }
    void interrupt() noexcept override {
        fd_.interrupt();
    }
    void wait() {
        std::unique_lock lock(mutex_);
        CHECK(changed_.wait_until(lock, deadline(), [&] { return entered_; }));
    }
    void release() {
        std::lock_guard lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }
};
void cancel(Message message, bool after) {
    Pty p;
    FdTransport t(p.take_slave());
    Peer peer(p.take_master());
    ReceiveBarrier transport(t, message, after);
    Host h(transport, host_selection());
    auto snapshot = h.discover(deadline());
    if (!after)
        Access::pause_message(t, message);
    auto pending = std::async(std::launch::async, [&] {
        error([&] { h.start(snapshot, BenchConfig{}.execution(), true, deadline(), h.fence()); },
              Result::bad_state);
    });
    transport.wait(); // Before registration, or after a real ACK but before Host admission.
    if (!after)
        CHECK(Access::await_message(t));
    auto stopping = std::async(std::launch::async, [&] { CHECK(h.stop(deadline())); });
    // STOP admission itself fences and removes unsent work; observe without sleeps.
    CHECK(Access::await(t, [](auto m) { return m.waits[2] == 1; }, deadline()));
    Access::gate(t, false);
    stopping.get();
    transport.release();
    pending.get();
    CHECK(h.stop_error() == Result::ok && peer.metrics().stops == 1);
    CHECK(peer.metrics().runs == (message == Message::run && after ? 1 : 0));
    peer.advance(1'000'000);
    CHECK(peer.metrics().pulses == 0);
    if (message == Message::claim)
        CHECK(!h.association().session);
    CHECK(h.retire_heartbeat(deadline()));
    peer.check();
}
void malformed_claim() {
    Pty p;
    FdTransport t(p.take_slave());
    Peer peer(p.take_master());
    Host h(t, host_selection());
    auto snapshot = h.discover(deadline());
    peer.mutation(4);
    error([&] { h.start(snapshot, BenchConfig{}.execution(), true, deadline(), h.fence()); },
          Result::stale_boot);
    CHECK(!h.association().session && !h.association().execution);
    CHECK(peer.wait(0x14, 1) && !peer.metrics().runs);
    CHECK(h.stop(deadline()));
    peer.check();
}
void failed_stop_reply() {
    Pty p;
    FdTransport t(p.take_slave());
    Peer peer(p.take_master());
    Host h(t, host_selection());
    auto snapshot = h.discover(deadline());
    Injection failure;
    failure.block_message = 0x13;
    failure.cleanup_error = true;
    peer.inject(failure);
    error([&] { h.start(snapshot, BenchConfig{}.execution(), true, deadline(), h.fence()); },
          Result::timeout); // Initiating failure survives a different controller cleanup error.
    CHECK(peer.wait(0x14, 1));
    CHECK(!h.stop(deadline()) && h.stop_error() == Result::internal_error);
    CHECK(peer.metrics().runs == 1 && peer.metrics().pulses == 0);
    CHECK(h.retire_heartbeat(deadline()));
    peer.check();
}
void reconciliation_failure() {
    Pty p;
    FdTransport t(p.take_slave());
    Peer peer(p.take_master());
    Host h(t, host_selection());
    auto snapshot = h.discover(deadline());
    h.start(snapshot, BenchConfig{}.execution(), true, deadline(), h.fence());
    auto old_fence = h.fence();
    Access::pause_message(t, Message::status);
    auto read =
        std::async(std::launch::async, [&] { error([&] { h.terminal(deadline()); }, Result::timeout); });
    CHECK(Access::await_message(t));
    auto previous_heartbeats = peer.request_count(0x12);
    auto heartbeat = std::async(std::launch::async, [&] { h.service_heartbeat(); });
    CHECK(Access::await(t, [](auto m) { return m.waits[1] > 0; }, deadline()));
    read.get();
    heartbeat.get();
    CHECK(h.fence() != old_fence && h.stop_error() == Result::timeout);
    Access::gate(t, false);
    CHECK(peer.wait(0x14, 1));
    CHECK(status(t).payload.bytes[3] == static_cast<uint8_t>(State::inhibited));
    CHECK(peer.request_count(0x12) == previous_heartbeats); // Queued lease renewal was canceled.
    peer.advance(1'000'000);
    CHECK(peer.metrics().pulses == 0 && h.stop(deadline()));
    CHECK(h.retire_heartbeat(deadline()));
    peer.check();
}
} // namespace
int main() {
    try {
        timeout(Message::run, 0);
        timeout(Message::run, 3);
        timeout(Message::claim, 0);
        timeout(Message::claim, 3);
        timeout(Message::run, 0, true);
        for (auto message : {Message::claim, Message::arm, Message::run})
            for (bool after : {false, true})
                cancel(message, after);
        malformed_claim();
        failed_stop_reply();
        reconciliation_failure();
        std::cout << "R1 queued/partial RUN/CLAIM abandonment, cancellation admission, STOP uncertainty and "
                     "fresh-authority recovery PASS\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
