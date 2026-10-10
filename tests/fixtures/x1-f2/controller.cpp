#include "controller.hpp"
#include <limits>

namespace x1::f2::simulation {
bool BenchConfig::valid() const {
    return current_ua >= 1'000 && current_ua <= max_current && period_us >= min_period &&
           period_us <= max_period && high_us >= min_high && high_us <= max_high && high_us <= period_us &&
           period_us - high_us >= min_low && high_us <= max_on && pulses && pulses <= max_pulses &&
           duration_us() <= uint64_t(max_run_ms) * 1'000;
}
Controller::Controller(uint64_t boot)
    : boot_(boot), calibration_(boot ^ BenchConfig::channel_uid ^ 0x43414c) {
    if (!boot || !calibration_)
        throw std::invalid_argument("Synthetic boot/calibration identity");
}
Frame Controller::response(const Frame &q, Result result, Stage stage) const {
    Frame r;
    r.kind = Class::response;
    r.session = q.session;
    r.request = q.request;
    r.message = q.message;
    auto l = layout(q.message);
    r.payload.size = l && l->response ? l->response : 4;
    put(r.payload, 0, 2, static_cast<uint16_t>(result));
    r.payload.bytes[2] = static_cast<uint8_t>(result == Result::ok ? stage : Stage::terminal);
    r.payload.bytes[3] = static_cast<uint8_t>(state_);
    return r;
}
void Controller::change_snapshot() {
    if (snapshot_ == UINT64_MAX) {
        inject_fault(Result::internal_error);
        return;
    }
    ++snapshot_;
}
void Controller::revoke_calibration() {
    calibrated_ = false;
    change_snapshot();
}
Result Controller::basis() const {
    if (!calibrated_)
        return Result::not_calibrated;
    if (basis_ != snapshot_)
        return Result::snapshot_changed;
    if (fault_ != Result::ok)
        return Result::hardware_fault;
    return Result::ok;
}
void Controller::finish(uint8_t reason, Result initiating, Result cleanup) {
    if (cleanup == Result::ok)
        cleanup = cleanup_;
    if (cleanup != Result::ok && fault_ == Result::ok) {
        fault_ = cleanup;
        fault_resolved_ = false;
        if (snapshot_ != UINT64_MAX)
            ++snapshot_;
    }
    state_ = State::stopping;
    if (execution_) {
        Payload p;
        p.size = 58;
        put(p, 0, 8, boot_);
        put(p, 8, 8, session_);
        put(p, 16, 8, execution_);
        put(p, 24, 4, arm_);
        p.bytes[28] = cleanup != Result::ok && reason == 1 ? 7 : reason;
        put(p, 29, 2, static_cast<uint16_t>(cleanup != Result::ok ? cleanup : initiating));
        auto elapsed = now_ - start_;
        auto count = std::min<uint64_t>(configured_.pulses, elapsed / configured_.period_us);
        put(p, 31, 4, count);
        p.bytes[35] = 2;
        p.bytes[36] = 1;
        put(p, 37, 8, now_);
        p.bytes[49] = 2;
        put(p, 50, 2, static_cast<uint16_t>(initiating));
        put(p, 52, 2, static_cast<uint16_t>(cleanup));
        if (revision_ == UINT32_MAX)
            terminal_.reset();
        else {
            put(p, 54, 4, ++revision_);
            terminal_ = p;
            Frame e;
            e.kind = Class::event;
            e.session = session_;
            e.message = 0x8001;
            e.payload = p;
            events_.push(e);
        }
        completed_pulses += count;
        last_execution_ = execution_;
    }
    execution_ = 0;
    arm_ = 0;
    lease_end_ = 0;
    basis_ = 0;
    state_ = fault_ == Result::ok ? State::inhibited : State::fault;
}
void Controller::advance(uint64_t now) {
    if (now < now_)
        throw std::invalid_argument("Synthetic clock reversal");
    // Process chronological boundaries; a late caller cannot complete pulses after lease expiry.
    auto due = next_deadline();
    if (due <= now && (state_ == State::armed || state_ == State::running)) {
        now_ = due;
        auto old_arm = arm_;
        auto old_execution = execution_;
        auto old_session = session_;
        if (state_ == State::running && start_ + configured_.duration_us() <= lease_end_ &&
            due == start_ + configured_.duration_us())
            finish(1, Result::ok);
        else {
            finish(3, Result::lease_expired);
            Frame e;
            e.kind = Class::event;
            e.session = old_session;
            e.message = 0x8003;
            e.payload.size = 16;
            put(e.payload, 0, 4, old_arm);
            put(e.payload, 4, 8, old_execution);
            put(e.payload, 12, 4, (now_ / 1'000) & UINT32_MAX);
            events_.push(e);
            session_ = 0;
            next_id_ = 1;
            cached_request_.reset();
            cached_response_.reset();
        }
    }
    now_ = now;
}
uint64_t Controller::next_deadline() const {
    if (state_ != State::armed && state_ != State::running)
        return UINT64_MAX;
    return state_ == State::running ? std::min(lease_end_, start_ + configured_.duration_us()) : lease_end_;
}
void Controller::reboot(uint64_t boot) {
    if (!boot || boot == boot_)
        throw std::invalid_argument("Fresh simulated boot required");
    *this = Controller(boot);
}
void Controller::inject_fault(Result code, bool resolved) {
    if (code == Result::ok)
        throw std::invalid_argument("Fault must be nonzero");
    if (fault_ == Result::ok) {
        fault_ = code;
        if (snapshot_ != UINT64_MAX)
            ++snapshot_;
    }
    fault_resolved_ = resolved;
    auto old_arm = arm_;
    auto old_execution = execution_;
    auto old_session = session_;
    finish(5, code);
    Frame e;
    e.kind = Class::event;
    e.session = old_session;
    e.message = 0x8002;
    e.payload.size = 24;
    put(e.payload, 0, 2, static_cast<uint16_t>(code));
    e.payload.bytes[2] = 4;
    e.payload.bytes[3] = 2;
    put(e.payload, 4, 4, old_arm);
    put(e.payload, 8, 8, old_execution);
    put(e.payload, 16, 8, now_);
    events_.push(e);
}
std::optional<Frame> Controller::receive(const Frame &q, uint64_t now) {
    advance(now);
    if (q.kind != Class::request)
        return {};
    auto l = layout(q.message);
    // Envelope-valid ordinary payload errors still consume/cache their admitted request ID.
    auto typed = [&] { return l && !l->event && q.payload.size == l->request; };
    if (q.message == 0x14 && q.request == 0) {
        if (!typed())
            return response(q, Result::invalid_argument);
        ++stop_requests;
        finish(2, Result::ok);
        auto r = response(q, cleanup_);
        if (cleanup_ == Result::ok)
            r.payload.bytes[4] = 2;
        return r;
    }
    if (q.message == 0x12) {
        if (q.request != 0 || !typed())
            return response(q, Result::invalid_argument);
        if (!session_)
            return response(q, Result::no_session);
        if (q.session != session_)
            return response(q, Result::stale_session);
        if (state_ != State::armed && state_ != State::running)
            return response(q, Result::bad_state);
        auto generation = get(q.payload, 0, 4), counter = get(q.payload, 4, 4);
        if (generation != arm_ || !counter || counter <= counter_)
            return response(q, Result::stale_request);
        if (now_ > UINT64_MAX - 500'000)
            return response(q, Result::limit_exceeded);
        counter_ = static_cast<uint32_t>(counter);
        lease_end_ = now_ + 500'000;
        auto r = response(q, Result::ok);
        put(r.payload, 4, 4, counter_);
        put(r.payload, 8, 2, 500);
        return r;
    }
    if (q.message == 2) {
        if (cached_request_ && q.session == session_ && q.request == 1 && cached_request_->request == 1)
            return q == *cached_request_ ? cached_response_
                                         : std::optional{response(q, Result::conflicting_duplicate)};
        if (q.request != 1 || !q.session || !typed())
            return response(q, Result::invalid_argument);
        if (get(q.payload, 0, 8) != boot_)
            return response(q, Result::stale_boot);
        if ((state_ != State::inhibited && state_ != State::fault) ||
            (state_ == State::fault && cleanup_ != Result::ok))
            return response(q, Result::busy);
        if (q.session == session_)
            return response(q, Result::stale_session);
        session_ = q.session;
        next_id_ = 2;
        last_execution_ = 0;
        arm_nonces_count_ = 0;
        executions_count_ = 0;
        auto r = response(q, Result::ok);
        put(r.payload, 4, 8, boot_);
        cached_request_ = q;
        cached_response_ = r;
        return r;
    }
    bool unbound = !q.session && !q.request;
    auto m = static_cast<Message>(q.message);
    bool query = m == Message::hello || m == Message::status || m == Message::capabilities ||
                 m == Message::channel_capabilities || m == Message::channel_status ||
                 m == Message::calibration || m == Message::sensor || m == Message::terminal;
    if (unbound && query)
        return typed() ? semantic(q, false) : response(q, Result::invalid_argument);
    if (!session_)
        return response(q, Result::no_session);
    if (q.session != session_)
        return response(q, Result::stale_session);
    if (cached_request_ && q.request == cached_request_->request)
        return q == *cached_request_ ? cached_response_
                                     : std::optional{response(q, Result::conflicting_duplicate)};
    if (!q.request || q.request != next_id_ || !next_id_)
        return response(q, Result::stale_request);
    next_id_ = q.request == UINT32_MAX ? 0 : q.request + 1;
    auto r = !l                             ? response(q, Result::unsupported)
             : !typed()                     ? response(q, Result::invalid_argument)
             : q.message == reject_message_ ? response(q, rejection_)
                                            : semantic(q, true);
    // CLEAR_FAULT invalidates the session, so it does not leave reusable authority/cache.
    if (session_) {
        cached_request_ = q;
        cached_response_ = r;
    }
    return r;
}
Frame Controller::semantic(const Frame &q, bool bound) {
    auto m = static_cast<Message>(q.message);
    const auto &p = q.payload;
    if (m == Message::hello && bound)
        return response(q, Result::invalid_argument);
    if (m == Message::channel_capabilities || m == Message::channel_status || m == Message::calibration ||
        m == Message::sensor) {
        if (get(p, 1, 8) != snapshot_)
            return response(q, Result::snapshot_changed);
        if (p.bytes[0] != 0 || (m == Message::sensor && p.bytes[9] != 0))
            return response(q, Result::invalid_argument);
    }
    auto r = response(q, Result::ok);
    auto &v = r.payload;
    switch (m) {
    case Message::hello:
        put(v, 4, 8, boot_);
        put(v, 12, 4, BenchConfig::controller);
        put(v, 16, 4, 0x00010001);
        break;
    case Message::capabilities:
        put(v, 4, 2, 1);
        v.bytes[6] = 1;
        v.bytes[7] = 0x43;
        put(v, 8, 4, BenchConfig::max_current);
        put(v, 12, 4, BenchConfig::min_period);
        put(v, 16, 4, BenchConfig::max_period);
        put(v, 20, 4, BenchConfig::min_high);
        put(v, 24, 4, BenchConfig::max_high);
        put(v, 28, 4, BenchConfig::min_low);
        put(v, 32, 4, BenchConfig::max_pulses);
        put(v, 36, 4, BenchConfig::max_run_ms);
        put(v, 40, 8, snapshot_);
        break;
    case Message::channel_capabilities:
        put(v, 4, 8, snapshot_);
        put(v, 12, 8, BenchConfig::channel_uid);
        v.bytes[20] = 0;
        put(v, 21, 2, 3);
        put(v, 23, 4, 1'000);
        put(v, 27, 4, BenchConfig::max_current);
        put(v, 31, 4, 1);
        put(v, 35, 4, BenchConfig::min_period);
        put(v, 39, 4, BenchConfig::max_period);
        put(v, 43, 4, BenchConfig::min_high);
        put(v, 47, 4, BenchConfig::max_high);
        put(v, 51, 4, BenchConfig::min_low);
        put(v, 55, 4, BenchConfig::max_pulses);
        put(v, 59, 4, BenchConfig::max_on);
        v.bytes[63] = 1;
        put(v, 64, 4, 1);
        break;
    case Message::channel_status:
        put(v, 4, 8, snapshot_);
        put(v, 12, 8, BenchConfig::channel_uid);
        v.bytes[21] = fault_ != Result::ok ? 3 : calibrated_ ? 4 : 1;
        v.bytes[22] = 1;
        v.bytes[23] = calibrated_ ? 2 : 3;
        put(v, 24, 2, static_cast<uint16_t>(fault_));
        put(v, 26, 4, config_generation_);
        put(v, 30, 2, 1 | (calibrated_ ? 4 : 0) | (fault_ == Result::ok ? 8 : 0));
        break;
    case Message::calibration:
        put(v, 4, 8, snapshot_);
        put(v, 12, 8, BenchConfig::channel_uid);
        v.bytes[21] = calibrated_ ? 2 : 3;
        v.bytes[22] = 2;
        v.bytes[23] = 1;
        put(v, 24, 8, calibrated_ ? calibration_ : 0);
        put(v, 32, 4, BenchConfig::board);
        put(v, 36, 4, provisioning_);
        put(v, 40, 4, config_generation_);
        put(v, 44, 2, 1);
        break;
    case Message::sensor:
        put(v, 4, 8, snapshot_);
        put(v, 12, 8, BenchConfig::channel_uid);
        put(v, 21, 8, 0x8266000153454e01);
        v.bytes[29] = 4;
        v.bytes[30] = 1;
        v.bytes[31] = 1;
        v.bytes[32] = 4;
        v.bytes[33] = 1;
        put(v, 38, 4, 1);
        put(v, 42, 4, 1);
        put(v, 50, 2, 1);
        break; // synthetic descriptor only, no value delivery
    case Message::status: {
        put(v, 4, 8, boot_);
        v.bytes[12] = session_ ? 1 : 0;
        if (bound) {
            put(v, 13, 4, arm_);
            put(v, 17, 8, execution_);
            put(v, 37, 2, lease_end_ > now_ ? (lease_end_ - now_) / 1'000 : 0);
            put(v, 47, 4, next_id_);
        }
        if (terminal_ && (bound ? get(*terminal_, 8, 8) == session_ : get(*terminal_, 8, 8) != session_)) {
            put(v, 25, 8, get(*terminal_, 16, 8));
            v.bytes[33] = terminal_->bytes[28];
            put(v, 34, 2, get(*terminal_, 29, 2));
            v.bytes[36] = 1 | (terminal_->bytes[49] == 2 ? 2 : 0);
            put(v, 39, 8, get(*terminal_, 8, 8));
        }
        break;
    }
    case Message::terminal:
        if (!terminal_)
            return response(q, Result::unavailable);
        if (get(p, 0, 8) != get(*terminal_, 0, 8) || get(p, 8, 8) != get(*terminal_, 8, 8) ||
            get(p, 16, 8) != get(*terminal_, 16, 8))
            return response(q, Result::stale_request);
        if (bound ? get(*terminal_, 8, 8) != session_ : get(*terminal_, 8, 8) == session_)
            return response(q, Result::unavailable);
        std::copy(terminal_->data().begin(), terminal_->data().end(), v.bytes.begin() + 4);
        v.bytes[2] = 2;
        break;
    case Message::configure: {
        if (get(p, 21, 8) != snapshot_)
            return response(q, Result::snapshot_changed);
        if (state_ != State::inhibited && state_ != State::configured)
            return response(q, Result::bad_state);
        if (!calibrated_)
            return response(q, Result::not_calibrated);
        if (!get(p, 13, 8))
            return response(q, Result::not_calibrated);
        if (get(p, 13, 8) != calibration_)
            return response(q, Result::calibration_invalid);
        BenchConfig c;
        c.current_ua = static_cast<uint32_t>(get(p, 1, 4));
        c.period_us = static_cast<uint32_t>(get(p, 5, 4));
        c.high_us = static_cast<uint32_t>(get(p, 9, 4));
        c.pulses = 1;
        if (p.bytes[0] || !c.valid())
            return response(q, Result::limit_exceeded);
        if (config_generation_ == UINT32_MAX)
            return response(q, Result::limit_exceeded);
        configured_ = c;
        basis_ = snapshot_;
        ++config_generation_;
        state_ = State::configured;
        r = response(q, Result::ok);
        put(r.payload, 4, 4, config_generation_);
        break;
    }
    case Message::arm: {
        if (state_ != State::configured)
            return response(q, Result::bad_state);
        if (auto b = basis(); b != Result::ok) {
            basis_ = 0;
            state_ = State::inhibited;
            return response(q, b);
        }
        const auto nonce = get(p, 0, 8);
        if (!nonce || std::find(arm_nonces_.begin(), arm_nonces_.begin() + arm_nonces_count_, nonce) !=
                          arm_nonces_.begin() + arm_nonces_count_)
            return response(q, Result::invalid_argument);
        if (arm_nonces_count_ == arm_nonces_.size() || next_arm_ == UINT32_MAX || now_ > UINT64_MAX - 500'000)
            return response(q, Result::limit_exceeded);
        arm_nonces_[arm_nonces_count_++] = nonce;
        arm_ = ++next_arm_;
        counter_ = 0;
        lease_end_ = now_ + 500'000;
        state_ = State::armed;
        r = response(q, Result::ok);
        put(r.payload, 4, 4, arm_);
        put(r.payload, 8, 2, 500);
        break;
    }
    case Message::run: {
        if (state_ != State::armed)
            return response(q, Result::bad_state);
        if (auto b = basis(); b != Result::ok) {
            finish(5, b);
            return response(q, b);
        }
        auto id = get(p, 4, 8);
        auto count = get(p, 12, 4);
        if (get(p, 0, 4) != arm_ || !id ||
            std::find(executions_.begin(), executions_.begin() + executions_count_, id) !=
                executions_.begin() + executions_count_)
            return response(q, Result::invalid_argument);
        auto c = configured_;
        c.pulses = static_cast<uint32_t>(count);
        if (!c.valid() || now_ > UINT64_MAX - c.duration_us())
            return response(q, Result::limit_exceeded);
        if (executions_count_ == executions_.size())
            return response(q, Result::limit_exceeded);
        executions_[executions_count_++] = id;
        configured_ = c;
        execution_ = id;
        start_ = now_;
        terminal_.reset();
        ++admitted_runs;
        state_ = State::running;
        r = response(q, Result::ok, Stage::accepted);
        put(r.payload, 4, 8, id);
        put(r.payload, 12, 4, count);
        break;
    }
    case Message::disarm:
        if (state_ != State::configured && state_ != State::armed && state_ != State::inhibited)
            return response(q, Result::bad_state);
        finish(2, Result::ok);
        r = response(q, Result::ok);
        break;
    case Message::clear_fault:
        if (state_ != State::fault)
            return response(q, Result::bad_state);
        if (!fault_resolved_ || cleanup_ != Result::ok)
            return response(q, Result::hardware_fault);
        fault_ = Result::ok;
        state_ = State::inhibited;
        session_ = 0;
        basis_ = 0;
        arm_ = 0;
        lease_end_ = 0;
        cached_request_.reset();
        cached_response_.reset();
        change_snapshot();
        r = response(q, Result::ok);
        break;
    default:
        return response(q, Result::unsupported);
    }
    return r;
}
} // namespace x1::f2::simulation
