#include "cloud_retry.hpp"
#include <iostream>

#define CHECK(value)                                                                                         \
    do {                                                                                                     \
        if (!(value))                                                                                        \
            throw std::runtime_error(#value);                                                                \
    } while (false)
int main() {
    try {
        using S = mantis::Status;
        using State = CloudRetry::State;
        CloudRetry r;
        CloudRetry::Key key{"/exact/project", "opaque-id", "hash", 1, 123, 1};
        CHECK(!r.due(0));
        r.observe(key);
        CHECK(r.due({})); // First discovery is immediate, even with an unavailable clock.
        int64_t now = 100;
        for (auto delay : {2000, 4000, 8000, 16000, 30000}) {
            r.failed(S::io, now);
            CHECK(r.state() == State::cooling && r.deadline() == now + delay);
            for (auto tick = now; tick < now + delay; tick += 500)
                CHECK(!r.due(tick));
            CHECK(!r.due({}) && !r.due(-1) && r.due(now + delay));
            now += delay;
            r.observe(key); // Identical evidence cannot erase the failure count/deadline.
        }
        r.failed(S::io, now);
        CHECK(r.state() == State::suppressed && r.failures() == 6 && !r.due(INT64_MAX));
        for (int i = 0; i < 1000; ++i)
            r.failed(S::busy, now);
        CHECK(r.failures() == 6 && !r.due(INT64_MAX));
        r.succeeded();
        CHECK(r.state() == State::loaded && !r.due(now));
        r.failed(S::io, now); // Explicit failure after prior success must not revive auto-loading.
        CHECK(r.state() == State::suppressed && !r.due(INT64_MAX));
        for (auto status : {S::corrupt, S::incompatible, S::not_found, S::unsupported, S::invalid_argument}) {
            r.observe({});
            r.observe(key);
            r.failed(status, 0);
            CHECK(!r.due(INT64_MAX) && r.state() == State::suppressed);
            r.failed(S::io, 0);
            CHECK(!r.due(INT64_MAX)); // Failed manual bypass stays terminal.
            r.succeeded();
            CHECK(r.state() == State::loaded && r.failures() == 0);
        }
        for (auto status : {S::io, S::busy, S::cancelled, S::plugin_failed, static_cast<S>(99)}) {
            r.observe({});
            r.observe(key);
            r.failed(status, 0);
            CHECK(!r.due(1999) && r.due(2000));
        }
        r.observe({});
        r.observe(key);
        r.failed({}, 0);
        CHECK(!r.due(1999) && r.due(2000));
        for (auto clock :
             {std::optional<int64_t>{}, std::optional<int64_t>{-1}, std::optional<int64_t>{INT64_MAX}}) {
            r.observe({});
            r.observe(key);
            r.failed(S::io, clock);
            CHECK(!r.deadline() && !r.due(INT64_MAX));
        }
        // Exact identities and published content evidence form the key, never a display prefix.
        for (int field = 0; field < 6; ++field) {
            r.observe(key);
            r.failed(S::corrupt, 0);
            auto changed = key;
            switch (field) {
            case 0:
                changed.project += "/other";
                break;
            case 1:
                changed.artifact += "-new";
                break;
            case 2:
                changed.hash += "-new";
                break;
            case 3:
                ++changed.schema;
                break;
            case 4:
                ++changed.bytes;
                break;
            case 5:
                ++changed.chunks;
                break;
            }
            r.observe(changed);
            CHECK(r.due(0) && r.failures() == 0);
        }
        r.observe({});
        CHECK(!r.key() && !r.due(0));
        std::cout << "PASS: monotonic deadlines 2/4/8/16/30s, six-attempt budget, permanent/manual "
                     "suppression, exact keys and invalid/overflow clocks\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
