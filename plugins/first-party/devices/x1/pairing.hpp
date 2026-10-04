#pragma once
// Private X1 pairing policy. Native counters are local to each camera.
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
namespace x1 {
inline constexpr size_t pairing_capacity = 2; // front + one bracketing observation
inline constexpr uint64_t startup_discard_limit = 32; // total across both cameras
inline constexpr uint64_t steady_state_discard_limit = 2; // total between published pairs
// Worst arbitrary phase is half a period. Recommend 20% headroom on that
// timestamp bound; requested FPS is only a baseline, checked against observations.
inline uint64_t nominal_half_period_ns(uint32_t fps) { return (500000000ull + fps - 1) / fps; }
inline uint64_t recommended_software_tolerance_ns(uint32_t fps) { return (600000000ull + fps - 1) / fps; }
template<class T, size_t Capacity = pairing_capacity> class PendingQueue {
    std::array<std::optional<T>, Capacity> slots_;
    size_t head_{}, size_{};
  public:
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    const T &operator[](size_t i) const { return *slots_.at((head_ + i) % Capacity); }
    void push(T value) {
        if (size_ == Capacity) throw std::runtime_error("Pairing pending queue saturated");
        slots_[(head_ + size_) % Capacity] = std::move(value); ++size_;
    }
    void pop() {
        if (!size_) throw std::runtime_error("Empty pairing pending queue");
        slots_[head_].reset(); head_ = (head_ + 1) % Capacity; --size_;
    }
    void clear() { while (size_) pop(); }
};
struct PairingObservation {
    uint32_t sequence;
    int64_t timestamp;
    std::string_view clock;
};
enum class PairAction { left, right, pair, discard_left, discard_right, fail };
struct PairDecision { PairAction action; const char *error{}; };
inline uint64_t timestamp_distance(int64_t a, int64_t b) {
    // Unsigned subtraction also handles the complete signed timestamp range.
    return a >= b ? uint64_t(a) - uint64_t(b) : uint64_t(b) - uint64_t(a);
}
inline int64_t native_sequence_offset(uint32_t left, uint32_t right) {
    const uint32_t difference = left - right;
    return difference <= uint32_t(std::numeric_limits<int32_t>::max()) ? int64_t(difference)
        : int64_t(difference) - (int64_t{1} << 32);
}
inline PairDecision choose_pair(std::span<const PairingObservation> left,
                               std::span<const PairingObservation> right,
                               bool hardware, uint64_t tolerance) {
    if (left.empty()) return {PairAction::left};
    if (right.empty()) return {PairAction::right};
    const auto &l = left[0], &r = right[0];
    if (l.clock.empty() || l.clock == "linux.v4l2.unknown" || l.clock != r.clock)
        return {PairAction::fail, "Camera timestamp clocks are not comparable"};
    const auto distance = timestamp_distance(l.timestamp, r.timestamp);
    if (hardware) {
        if (l.sequence != r.sequence) return {PairAction::fail, "Hardware-mode native camera counters disagree"};
        if (distance > tolerance) return {PairAction::fail, "Camera timestamp delta exceeds profile pairing limit"};
        return {PairAction::pair};
    }
    if (!distance) return {PairAction::pair}; // exact match needs no lookahead
    const bool older_left = l.timestamp < r.timestamp;
    const auto older = older_left ? left : right;
    const auto newer = older_left ? r.timestamp : l.timestamp;
    const auto discard = older_left ? PairAction::discard_left : PairAction::discard_right;
    if (older.size() >= 2) {
        if (older[1].clock != l.clock)
            return {PairAction::fail, "Camera timestamp clock changed during pairing"};
        const auto next_distance = timestamp_distance(older[1].timestamp, newer);
        if (older[1].timestamp >= newer && distance > tolerance && next_distance > tolerance)
            return {PairAction::fail, "Nearest camera timestamps exceed profile pairing limit"};
        // Ties select the earlier observation, independently of receive order.
        if (next_distance < distance) {
            return {discard};
        }
        if (distance <= tolerance) return {PairAction::pair};
    }
    return {older_left ? PairAction::left : PairAction::right};
}
} // namespace x1
