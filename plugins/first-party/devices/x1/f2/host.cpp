#include "host.hpp"
#include <algorithm>
#include <random>

namespace x1::f2 {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
uint64_t fresh_identity() {
    // Linux libstdc++ random_device uses the OS entropy source, never a clock or PRNG seed.
    std::random_device r;
    if (r.entropy() == 0)
        throw std::runtime_error("Strong session entropy unavailable");
    uint64_t value{};
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        value = (uint64_t(r()) << 32) | r();
        if (value)
            return value;
    }
    throw std::runtime_error("Nonzero session entropy unavailable");
}
namespace {
Result code(const Frame &f) {
    return static_cast<Result>(get(f.payload, 0, 2));
}
} // namespace
Host::Host(Transport &transport, Selection selection, std::function<uint64_t()> identity)
    : transport_(transport), selection_(selection),
      identity_(identity ? std::move(identity) : fresh_identity) {
    if (!selection_.valid())
        throw ProtocolError(Result::invalid_argument, "Invalid explicit controller selection");
    heartbeat_ = std::jthread([this](std::stop_token s) { heartbeat(s); });
}
Host::~Host() {
    {
        std::lock_guard lock(control_);
        heartbeat_.request_stop();
        changed_.notify_all();
    }
    transport_.interrupt();
    if (heartbeat_.joinable())
        heartbeat_.join();
}
bool Host::shutdown(Clock::time_point deadline) {
    stop(Clock::now()); // dispatch, never wait for an acknowledgement already reported by stop/abort
    return retire_heartbeat(deadline);
}
bool Host::retire_heartbeat(Clock::time_point deadline) {
    // Lifecycle serialization never gates STOP dispatch or ordinary byte operations.
    std::unique_lock lifetime(lifetime_, std::defer_lock);
    if (!lifetime.try_lock_until(deadline))
        return false;
    {
        std::lock_guard lock(control_);
        heartbeat_.request_stop();
        changed_.notify_all();
    }
    transport_.interrupt();
    std::unique_lock lock(control_);
    if (!changed_.wait_until(lock, deadline, [&] { return exited_; }))
        return false;
    lock.unlock();
    if (heartbeat_.joinable())
        heartbeat_.join();
    return true;
}
void Host::resume_heartbeat(Clock::time_point deadline) {
    std::unique_lock lifetime(lifetime_, std::defer_lock);
    if (!lifetime.try_lock_until(deadline))
        throw ProtocolError(Result::busy, "Heartbeat lifecycle busy");
    if (heartbeat_.joinable() && heartbeat_.get_stop_token().stop_requested()) {
        std::unique_lock lock(control_);
        if (!changed_.wait_until(lock, deadline, [&] { return exited_; }))
            throw ProtocolError(Result::timeout, "Previous heartbeat still retiring");
        lock.unlock();
        heartbeat_.join();
    }
    if (!heartbeat_.joinable()) {
        std::lock_guard lock(control_);
        exited_ = false;
        heartbeat_ = std::jthread([this](std::stop_token s) { heartbeat(s); });
    }
}
Frame Host::exchange(const Frame &q, Clock::time_point deadline, bool enabling, uint64_t fence) {
    auto wire = encode(q);
    if (!wire)
        throw ProtocolError(Result::invalid_argument, "Invalid host frame");
    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        if (Clock::now() > deadline)
            break;
        // Gate only the bounded byte dispatch, never response waiting. STOP fences under this lock.
        {
            std::lock_guard lock(control_);
            if (enabling && (inhibited_ || fence != fence_))
                throw ProtocolError(Result::bad_state, "Fenced host operation");
            auto submitted = transport_.submit(*wire);
            if (submitted != TransportStatus::ready)
                throw ProtocolError(submitted == TransportStatus::invalid ? Result::invalid_argument
                                                                          : Result::unavailable,
                                    "F2 transport submission failed");
            if (attempt == 0 && q.message == 0x13)
                command_dispatch_ns_ =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
                        .count();
        }
        auto r = transport_.receive(q, std::min(deadline, Clock::now() + retry_interval));
        if (r.status == TransportStatus::interrupted) {
            std::lock_guard lock(control_);
            throw ProtocolError(enabling && (inhibited_ || fence != fence_) ? Result::bad_state
                                                                            : Result::unavailable,
                                "F2 response read interrupted");
        }
        if (r.status != TransportStatus::ready && r.status != TransportStatus::timeout)
            throw ProtocolError(Result::unavailable, "F2 transport response unavailable");
        if (!r)
            continue;
        if (!matches_response(*r, q))
            throw ProtocolError(Result::stale_request, "Unmatched F2 transport response");
        if (!valid_message(*r))
            throw ProtocolError(Result::invalid_argument, "Malformed typed response");
        if (code(*r) != Result::ok)
            throw ProtocolError(code(*r), "Controller rejected transaction",
                                code(*r) != Result::no_session && code(*r) != Result::stale_session &&
                                    code(*r) != Result::stale_request &&
                                    code(*r) != Result::conflicting_duplicate);
        return *r;
    }
    throw ProtocolError(Result::timeout, "Ambiguous F2 transaction deadline");
}
Frame Host::ordinary(Message m, const Payload &p, Clock::time_point deadline, bool enabling) {
    std::unique_lock transaction(ordinary_, std::defer_lock);
    if (!transaction.try_lock_until(deadline))
        throw ProtocolError(Result::busy, "Ordinary transaction already outstanding");
    Association a;
    uint64_t f;
    {
        std::lock_guard lock(control_);
        a = association_;
        f = fence_;
    }
    if (!a.session || !next_id_)
        throw ProtocolError(Result::stale_session, "Ordinary session exhausted");
    auto q = request(m, a.session, next_id_);
    q.payload = p;
    // Ambiguous failure permanently fences this session. Never allocate a fresh RUN to recover it.
    try {
        auto r = exchange(q, deadline, enabling, f);
        next_id_ = next_id_ == UINT32_MAX ? 0 : next_id_ + 1;
        return r;
    } catch (const ProtocolError &e) {
        if (e.consumed)
            next_id_ = next_id_ == UINT32_MAX ? 0 : next_id_ + 1;
        else {
            std::lock_guard lock(control_);
            inhibited_ = true;
            lease_active_ = false;
            ++fence_;
            changed_.notify_all();
        }
        throw;
    }
}
Snapshot Host::discover(Clock::time_point deadline) {
    std::unique_lock transaction(ordinary_, std::defer_lock);
    if (!transaction.try_lock_until(deadline))
        throw ProtocolError(Result::busy, "Ordinary transaction already outstanding");
    for (unsigned attempt = 0; attempt < 3 && Clock::now() <= deadline; ++attempt) {
        try {
            auto hello = exchange(request(Message::hello), deadline);
            Snapshot s;
            s.boot = get(hello.payload, 4, 8);
            s.selection = selection_;
            if (get(hello.payload, 12, 4) != selection_.controller_id)
                throw ProtocolError(Result::unsupported, "Controller identity mismatch");
            s.global = exchange(request(Message::capabilities), deadline);
            s.generation = get(s.global.payload, 40, 8);
            if (s.global.payload.bytes[6] != 1 || s.global.payload.bytes[7] != 0x43)
                throw ProtocolError(Result::unsupported, "Simulation capability mismatch");
            auto query = [&](Message m) {
                auto q = request(m);
                q.payload.bytes[0] = selection_.channel_index;
                put(q.payload, 1, 8, s.generation);
                auto r = exchange(q, deadline);
                if (get(r.payload, 4, 8) != s.generation || get(r.payload, 12, 8) != selection_.channel_uid ||
                    r.payload.bytes[20] != selection_.channel_index)
                    throw ProtocolError(Result::snapshot_changed, "Mixed snapshot");
                return r;
            };
            s.channel = query(Message::channel_capabilities);
            s.uid = get(s.channel.payload, 12, 8);
            s.status = query(Message::channel_status);
            s.calibration_status = query(Message::calibration);
            if (s.status.payload.bytes[21] != 4 || s.status.payload.bytes[22] > 1 ||
                s.status.payload.bytes[23] != 2 || s.calibration_status.payload.bytes[21] != 2 ||
                s.calibration_status.payload.bytes[23] != selection_.calibration_scope ||
                get(s.calibration_status.payload, 32, 4) != selection_.board_revision ||
                !get(s.calibration_status.payload, 36, 4) ||
                get(s.calibration_status.payload, 44, 2) != selection_.calibration_provenance)
                throw ProtocolError(Result::unavailable, "Synthetic readiness/calibration unavailable");
            s.calibration = get(s.calibration_status.payload, 24, 8);
            if (s.channel.payload.bytes[63] != 1 || get(s.channel.payload, 21, 2) != 3 ||
                get(s.status.payload, 30, 2) != 13)
                throw ProtocolError(Result::unsupported, "Unexpected sensor inventory");
            s.sensor = query(Message::sensor);
            if (s.sensor.payload.bytes[29] != 4 || s.sensor.payload.bytes[30] != 1 ||
                s.sensor.payload.bytes[31] != 1 || s.sensor.payload.bytes[32] != 4 ||
                s.sensor.payload.bytes[33] != 1 || get(s.sensor.payload, 46, 4))
                throw ProtocolError(Result::unsupported, "Synthetic register descriptor required");
            auto end = exchange(request(Message::capabilities), deadline);
            auto boot = exchange(request(Message::hello), deadline);
            if (end.payload != s.global.payload || get(boot.payload, 4, 8) != s.boot ||
                get(boot.payload, 12, 4) != get(hello.payload, 12, 4) ||
                get(boot.payload, 16, 4) != get(hello.payload, 16, 4))
                throw ProtocolError(Result::snapshot_changed, "Discovery changed");
            return s;
        } catch (const ProtocolError &e) {
            if (e.result != Result::snapshot_changed)
                throw;
        }
    }
    throw ProtocolError(Result::snapshot_changed, "Complete discovery retry limit");
}
bool configuration_fits(const Snapshot &s, const Selection &selection, const FiniteExecution &c) {
    if (!selection.valid() || !c.valid() || !s.generation || !s.boot || !s.calibration ||
        !valid_message(s.channel) || !valid_message(s.global) || s.uid != selection.channel_uid ||
        s.selection != selection)
        return false;
    auto &p = s.channel.payload;
    auto &g = s.global.payload;
    auto current_min = get(p, 23, 4), current_max = get(p, 27, 4), resolution = get(p, 31, 4);
    return resolution && current_min <= c.current_ua && c.current_ua <= current_max &&
           c.current_ua <= get(g, 8, 4) && (c.current_ua - current_min) % resolution == 0 &&
           c.period_us >= get(p, 35, 4) && c.period_us <= get(p, 39, 4) && c.period_us >= get(g, 12, 4) &&
           c.period_us <= get(g, 16, 4) && c.high_us >= get(p, 43, 4) && c.high_us <= get(p, 47, 4) &&
           c.high_us >= get(g, 20, 4) && c.high_us <= get(g, 24, 4) &&
           c.period_us - c.high_us >= get(p, 51, 4) && c.period_us - c.high_us >= get(g, 28, 4) &&
           c.pulses <= get(p, 55, 4) && c.pulses <= get(g, 32, 4) && c.high_us <= get(p, 59, 4) &&
           c.duration_us() <= get(g, 36, 4) * 1'000;
}
Association Host::start(const Snapshot &s, const FiniteExecution &config, bool on, Clock::time_point deadline,
                        uint64_t expected_fence) {
    resume_heartbeat(deadline);
    if (!configuration_fits(s, selection_, config))
        throw ProtocolError(Result::limit_exceeded, "Invalid finite configuration");
    auto fresh = discover(deadline);
    if (fresh.boot != s.boot || fresh.generation != s.generation || fresh.calibration != s.calibration)
        throw ProtocolError(Result::snapshot_changed, "Prepared snapshot changed");
    uint64_t f;
    auto session = identity_();
    {
        std::lock_guard lock(control_);
        if (fence_ != expected_fence)
            throw ProtocolError(Result::bad_state, "Start fenced during discovery");
        ++fence_;
        f = fence_;
        inhibited_ = false;
        if (!session || session == association_.session)
            throw ProtocolError(Result::invalid_argument, "Fresh nonzero session required");
        association_ = {s.boot, session, 0, 0};
        counter_ = 0;
    }
    if (!association().session)
        throw ProtocolError(Result::invalid_argument, "Zero session identity");
    {
        std::unique_lock transaction(ordinary_, std::defer_lock);
        if (!transaction.try_lock_until(deadline))
            throw ProtocolError(Result::busy, "Ordinary transaction already outstanding");
        auto q = request(Message::claim, association().session, 1);
        put(q.payload, 0, 8, s.boot);
        exchange(q, deadline, true, f);
        next_id_ = 2;
    }
    if (!on) {
        if (!stop(deadline))
            throw ProtocolError(Result::timeout, "OFF STOP acknowledgement unavailable");
        return association();
    }
    auto p = request(Message::configure).payload;
    p.bytes[0] = selection_.channel_index;
    put(p, 1, 4, config.current_ua);
    put(p, 5, 4, config.period_us);
    put(p, 9, 4, config.high_us);
    put(p, 13, 8, s.calibration);
    put(p, 21, 8, s.generation);
    ordinary(Message::configure, p, deadline, true);
    p = request(Message::arm).payload;
    put(p, 0, 8, identity_());
    auto armed = ordinary(Message::arm, p, deadline, true);
    auto execution = identity_();
    {
        std::lock_guard lock(control_);
        if (f != fence_ || inhibited_)
            throw ProtocolError(Result::bad_state, "ARM reply fenced");
        association_.arm = static_cast<uint32_t>(get(armed.payload, 4, 4));
        lease_active_ = true;
        association_.execution = execution;
    }
    changed_.notify_all();
    auto a = association();
    p = request(Message::run).payload;
    put(p, 0, 4, a.arm);
    put(p, 4, 8, a.execution);
    put(p, 12, 4, config.pulses);
    auto admitted = ordinary(Message::run, p, deadline, true);
    if (get(admitted.payload, 4, 8) != a.execution || get(admitted.payload, 12, 4) != config.pulses ||
        admitted.payload.bytes[2] != 0)
        throw ProtocolError(Result::invalid_argument, "Mismatched RUN admission");
    return a;
}
Association Host::association() {
    std::lock_guard lock(control_);
    return association_;
}
void Host::heartbeat(std::stop_token token) {
    std::unique_lock lock(control_);
    while (!token.stop_requested()) {
        changed_.wait(lock, [&] { return token.stop_requested() || (!inhibited_ && lease_active_); });
        if (token.stop_requested())
            break;
        if (changed_.wait_for(lock, 100ms,
                              [&] { return token.stop_requested() || inhibited_ || !lease_active_; }))
            continue;
        lock.unlock();
        service_heartbeat();
        lock.lock();
    }
    exited_ = true;
    changed_.notify_all();
}
void Host::service_heartbeat() {
    std::unique_lock heartbeat(heartbeat_gate_, std::try_to_lock);
    if (!heartbeat.owns_lock())
        return;
    Association a;
    uint64_t f;
    uint32_t counter;
    {
        std::lock_guard lock(control_);
        if (inhibited_ || !lease_active_ || !association_.arm || counter_ == UINT32_MAX)
            return;
        a = association_;
        f = fence_;
        counter = ++counter_;
    }
    try {
        auto q = request(Message::keepalive, a.session);
        put(q.payload, 0, 4, a.arm);
        put(q.payload, 4, 4, counter);
        exchange(q, Clock::now() + 50ms, true, f);
    } catch (...) { /* Lease is never inferred from an unavailable response. */
    }
}
std::optional<Payload> Host::terminal(Clock::time_point deadline) {
    auto a = association();
    auto f = fence();
    if (!a.execution)
        return {};
    auto matches = [&](const Payload &p) {
        return valid_terminal(p.data()) && get(p, 0, 8) == a.boot && get(p, 8, 8) == a.session &&
               get(p, 16, 8) == a.execution && get(p, 24, 4) == a.arm;
    };
    auto event = transport_.event(std::min(deadline, Clock::now() + 5ms));
    if (event.status == TransportStatus::interrupted)
        return {};
    if (event.status != TransportStatus::ready && event.status != TransportStatus::timeout)
        throw ProtocolError(Result::unavailable, "F2 event transport unavailable");
    if (event) {
        if (f != fence())
            return {};
        if (event->message == 0x8001 && !valid_message(*event))
            throw ProtocolError(Result::invalid_argument, "Malformed terminal event");
        if (event->message == 0x8001 && valid_message(*event) && matches(event->payload)) {
            std::lock_guard lock(control_);
            lease_active_ = false;
            changed_.notify_all();
            return event->payload;
        }
    }
    if (f != fence() || Clock::now() + 1ms >= deadline)
        return {};
    // Bounded read-only reconciliation; never submit another RUN_FINITE.
    Frame status;
    bool unbound = false;
    try {
        status = ordinary(Message::status, {}, deadline);
    } catch (const ProtocolError &e) {
        if (e.result != Result::no_session && e.result != Result::stale_session)
            throw;
        unbound = true;
        status = exchange(request(Message::status), deadline);
    }
    if (get(status.payload, 4, 8) != a.boot)
        throw ProtocolError(Result::stale_boot, "Controller reboot erased execution");
    if (!(status.payload.bytes[36] & 1))
        return {};
    auto p = request(Message::terminal).payload;
    put(p, 0, 8, a.boot);
    put(p, 8, 8, a.session);
    put(p, 16, 8, a.execution);
    Frame result;
    if (unbound) {
        auto q = request(Message::terminal);
        q.payload = p;
        result = exchange(q, deadline);
    } else
        result = ordinary(Message::terminal, p, deadline);
    Payload record;
    record.size = 58;
    std::copy_n(result.payload.bytes.begin() + 4, 58, record.bytes.begin());
    if (!matches(record))
        throw ProtocolError(Result::invalid_argument, "Mismatched terminal record");
    {
        std::lock_guard lock(control_);
        lease_active_ = false;
        changed_.notify_all();
    }
    return record;
}
bool Host::stop(Clock::time_point deadline) {
    {
        std::lock_guard lock(control_);
        inhibited_ = true;
        lease_active_ = false;
        ++fence_;
        changed_.notify_all();
    }
    // No ordinary lock, no event slot, no heartbeat backlog. Dedicated response slot.
    auto q = request(Message::stop);
    auto wire = encode(q);
    if (transport_.submit(*wire) != TransportStatus::ready) {
        std::lock_guard lock(control_);
        stop_error_ = Result::unavailable;
        return false;
    }
    {
        std::lock_guard lock(control_);
        stop_dispatch_ns_ =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
    }
    transport_.interrupt();
    auto r = transport_.receive(q, deadline);
    auto error = r.status != TransportStatus::ready && r.status != TransportStatus::timeout
                     ? Result::unavailable
                 : !r                                             ? Result::timeout
                 : !matches_response(*r, q) || !valid_message(*r) ? Result::invalid_argument
                                                                  : code(*r);
    {
        std::lock_guard lock(control_);
        stop_error_ = error;
    }
    return error == Result::ok && r->payload.bytes[4] == 2;
}
} // namespace x1::f2
