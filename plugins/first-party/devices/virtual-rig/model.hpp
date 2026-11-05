#pragma once
#include "../x1/pairing.hpp"
#include "profile.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <vector>
namespace virtual_rig {
struct Event {
    uint64_t index{}, trigger{}, generation{}, trigger_generation{};
    uint32_t native_sequence{};
    int64_t requested{}, exposure{}, native{}, available{}, received{};
    bool timestamp_valid{true}, comparable{true}, counter_valid{true};
};
struct Accounting {
    uint64_t exposures{}, source_drops{}, missing_triggers{}, overruns{}, received{}, published{},
        startup_unmatched{}, steady_unmatched{}, shutdown_unmatched{}, high_water{};
};
// This model stores finite captured scene samples, not a deadline-debt queue.
// Pixels are a pure function of a captured sample and the immutable static scene.
// Materializing a sample into SDK memory later cannot create a new exposure.
class Model {
    const Profile &p_;
    std::array<uint64_t, 2> cursor_{};
    static uint64_t mix(uint64_t x) {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ull;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebull;
        return x ^ (x >> 31);
    }
    const Fault *fault(size_t i, uint64_t n) const {
        for (const auto &f : p_.faults)
            if (f.camera == i && f.at == n)
                return &f;
        return nullptr;
    }
    uint64_t end_at(size_t i, int64_t t) const {
        // Monotone exposure times: jitter <= 1/8 period. A binary search bounds
        // work even for a jump over the entire supported 24-hour generation.
        uint64_t lo{}, hi = 20736002;
        while (lo < hi) {
            auto mid = lo + (hi - lo) / 2;
            if (exposure(i, mid) <= t)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

  public:
    uint64_t generation;
    int64_t now{};
    std::array<std::vector<Event>, 2> source;
    std::array<x1::PendingQueue<Event>, 2> pending;
    std::array<std::optional<Event>, 2> inflight;
    std::array<Accounting, 2> stats;
    std::array<std::optional<uint32_t>, 2> last_sequence;
    std::array<std::optional<int64_t>, 2> last_exposure;
    uint64_t publications{}, exclusions_since_pair{};
    std::vector<std::pair<size_t, Event>> exclusions;
    explicit Model(const Profile &p, uint64_t gen) : p_(p), generation(gen) {
        for (size_t i = 0; i < p.cameras.size(); ++i)
            source[i].reserve(p.capacity);
        exclusions.reserve(x1::startup_discard_limit);
    }
    int64_t requested(size_t i, uint64_t n) const {
        const auto &c = p_.cameras[i];
        // quotient/remainder avoids overflow from n * 1e9 * rate_den.
        const auto numerator = 1000000000ull * c.rate_den;
        return int64_t(n * (numerator / c.rate_num) + n * (numerator % c.rate_num) / c.rate_num);
    }
    int64_t exposure(size_t i, uint64_t n) const {
        const auto &c = p_.cameras[i];
        const auto noise = int64_t(mix(p_.seed ^ (uint64_t(i) << 48) ^ n) % (uint64_t(c.jitter) + 1));
        return requested(i, n) + (p_.triggered ? c.latency : c.phase) + noise;
    }
    int64_t next_exposure() const {
        int64_t result = horizon_ns;
        for (size_t i = 0; i < p_.cameras.size(); ++i)
            result = std::min(result, exposure(i, cursor_[i]));
        return result;
    }
    Event sample(size_t i, uint64_t n) const {
        const auto &c = p_.cameras[i];
        Event e;
        e.index = e.trigger = n;
        e.native_sequence = uint32_t(n);
        e.generation = e.trigger_generation = generation;
        e.requested = requested(i, n);
        e.exposure = exposure(i, n);
        e.native = e.exposure + c.offset + (e.exposure / 1000000) * c.drift_ppm +
                   (e.exposure % 1000000) * c.drift_ppm / 1000000;
        e.available = e.exposure + c.delivery;
        e.comparable = c.comparable;
        if (const auto f = fault(i, n)) {
            if (f->kind == "duplicate-trigger")
                --e.trigger;
            if (f->kind == "stale-trigger")
                --e.trigger_generation;
            if (f->kind == "generation-change")
                ++e.generation;
            if (f->kind == "duplicate-counter" || f->kind == "reverse-counter" ||
                f->kind == "reset-counter") {
                e.counter_valid = false;
                e.native_sequence =
                    f->kind == "reset-counter" ? 0 : uint32_t(n - (f->kind == "reverse-counter" ? 2 : 1));
            }
            if (f->kind == "missing-time")
                e.timestamp_valid = false;
            if (f->kind == "incomparable-time")
                e.comparable = false;
            if (f->kind == "delay")
                e.available += f->delay;
        }
        for (const auto &f : p_.faults)
            if (f.camera == i && f.kind == "stall" && n >= f.at)
                e.available = std::max(e.available, exposure(i, f.at) + f.delay);
        return e;
    }
    void advance(int64_t target) {
        if (target < now || target > horizon_ns)
            throw std::runtime_error("Virtual clock must be monotone within the 24-hour generation");
        std::optional<std::pair<int64_t, size_t>> terminal;
        for (const auto &f : p_.faults)
            if (f.kind == "disconnect" && f.at >= cursor_[f.camera]) {
                const auto key = std::pair(exposure(f.camera, f.at), f.camera);
                if (key.first <= target && (!terminal || key < *terminal))
                    terminal = key;
            }
        for (size_t i = 0; i < p_.cameras.size(); ++i) {
            auto end = end_at(i, terminal ? terminal->first : target);
            // Global event order is (exposure time, camera index). The terminal
            // event itself and same-time later cameras never become exposures.
            if (terminal && i >= terminal->second && end && exposure(i, end - 1) == terminal->first)
                --end;
            const auto begin = cursor_[i];
            auto &s = stats[i];
            if (end == begin)
                continue;
            // Finite fault table is checked even when a host misses many events.
            uint64_t drops{}, missing{};
            for (const auto &f : p_.faults) {
                if (f.camera != i || f.at < begin || f.at >= end)
                    continue;
                drops += f.kind == "drop";
                missing += f.kind == "missing-trigger";
            }
            const auto observations = end - begin - drops - missing;
            const auto keep = size_t(std::min<uint64_t>(observations, p_.capacity));
            std::array<uint64_t, 16> retained{};
            size_t count{};
            auto candidate = end;
            // At most capacity + fault-count index probes, never a historical
            // exposure catch-up loop. Retain actual captures across fault holes.
            for (size_t probes = 0;
                 probes < p_.capacity + p_.faults.size() && candidate > begin && count < keep; ++probes) {
                --candidate;
                const auto f = fault(i, candidate);
                if (!f || (f->kind != "drop" && f->kind != "missing-trigger"))
                    retained[count++] = candidate;
            }
            if (count != keep)
                throw std::runtime_error("Bounded source selection invariant failed");
            s.exposures += end - begin - missing;
            s.source_drops += drops;
            s.missing_triggers += missing;
            s.overruns += observations - keep;
            auto &q = source[i];
            if (observations >= p_.capacity) {
                s.overruns += q.size();
                q.clear();
            }
            // At most capacity samples per camera, independent of elapsed time.
            while (count) {
                const auto n = retained[--count];
                if (q.size() == p_.capacity) {
                    q.erase(q.begin());
                    ++s.overruns;
                }
                q.push_back(sample(i, n));
                s.high_water = std::max(s.high_water, uint64_t(q.size()));
            }
            cursor_[i] = end;
        }
        now = terminal ? terminal->first : target;
        if (terminal)
            throw std::runtime_error("Simulated camera disconnected");
    }
    bool receive(size_t i, int64_t host_now, int64_t reference_now) {
        auto &q = source[i];
        if (q.empty())
            return false;
        auto it = std::min_element(q.begin(), q.end(), [](const Event &a, const Event &b) {
            return std::pair(a.available, a.index) < std::pair(b.available, b.index);
        });
        if (it->available > reference_now)
            return false;
        auto e = *it;
        // Retire received invalid observations too; accounting survives failures.
        q.erase(it);
        ++stats[i].received;
        e.received = host_now;
        pending[i].push(e);
        if (e.generation != generation)
            throw std::runtime_error("Camera generation changed during acquisition");
        if (!e.counter_valid || (last_sequence[i] && e.native_sequence <= *last_sequence[i]))
            throw std::runtime_error("Native counter repeated, reversed or reset");
        if (last_exposure[i] && e.exposure <= *last_exposure[i])
            throw std::runtime_error("Out-of-order exposure observation");
        if (p_.triggered && (e.trigger != e.index || e.trigger_generation != generation))
            throw std::runtime_error("Invalid per-exposure trigger association or generation");
        last_sequence[i] = e.native_sequence;
        last_exposure[i] = e.exposure;
        return true;
    }
    void exclude(size_t i) {
        const auto limit = publications ? x1::steady_state_discard_limit : x1::startup_discard_limit;
        if (exclusions_since_pair == limit)
            throw std::runtime_error("Timestamp realignment exceeded unmatched-observation limit");
        exclusions.emplace_back(i, pending[i][0]);
        pending[i].pop();
        ++exclusions_since_pair;
        if (publications)
            ++stats[i].steady_unmatched;
        else
            ++stats[i].startup_unmatched;
    }
    void retire() noexcept {
        for (size_t i = 0; i < p_.cameras.size(); ++i) {
            stats[i].shutdown_unmatched += source[i].size() + pending[i].size();
            source[i].clear();
            pending[i].clear();
        }
    }
    void retire_inflight() noexcept {
        for (size_t i = 0; i < p_.cameras.size(); ++i)
            if (inflight[i]) {
                ++stats[i].shutdown_unmatched;
                inflight[i].reset();
            }
    }
};
} // namespace virtual_rig
