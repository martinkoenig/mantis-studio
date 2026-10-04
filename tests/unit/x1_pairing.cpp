#include "../../plugins/first-party/devices/x1/pairing.hpp"
#include <iostream>
#include <vector>
using namespace x1;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
int main() {
    try {
        constexpr int64_t period = 8390000, tolerance = 4000000;
        auto frame = [](uint32_t seq, int64_t t) { return PairingObservation{seq, t, "linux.monotonic"}; };
        auto decide = [&](std::vector<PairingObservation> l, std::vector<PairingObservation> r, bool hw = false, bool aligned = false) {
            return choose_pair(l, r, hw, tolerance, aligned).action;
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
        CHECK(decide({frame(6, 6 * period), frame(7, 7 * period)}, {frame(0, 6 * period + 1800000)}, false, true) == PairAction::pair);
        CHECK(decide({frame(0, 0), frame(1, period)}, {frame(0, period / 2)}) == PairAction::fail); // neither within 4ms
        CHECK(decide({frame(0, 0), frame(1, 6000000)}, {frame(0, 3000000)}) == PairAction::pair); // tie -> older
        CHECK(decide({frame(0, 0), frame(1, 6000000)}, {frame(0, 4000000)}) == PairAction::discard_left);
        CHECK(decide({frame(0, 0), frame(1, 6000000)}, {frame(0, 4000000)}, false, true) == PairAction::fail);
        CHECK(decide({frame(0, 0)}, {frame(0, period)}, false, true) == PairAction::fail);
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
