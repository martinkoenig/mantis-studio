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
namespace x1::f2 {
// Defined only in this unit test; production Host exposes no arbitrary opcode API.
struct HostConformanceAccess {
    static Frame transaction(Host &host, const Frame &q, Deadline d) {
        std::unique_lock ordinary(host.ordinary_, std::defer_lock);
        if (!ordinary.try_lock_until(d))
            throw ProtocolError(Result::busy, "Conformance transaction busy");
        return host.exchange(q, d);
    }
};
} // namespace x1::f2
Frame unknown_request(uint16_t message = 0x1234, uint64_t session = 0, uint32_t id = 0) {
    Frame q;
    q.message = message;
    q.session = session;
    q.request = id;
    return q;
}
Frame negative(const Frame &q, Result code = Result::unsupported) {
    Frame r;
    r.kind = Class::response;
    r.message = q.message;
    r.session = q.session;
    r.request = q.request;
    r.payload.size = 4;
    put(r.payload, 0, 2, static_cast<uint16_t>(code));
    r.payload.bytes[2] = 2;
    r.payload.bytes[3] = 1;
    return r;
}
// Replacement replies still traverse Link's real encoder and host-side parser.
// This is response mutation instrumentation, not an alternative controller or Host.
class Reply final : public Transport {
    Link &link_;
    Frame reply_;

  public:
    Reply(Link &link, const Frame &q, Frame reply) : link_(link), reply_(reply) {
        Injection injection;
        injection.block_message = q.message;
        link_.inject(injection);
    }
    TransportStatus submit(const Wire &wire) override {
        auto status = link_.submit(wire);
        if (status == TransportStatus::ready)
            link_.inject_event(reply_);
        return status;
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
};
void host_failure(Host &host, const Frame &q, Result expected, bool consumed) {
    try {
        HostConformanceAccess::transaction(host, q, Clock::now() + 100ms);
    } catch (const ProtocolError &e) {
        CHECK(e.result == expected && e.consumed == consumed);
        return;
    }
    throw std::runtime_error("Negative transaction succeeded");
}
void unknown_responses() {
    // T11: actual private transaction receives the simulator's four-byte error.
    for (auto opcode : {uint16_t(0x1234), uint16_t(0x8001), uint16_t(0x8002), uint16_t(0x8003)}) {
        Link link(true);
        Host host(link, host_selection());
        auto q = unknown_request(opcode);
        host_failure(host, q, Result::unsupported, false); // Unbound probes consume no regular ID.
        CHECK(!host.association().session && link.metrics().runs == 0);
    }
    // Every defined nonzero code/state, including unconsumed admission failures.
    for (uint16_t result = 1; result <= 18; ++result)
        for (uint8_t state = 0; state <= 6; ++state) {
            Link link(true);
            auto q = unknown_request(0x8001, 42, 2);
            auto r = negative(q, static_cast<Result>(result));
            r.payload.bytes[3] = state;
            Reply replies(link, q, r);
            Host host(replies, host_selection());
            bool consumed = result != 4 && result != 6 && result != 7 && result != 8;
            host_failure(host, q, static_cast<Result>(result), consumed);
            CHECK(!valid_message(r)); // F3b remains strict even for an accepted adapter rejection.
        }
    // T12: wrong length, positive/undefined result, non-terminal stage and invalid state.
    for (unsigned mutation = 0; mutation < 10; ++mutation) {
        Link link(true);
        auto q = unknown_request();
        auto r = negative(q);
        if (mutation < 4)
            r.payload.size = std::array<size_t, 4>{0, 3, 5, max_payload}[mutation];
        else if (mutation == 4)
            put(r.payload, 0, 2, 0);
        else if (mutation == 5)
            put(r.payload, 0, 2, 19);
        else if (mutation < 9)
            r.payload.bytes[2] = std::array<uint8_t, 3>{0, 1, 3}[mutation - 6];
        else
            r.payload.bytes[3] = 7;
        Reply replies(link, q, r);
        Host host(replies, host_selection());
        host_failure(host, q, Result::invalid_argument, false);
    }
    // Mismatches/direction errors are discarded by the framed transport before Host admission.
    for (unsigned mismatch = 0; mismatch < 5; ++mismatch) {
        Link link(true);
        auto q = unknown_request();
        auto r = negative(q);
        if (mismatch == 0)
            ++r.message;
        else if (mismatch == 1)
            ++r.session;
        else if (mismatch == 2)
            ++r.request;
        else
            r.kind = mismatch == 3 ? Class::event : Class::request;
        Reply replies(link, q, r);
        Host host(replies, host_selection());
        host_failure(host, q, Result::timeout, false);
        CHECK(link.metrics().runs == 0);
    }
    {
        Link link(true);
        auto unsolicited = negative(unknown_request());
        link.inject_event(unsolicited);
        Host host(link, host_selection());
        auto snapshot = host.discover(Clock::now() + 1s);
        CHECK(snapshot.boot == 0x8266000000000001 && !host.association().session);
        CHECK(!link.receive(unknown_request(), Clock::now())); // Discarded during matched HELLO.
    }
    // T13/T14: the exception cannot accept short known replies or relaxed rejection stages.
    for (const auto &l : registry) {
        if (!request_layout(l.id))
            continue;
        for (bool wrong_stage : {false, true}) {
            Link link(true);
            auto q = request(static_cast<Message>(l.id));
            auto r = negative(q);
            if (wrong_stage) {
                r.payload.size = l.response;
                r.payload.bytes[2] = 1;
            } else if (l.response == 4)
                r.payload.size = 3;
            Reply replies(link, q, r);
            Host host(replies, host_selection());
            host_failure(host, q, Result::invalid_argument, false);
            CHECK(!valid_message(r));
        }
    }
    // T16: a blocked unknown ordinary receive does not gate heartbeat or priority STOP.
    {
        Link link(true);
        uint64_t id = 100;
        Host host(link, host_selection(), [&] { return ++id; });
        auto snapshot = host.discover(Clock::now() + 1s);
        auto association = host.start(snapshot, BenchConfig{10'000, 10'000, 1'000, 100}.execution(), true,
                                      Clock::now() + 1s, host.fence());
        auto q = unknown_request(0x1234, association.session, 5);
        Injection injection;
        injection.block_message = q.message;
        link.inject(injection);
        auto pending =
            std::async(std::launch::async, [&] { host_failure(host, q, Result::unavailable, false); });
        CHECK(link.wait_receives(static_cast<Message>(q.message), 1, Clock::now() + 1s));
        link.advance(100'000);
        for (unsigned i = 0; i < 32; ++i)
            CHECK(link.send(*encode(unknown_request())));
        link.mutate([&](Controller &c) {
            CHECK(c.session() == association.session && c.state() == State::running &&
                  c.lease_end() == 500'000 && c.configuration_generation() == 1);
        });
        host.service_heartbeat();
        CHECK(link.wait_requests(Message::keepalive, 1, Clock::now() + 1s));
        link.mutate([](Controller &c) { CHECK(c.lease_end() == 600'000); });
        link.saturate();
        CHECK(host.stop(Clock::now() + 100ms));
        pending.get();
        CHECK(host.retire_heartbeat(Clock::now() + 100ms));
        CHECK(link.metrics().stops == 1 && link.metrics().runs == 1 && link.metrics().pulses == 10);
    }
    std::cout << "F2-ERR-001 actual Host T11-T16: framed errors, correlation, strict known layouts, "
                 "heartbeat and priority STOP passed\n";
}
int main() {
    try {
        unknown_responses();
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
