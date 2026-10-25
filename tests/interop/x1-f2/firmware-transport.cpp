#include "firmware-transport.hpp"
namespace interop {
FirmwareTransport::FirmwareTransport() {
    mcu::SimulationProfile p;
    p.controller_id = 1;
    p.hardware_revision = 1;
    p.channel_uid = 1;
    p.firmware_build_id = 4;
    p.max_current_ua = 50'000;
    p.min_high_us = 100;
    p.max_high_us = 5'000;
    p.max_continuous_on_us = 5'000;
    p.min_low_us = 100;
    p.max_pulses_per_run = 1'000;
    p.max_run_ms = 2'000;
    CHECK(controller_.initialize(p, boot) == mcu::EngineError::None);
    CHECK(controller_.provision_synthetic(token, 1) == mcu::EngineError::None);
}
void FirmwareTransport::accept_bytes(const Wire &w) {
    // Called under the short state lock by output, or only by quiescent test callers.
    for (auto b : w.data())
        if (auto f = receiver_.feed(b, uint64_t(tick_) * 1000)) {
            if (f->kind == Class::response) {
                if (f->message == 0x14)
                    stop_ = *f;
                else if (f->message == 0x12)
                    heartbeat_ = *f;
                else if (!replies_.push(*f))
                    ++queue_drops_;
            } else if (f->kind == Class::event && !events_.push(*f))
                ++queue_drops_;
        }
    wake_.notify_all();
}
void FirmwareTransport::output(const fw::Frame &frame) {
    auto f = frame;
    // Faults operate on firmware-generated responses, never alternative peer semantics.
    if (f.header.message_class == 2) {
        if (injection_.block && injection_.block == f.header.message_id)
            return;
        if (injection_.drop == f.header.message_id && injection_.drops) {
            --injection_.drops;
            return;
        }
        if (injection_.mutation == 1)
            f.header.payload_length = 4;
        if (injection_.mutation == 2)
            f.payload[2] = 3;
        if (injection_.mutation == 3)
            f.payload[3] = 7;
        if (injection_.mutation == 4)
            ++f.header.request_id;
        if (injection_.mutation == 5)
            f.payload[2] = 1;
        if (injection_.mutation == 6) {
            f.payload[0] = 19;
            f.payload[1] = 0;
        }
        if (injection_.mutation == 7) {
            f.payload[0] = 0;
            f.payload[1] = 0;
        }
        if (injection_.mutation == 8)
            ++f.header.message_id;
        if (injection_.mutation == 9)
            ++f.header.session_token;
    } else if (injection_.lose_events)
        return;
    Wire w;
    CHECK(fw::encode_frame(f.view(), fw::Buffer(w.bytes.data(), w.bytes.size()), w.size) == fw::Error::None);
    if (f.header.message_class == 3)
        last_event_ = w;
    accept_bytes(w);
}
void FirmwareTransport::progress() {
    CHECK(controller_.advance_time(tick_) == mcu::EngineError::None);
    fw::Frame event;
    while (controller_.take_event(event))
        output(event); // At most firmware kEventCapacity=4.
}
TransportStatus FirmwareTransport::submit(const Wire &w) {
    return batch({&w, 1});
}
TransportStatus FirmwareTransport::batch(std::span<const Wire> wires) {
    std::lock_guard lock(mutex_);
    if (closed_)
        return TransportStatus::closed;
    if (wires.empty() || wires.size() > mcu::kBatchCapacity)
        return TransportStatus::invalid;
    for (const auto &w : wires)
        if (w.size > max_wire)
            return TransportStatus::invalid;
    // Bounded diagnostic loss must never gate STOP or heartbeat admission.
    for (const auto &w : wires) {
        if (submissions_ < submitted_.size())
            submitted_[submissions_++] = w;
        else if (diagnostic_drops_ != SIZE_MAX)
            ++diagnostic_drops_;
    }
    std::array<fw::Frame, mcu::kBatchCapacity> inputs{}, outputs{};
    size_t count = 0;
    for (const auto &w : wires)
        for (auto b : w.data()) {
            auto got = peer_.feed_byte(b, tick_);
            if (got.kind == fw::ReceiveKind::Rejected)
                ++rejected_frames_;
            if (got.kind == fw::ReceiveKind::Frame) {
                if (count == inputs.size())
                    return TransportStatus::invalid;
                if (fw::validate_message(got.frame->view(), fw::Direction::HostToController) ==
                    fw::CodecError::WrongDirection) {
                    ++rejected_frames_;
                    continue;
                }
                inputs[count++] = *got.frame;
            }
        }
    if (!count)
        return TransportStatus::ready;
    std::array<fw::FrameView, mcu::kBatchCapacity> views{inputs[0].view(), inputs[1].view(), inputs[2].view(),
                                                         inputs[3].view(), inputs[4].view(), inputs[5].view(),
                                                         inputs[6].view(), inputs[7].view()};
    std::array<mcu::DispatchResult, mcu::kBatchCapacity> details{};
    CHECK(controller_.process_batch(views.data(), count, outputs.data(), details.data()).error ==
          mcu::EngineError::None);
    for (size_t i = 0; i < count; ++i) {
        ++dispatched_;
        CHECK(details[i].error == mcu::EngineError::None || details[i].error == mcu::EngineError::WrongClass);
        if (!details[i].response)
            continue;
        Exchange scratch;
        auto &t = traces_ < trace_.size() ? trace_[traces_++] : scratch;
        if (&t == &scratch && diagnostic_drops_ != SIZE_MAX)
            ++diagnostic_drops_;
        CHECK(fw::encode_frame(inputs[i].view(), fw::Buffer(t.request.bytes.data(), max_wire),
                               t.request.size) == fw::Error::None);
        CHECK(fw::encode_frame(outputs[i].view(), fw::Buffer(t.response.bytes.data(), max_wire),
                               t.response.size) == fw::Error::None);
        t.replay = details[i].replay;
        t.consumed = details[i].consumed;
        output(outputs[i]);
    }
    progress();
    wake_.notify_all();
    return TransportStatus::ready;
}
FrameRead FirmwareTransport::receive(const Frame &q, Deadline d) {
    std::unique_lock lock(mutex_);
    const auto interrupted = interrupts_;
    const auto l = layout(q.message);
    ++waits_[l ? static_cast<size_t>(l - registry.data()) : registry.size()];
    wake_.notify_all();
    for (;;) {
        if (closed_)
            return {TransportStatus::closed, {}};
        if (q.message != 0x14 && interrupts_ != interrupted)
            return {TransportStatus::interrupted, {}};
        if (q.message == 0x14 || q.message == 0x12) {
            auto &slot = q.message == 0x14 ? stop_ : heartbeat_;
            if (slot) {
                auto r = *slot;
                slot.reset();
                if (matches_response(r, q))
                    return {TransportStatus::ready, r};
            }
        } else
            while (auto r = replies_.pop())
                if (matches_response(*r, q))
                    return {TransportStatus::ready, *r};
        if (std::chrono::steady_clock::now() >= d)
            return {TransportStatus::timeout, {}};
        wake_.wait_until(lock, d);
    }
}
FrameRead FirmwareTransport::event(Deadline d) {
    std::unique_lock lock(mutex_);
    const auto interrupted = interrupts_;
    for (;;) {
        if (closed_)
            return {TransportStatus::closed, {}};
        if (interrupted != interrupts_)
            return {TransportStatus::interrupted, {}};
        if (auto e = events_.pop())
            return {TransportStatus::ready, *e};
        if (std::chrono::steady_clock::now() >= d)
            return {TransportStatus::timeout, {}};
        wake_.wait_until(lock, d);
    }
}
void FirmwareTransport::interrupt() noexcept {
    std::lock_guard lock(mutex_);
    ++interrupts_;
    wake_.notify_all();
}
void FirmwareTransport::close() {
    std::lock_guard lock(mutex_);
    closed_ = true;
    wake_.notify_all();
}
void FirmwareTransport::advance(uint32_t ms) {
    std::lock_guard lock(mutex_);
    tick_ = ms;
    peer_.advance_time(tick_);
    progress();
    wake_.notify_all();
}
void FirmwareTransport::reboot(uint64_t b) {
    std::lock_guard lock(mutex_);
    CHECK(controller_.reboot(b, tick_) == mcu::EngineError::None);
    wake_.notify_all();
}
void FirmwareTransport::invalidate_snapshot() {
    std::lock_guard lock(mutex_);
    CHECK(controller_.invalidate_snapshot() == mcu::EngineError::None);
}
bool FirmwareTransport::wait_receive(uint16_t op, uint64_t count, Deadline d) {
    std::unique_lock lock(mutex_);
    const auto l = layout(op);
    return wake_.wait_until(lock, d, [&] {
        return waits_[l ? static_cast<size_t>(l - registry.data()) : registry.size()] >= count;
    });
}
FirmwareTransport::Facts FirmwareTransport::facts() const {
    std::lock_guard lock(mutex_);
    return {controller_.boot_token(),
            controller_.session_token(),
            controller_.execution_id(),
            controller_.snapshot_generation(),
            controller_.next_request_id(),
            controller_.config_generation(),
            controller_.arm_generation(),
            controller_.heartbeat_counter(),
            controller_.statistics().executions,
            controller_.lease_remaining_ms(),
            controller_.state()};
}
std::pair<size_t, size_t> FirmwareTransport::high_water() const {
    std::lock_guard lock(mutex_);
    return {replies_.high, events_.high};
}
} // namespace interop

