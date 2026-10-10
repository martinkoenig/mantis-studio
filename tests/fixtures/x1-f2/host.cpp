#include "host.hpp"
#include <random>

namespace x1::f2::simulation {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
uint64_t Link::now_locked() const {
    return manual_
               ? manual_now_
               : static_cast<uint64_t>(
                     std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - epoch_).count());
}
void Link::deliver_locked(const Frame &f) {
    auto wire = encode(f);
    if (!wire)
        throw std::logic_error("Simulator invalid frame");
    if (injection_.corrupt && f.kind == Class::response)
        wire->bytes[wire->size - 3] ^= 0x80;
    for (auto byte : wire->data())
        if (auto decoded = receiver_.feed(byte, now_locked())) {
            if (decoded->kind == Class::event) {
                if (!injection_.lose_terminal || decoded->message != 0x8001)
                    events_.push(*decoded);
            } else if (decoded->message == 0x14)
                stop_reply_ = *decoded;
            else if (decoded->message == 0x12)
                heartbeat_reply_ = *decoded;
            else
                replies_.push(*decoded);
        }
    wake_.notify_all();
}
void Link::progress_locked() {
    auto now = now_locked();
    controller_.advance(now);
    if (delayed_ && now >= delayed_at_) {
        auto f = *delayed_;
        delayed_.reset();
        deliver_locked(f);
    }
    if (delayed_event_ && now >= delayed_event_at_) {
        auto f = *delayed_event_;
        delayed_event_.reset();
        deliver_locked(f);
    }
    while (auto f = controller_.event()) {
        if (injection_.terminal_delay_us && f->message == 0x8001) {
            delayed_event_ = *f;
            delayed_event_at_ = now + injection_.terminal_delay_us;
        } else
            deliver_locked(*f);
    }
}
bool Link::send(const Wire &wire) {
    if (wire.size > max_wire)
        return false;
    std::lock_guard lock(mutex_);
    if (injection_.fail_link)
        return false;
    progress_locked();
    for (auto byte : wire.data())
        if (auto f = endpoint_.feed(byte, now_locked())) {
            auto found = layout(f->message);
            if (found)
                ++requests_[static_cast<size_t>(found - registry.data())];
            wake_.notify_all();
            auto response = controller_.receive(*f, now_locked());
            if (injection_.snapshot_after_message == f->message) {
                controller_.change_snapshot();
                injection_.snapshot_after_message = 0;
            }
            if (injection_.reboot_after_message == f->message) {
                controller_.reboot(0x8266000000000002);
                injection_.reboot_after_message = 0;
            }
            if (!response)
                continue;
            if (injection_.block_message == f->message || (f->message == 0x14 && injection_.fail_stop))
                continue;
            if (injection_.drop_message == f->message && injection_.drops) {
                --injection_.drops;
                continue;
            }
            if (injection_.delay_us && f->request && f->message != 0x14) {
                if (!delayed_) {
                    delayed_ = *response;
                    delayed_at_ = now_locked() + injection_.delay_us;
                }
            } else {
                deliver_locked(*response);
                if (injection_.duplicate && f->message != 0x14 && f->message != 0x12)
                    deliver_locked(*response);
            }
        }
    progress_locked();
    return true;
}
std::optional<Frame> Link::receive(const Frame &q, Clock::time_point deadline) {
    std::unique_lock lock(mutex_);
    auto match = [&](const Frame &f) {
        return f.kind == Class::response && f.message == q.message && f.request == q.request &&
               f.session == q.session;
    };
    do {
        progress_locked();
        if (q.message == 0x14 || q.message == 0x12) {
            auto &slot = q.message == 0x14 ? stop_reply_ : heartbeat_reply_;
            if (slot) {
                auto f = *slot;
                slot.reset();
                if (match(f))
                    return f;
            }
        } else
            while (auto f = replies_.pop())
                if (match(*f))
                    return f;
        if (Clock::now() >= deadline || injection_.fail_link)
            return {};
        auto until = deadline;
        if (!manual_) {
            auto due = std::min(controller_.next_deadline(), delayed_ ? delayed_at_ : UINT64_MAX);
            if (due != UINT64_MAX)
                until = std::min(until, epoch_ + std::chrono::microseconds(due));
        }
        wake_.wait_until(lock, until);
    } while (true);
}
std::optional<Frame> Link::event(Clock::time_point deadline) {
    std::unique_lock lock(mutex_);
    auto interrupted = interrupts_;
    ++event_waits_;
    wake_.notify_all();
    do {
        progress_locked();
        if (interrupted != interrupts_)
            return {};
        if (!injection_.block_next)
            if (auto f = events_.pop())
                return f;
        if (Clock::now() >= deadline || injection_.fail_link)
            return {};
        auto until = deadline;
        if (!manual_ && controller_.next_deadline() != UINT64_MAX)
            until = std::min(until, epoch_ + std::chrono::microseconds(controller_.next_deadline()));
        wake_.wait_until(lock, until);
    } while (true);
}
bool Link::wait_requests(Message message, uint64_t count, Clock::time_point deadline) {
    std::unique_lock lock(mutex_);
    auto l = layout(static_cast<uint16_t>(message));
    if (!l)
        return false;
    return wake_.wait_until(lock, deadline,
                            [&] { return requests_[static_cast<size_t>(l - registry.data())] >= count; });
}
bool Link::wait_events(uint64_t count, Clock::time_point deadline) {
    std::unique_lock lock(mutex_);
    return wake_.wait_until(lock, deadline, [&] { return event_waits_ >= count; });
}
void Link::saturate() {
    std::lock_guard lock(mutex_);
    Frame f;
    f.kind = Class::response;
    f.message = 3;
    f.session = UINT64_MAX;
    f.payload.size = 51;
    for (unsigned i = 0; i < 8; ++i) {
        replies_.push(f);
        f.kind = Class::event;
        events_.push(f);
        f.kind = Class::response;
    }
}
void Link::inject_event(const Frame &f) {
    std::lock_guard lock(mutex_);
    deliver_locked(f);
}
void Link::interrupt() {
    std::lock_guard lock(mutex_);
    ++interrupts_;
    wake_.notify_all();
}
void Link::advance(uint64_t now) {
    std::lock_guard lock(mutex_);
    if (!manual_ || now < manual_now_)
        throw std::invalid_argument("Manual clock advance");
    manual_now_ = now;
    progress_locked();
    wake_.notify_all();
}
void Link::inject(const Injection &v) {
    std::lock_guard lock(mutex_);
    injection_ = v;
    controller_.reject(v.reject_message, v.rejection);
    controller_.cleanup_error(v.cleanup_error ? Result::internal_error : Result::ok);
    wake_.notify_all();
}
Injection Link::injection() const {
    std::lock_guard lock(mutex_);
    return injection_;
}
void Link::mutate(const std::function<void(Controller &)> &f) {
    std::lock_guard lock(mutex_);
    progress_locked();
    f(controller_);
    progress_locked();
    wake_.notify_all();
}
Link::Metrics Link::metrics() const {
    std::lock_guard lock(mutex_);
    return {endpoint_.accepted,        endpoint_.rejected + receiver_.rejected,
            controller_.admitted_runs, controller_.completed_pulses,
            controller_.stop_requests, std::max(endpoint_.high_water, receiver_.high_water),
            replies_.high_water,       events_.high_water};
}
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
Host::Host(Link &l, std::function<uint64_t()> identity)
    : link_(l), identity_(identity ? std::move(identity) : fresh_identity) {
    heartbeat_ = std::jthread([this](std::stop_token s) { heartbeat(s); });
}
Host::~Host() {
    {
        std::lock_guard lock(control_);
        heartbeat_.request_stop();
        changed_.notify_all();
    }
    link_.interrupt();
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
    link_.interrupt();
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
            if (!link_.send(*wire))
                throw ProtocolError(Result::unavailable, "Simulated link unavailable");
            if (attempt == 0 && q.message == 0x13)
                command_dispatch_ns_ =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
                        .count();
        }
        auto r = link_.receive(q, std::min(deadline, Clock::now() + retry_interval));
        if (!r)
            continue;
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
            if (get(hello.payload, 12, 4) != BenchConfig::controller)
                throw ProtocolError(Result::unsupported, "Controller identity mismatch");
            s.global = exchange(request(Message::capabilities), deadline);
            s.generation = get(s.global.payload, 40, 8);
            if (s.global.payload.bytes[6] != 1 || s.global.payload.bytes[7] != 0x43)
                throw ProtocolError(Result::unsupported, "Simulation capability mismatch");
            auto query = [&](Message m) {
                auto q = request(m);
                put(q.payload, 1, 8, s.generation);
                auto r = exchange(q, deadline);
                if (get(r.payload, 4, 8) != s.generation ||
                    get(r.payload, 12, 8) != BenchConfig::channel_uid || r.payload.bytes[20] != 0)
                    throw ProtocolError(Result::snapshot_changed, "Mixed snapshot");
                return r;
            };
            s.channel = query(Message::channel_capabilities);
            s.uid = get(s.channel.payload, 12, 8);
            s.status = query(Message::channel_status);
            s.calibration_status = query(Message::calibration);
            if (s.status.payload.bytes[21] != 4 || s.status.payload.bytes[22] > 1 ||
                s.status.payload.bytes[23] != 2 || s.calibration_status.payload.bytes[21] != 2 ||
                s.calibration_status.payload.bytes[23] != 1 ||
                get(s.calibration_status.payload, 32, 4) != BenchConfig::board ||
                !get(s.calibration_status.payload, 36, 4))
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
bool configuration_fits(const Snapshot &s, const BenchConfig &c) {
    if (!c.valid() || !s.generation || !s.boot || !s.calibration || s.uid != BenchConfig::channel_uid)
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
Association Host::start(const Snapshot &s, const BenchConfig &config, bool on, Clock::time_point deadline,
                        uint64_t expected_fence) {
    resume_heartbeat(deadline);
    if (!configuration_fits(s, config))
        throw ProtocolError(Result::limit_exceeded, "Invalid bench configuration");
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
    p.bytes[0] = BenchConfig::channel;
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
    if (!heartbeat.owns_lock() || link_.injection().disable_heartbeat)
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
    if (auto event = link_.event(std::min(deadline, Clock::now() + 5ms))) {
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
    if (f != fence() || link_.injection().block_next || Clock::now() + 1ms >= deadline)
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
    if (!link_.send(*wire)) {
        std::lock_guard lock(control_);
        stop_error_ = Result::unavailable;
        return false;
    }
    {
        std::lock_guard lock(control_);
        stop_dispatch_ns_ =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
    }
    link_.interrupt();
    auto r = link_.receive(q, deadline);
    auto error = !r ? Result::timeout : !valid_message(*r) ? Result::invalid_argument : code(*r);
    {
        std::lock_guard lock(control_);
        stop_error_ = error;
    }
    return error == Result::ok && r->payload.bytes[4] == 2;
}
} // namespace x1::f2::simulation
