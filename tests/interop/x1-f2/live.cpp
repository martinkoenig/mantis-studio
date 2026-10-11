#include "../../fixtures/x1-f2/golden.hpp"
#include "firmware-transport.hpp"
#include <fstream>
#include <future>
#include <sstream>
using namespace interop;
namespace {
struct Reference {
    std::string_view name;
    Wire request, response;
};
std::array<Reference, 128> corpus{};
size_t references{};
void capture(std::string_view name, const FirmwareTransport::Exchange &e) {
    CHECK(references < corpus.size());
    corpus[references++] = {name, e.request, e.response};
}
Frame tx(FirmwareTransport &t, const Frame &q, Result expected = Result::ok, std::string_view label = {}) {
    auto w = encode(q);
    CHECK(w && t.submit(*w) == TransportStatus::ready);
    auto r = t.receive(q, deadline());
    CHECK(r && matches_response(*r, q));
    if (get(r->payload, 0, 2) != static_cast<uint16_t>(expected))
        throw std::runtime_error("Unexpected firmware result; REQUEST=" + hex(*w) +
                                 " RESPONSE=" + hex(*encode(*r)));
    CHECK(valid_message(*r) ||
          (!request_layout(q.message) && r->payload.size == 4 && r->payload.bytes[2] == 2));
    if (!label.empty())
        capture(label, t.exchange(t.trace_count() - 1));
    return *r;
}
struct Rig {
    FirmwareTransport t;
    uint32_t next = 2, arm{};
    void claim() {
        auto q = request(Message::claim, session, 1);
        put(q.payload, 0, 8, boot);
        tx(t, q, Result::ok, "claim");
    }
    Frame configure(uint64_t generation = 2, uint64_t credential = token) {
        auto q = request(Message::configure, session, next++);
        put(q.payload, 1, 4, 500);
        put(q.payload, 5, 4, 10'000);
        put(q.payload, 9, 4, 100);
        put(q.payload, 13, 8, credential);
        put(q.payload, 21, 8, generation);
        return q;
    }
    void ready() {
        claim();
        tx(t, configure(), Result::ok, "configure");
        auto q = request(Message::arm, session, next++);
        put(q.payload, 0, 8, 777);
        arm = static_cast<uint32_t>(get(tx(t, q, Result::ok, "arm").payload, 4, 4));
    }
    Frame run(uint32_t count = 20) {
        auto q = request(Message::run, session, next++);
        put(q.payload, 0, 4, arm);
        put(q.payload, 4, 8, 123);
        put(q.payload, 12, 4, count);
        return q;
    }
    Frame heartbeat(uint32_t count, uint64_t s = session, uint32_t a = 0) {
        auto q = request(Message::keepalive, s);
        put(q.payload, 0, 4, a ? a : arm);
        put(q.payload, 4, 4, count);
        return q;
    }
    Frame terminal(bool bound = true) {
        auto q = request(Message::terminal, bound ? session : 0, bound ? next++ : 0);
        put(q.payload, 0, 8, boot);
        put(q.payload, 8, 8, session);
        put(q.payload, 16, 8, 123);
        return q;
    }
};
void discovery() {
    FirmwareTransport t;
    Host h(t, selection());
    auto s = h.discover(deadline());
    CHECK(s.boot == boot && s.generation == 2 && s.calibration == token && s.uid == 1);
    CHECK(s.channel.payload.bytes[63] == 0 && s.sensor.payload.size == 0);
    CHECK(get(s.calibration_status.payload, 44, 2) == 0 && s.status.payload.bytes[21] == 4 &&
          s.status.payload.bytes[22] == 1);
    CHECK(s.global.payload.bytes[7] == 0x43);
    for (size_t i = 0; i < t.trace_count(); ++i)
        capture("discovery", t.exchange(i));
    auto status = tx(t, request(Message::status), Result::ok, "unbound-status");
    CHECK(get(status.payload, 4, 8) == boot && !get(status.payload, 13, 4));
    auto sensor = request(Message::sensor);
    put(sensor.payload, 1, 8, 2);
    tx(t, sensor, Result::unavailable, "unbound-sensor-empty");
    put(sensor.payload, 1, 8, 1);
    tx(t, sensor, Result::snapshot_changed, "unbound-sensor-stale");
    // Exact provenance remains mandatory; zero is not a wildcard.
    auto wrong = selection();
    wrong.calibration_provenance = 1;
    Host rejected(t, wrong);
    error([&] { rejected.discover(deadline()); }, Result::unavailable);
    fw::Frame parsed;
    auto hello = t.exchange(0);
    CHECK(fw::decode_frame(fw::Bytes(hello.response.bytes.data(), hello.response.size), parsed) ==
          fw::Error::None);
    fw::HelloResponse typed;
    CHECK(fw::decode(parsed.view().payload, typed) == fw::CodecError::None);
    CHECK(typed.boot_token == s.boot && typed.controller_id == selection().controller_id);
}
void sessions_and_erratum() {
    for (auto op : {uint16_t(0), uint16_t(0x1234), uint16_t(0x8001), uint16_t(0x8002), uint16_t(0x8003)}) {
        Rig r;
        for (auto size : {size_t(0), max_payload}) {
            auto before = r.t.facts();
            tx(r.t, opaque(op, 0, 0, size), Result::unsupported, "unbound-unknown");
            CHECK(r.t.facts() == before);
        }
        r.ready();
        auto run = r.run();
        tx(r.t, run, Result::ok, "run");
        for (auto q : {opaque(op), opaque(op, 0, 2), opaque(op, session + 1, 0), opaque(op, session, 0),
                       opaque(op, session, 99)}) {
            auto before = r.t.facts();
            auto result = !q.session && !q.request ? Result::unsupported
                          : q.session != session   ? Result::stale_session
                                                   : Result::stale_request;
            tx(r.t, q, result, "unknown-admission");
            CHECK(r.t.facts() == before);
        }
        auto q = opaque(op, session, r.next++, 132);
        auto first = tx(r.t, q, Result::unsupported, "bound-unknown");
        CHECK(r.t.facts().next == r.next && r.t.exchange(r.t.trace_count() - 1).consumed);
        r.t.advance(200);
        CHECK(r.t.facts().state == fw::ControllerState::Inhibited);
        auto replay = tx(r.t, q, Result::unsupported, "unknown-replay");
        CHECK(replay == first && r.t.exchange(r.t.trace_count() - 1).replay);
        auto conflict = q;
        conflict.payload.bytes[131] ^= 1;
        auto before = r.t.facts();
        tx(r.t, conflict, Result::conflicting_duplicate, "unknown-conflict");
        CHECK(r.t.facts() == before);
        CHECK(tx(r.t, q, Result::unsupported) == first);
        Host h(r.t, selection());
        error([&] { HostConformanceAccess::transaction(h, q, deadline()); }, Result::unsupported, true);
        // Known gaps/stale IDs have identical admission precedence.
        tx(r.t, request(Message::status, session, r.next + 1), Result::stale_request, "known-gap");
        tx(r.t, request(Message::status, session + 1, r.next), Result::stale_session, "known-session");
    }
}
void configuration() {
    Rig r;
    r.claim();
    auto before = r.t.facts();
    tx(r.t, r.configure(1), Result::snapshot_changed, "configure-stale");
    CHECK(r.t.facts().config == 0 && r.t.facts().snapshot == before.snapshot);
    tx(r.t, r.configure(2, 0), Result::invalid_argument, "configure-missing-token");
    tx(r.t, r.configure(2, 1234), Result::calibration_invalid, "configure-stale-token");
    auto bad = r.configure();
    put(bad.payload, 9, 4, 10'000);
    tx(r.t, bad, Result::invalid_argument, "configure-high-equals-period");
    tx(r.t, r.configure(), Result::ok, "configure-after-rejections");
    CHECK(r.t.facts().config == 1 && r.t.facts().snapshot == 2);
    r.t.invalidate_snapshot();
    auto arm = request(Message::arm, session, r.next++);
    put(arm.payload, 0, 8, 555);
    tx(r.t, arm, Result::snapshot_changed, "arm-stale-basis");
    CHECK(!r.t.facts().arm && !r.t.facts().executions);
}
void finite_and_recovery() {
    Rig r;
    r.ready();
    auto q = r.run();
    auto first = tx(r.t, q, Result::ok, "finite-run");
    r.t.advance(100);
    CHECK(r.t.facts().execution == 123 && r.t.facts().executions == 1);
    CHECK(tx(r.t, q) == first && r.t.facts().lease == 400); // Cached RUN does not refresh lease.
    r.t.advance(200);
    auto e = r.t.event(deadline());
    CHECK(e && e->message == 0x8001 && valid_message(*e));
    CHECK(r.t.event_wire() && *r.t.event_wire() == *encode(*e));
    capture("terminal-event", {{}, *r.t.event_wire(), false, false});
    auto result = tx(r.t, r.terminal(), Result::ok, "terminal-bound");
    CHECK(std::equal(e->payload.data().begin(), e->payload.data().end(), result.payload.bytes.begin() + 4));
    CHECK(get(e->payload, 0, 8) == boot && get(e->payload, 8, 8) == session && get(e->payload, 16, 8) == 123);
    CHECK(e->payload.bytes[28] == 1 && get(e->payload, 29, 2) == 0 && e->payload.bytes[35] == 1);
    CHECK(get(e->payload, 31, 4) == 0 && e->payload.bytes[49] == 2); // No observed pulse-count evidence.
    tx(r.t, r.terminal(false), Result::busy, "terminal-unbound-active-origin");
    auto claim = request(Message::claim, session + 1, 1);
    put(claim.payload, 0, 8, boot);
    tx(r.t, claim);
    tx(r.t, r.terminal(false), Result::ok, "terminal-historical");
    // Actual Host retries a lost firmware ACK without a new execution identity.
    for (bool lost_event : {false, true}) {
        FirmwareTransport t;
        Host h(t, selection(), [id = uint64_t(100)]() mutable { return ++id; });
        auto s = h.discover(deadline());
        t.inject({0, 0x13, 1, lost_event, 0});
        auto a = h.start(s, execution(), true, deadline(), h.fence());
        CHECK(t.facts().executions == 1 && t.facts().execution == a.execution);
        CHECK(h.retire_heartbeat(deadline()));
        std::array<Wire, 3> sent{};
        size_t runs = 0;
        for (size_t i = 0; i < t.submission_count(); ++i) {
            auto w = t.submission(i);
            auto f = decode(w.data().subspan(1, w.size - 2));
            if (f && f->message == 0x13) {
                CHECK(runs < sent.size());
                sent[runs++] = w;
            }
        }
        CHECK(runs == 2 && sent[0] == sent[1]);
        t.advance(200);
        auto terminal = h.terminal(deadline());
        CHECK(terminal && terminal->bytes[28] == 1);
        CHECK(get(*terminal, 16, 8) == a.execution && get(*terminal, 24, 4) == a.arm &&
              terminal->bytes[35] == 1);
        CHECK(t.facts().executions == 1 && h.stop(deadline()));
    }
    // OFF-only still uses real firmware STOP/DISARM, never RUN.
    FirmwareTransport t;
    Host h(t, selection(), [id = uint64_t(200)]() mutable { return ++id; });
    auto s = h.discover(deadline());
    h.start(s, execution(), false, deadline(), h.fence());
    CHECK(!t.facts().executions && h.stop(deadline()));
}
void priority_and_lease() {
    // Explicit firmware batch: STOP fences ordinary RUN regardless of input order.
    Rig r;
    r.ready();
    auto run = r.run();
    auto stop = request(Message::stop);
    auto heartbeat = r.heartbeat(1);
    std::array<Wire, 3> batch{*encode(run), *encode(heartbeat), *encode(stop)};
    CHECK(r.t.batch(batch) == TransportStatus::ready);
    auto rejected = r.t.receive(run, deadline());
    auto off = r.t.receive(stop, deadline());
    CHECK(rejected && get(rejected->payload, 0, 2) == 3 && off && !r.t.facts().executions &&
          !r.t.facts().arm);
    // A blocked ordinary Host receive cannot own the control dispatch lock.
    FirmwareTransport t;
    Host h(t, selection(), [id = uint64_t(300)]() mutable { return ++id; });
    auto s = h.discover(deadline());
    t.inject({0x13});
    auto pending = std::async(std::launch::async, [&] {
        error([&] { h.start(s, execution(100), true, deadline(), h.fence()); }, Result::bad_state);
    });
    CHECK(t.wait_receive(0x13, 1, deadline()));
    CHECK(t.facts().executions == 1);
    t.advance(100);
    auto a = h.association();
    CHECK(a.arm && a.session);
    h.service_heartbeat();
    CHECK(t.facts().heartbeat == 1 && t.facts().lease == 500);
    // Unknown traffic saturates the ordinary queue without touching heartbeat/STOP slots.
    std::array<Wire, 8> flood;
    flood.fill(*encode(opaque(0x1234)));
    CHECK(t.batch(flood) == TransportStatus::ready); // Atomic publication of all eight replies.
    for (const auto &ref : corpus)
        if (ref.name == "terminal-event")
            for (unsigned i = 0; i < 12; ++i)
                t.inject_bytes(ref.response);
    CHECK(t.high_water().second == 8);
    auto start = std::chrono::steady_clock::now();
    CHECK(h.stop(deadline()));
    CHECK(std::chrono::steady_clock::now() - start < 500ms);
    pending.get();
    CHECK(h.retire_heartbeat(deadline()));
    CHECK(t.facts().state == fw::ControllerState::Inhibited && !t.facts().execution &&
          t.facts().executions == 1);
    CHECK(t.high_water().first == 8);
    // RUN's ACK was never established: Host preserves its fenced uncertainty,
    // rather than guessing a fresh request ID or manufacturing terminal evidence.
    error([&] { h.terminal(deadline()); }, Result::conflicting_duplicate);
    auto lookup = request(Message::terminal, a.session, 5);
    put(lookup.payload, 0, 8, a.boot);
    put(lookup.payload, 8, 8, a.session);
    put(lookup.payload, 16, 8, a.execution);
    auto result = tx(t, lookup);
    CHECK(result.payload.bytes[32] == 2 && result.payload.bytes[53] == 2);
    // Heartbeat and arbitrary traffic cannot revive an expired authority.
    Rig lease;
    lease.ready();
    tx(lease.t, lease.run(100));
    lease.t.advance(100);
    tx(lease.t, lease.heartbeat(1));
    CHECK(lease.t.facts().lease == 500);
    lease.t.advance(200);
    auto before = lease.t.facts();
    tx(lease.t, lease.heartbeat(1), Result::stale_request);
    CHECK(lease.t.facts() == before);
    tx(lease.t, opaque(0x8001), Result::unsupported);
    CHECK(lease.t.facts() == before);
    lease.t.advance(600);
    CHECK(!lease.t.facts().session && !lease.t.facts().arm && !lease.t.facts().execution);
    auto terminal = tx(lease.t, lease.terminal(false), Result::ok, "lease-expired-terminal");
    CHECK(terminal.payload.bytes[32] == 3 && get(terminal.payload, 33, 2) == 10);
    tx(lease.t, lease.heartbeat(2), Result::no_session, "heartbeat-after-expiry");
}
Wire changed_envelope(const Wire &w, size_t offset, uint8_t value) {
    std::array<uint8_t, 160> raw{};
    size_t n = 0;
    CHECK(fw::cobs_decode(fw::Bytes(w.bytes.data() + 1, w.size - 2), fw::Buffer(raw.data(), raw.size()), n) ==
          fw::Error::None);
    raw[offset] = value;
    uint32_t crc = 0;
    CHECK(fw::crc32c(fw::Bytes(raw.data(), n - 4), crc) == fw::Error::None);
    for (unsigned i = 0; i < 4; ++i)
        raw[n - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    Wire out;
    size_t count = 0;
    CHECK(fw::cobs_encode(fw::Bytes(raw.data(), n), fw::Buffer(out.bytes.data() + 1, max_wire - 2), count) ==
          fw::Error::None);
    out.size = count + 2;
    out.bytes[out.size - 1] = 0;
    return out;
}
void framing_and_strictness() {
    const std::array<uint8_t, 9> crc_input{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint32_t crc_value = 0;
    CHECK(fw::crc32c(fw::Bytes(crc_input.data(), crc_input.size()), crc_value) == fw::Error::None);
    CHECK(crc_value == 0xe3069283 && crc32c(crc_input) == crc_value);
    for (auto literal : f2_golden) {
        auto original = unhex(literal);
        fw::Frame parsed;
        CHECK(fw::decode_frame(fw::Bytes(original.bytes.data(), original.size), parsed) == fw::Error::None);
        Wire encoded;
        CHECK(fw::encode_frame(parsed.view(), fw::Buffer(encoded.bytes.data(), max_wire), encoded.size) ==
              fw::Error::None);
        CHECK(encoded == original && decode(original.data().subspan(1, original.size - 2)));
    }

    FirmwareTransport t;
    auto q = opaque(0x1234, 0, 0, 132);
    auto w = *encode(q);
    CHECK(w.size == max_wire);
    fw::Frame f;
    CHECK(fw::decode_frame(fw::Bytes(w.bytes.data(), w.size), f) == fw::Error::None);
    Wire roundtrip;
    CHECK(fw::encode_frame(f.view(), fw::Buffer(roundtrip.bytes.data(), max_wire), roundtrip.size) ==
          fw::Error::None);
    CHECK(roundtrip == w);
    for (auto b : w.data()) {
        Wire part;
        part.size = 1;
        part.bytes[0] = b;
        CHECK(t.submit(part) == TransportStatus::ready);
    }
    CHECK(t.receive(q, deadline()));
    auto before = t.facts();
    auto calls = t.dispatched();
    for (auto [offset, value] : std::array<std::pair<size_t, uint8_t>, 8>{
             {{0, 0x51}, {2, 2}, {3, 1}, {4, 23}, {5, 4}, {6, 1}, {7, 1}, {22, 131}}})
        CHECK(t.submit(changed_envelope(w, offset, value)) == TransportStatus::ready);
    auto crc = w;
    crc.bytes[crc.size - 3] ^= 1;
    CHECK(t.submit(crc) == TransportStatus::ready);
    auto cobs = w;
    cobs.bytes[1] = 255;
    CHECK(t.submit(cobs) == TransportStatus::ready);
    for (auto kind : {Class::event, Class::response}) {
        auto wrong = q;
        wrong.kind = kind;
        CHECK(t.submit(*encode(wrong)) == TransportStatus::ready);
    }
    CHECK(t.facts() == before && t.dispatched() == calls && !t.receive(q, std::chrono::steady_clock::now()));
    CHECK(t.rejected() >= 10);
    Wire partial = w;
    partial.size = 5;
    CHECK(t.submit(partial) == TransportStatus::ready);
    t.advance(51);
    CHECK(t.submit(w) == TransportStatus::ready && t.receive(q, deadline()));
    std::array<Wire, 9> overflow{};
    overflow.fill(w);
    CHECK(t.batch(overflow) == TransportStatus::invalid);
    CHECK(t.high_water().first <= 8 && t.high_water().second <= 8);
    // Malformed known replies originate in firmware, then link corruption is injected.
    for (unsigned mutation : {1u, 2u, 3u, 4u}) {
        FirmwareTransport peer;
        Host h(peer, selection());
        peer.inject({0, 0, 0, false, mutation});
        error([&] { h.discover(std::chrono::steady_clock::now() + 100ms); },
              mutation == 4 ? Result::timeout : Result::invalid_argument);
    }
    for (unsigned mutation : {1u, 2u, 3u, 4u, 5u, 6u}) {
        FirmwareTransport real;
        Host host(real, selection());
        real.inject({0, 0, 0, false, mutation});
        auto sensor = request(Message::sensor);
        put(sensor.payload, 1, 8, 2);
        error([&] { HostConformanceAccess::transaction(host, sensor, deadline()); },
              mutation == 4 ? Result::timeout : Result::invalid_argument);
    }
    for (unsigned mutation : {4u, 7u, 8u, 9u}) {
        FirmwareTransport real;
        Host host(real, selection());
        real.inject({0, 0, 0, false, mutation});
        error([&] { HostConformanceAccess::transaction(host, opaque(0x8001), deadline()); },
              mutation == 7 ? Result::invalid_argument : Result::timeout);
    }
    FirmwareTransport bounded;
    for (unsigned i = 0; i < 256; ++i)
        CHECK(bounded.submit(*encode(opaque(0x1234))) == TransportStatus::ready);
    auto facts = bounded.facts();
    auto missing = opaque(0x4321);
    CHECK(bounded.submit(*encode(missing)) == TransportStatus::ready);
    CHECK(!bounded.receive(missing, std::chrono::steady_clock::now()));
    CHECK(bounded.diagnostic_drops() >= 2 && bounded.facts() == facts && bounded.high_water().first == 8);
    tx(bounded, request(Message::stop)); // Diagnostic and ordinary saturation cannot gate STOP.
    // Real unknown rejections cannot satisfy a different outstanding transaction.
    FirmwareTransport peer;
    Host h(peer, selection());
    peer.inject({0, 0, 0, false, 4});
    error([&] { HostConformanceAccess::transaction(h, opaque(0x8001), deadline()); }, Result::timeout);
    // Interrupt/close wake readers through explicit transport results.
    FirmwareTransport interrupted;
    interrupted.inject({0x1234});
    CHECK(interrupted.submit(*encode(opaque(0x1234))) == TransportStatus::ready);
    auto pending =
        std::async(std::launch::async, [&] { return interrupted.receive(opaque(0x1234), deadline()); });
    CHECK(interrupted.wait_receive(0x1234, 1, deadline()));
    interrupted.interrupt();
    CHECK(pending.get().status == TransportStatus::interrupted);
    auto closed =
        std::async(std::launch::async, [&] { return interrupted.receive(opaque(0x1234), deadline()); });
    CHECK(interrupted.wait_receive(0x1234, 2, deadline()));
    interrupted.close();
    CHECK(closed.get().status == TransportStatus::closed && interrupted.submit(w) == TransportStatus::closed);
}
void reboot() {
    FirmwareTransport t;
    Host h(t, selection(), [id = uint64_t(400)]() mutable { return ++id; });
    auto s = h.discover(deadline());
    auto a = h.start(s, execution(), true, deadline(), h.fence());
    CHECK(h.retire_heartbeat(deadline()));
    t.reboot(boot + 1);
    error([&] { h.terminal(deadline()); }, Result::stale_boot);
    CHECK(!t.facts().session && !t.facts().execution && !t.facts().arm);
    tx(t, opaque(0x1234, a.session, 5), Result::no_session, "reboot-old-session");
    auto claim = request(Message::claim, a.session + 1, 1);
    put(claim.payload, 0, 8, boot);
    tx(t, claim, Result::stale_boot, "reboot-old-boot");
    auto terminal = request(Message::terminal);
    put(terminal.payload, 0, 8, boot);
    put(terminal.payload, 8, 8, a.session);
    put(terminal.payload, 16, 8, a.execution);
    tx(t, terminal, Result::unavailable, "reboot-erased-terminal");
}
std::string fixture_text() {
    std::ostringstream out;
    out << "// Generated test data, not firmware implementation. Regenerate with the live target.\n"
        << "// Exact accepted firmware source: " << firmware_sha << "\n"
        << "// clang-format off\n"
        << "inline constexpr std::string_view reference_sha = \"" << firmware_sha << "\";\n"
        << "inline constexpr std::array<Reference, " << references << "> reference{{\n";
    for (size_t i = 0; i < references; ++i)
        out << "    {\"" << corpus[i].name << "\", \"" << hex(corpus[i].request) << "\", \""
            << hex(corpus[i].response) << "\"},\n";
    out << "}};\n// clang-format on\n";
    return out.str();
}
} // namespace
int main(int argc, char **argv) {
    try {
        discovery();
        sessions_and_erratum();
        configuration();
        finite_and_recovery();
        priority_and_lease();
        framing_and_strictness();
        reboot();
        if (argc == 3 && std::string_view(argv[1]) == "--write-fixtures") {
            std::ofstream out(argv[2]);
            CHECK(out);
            out << fixture_text();
            CHECK(out);
        } else if (argc == 3 && std::string_view(argv[1]) == "--check-fixtures") {
            std::ifstream in(argv[2]);
            CHECK(in);
            std::ostringstream s;
            s << in.rdbuf();
            CHECK(s.str() == fixture_text());
        } else
            CHECK(argc == 1);
        std::cout << "Live firmware " << firmware_sha << ": T01-T13 PASS; " << references
                  << " reference exchanges; transport bytes=" << sizeof(FirmwareTransport) << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