namespace interop {
void FirmwareTransport::inject(Injection i) {
    std::lock_guard lock(mutex_);
    injection_ = i;
}
size_t FirmwareTransport::trace_count() const {
    std::lock_guard lock(mutex_);
    return traces_;
}
FirmwareTransport::Exchange FirmwareTransport::exchange(size_t i) const {
    std::lock_guard lock(mutex_);
    CHECK(i < traces_);
    return trace_[i];
}
size_t FirmwareTransport::submission_count() const {
    std::lock_guard lock(mutex_);
    return submissions_;
}
Wire FirmwareTransport::submission(size_t i) const {
    std::lock_guard lock(mutex_);
    CHECK(i < submissions_);
    return submitted_[i];
}
size_t FirmwareTransport::rejected() const {
    std::lock_guard lock(mutex_);
    return rejected_frames_;
}
size_t FirmwareTransport::dispatched() const {
    std::lock_guard lock(mutex_);
    return dispatched_;
}
void FirmwareTransport::inject_bytes(const Wire &w) {
    std::lock_guard lock(mutex_);
    CHECK(w.size <= max_wire);
    accept_bytes(w);
}
} // namespace interop

namespace interop {
std::optional<Wire> FirmwareTransport::event_wire() const {
    std::lock_guard lock(mutex_);
    return last_event_;
}
} // namespace interop

namespace interop {
size_t FirmwareTransport::diagnostic_drops() const {
    std::lock_guard lock(mutex_);
    return diagnostic_drops_;
}
} // namespace interop
