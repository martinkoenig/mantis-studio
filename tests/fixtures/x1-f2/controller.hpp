#pragma once
#include "../../../plugins/first-party/devices/x1/f2/codec.hpp"
#include <algorithm>

namespace x1::f2::simulation {
// Software fixtures only. No physical transport/output type exists.
struct BenchConfig {
    static constexpr uint32_t schema = 1, controller = 0x82660001, board = 1, rig_revision = 1;
    static constexpr uint64_t channel_uid = 0x8266000100000001;
    static constexpr uint8_t channel = 0;
    uint32_t current_ua = 10'000, period_us = 10'000, high_us = 1'000, pulses = 20;
    static constexpr uint32_t max_current = 50'000, min_period = 1'000, max_period = 100'000, min_high = 100,
                              max_high = 5'000, min_low = 100, max_pulses = 1'000, max_on = 5'000,
                              max_run_ms = 2'000;
    bool valid() const;
    uint64_t duration_us() const {
        return uint64_t(period_us) * pulses;
    }
};
template <class T, size_t N> class Ring {
    std::array<T, N> slots_{};
    size_t read_{}, size_{};

  public:
    size_t high_water{}, overflows{};
    bool push(const T &v) {
        if (size_ == N) {
            ++overflows;
            return false;
        }
        slots_[(read_ + size_) % N] = v;
        ++size_;
        high_water = std::max(high_water, size_);
        return true;
    }
    std::optional<T> pop() {
        if (!size_)
            return {};
        auto v = slots_[read_];
        read_ = (read_ + 1) % N;
        --size_;
        return v;
    }
    void clear() {
        read_ = size_ = 0;
    }
    size_t size() const {
        return size_;
    }
};
class Controller {
    uint64_t boot_, snapshot_ = 1, session_{}, calibration_, basis_{}, execution_{}, last_execution_{};
    uint32_t next_id_ = 1, config_generation_{}, arm_{}, next_arm_{}, counter_{}, revision_{},
             provisioning_ = 1;
    State state_ = State::inhibited;
    bool calibrated_ = true, fault_resolved_ = true;
    BenchConfig configured_;
    uint64_t lease_end_{}, start_{}, now_{};
    std::optional<Frame> cached_request_, cached_response_;
    std::optional<Payload> terminal_;
    Ring<Frame, 8> events_;
    Result fault_ = Result::ok, cleanup_ = Result::ok;
    uint16_t reject_message_{};
    Result rejection_ = Result::hardware_fault;
    std::array<uint64_t, 64> executions_{};
    size_t executions_count_{};
    std::array<uint64_t, 64> arm_nonces_{};
    size_t arm_nonces_count_{};
    Frame response(const Frame &, Result, Stage = Stage::applied) const;
    void finish(uint8_t reason, Result initiating, Result cleanup = Result::ok);
    Frame semantic(const Frame &, bool bound);
    Result basis() const;

  public:
    explicit Controller(uint64_t boot = 0x8266000000000001);
    // Input reaches dispatch only after full streaming/framing validation in Link.
    std::optional<Frame> receive(const Frame &, uint64_t now_us);
    void advance(uint64_t now_us);
    void reboot(uint64_t new_boot);
    // Synthetic eligibility invalidation at the current manual/controller clock.
    // Running revocation faults; idle configuration/arming is merely invalidated.
    void revoke_calibration();
    // A discovery revision alone does not assert an execution-time controller fault.
    void change_snapshot();
    void inject_fault(Result code, bool resolved = false);
    void reject(uint16_t message, Result result) {
        reject_message_ = message;
        rejection_ = result;
    }
    void cleanup_error(Result result) {
        cleanup_ = result;
    }
    void resolve_fault() {
        fault_resolved_ = true;
    }
    std::optional<Frame> event() {
        return events_.pop();
    }
    uint64_t next_deadline() const;
    State state() const {
        return state_;
    }
    uint64_t session() const {
        return session_;
    }
    uint64_t snapshot() const {
        return snapshot_;
    }
    uint64_t calibration() const {
        return calibration_;
    }
    uint64_t now() const {
        return now_;
    }
    uint64_t lease_end() const {
        return lease_end_;
    }
    uint32_t configuration_generation() const {
        return config_generation_;
    }
    uint64_t admitted_runs{}, completed_pulses{}, stop_requests{};
    size_t event_high_water() const {
        return events_.high_water;
    }
};
} // namespace x1::f2::simulation
