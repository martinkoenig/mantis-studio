#include "../../plugins/first-party/devices/x1/pairing.hpp"
#include <iostream>
#include <set>
#include <vector>
using namespace x1;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
struct Trace {
    std::array<uint64_t, 2> received{}, startup{}, steady{};
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
};
Trace simulate(int64_t left_period, int64_t right_period, int64_t phase, uint32_t count) {
    Trace trace;
    std::array<std::vector<PairingObservation>, 2> pending;
    std::array<std::set<uint32_t>, 2> accounted;
    uint64_t since_pair{};
    while (trace.pairs.size() < count) {
        auto decision = choose_pair(pending[0], pending[1], false, 5000000);
        CHECK(decision.action != PairAction::fail);
        if (decision.action == PairAction::pair) {
            CHECK(timestamp_distance(pending[0][0].timestamp, pending[1][0].timestamp) <= 5000000);
            trace.pairs.emplace_back(pending[0][0].sequence, pending[1][0].sequence);
            for (size_t i = 0; i < 2; ++i) {
                CHECK(accounted[i].insert(pending[i][0].sequence).second);
                pending[i].erase(pending[i].begin());
            }
            since_pair = 0;
        } else if (decision.action == PairAction::discard_left || decision.action == PairAction::discard_right) {
            size_t i = decision.action == PairAction::discard_right ? 1 : 0;
            if (trace.pairs.empty()) ++trace.startup[i];
            else { ++trace.steady[i]; CHECK(++since_pair <= steady_state_discard_limit); }
            CHECK(accounted[i].insert(pending[i][0].sequence).second);
            pending[i].erase(pending[i].begin());
        } else {
            size_t i = decision.action == PairAction::right ? 1 : 0;
            CHECK(pending[i].size() < pairing_capacity);
            uint32_t native = static_cast<uint32_t>(trace.received[i]++);
            pending[i].push_back({native, int64_t(native) * (i ? right_period : left_period) + (i ? phase : 0), "linux.monotonic"});
        }
    }
    for (size_t i = 0; i < 2; ++i) {
        CHECK(trace.received[i] == trace.pairs.size() + trace.startup[i] + trace.steady[i] + pending[i].size());
        for (auto f : pending[i]) CHECK(accounted[i].insert(f.sequence).second);
        CHECK(accounted[i].size() == trace.received[i]); // each observation accounted exactly once
    }
    return trace;
}
int main() {
    try {
        constexpr int64_t period = 8390000, tolerance = 4000000;
        auto frame = [](uint32_t seq, int64_t t) { return PairingObservation{seq, t, "linux.monotonic"}; };
        auto decide = [&](std::vector<PairingObservation> l, std::vector<PairingObservation> r, bool hw = false) {
            return choose_pair(l, r, hw, tolerance).action;
        };
        CHECK(decide({}, {}) == PairAction::left);
        CHECK(decide({frame(0, 0)}, {}) == PairAction::right);
        CHECK(decide({frame(0, 0)}, {frame(0, 0)}) == PairAction::pair);
        CHECK(decide({frame(0, 0)}, {frame(0, 1800000)}) == PairAction::left);
        CHECK(decide({frame(6, 6 * period), frame(7, 7 * period)}, {frame(0, 6 * period + 1800000)}) == PairAction::pair);
        CHECK(decide({frame(0, 6 * period + 1800000)}, {frame(6, 6 * period), frame(7, 7 * period)}) == PairAction::pair);
        for (uint32_t seq = 0; seq < 6; ++seq) {
            CHECK(decide({frame(seq, seq * period), frame(seq + 1, (seq + 1) * period)},
                         {frame(0, 6 * period + 1800000)}) == PairAction::discard_left);
            CHECK(decide({frame(0, 6 * period + 1800000)},
                         {frame(seq, seq * period), frame(seq + 1, (seq + 1) * period)}) == PairAction::discard_right);
        }
        CHECK(decide({frame(6, 6 * period), frame(7, 7 * period)}, {frame(0, 6 * period + 1800000)}, false) == PairAction::pair);
        CHECK(decide({frame(0, 0), frame(1, period)}, {frame(0, period / 2)}) == PairAction::fail); // neither within 4ms
        CHECK(decide({frame(0, 0), frame(1, 6000000)}, {frame(0, 3000000)}) == PairAction::pair); // tie -> older
        CHECK(decide({frame(0, 0), frame(1, 6000000)}, {frame(0, 4000000)}) == PairAction::discard_left);
        CHECK(decide({frame(0, 0)}, {frame(0, period)}, false) == PairAction::left);
        CHECK(decide({frame(6, 0)}, {frame(0, 0)}, true) == PairAction::fail);
        CHECK(decide({frame(6, 0)}, {frame(6, 1800000)}, true) == PairAction::pair);
        CHECK(decide({frame(6, 0)}, {frame(6, period)}, true) == PairAction::fail);
        for (bool hw : {false, true}) {
            CHECK(decide({frame(0, 0)}, {{0, 0, "other.clock"}}, hw) == PairAction::fail);
            CHECK(decide({{0, 0, "linux.v4l2.unknown"}}, {{0, 0, "linux.v4l2.unknown"}}, hw) == PairAction::fail);
            CHECK(decide({{0, 0, ""}}, {{0, 0, ""}}, hw) == PairAction::fail);
        }
        CHECK(native_sequence_offset(6, 0) == 6 && native_sequence_offset(0, 6) == -6);
        CHECK(native_sequence_offset(1, UINT32_MAX) == 2);
        CHECK(timestamp_distance(INT64_MIN, INT64_MAX) == UINT64_MAX);
        CHECK(nominal_half_period_ns(120) == 4166667);
        CHECK(recommended_software_tolerance_ns(120) == 5000000);
        CHECK(recommended_software_tolerance_ns(60) == 10000000);
        constexpr int64_t measured_period = 8384000;
        // Both signs, every phase over a period, and the exact tie are exercised.
        for (int64_t phase = -measured_period; phase <= measured_period; phase += 1000) {
            auto trace = simulate(measured_period, measured_period, phase, 4);
            CHECK(trace.steady[0] == 0 && trace.steady[1] == 0);
        }
        for (int64_t nearest : {4000000, 4100000, 4192000, 4200000, 4300000}) {
            const int64_t p = std::max(measured_period, 2 * nearest);
            std::vector<PairingObservation> l{frame(0, 0), frame(1, p)}, r{frame(0, nearest)};
            CHECK(choose_pair(l, r, false, 5000000).action == PairAction::pair);
            if (nearest > 4000000) CHECK(choose_pair(l, r, false, 4000000).action == PairAction::fail);
            // Hardware mode still applies its independent strict configured bound.
            CHECK((choose_pair(l, r, true, 4000000).action == PairAction::pair) == (nearest <= 4000000));
        }
        auto left_drift = simulate(measured_period, measured_period + 210, measured_period / 2 - 5000, 90000);
        auto right_drift = simulate(measured_period + 210, measured_period, measured_period / 2 + 5000, 90000);
        CHECK(left_drift.steady[0] >= 3 && left_drift.steady[1] == 0);
        CHECK(right_drift.steady[1] >= 3 && right_drift.steady[0] == 0);
        CHECK(left_drift.pairs == simulate(measured_period, measured_period + 210, measured_period / 2 - 5000, 90000).pairs);
        PendingQueue<int> queue;
        queue.push(1); queue.push(2);
        bool saturated{}; try { queue.push(3); } catch (const std::runtime_error &) { saturated = true; }
        CHECK(saturated && queue.size() == 2 && queue[0] == 1 && queue[1] == 2);
        queue.pop(); queue.push(3); CHECK(queue[0] == 2 && queue[1] == 3);
        queue.clear(); CHECK(queue.empty());
        std::cout << "bounded timestamp-nearest pairing passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
