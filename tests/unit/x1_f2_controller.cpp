#include "../fixtures/x1-f2/layout-golden.hpp"
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
Frame tx(Link &l, const Frame &q, Result expected = Result::ok) {
    CHECK(l.send(*encode(q)));
    auto r = l.receive(q, Clock::now());
    CHECK(r);
    CHECK(valid_message(*r));
    if (get(r->payload, 0, 2) != static_cast<uint16_t>(expected))
        throw std::runtime_error("F2 message=" + std::to_string(q.message) +
                                 " request=" + std::to_string(q.request) +
                                 " result=" + std::to_string(get(r->payload, 0, 2)) +
                                 " expected=" + std::to_string(static_cast<uint16_t>(expected)));
    return *r;
}
struct Rig {
    Link link{true};
    uint64_t boot = 0x8266000000000001, session = 42, snapshot = 1, calibration{};
    uint32_t id = 2, arm{};
    Rig() {
        auto q = request(Message::calibration);
        put(q.payload, 1, 8, snapshot);
        calibration = get(tx(link, q).payload, 24, 8);
    }
    void claim() {
        id = 2;
        auto q = request(Message::claim, session, 1);
        put(q.payload, 0, 8, boot);
        tx(link, q);
    }
    Frame configure(uint64_t token = 0, uint64_t generation = 1) {
        auto q = request(Message::configure, session, id++);
        put(q.payload, 1, 4, 10'000);
        put(q.payload, 5, 4, 10'000);
        put(q.payload, 9, 4, 1'000);
        put(q.payload, 13, 8, token ? token : calibration);
        put(q.payload, 21, 8, generation);
        return q;
    }
    Frame arm_request() {
        auto q = request(Message::arm, session, id++);
        put(q.payload, 0, 8, 1);
        return q;
    }
    void ready() {
        claim();
        tx(link, configure());
        arm = static_cast<uint32_t>(get(tx(link, arm_request()).payload, 4, 4));
    }
    Frame run(uint64_t execution = 777, uint32_t count = 20) {
        auto q = request(Message::run, session, id++);
        put(q.payload, 0, 4, arm);
        put(q.payload, 4, 8, execution);
        put(q.payload, 12, 4, count);
        return q;
    }
    Frame heartbeat(uint32_t counter) {
        auto q = request(Message::keepalive, session);
        put(q.payload, 0, 4, arm);
        put(q.payload, 4, 4, counter);
        return q;
    }
    Frame terminal(uint64_t execution = 777, bool bound = true) {
        auto q = request(Message::terminal, bound ? session : 0, bound ? id++ : 0);
        put(q.payload, 0, 8, boot);
        put(q.payload, 8, 8, session);
        put(q.payload, 16, 8, execution);
        return q;
    }
};
template <class F> void failure(F f, Result r) {
    try {
        f();
    } catch (const ProtocolError &e) {
        CHECK(e.result == r);
        return;
    }
    throw std::runtime_error("Missing expected F2 error");
}
Payload literal(std::string_view text) {
    Payload p;
    p.size = text.size() / 2;
    CHECK(p.size <= 132);
    for (size_t i = 0; i < p.size; ++i)
        p.bytes[i] = static_cast<uint8_t>(std::stoul(std::string(text.substr(2 * i, 2)), nullptr, 16));
    return p;
}
void calibration_revocation() {
    // CONFIGURED/ARMED invalidation removes authority without inventing a controller fault.
    for (bool armed : {false, true}) {
        Rig r;
        r.claim();
        tx(r.link, r.configure());
        if (armed)
            r.arm = static_cast<uint32_t>(get(tx(r.link, r.arm_request()).payload, 4, 4));
        r.link.mutate([](Controller &c) {
            CHECK(c.snapshot() == 1);
            c.revoke_calibration();
            CHECK(c.state() == State::inhibited && !c.lease_end() && c.session() == 42);
            CHECK(c.snapshot() == 2 && c.configuration_generation() == 1);
            c.revoke_calibration();
            CHECK(c.snapshot() == 2); // Repeated revocation is not another eligibility change.
        });
        CHECK(!r.link.event(Clock::now())); // No invented execution terminal or fault.
        tx(r.link, r.arm_request(), Result::bad_state);
        tx(r.link, r.run(), Result::bad_state);
        tx(r.link, r.configure(), Result::snapshot_changed);
        auto revoked = r.configure(0, 2);
        auto rejection = tx(r.link, revoked, Result::calibration_invalid);
        CHECK(tx(r.link, revoked, Result::calibration_invalid) == rejection);
        auto status = request(Message::channel_status);
        put(status.payload, 1, 8, 2);
        auto response = tx(r.link, status);
        CHECK(response.payload.bytes[21] == 1 && response.payload.bytes[23] == 3 &&
              !get(response.payload, 24, 2)); // NotReady/invalid calibration, no controller fault.
        CHECK(r.link.metrics().runs == 0 && r.link.metrics().pulses == 0);
    }
    for (auto now : {uint64_t(0), uint64_t(35'000)})
        for (auto cleanup : {Result::ok, Result::internal_error}) {
            Rig r;
            r.ready();
            tx(r.link, r.run());
            r.link.advance(now);
            r.link.mutate([&](Controller &c) {
                CHECK(c.snapshot() == 1 && c.state() == State::running);
                c.cleanup_error(cleanup);
                c.revoke_calibration();
                CHECK(c.state() == State::fault && !c.lease_end() && c.session() == r.session);
                CHECK(c.snapshot() == 2 && c.completed_pulses == now / 10'000);
            });
            auto event = r.link.event(Clock::now());
            CHECK(event && valid_message(*event) && event->message == 0x8001);
            const auto &record = event->payload;
            CHECK(event->session == r.session && record.size == 58);
            CHECK(get(record, 0, 8) == r.boot && get(record, 8, 8) == r.session &&
                  get(record, 16, 8) == 777 && get(record, 24, 4) == r.arm);
            CHECK(record.bytes[28] == 5 &&
                  get(record, 29, 2) ==
                      static_cast<uint16_t>(cleanup == Result::ok ? Result::calibration_invalid : cleanup));
            CHECK(get(record, 31, 4) == now / 10'000 && record.bytes[35] == 2 && record.bytes[36] == 1 &&
                  get(record, 37, 8) == now && !get(record, 45, 4));
            CHECK(record.bytes[49] == 2 && get(record, 50, 2) == 18 &&
                  get(record, 52, 2) == static_cast<uint16_t>(cleanup) && get(record, 54, 4) == 1);
            auto fault = r.link.event(Clock::now());
            CHECK(fault && valid_message(*fault) && fault->message == 0x8002 && fault->session == r.session);
            CHECK(get(fault->payload, 0, 2) == 18 && fault->payload.bytes[2] == 4 &&
                  fault->payload.bytes[3] == 2 && get(fault->payload, 4, 4) == r.arm &&
                  get(fault->payload, 8, 8) == 777 && get(fault->payload, 16, 8) == now);
            auto recovered = tx(r.link, r.terminal());
            CHECK(
                std::equal(record.data().begin(), record.data().end(), recovered.payload.bytes.begin() + 4));
            r.link.advance(1'000'000); // Beyond completion and original lease, still no more pulses.
            tx(r.link, request(Message::stop), cleanup);
            CHECK(r.link.metrics().pulses == now / 10'000 && r.link.metrics().runs == 1);
            CHECK(!r.link.event(Clock::now())); // STOP cannot rewrite the immutable terminal.
            auto retained = tx(r.link, r.terminal());
            CHECK(std::equal(record.data().begin(), record.data().end(), retained.payload.bytes.begin() + 4));
            CHECK(tx(r.link, request(Message::status)).payload.bytes[3] == 6);
            auto channel = request(Message::channel_status);
            put(channel.payload, 1, 8, 2);
            auto channel_status = tx(r.link, channel);
            CHECK(channel_status.payload.bytes[21] == 3 && channel_status.payload.bytes[23] == 3 &&
                  get(channel_status.payload, 24, 2) == 18);
            tx(r.link, r.configure(), Result::snapshot_changed);
            tx(r.link, r.configure(0, 2), Result::bad_state);
            tx(r.link, r.arm_request(), Result::bad_state);
            tx(r.link, r.run(), Result::bad_state);
            tx(r.link, r.heartbeat(1), Result::bad_state);
            tx(r.link, request(Message::clear_fault, r.session, r.id++), Result::hardware_fault);
            // Reboot loads a fresh synthetic boot-scoped record. Old authority cannot resume.
            ++r.boot;
            r.link.mutate([&](Controller &c) { c.reboot(r.boot); });
            tx(r.link, r.configure(), Result::no_session);
            auto calibration = request(Message::calibration);
            put(calibration.payload, 1, 8, 1);
            auto fresh_token = get(tx(r.link, calibration).payload, 24, 8);
            CHECK(fresh_token && fresh_token != r.calibration);
            r.claim();
            tx(r.link, r.configure(), Result::calibration_invalid);
            tx(r.link, r.arm_request(), Result::bad_state);
            r.calibration = fresh_token;
            tx(r.link, r.configure());
            r.arm = static_cast<uint32_t>(get(tx(r.link, r.arm_request()).payload, 4, 4));
            tx(r.link, r.run(778));
            r.link.advance(1'200'000);
            CHECK(tx(r.link, r.terminal(778)).payload.bytes[32] == 1);
            r.link.mutate([](Controller &c) { CHECK(c.snapshot() == 1); });
        }
    // A harmless discovery revision is distinct from calibration revocation/fault.
    Rig harmless;
    harmless.ready();
    tx(harmless.link, harmless.run());
    harmless.link.mutate([](Controller &c) {
        c.change_snapshot();
        CHECK(c.state() == State::running && c.snapshot() == 2);
    });
    harmless.link.advance(200'000);
    CHECK(tx(harmless.link, harmless.terminal()).payload.bytes[32] == 1);
    CHECK(harmless.link.metrics().pulses == 20);
}
int main() {
    try {
        calibration_revocation();
        {
            Rig r;
            for (auto [opcode, expected] : f2_payloads) {
                auto q = request(static_cast<Message>(opcode));
                if (q.payload.size)
                    put(q.payload, 1, 8, 1);
                auto f = tx(r.link, q);
                CHECK(f.payload == literal(expected));
                for (auto index : {size_t(0), size_t(2), size_t(3)}) {
                    auto bad = f;
                    bad.payload.bytes[index] = 255;
                    CHECK(!valid_message(bad));
                }
                if (opcode == 4) {
                    auto bad = f;
                    bad.payload.bytes[7] |= 0x80;
                    CHECK(!valid_message(bad));
                }
                if (opcode == 5) {
                    auto bad = f;
                    put(bad.payload, 21, 2, 0x8000);
                    CHECK(!valid_message(bad));
                }
                if (opcode == 6) {
                    for (auto index : {21u, 22u, 23u}) {
                        auto bad = f;
                        bad.payload.bytes[index] = 255;
                        CHECK(!valid_message(bad));
                    }
                    auto bad = f;
                    put(bad.payload, 30, 2, 16);
                    CHECK(!valid_message(bad));
                }
                if (opcode == 7) {
                    for (auto index : {21u, 22u, 23u}) {
                        auto bad = f;
                        bad.payload.bytes[index] = 255;
                        CHECK(!valid_message(bad));
                    }
                }
                if (opcode == 8) {
                    for (auto index : {29u, 30u, 31u, 32u, 33u}) {
                        auto bad = f;
                        bad.payload.bytes[index] = 255;
                        CHECK(!valid_message(bad));
                    }
                    auto bad = f;
                    put(bad.payload, 50, 2, 2);
                    CHECK(!valid_message(bad));
                }
            }
            r.ready();
            tx(r.link, r.run());
            r.link.advance(200'000);
            auto event = r.link.event(Clock::now());
            CHECK(event && event->payload == literal(f2_terminal));
            for (auto index : {28u, 35u, 36u, 49u}) {
                auto bad = *event;
                bad.payload.bytes[index] = 255;
                CHECK(!valid_message(bad));
            }
            auto bad = *event;
            bad.payload.bytes[36] = 0;
            CHECK(!valid_message(bad));
        }
        {
            Rig r;
            auto q = r.configure();
            tx(r.link, q, Result::no_session);
            q = request(Message::claim, 42, 1);
            put(q.payload, 0, 8, 1);
            tx(r.link, q, Result::stale_boot);
            r.claim();
            auto wrong = r.configure();
            wrong.session = 43;
            tx(r.link, wrong, Result::stale_session);
            --r.id;
            auto config = r.configure();
            auto response = tx(r.link, config);
            CHECK(tx(r.link, config) == response);
            auto conflict = config;
            put(conflict.payload, 1, 4, 11'000);
            tx(r.link, conflict, Result::conflicting_duplicate);
            auto gap = r.arm_request();
            gap.request += 1;
            tx(r.link, gap, Result::stale_request);
            --r.id;
            auto arm = r.arm_request();
            tx(r.link, arm);
            CHECK(r.link.metrics().runs == 0); // ARM never starts pulses
        }
        {
            Rig r;
            r.claim();
            auto bad = r.configure(123);
            auto rejected = tx(r.link, bad, Result::calibration_invalid);
            CHECK(tx(r.link, bad, Result::calibration_invalid) == rejected);
            auto malformed = r.configure();
            malformed.payload.size = 21;
            tx(r.link, malformed, Result::invalid_argument);
            auto stale = r.configure(0, 9);
            tx(r.link, stale, Result::snapshot_changed);
            r.link.mutate([](Controller &c) {
                CHECK(c.configuration_generation() == 0 && c.state() == State::inhibited && !c.lease_end());
            });
            tx(r.link, r.configure());
            r.link.mutate([](Controller &c) {
                CHECK(c.snapshot() == 1 && c.configuration_generation() == 1);
                c.change_snapshot();
            });
            tx(r.link, r.arm_request(), Result::snapshot_changed);
            auto again = r.configure(0, 2);
            tx(r.link, again);
            auto arm = r.arm_request();
            put(arm.payload, 0, 8, 2);
            r.arm = static_cast<uint32_t>(get(tx(r.link, arm).payload, 4, 4));
            r.link.mutate([](Controller &c) { c.change_snapshot(); });
            tx(r.link, r.run(), Result::snapshot_changed);
            CHECK(r.link.metrics().runs == 0);
        }
        {
            Rig r;
            r.ready();
            tx(r.link, request(Message::stop));
            tx(r.link, r.configure());
            auto arm = r.arm_request();
            put(arm.payload, 0, 8, 2);
            tx(r.link, arm);
            tx(r.link, request(Message::stop));
            tx(r.link, r.configure());
            auto reused = r.arm_request(); // An older nonce is still consumed intent.
            tx(r.link, reused, Result::invalid_argument);
            r.link.mutate([](Controller &c) { CHECK(c.state() == State::configured); });
            CHECK(r.link.metrics().runs == 0);
        }
        {
            Rig r;
            r.ready();
            auto run = r.run();
            auto ack = tx(r.link, run);
            CHECK(ack.payload.bytes[2] == 0 && ack.payload.bytes[3] == 4);
            CHECK(tx(r.link, run) == ack && r.link.metrics().runs == 1);
            auto conflict = run;
            put(conflict.payload, 12, 4, 21);
            tx(r.link, conflict, Result::conflicting_duplicate);
            r.link.advance(200'000);
            auto event = r.link.event(Clock::now());
            CHECK(event && valid_message(*event));
            auto recovered = tx(r.link, r.terminal());
            CHECK(std::equal(event->payload.data().begin(), event->payload.data().end(),
                             recovered.payload.bytes.begin() + 4));
            CHECK(get(event->payload, 31, 4) == 20 && event->payload.bytes[28] == 1 &&
                  get(event->payload, 50, 2) == 0 && event->payload.bytes[49] == 2);
            auto public_status = tx(r.link, request(Message::status));
            CHECK(!get(public_status.payload, 13, 4) && !get(public_status.payload, 17, 8) &&
                  !get(public_status.payload, 25, 8) && !get(public_status.payload, 39, 8) &&
                  !get(public_status.payload, 47, 4));
            tx(r.link, r.terminal(777, false), Result::unavailable);
            auto wrong = r.terminal(778);
            tx(r.link, wrong, Result::stale_request);
            tx(r.link, r.run(778), Result::bad_state);
            tx(r.link, request(Message::stop));
            tx(r.link, request(Message::stop));
            CHECK(r.link.metrics().runs == 1 && r.link.metrics().pulses == 20);
        }
        {
            Rig r;
            r.ready();
            tx(r.link, r.run(777, 100));
            r.link.advance(200'000);
            auto keep = r.heartbeat(1);
            tx(r.link, keep);
            r.link.advance(600'000);
            tx(r.link, keep, Result::stale_request);
            auto bad = r.heartbeat(2);
            put(bad.payload, 0, 4, r.arm + 1);
            tx(r.link, bad, Result::stale_request);
            bad = r.heartbeat(2);
            bad.session = 99;
            tx(r.link, bad, Result::stale_session);
            auto unbound_status = tx(r.link, request(Message::status));
            CHECK(!get(unbound_status.payload, 37, 2));
            // Valid unrelated traffic and malformed bytes cannot extend the 700ms lease.
            auto corrupt = *encode(r.heartbeat(2));
            corrupt.bytes[corrupt.size - 3] ^= 0x80;
            CHECK(r.link.send(corrupt));
            r.link.advance(900'000);
            auto terminal = tx(r.link, r.terminal(777, false));
            CHECK(terminal.payload.bytes[32] == 3 &&
                  get(terminal.payload, 35, 4) == 70); // reason/count offsets +4
            CHECK(get(terminal.payload, 54, 2) == 10 && r.link.metrics().pulses == 70);
            tx(r.link, r.heartbeat(3), Result::no_session);
            CHECK(get(tx(r.link, request(Message::status)).payload, 39, 8) == 42);
            auto reboot_boot = r.boot + 1;
            r.link.mutate([&](Controller &c) { c.reboot(reboot_boot); });
            tx(r.link, r.terminal(777, false), Result::unavailable);
            tx(r.link, r.configure(), Result::no_session);
            auto stale = request(Message::claim, 99, 1);
            put(stale.payload, 0, 8, r.boot);
            tx(r.link, stale, Result::stale_boot);
        }
        {
            Rig r;
            r.ready();
            tx(r.link, r.run());
            r.link.advance(55'000);
            tx(r.link, request(Message::disarm, r.session, r.id++), Result::bad_state);
            tx(r.link, request(Message::stop));
            auto terminal = tx(r.link, r.terminal());
            CHECK(get(terminal.payload, 35, 4) == 5 && terminal.payload.bytes[32] == 2);
            r.link.advance(1'000'000);
            CHECK(r.link.metrics().pulses == 5); // No post-STOP pulses.
            tx(r.link, request(Message::disarm, r.session, r.id++));
            tx(r.link, request(Message::clear_fault, r.session, r.id++), Result::bad_state);
        }
        {
            Rig r;
            r.ready();
            tx(r.link, r.run());
            r.link.advance(25'000);
            r.link.mutate([](Controller &c) { c.inject_fault(Result::hardware_fault); });
            auto terminal = tx(r.link, r.terminal());
            CHECK(get(terminal.payload, 35, 4) == 2 && terminal.payload.bytes[32] == 5);
            tx(r.link, request(Message::clear_fault, r.session, r.id++), Result::hardware_fault);
            tx(r.link, request(Message::stop));
            r.link.mutate([](Controller &c) { c.resolve_fault(); });
            tx(r.link, request(Message::clear_fault, r.session, r.id++));
            tx(r.link, r.configure(), Result::no_session);
            tx(r.link, r.terminal(777, false));
        }
        {
            Rig r;
            auto sensor = request(Message::sensor);
            put(sensor.payload, 1, 8, 1);
            auto found = tx(r.link, sensor);
            CHECK(found.payload.bytes[29] == 4 && found.payload.bytes[31] == 1 && !get(found.payload, 46, 4));
            sensor.payload.bytes[9] = 1;
            tx(r.link, sensor, Result::invalid_argument);
            sensor.payload.bytes[9] = 0;
            put(sensor.payload, 1, 8, 2);
            tx(r.link, sensor, Result::snapshot_changed);
            auto wrong = request(Message::stop);
            wrong.kind = Class::response;
            CHECK(r.link.send(*encode(wrong)));
            CHECK(!r.link.receive(wrong, Clock::now()));
            CHECK(r.link.metrics().stops == 0);
            r.claim();
            r.link.mutate([](Controller &c) { c.revoke_calibration(); });
            tx(r.link, r.configure(0, 2), Result::calibration_invalid);
        }
        {
            Rig r;
            r.claim();
            auto c = r.configure();
            put(c.payload, 9, 4, 10'000);
            tx(r.link, c, Result::limit_exceeded);
            c = r.configure();
            put(c.payload, 1, 4, UINT32_MAX);
            tx(r.link, c, Result::limit_exceeded);
            tx(r.link, r.configure());
            r.arm = static_cast<uint32_t>(get(tx(r.link, r.arm_request()).payload, 4, 4));
            tx(r.link, r.run(777, UINT32_MAX), Result::limit_exceeded);
            CHECK(r.link.metrics().runs == 0);
        }
        {
            Rig r;
            Injection f;
            f.cleanup_error = true;
            r.link.inject(f);
            r.ready();
            tx(r.link, r.run());
            r.link.advance(25'000);
            tx(r.link, request(Message::stop), Result::internal_error);
            auto record = tx(r.link, r.terminal());
            CHECK(get(record.payload, 54, 2) == 0 && get(record.payload, 56, 2) == 15 &&
                  get(record.payload, 35, 4) == 2);
            tx(r.link, request(Message::stop), Result::internal_error);
            CHECK(r.link.metrics().pulses == 2);
            tx(r.link, request(Message::clear_fault, r.session, r.id++), Result::hardware_fault);
            auto takeover = request(Message::claim, 43, 1);
            put(takeover.payload, 0, 8, r.boot);
            tx(r.link, takeover, Result::busy);
            r.link.mutate([](Controller &c) { c.resolve_fault(); });
            tx(r.link, request(Message::clear_fault, r.session, r.id++), Result::hardware_fault);
            r.link.mutate([](Controller &c) { c.cleanup_error(Result::ok); });
            tx(r.link, request(Message::clear_fault, r.session, r.id++));
            tx(r.link, r.configure(), Result::no_session);
        }
        {
            Rig r;
            r.claim();
            auto q = request(Message::disarm, r.session, r.id++);
            q.message = 0x1234;
            CHECK(r.link.send(*encode(q)));
            auto first = r.link.receive(q, Clock::now());
            CHECK(first && first->payload.size == 4 && get(first->payload, 0, 2) == 2 &&
                  first->payload.bytes[2] == 2);
            CHECK(r.link.send(*encode(q)));
            CHECK(r.link.receive(q, Clock::now()) == first);
            tx(r.link, r.configure());
            CHECK(r.link.metrics().runs == 0);
        }
        // Host tests also use real framed bytes; identities are injectable only in this fixture.
        for (auto scenario : {"normal", "lost_ack", "lost_event", "delayed_event", "duplicate", "snapshot"}) {
            Link link(true);
            uint64_t id = 100;
            Host host(link, host_selection(), [&] { return ++id; });
            Injection fault;
            if (std::string_view(scenario) == "snapshot")
                fault.snapshot_after_message = 4;
            link.inject(fault);
            auto snapshot = host.discover(Clock::now() + 1s);
            CHECK(snapshot.generation == (std::string_view(scenario) == "snapshot" ? 2 : 1));
            fault.snapshot_after_message = 0;
            if (std::string_view(scenario) == "lost_ack") {
                fault.drop_message = 0x13;
                fault.drops = 1;
            }
            if (std::string_view(scenario) == "lost_event")
                fault.lose_terminal = true;
            if (std::string_view(scenario) == "delayed_event")
                fault.terminal_delay_us = 100'000;
            if (std::string_view(scenario) == "duplicate")
                fault.duplicate = true;
            link.inject(fault);
            auto a = host.start(snapshot, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence());
            CHECK(a.arm && a.execution);
            link.advance(200'000);
            auto terminal = host.terminal(Clock::now() + 100ms);
            CHECK(terminal && terminal->bytes[28] == 1 && get(*terminal, 31, 4) == 20);
            CHECK(link.metrics().runs == 1 && host.stop(Clock::now() + 100ms));
        }
        {
            Link link(true);
            uint64_t id = 100;
            Host host(link, host_selection(), [&] { return ++id; });
            auto snapshot = host.discover(Clock::now() + 1s);
            auto a = host.start(snapshot, BenchConfig{}.execution(), false, Clock::now() + 1s, host.fence());
            CHECK(!a.arm && !a.execution && link.metrics().runs == 0 && link.metrics().stops == 1);
        }
        for (auto msg : {uint16_t(0x10), uint16_t(0x11)}) {
            Link link(true);
            uint64_t id = 100;
            Host host(link, host_selection(), [&] { return ++id; });
            auto s = host.discover(Clock::now() + 1s);
            Injection f;
            f.reject_message = msg;
            link.inject(f);
            failure([&] { host.start(s, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence()); },
                    Result::hardware_fault);
            CHECK(link.metrics().runs == 0);
            CHECK(host.stop(Clock::now() + 100ms));
        }
        {
            Link link(true);
            uint64_t id = 100;
            Host host(link, host_selection(), [&] { return ++id; });
            auto s = host.discover(Clock::now() + 1s);
            link.mutate([](Controller &c) { c.change_snapshot(); });
            failure([&] { host.start(s, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence()); },
                    Result::snapshot_changed);
            CHECK(link.metrics().runs == 0);
        }
        {
            Link link(true);
            uint64_t id = 100;
            Host host(link, host_selection(), [&] { return ++id; });
            auto s = host.discover(Clock::now() + 1s);
            Injection f;
            f.block_message = 0x13;
            link.inject(f);
            auto pending = std::async(std::launch::async, [&] {
                failure(
                    [&] {
                        host.start(s, BenchConfig{10'000, 10'000, 1'000, 100}.execution(), true,
                                   Clock::now() + 1s, host.fence());
                    },
                    Result::bad_state);
            });
            CHECK(link.wait_requests(Message::run, 1, Clock::now() + 1s));
            link.advance(400'000);
            host.service_heartbeat(); // ordinary RUN transaction is still waiting
            link.advance(600'000);
            CHECK(link.metrics().pulses == 0); // still RUNNING: heartbeat extended the original lease
            auto begin = Clock::now();
            CHECK(host.stop(Clock::now() + 100ms));
            auto latency = Clock::now() - begin;
            pending.get();
            CHECK(link.metrics().runs == 1);
            std::cout << "Priority STOP with ordinary RUN blocked: "
                      << std::chrono::duration<double, std::micro>(latency).count() << " us synthetic\n";
        }
        {
            Link link(true);
            uint64_t id = 100;
            Host host(link, host_selection(), [&] { return ++id; });
            auto s = host.discover(Clock::now() + 1s);
            host.start(s, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence());
            Injection f;
            f.block_next = true;
            link.inject(f);
            link.saturate();
            auto pending = std::async(std::launch::async, [&] { return host.terminal(Clock::now() + 1s); });
            CHECK(link.wait_events(1, Clock::now() + 1s));
            CHECK(host.stop(Clock::now() + 100ms));
            CHECK(!pending.get());
            CHECK(link.metrics().reply_high_water == 8 && link.metrics().event_high_water == 8);
        }
        {
            Link link(true);
            uint64_t id = 100;
            Host host(link, host_selection(), [&] { return ++id; });
            auto s = host.discover(Clock::now() + 1s);
            host.start(s, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence());
            Injection f;
            f.fail_stop = true;
            link.inject(f);
            CHECK(!host.stop(Clock::now() + 20ms));
            CHECK(link.metrics().stops == 1);
            link.advance(200'000);
            CHECK(link.metrics().pulses == 0);
        }
        for (auto scenario : {"corrupt", "link", "timeout", "reboot", "cleanup", "lease"}) {
            Link link(true);
            uint64_t id = 100;
            Host host(link, host_selection(), [&] { return ++id; });
            auto s = host.discover(Clock::now() + 1s);
            Injection f;
            if (std::string_view(scenario) == "corrupt") {
                f.corrupt = true;
                link.inject(f);
                failure(
                    [&] { host.start(s, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence()); },
                    Result::timeout);
                continue;
            }
            if (std::string_view(scenario) == "link") {
                f.fail_link = true;
                link.inject(f);
                failure(
                    [&] { host.start(s, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence()); },
                    Result::unavailable);
                continue;
            }
            if (std::string_view(scenario) == "timeout") {
                f.block_message = 0x13;
                link.inject(f);
                failure(
                    [&] { host.start(s, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence()); },
                    Result::timeout);
                CHECK(link.metrics().runs == 1);
                CHECK(host.stop(Clock::now() + 100ms));
                continue;
            }
            BenchConfig config;
            if (std::string_view(scenario) == "lease") {
                config.pulses = 100;
                f.disable_heartbeat = true;
                f.lose_terminal = true;
            }
            if (std::string_view(scenario) == "cleanup")
                f.cleanup_error = true;
            link.inject(f);
            host.start(s, config.execution(), true, Clock::now() + 1s, host.fence());
            if (std::string_view(scenario) == "reboot") {
                link.mutate([](Controller &c) { c.reboot(0x8266000000000002); });
                failure([&] { host.terminal(Clock::now() + 100ms); }, Result::stale_boot);
            } else {
                link.advance(600'000);
                auto record = host.terminal(Clock::now() + 100ms);
                CHECK(record);
                if (std::string_view(scenario) == "cleanup")
                    CHECK(get(*record, 52, 2) == 15);
                if (std::string_view(scenario) == "lease")
                    CHECK(record->bytes[28] == 3 && get(*record, 31, 4) == 50);
            }
        }
        {
            Link link(true);
            uint64_t id = 100;
            Host host(link, host_selection(), [&] { return ++id; });
            auto s = host.discover(Clock::now() + 1s);
            Injection f;
            f.delay_us = 30'000;
            link.inject(f);
            auto pending = std::async(std::launch::async, [&] {
                return host.start(s, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence());
            });
            CHECK(link.wait_requests(Message::claim, 1, Clock::now() + 1s));
            // Manual discovery delayed responses are released by explicit deterministic clock advancement.
            link.advance(30'000);
            f.delay_us = 0;
            link.inject(f);
            link.advance(60'000);
            auto a = pending.get();
            CHECK(a.execution && link.metrics().runs == 1);
            CHECK(host.stop(Clock::now() + 100ms));
        }
        {
            Link link(true);
            uint64_t id = 100;
            Host host(link, host_selection(), [&] { return ++id; });
            auto s = host.discover(Clock::now() + 1s);
            auto a = host.start(s, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence());
            Frame stale;
            stale.kind = Class::event;
            stale.session = a.session + 1;
            stale.message = 0x8001;
            stale.payload.size = 58;
            put(stale.payload, 0, 8, a.boot);
            put(stale.payload, 8, 8, stale.session);
            put(stale.payload, 16, 8, a.execution + 1);
            put(stale.payload, 24, 4, a.arm);
            stale.payload.bytes[28] = 1;
            stale.payload.bytes[35] = 1;
            stale.payload.bytes[36] = 1;
            stale.payload.bytes[49] = 1;
            put(stale.payload, 54, 4, 1);
            link.inject_event(stale);
            CHECK(!host.terminal(Clock::now() + 100ms));
            auto malformed = stale;
            malformed.session = a.session;
            put(malformed.payload, 8, 8, a.session);
            put(malformed.payload, 16, 8, a.execution);
            malformed.payload.bytes[35] = 3;
            link.inject_event(malformed);
            failure([&] { host.terminal(Clock::now() + 100ms); }, Result::invalid_argument);
            CHECK(host.stop(Clock::now() + 100ms));
        }
        {
            Link link(true);
            uint64_t id = 100;
            Host host(link, host_selection(), [&] { return ++id; });
            auto s = host.discover(Clock::now() + 1s);
            Injection f;
            f.block_message = 0x11;
            link.inject(f);
            auto pending = std::async(std::launch::async, [&] {
                failure(
                    [&] { host.start(s, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence()); },
                    Result::bad_state);
            });
            CHECK(link.wait_requests(Message::arm, 1, Clock::now() + 1s));
            CHECK(host.stop(Clock::now() + 100ms));
            pending.get();
            CHECK(link.metrics().runs == 0);
        }
        {
            Rig r;
            std::array<double, 100> latency{};
            for (auto &value : latency) {
                auto begin = Clock::now();
                tx(r.link, request(Message::status));
                value = std::chrono::duration<double, std::micro>(Clock::now() - begin).count();
            }
            std::sort(latency.begin(), latency.end());
            std::cout << "Framed in-memory GET_STATUS round-trip us: p50=" << latency[49]
                      << " p95=" << latency[94] << " p99=" << latency[98] << " max=" << latency[99]
                      << "; reply/event capacity=8, STOP/HB slots=1\n";
        }
        {
            Link link(true);
            uint64_t identity = 100;
            Host host(link, host_selection(), [&] { return ++identity; });
            auto snapshot = host.discover(Clock::now() + 1s);
            for (unsigned cycle = 0; cycle < 3; ++cycle) {
                host.start(snapshot, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence());
                CHECK(link.wait_requests(Message::keepalive, cycle + 1, Clock::now() + 1s));
                link.advance((cycle + 1) * 200'000);
                CHECK(host.terminal(Clock::now() + 100ms));
                CHECK(host.stop(Clock::now() + 100ms));
                CHECK(host.retire_heartbeat(Clock::now() + 100ms));
                CHECK(host.shutdown(Clock::now())); // Zero-budget close after bounded abort retirement.
            }
            CHECK(link.metrics().runs == 3);
        }
        auto begin = Clock::now();
        for (unsigned i = 0; i < 100; ++i) {
            Link link(true);
            uint64_t id = 100;
            Host host(link, host_selection(), [&] { return ++id; });
            auto s = host.discover(Clock::now() + 1s);
            host.start(s, BenchConfig{}.execution(), true, Clock::now() + 1s, host.fence());
            link.advance(200'000);
            CHECK(host.terminal(Clock::now() + 100ms));
            CHECK(host.stop(Clock::now() + 100ms));
            CHECK(link.metrics().runs == 1 && link.metrics().collector <= 161);
        }
        std::cout << "F2 controller/session/retry/discovery/lease/terminal/priority tests passed; 100 "
                     "complete lifetimes in "
                  << std::chrono::duration<double>(Clock::now() - begin).count() << " s; no image buffers\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
