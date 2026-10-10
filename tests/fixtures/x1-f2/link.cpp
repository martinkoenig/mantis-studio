#include "link.hpp"

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
TransportStatus Link::submit(const Wire &wire) {
    if (wire.size > max_wire)
        return TransportStatus::invalid;
    std::lock_guard lock(mutex_);
    if (closed_)
        return TransportStatus::closed;
    if (injection_.fail_link)
        return TransportStatus::unavailable;
    progress_locked();
    for (auto byte : wire.data())
        if (auto f = endpoint_.feed(byte, now_locked())) {
            auto found = layout(f->message);
            if (found)
                ++requests_[static_cast<size_t>(found - registry.data())];
            wake_.notify_all();
            // Model lost heartbeat bytes at the peer, never through host policy.
            if (f->message == 0x12 && injection_.disable_heartbeat)
                continue;
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
    return TransportStatus::ready;
}
FrameRead Link::receive(const Frame &q, Clock::time_point deadline) {
    std::unique_lock lock(mutex_);
    auto interrupted = interrupts_;
    if (auto l = layout(q.message))
        ++response_waits_[static_cast<size_t>(l - registry.data())];
    wake_.notify_all();
    do {
        if (closed_)
            return {TransportStatus::closed, {}};
        if (injection_.fail_link)
            return {TransportStatus::unavailable, {}};
        if (q.message != 0x14 && interrupted != interrupts_)
            return {TransportStatus::interrupted, {}};
        progress_locked();
        if (q.message == 0x14 || q.message == 0x12) {
            auto &slot = q.message == 0x14 ? stop_reply_ : heartbeat_reply_;
            if (slot) {
                auto f = *slot;
                slot.reset();
                if (matches_response(f, q))
                    return {TransportStatus::ready, f};
            }
        } else
            while (auto f = replies_.pop())
                if (matches_response(*f, q))
                    return {TransportStatus::ready, *f};
        if (Clock::now() >= deadline)
            return {TransportStatus::timeout, {}};
        auto until = deadline;
        if (!manual_) {
            auto due = std::min(controller_.next_deadline(), delayed_ ? delayed_at_ : UINT64_MAX);
            if (due != UINT64_MAX)
                until = std::min(until, epoch_ + std::chrono::microseconds(due));
        }
        wake_.wait_until(lock, until);
    } while (true);
}
FrameRead Link::event(Clock::time_point deadline) {
    std::unique_lock lock(mutex_);
    auto interrupted = interrupts_;
    ++event_waits_;
    wake_.notify_all();
    do {
        if (closed_)
            return {TransportStatus::closed, {}};
        if (injection_.fail_link)
            return {TransportStatus::unavailable, {}};
        progress_locked();
        if (interrupted != interrupts_)
            return {TransportStatus::interrupted, {}};
        if (!injection_.block_next)
            if (auto f = events_.pop())
                return {TransportStatus::ready, *f};
        if (Clock::now() >= deadline)
            // The blocked-read injection cancels this read, including subsequent
            // reconciliation, through the real transport outcome. Host sees no injection.
            return {injection_.block_next ? TransportStatus::interrupted : TransportStatus::timeout, {}};
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
bool Link::wait_receives(Message message, uint64_t count, Deadline deadline) {
    std::unique_lock lock(mutex_);
    auto l = layout(static_cast<uint16_t>(message));
    return l && wake_.wait_until(lock, deadline, [&] {
        return response_waits_[static_cast<size_t>(l - registry.data())] >= count;
    });
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
void Link::interrupt() noexcept {
    std::lock_guard lock(mutex_);
    ++interrupts_;
    wake_.notify_all();
}
void Link::close() {
    std::lock_guard lock(mutex_);
    closed_ = true;
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
} // namespace x1::f2::simulation
