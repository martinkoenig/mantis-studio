#include "../fixtures/x1-f2/link.hpp"
#include <future>
#include <iostream>
using namespace x1::f2;
using namespace x1::f2::simulation;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)

// Instrumentation wraps the genuine byte transport. No alternative MCU/Host engine.
class Trace final : public Transport {
    Link &link_;
    std::mutex mutex_;
    std::array<Wire, Host::max_attempts> run_bytes_{};
    size_t runs_{};

  public:
    explicit Trace(Link &link) : link_(link) {}
    TransportStatus submit(const Wire &wire) override {
        if (wire.size < 3 || wire.size > max_wire)
            return TransportStatus::invalid;
        auto f = decode(wire.data().subspan(1, wire.size - 2));
        if (f && f->message == 0x13) {
            std::lock_guard lock(mutex_);
            CHECK(runs_ < run_bytes_.size());
            run_bytes_[runs_++] = wire;
        }
        return link_.submit(wire);
    }
    FrameRead receive(const Frame &q, Deadline d) override {
        return link_.receive(q, d);
    }
    FrameRead event(Deadline d) override {
        return link_.event(d);
    }
    void interrupt() noexcept override {
        link_.interrupt();
    }
    void check_retries() {
        std::lock_guard lock(mutex_);
        CHECK(runs_ == 3 && run_bytes_[0] == run_bytes_[1] && run_bytes_[1] == run_bytes_[2]);
    }
};
template <class F> void error(F f, Result result) {
    try {
        f();
    } catch (const ProtocolError &e) {
        CHECK(e.result == result);
        return;
    }
    throw std::runtime_error("Missing expected error");
}
int main() {
    try {
        {
            Link link(true);
            Transport &transport = link;
            auto q = request(Message::hello);
            auto wire = *encode(q);
            // Actual peer parser receives arbitrary fragments, including single bytes.
            for (auto byte : wire.data()) {
                Wire fragment;
                fragment.size = 1;
                fragment.bytes[0] = byte;
                CHECK(link.send(fragment));
            }
            CHECK(transport.receive(q, Clock::now()));
            auto corrupt = wire;
            corrupt.bytes[corrupt.size - 3] ^= 0x80;
            CHECK(link.send(corrupt));
            CHECK(transport.receive(q, Clock::now()).status == TransportStatus::timeout);
            Wire noise;
            noise.size = max_wire;
            noise.bytes.fill(255); // invalid COBS and encoded overflow across fragments
            CHECK(link.send(noise) && link.send(noise));
            Wire delimiter;
            delimiter.size = 1;
            CHECK(link.send(delimiter));
            Wire partial = wire;
            partial.size /= 2;
            CHECK(link.send(partial));
            link.advance(Parser::idle_us + 1);
            CHECK(link.send(delimiter));
            CHECK(transport.submit(wire) == TransportStatus::ready);
            CHECK(transport.receive(q, Clock::now()));
            CHECK(link.metrics().frames == 2 && link.metrics().malformed >= 3 &&
                  link.metrics().collector <= max_encoded && link.metrics().runs == 0);
            Wire overflow;
            overflow.size = max_wire + 1;
            CHECK(transport.submit(overflow) == TransportStatus::invalid);
        }
        {
            Link link(true);
            Transport &transport = link;
            auto q = request(Message::hello);
            Injection fault;
            fault.block_message = 1;
            link.inject(fault);
            CHECK(transport.submit(*encode(q)) == TransportStatus::ready);
            Frame reply;
            reply.kind = Class::response;
            reply.message = q.message;
            reply.payload.size = 24;
            for (unsigned mismatch = 0; mismatch < 3; ++mismatch) {
                auto stale = reply;
                if (mismatch == 0)
                    stale.session = 99;
                if (mismatch == 1)
                    stale.request = 99;
                if (mismatch == 2)
                    stale.message = 3;
                link.inject_event(stale); // Encodes and parses reply bytes, despite helper name.
            }
            auto event = reply;
            event.kind = Class::event;
            event.message = 0x8002;
            put(event.payload, 0, 2, 13);
            event.payload.bytes[2] = 4;
            event.payload.bytes[3] = 2;
            CHECK(valid_message(event));
            link.inject_event(event);
            link.inject_event(reply);
            CHECK(transport.receive(q, Clock::now())->session == q.session);
            CHECK(!transport.receive(q, Clock::now())); // stale replies were discarded
            CHECK(transport.event(Clock::now())->kind == Class::event);
            auto pending =
                std::async(std::launch::async, [&] { return transport.receive(q, Clock::now() + 1s); });
            CHECK(link.wait_receives(Message::hello, 3, Clock::now() + 1s));
            transport.interrupt();
            CHECK(pending.get().status == TransportStatus::interrupted);
            auto closed =
                std::async(std::launch::async, [&] { return transport.receive(q, Clock::now() + 1s); });
            CHECK(link.wait_receives(Message::hello, 4, Clock::now() + 1s));
            link.close();
            CHECK(closed.get().status == TransportStatus::closed);
            CHECK(transport.event(Clock::now()).status == TransportStatus::closed &&
                  transport.submit(*encode(request(Message::stop))) == TransportStatus::closed);
        }
        {
            Link link(true);
            Trace trace(link);
            uint64_t identity = 100;
            Host host(trace, host_selection(), [&] { return ++identity; });
            auto snapshot = host.discover(Clock::now() + 1s);
            auto wrong = host_selection();
            ++wrong.board_revision;
            CHECK(!configuration_fits(snapshot, wrong, BenchConfig{}.execution()));
            CHECK(!configuration_fits(snapshot, host_selection(), {}));
            auto unbounded = BenchConfig{}.execution();
            unbounded.period_us = unbounded.pulses = UINT32_MAX;
            CHECK(unbounded.duration_us() == uint64_t(UINT32_MAX) * UINT32_MAX);
            CHECK(!configuration_fits(snapshot, host_selection(), unbounded));
            Injection fault;
            fault.drop_message = 0x13;
            fault.drops = 2;
            link.inject(fault);
            host.start(snapshot, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence());
            trace.check_retries();
            CHECK(link.metrics().runs == 1); // Byte-identical retry cannot execute again.
            link.advance(200'000);
            CHECK(host.terminal(Clock::now() + 100ms));
            CHECK(host.stop(Clock::now() + 100ms));
        }
        {
            Link link(true);
            uint64_t identity = 100;
            Host host(link, host_selection(), [&] { return ++identity; });
            auto snapshot = host.discover(Clock::now() + 1s);
            Injection fault;
            fault.block_message = 0x13;
            link.inject(fault);
            auto pending = std::async(std::launch::async, [&] {
                error(
                    [&] {
                        host.start(snapshot, BenchConfig{}.execution(), true, Clock::now() + 1s,
                                   host.fence());
                    },
                    Result::bad_state);
            });
            CHECK(link.wait_receives(Message::run, 1, Clock::now() + 1s));
            // A second ordinary transaction cannot submit while RUN is awaiting its ACK.
            error([&] { host.discover(Clock::now()); }, Result::busy);
            host.service_heartbeat();
            CHECK(link.wait_requests(Message::keepalive, 1, Clock::now() + 1s));
            link.saturate();
            CHECK(host.shutdown(Clock::now() + 100ms));
            pending.get(); // Shutdown interrupted receive; caller quiesces before destruction.
            CHECK(link.metrics().runs == 1 && link.metrics().stops == 1);
        }
        {
            Link link(true);
            uint64_t identity = 100;
            Host host(link, host_selection(), [&] { return ++identity; });
            auto snapshot = host.discover(Clock::now() + 1s);
            host.start(snapshot, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence());
            Injection fault;
            fault.block_message = 0x12;
            link.inject(fault);
            auto heartbeat = std::async(std::launch::async, [&] { host.service_heartbeat(); });
            CHECK(link.wait_receives(Message::keepalive, 1, Clock::now() + 1s));
            CHECK(host.stop(Clock::now() + 100ms));
            heartbeat.get();
            CHECK(host.retire_heartbeat(Clock::now() + 100ms));
            CHECK(link.metrics().stops == 1);
        }
        std::cout << "F2 transport framing/matching/closure/interrupt/retry/priority conformance passed; "
                     "caller-owned transport, no shared allocation or images; sizeof(Host)="
                  << sizeof(Host) << ", sizeof(Link)=" << sizeof(Link) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
