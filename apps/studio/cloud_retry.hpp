#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <mantis/base.hpp>
#include <optional>
#include <string>
#include <utility>

// Frontend scheduling only. One candidate, no packets, QObject, transport or timer.
class CloudRetry {
  public:
    using Millis = int64_t;
    using Clock = std::function<std::optional<Millis>()>;
    struct Key {
        std::string project, artifact, hash;
        uint32_t schema{};
        uint64_t bytes{}, chunks{};
        bool operator==(const Key &) const = default;
    };
    enum class State { ready, cooling, suppressed, loaded };
    static std::optional<Millis> monotonicNow() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    void observe(std::optional<Key> key) {
        if (key_ == key)
            return;
        key_ = std::move(key);
        state_ = State::ready;
        failures_ = 0;
        deadline_.reset();
    }
    const std::optional<Key> &key() const {
        return key_;
    }
    State state() const {
        return state_;
    }
    unsigned failures() const {
        return failures_;
    }
    std::optional<Millis> deadline() const {
        return deadline_;
    }
    bool due(std::optional<Millis> now) const {
        return key_ && (state_ == State::ready ||
                        (state_ == State::cooling && now && *now >= 0 && deadline_ && *now >= *deadline_));
    }
    void succeeded() {
        state_ = State::loaded;
        failures_ = 0;
        deadline_.reset();
    }
    void failed(std::optional<mantis::Status> status, std::optional<Millis> now) {
        // A failed manual attempt cannot downgrade terminal suppression or restart its budget.
        failures_ = std::min(failures_ + 1, 6u);
        deadline_.reset();
        if (state_ == State::suppressed || state_ == State::loaded || permanent(status) || failures_ == 6) {
            state_ = State::suppressed;
            return;
        }
        state_ = State::cooling;
        constexpr Millis delays[]{2000, 4000, 8000, 16000, 30000};
        const auto delay = delays[failures_ - 1];
        // Invalid/uninitialized/overflowing clocks fail closed, never spin or wrap a deadline.
        if (now && *now >= 0 && *now <= std::numeric_limits<Millis>::max() - delay)
            deadline_ = *now + delay;
    }
    static bool permanent(std::optional<mantis::Status> status) {
        if (!status)
            return false;
        switch (*status) {
        case mantis::Status::invalid_argument:
        case mantis::Status::not_found:
        case mantis::Status::incompatible:
        case mantis::Status::unsupported:
        case mantis::Status::corrupt:
            return true;
        default:
            return false; // io/busy/cancelled/plugin_failed/untyped/unknown: bounded transient budget.
        }
    }

  private:
    std::optional<Key> key_;
    State state_{State::ready};
    unsigned failures_{};
    std::optional<Millis> deadline_;
};
