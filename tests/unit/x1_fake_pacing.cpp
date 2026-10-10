#include "../../plugins/first-party/devices/x1/fake.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace x1;
using namespace std::chrono_literals;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error("Check failed: " #x);                                                   \
    } while (false)

namespace {
struct Clock {
    FakeTiming::Clock::time_point value{};
    unsigned waits{};
    std::shared_ptr<FakeTiming> timing() {
        return std::make_shared<FakeTiming>(FakeTiming{[this] { return value; },
                                                       [this](auto deadline) {
                                                           ++waits;
                                                           value = std::max(value, deadline);
                                                       }});
    }
};
struct Frame {
    std::vector<std::byte> pixels;
    uint32_t sequence, width, height, stride, buffer_size, flags;
    int64_t timestamp, received;
    std::string clock, format;
    Metadata controls;
    explicit Frame(const FrameView &v)
        : pixels(v.bytes.begin(), v.bytes.end()), sequence(v.sequence), width(v.width), height(v.height),
          stride(v.stride), buffer_size(v.buffer_size), flags(v.flags), timestamp(v.timestamp_ns),
          received(v.received_ns), clock(v.clock), format(v.fourcc), controls(v.controls) {}
    bool operator==(const Frame &) const = default;
};
void pace(const char *value) {
    CHECK(value ? setenv("MANTIS_X1_FAKE_PACE", value, 1) == 0 : unsetenv("MANTIS_X1_FAKE_PACE") == 0);
}
struct Fixture {
    Clock clock;
    std::unique_ptr<Backend> backend;
    std::unique_ptr<Camera> camera;
    Mode mode{64, 48, 120, "GREY", false};
    CameraInfo identity;
    Fixture(const std::string &scenario, bool right, const char *paced, const std::string &format = "GREY") {
        pace(paced);
        backend = fake_backend(scenario, clock.timing());
        identity = backend->discover().at(right ? 1 : 0);
        mode.fourcc = format;
        camera = backend->open(identity, mode);
        camera->start();
    }
    ~Fixture() {
        camera->stop();
    }
    Frame next() {
        std::optional<Frame> result;
        CHECK(camera->next(1000, [&](const FrameView &v) {
            CHECK(!result);
            result.emplace(v);
        }));
        CHECK(result);
        return std::move(*result);
    }
    auto period() const {
        return std::chrono::nanoseconds(1000000000 / mode.fps);
    }
};
void finite_rate(const std::string &scenario) {
    for (bool right : {false, true})
        for (const auto &format : {"GREY", "Y10P"}) {
            Fixture f(scenario, right, "1", format);
            CHECK(f.next().sequence == 0);
            auto previous = f.clock.value;
            for (uint32_t i = 1; i < 70; ++i) {
                auto frame = f.next();
                CHECK(frame.sequence == i);
                CHECK(f.clock.value - previous == f.period());
                previous = f.clock.value;
            }
            CHECK(f.clock.waits == 69);
        }
}
void delayed() {
    for (const auto &scenario : {"startup-left", "startup-right", "phase-half", "normal", "eagain"}) {
        Fixture startup(scenario, false, "1");
        // Camera start can precede scheduling of the acquisition thread. Even
        // its very first delivery must not inherit a backlog of host deadlines.
        startup.clock.value += 500ms;
        CHECK(startup.next().sequence == 0);
        auto previous = startup.clock.value;
        for (uint32_t i = 1; i < 66; ++i) {
            CHECK(startup.next().sequence == i);
            CHECK(startup.clock.value - previous == startup.period());
            previous = startup.clock.value;
        }
        Fixture f(scenario, false, "1");
        CHECK(f.next().sequence == 0);
        // Simulate a descheduled caller: one pending delivery is allowed immediately,
        // but no accumulated deadline debt can release the following frames in a burst.
        f.clock.value += 1s;
        const auto late = f.clock.value;
        CHECK(f.next().sequence == 1);
        CHECK(f.clock.value == late);
        for (uint32_t i = 2; i < 66; ++i) {
            const auto before = f.clock.value;
            CHECK(f.next().sequence == i);
            CHECK(f.clock.value - before == f.period());
        }
        // A blocked delivery callback must also schedule from completion, not entry.
        CHECK(f.camera->next(1000, [&](const FrameView &v) {
            CHECK(v.sequence == 66);
            f.clock.value += 1s;
        }));
        const auto completed = f.clock.value;
        bool emitted{};
        CHECK(!f.camera->next(1, [&](const FrameView &) { emitted = true; }));
        CHECK(!emitted);
        CHECK(f.next().sequence == 67); // timeout neither consumes nor fabricates a frame
        CHECK(f.clock.value - completed == f.period());
    }
    Fixture delayed_start("startup-delayed-right", true, "1");
    CHECK(!delayed_start.camera->next(100, [](const FrameView &) { CHECK(false); }));
    CHECK(delayed_start.next().sequence == 0);
    CHECK(delayed_start.clock.value.time_since_epoch() == 250ms);
}
void native_preservation() {
    for (const auto &scenario : {"normal",
                                 "startup-left",
                                 "startup-right",
                                 "phase-half",
                                 "phase-4300",
                                 "phase-drift-left",
                                 "phase-drift-right",
                                 "phase-drift-gap",
                                 "phase-jitter-left",
                                 "phase-jitter-right",
                                 "startup-left-gap",
                                 "startup-left-repeat",
                                 "startup-left-reverse",
                                 "startup-left-timestamp-jump",
                                 "startup-left-clock-change",
                                 "startup-left-steady-delta",
                                 "drop-left",
                                 "drop-right",
                                 "mismatch",
                                 "repeat",
                                 "timestamp-jump",
                                 "unknown-clock",
                                 "incomparable"}) {
        for (bool right : {false, true})
            for (const auto &format : {"GREY", "Y10P"}) {
                Fixture original(scenario, right, "0", format);
                Fixture paced(scenario, right, "1", format);
                CHECK(original.identity.sensor == paced.identity.sensor);
                CHECK(original.identity.bus == paced.identity.bus);
                CHECK(original.identity.video == paced.identity.video);
                CHECK(original.identity.formats == paced.identity.formats);
                CHECK(original.identity.role == paced.identity.role);
                CHECK(original.identity.media == paced.identity.media);
                CHECK(original.identity.route == paced.identity.route);
                for (unsigned i = 0; i < 48; ++i) {
                    if (i == 7)
                        paced.clock.value += 1s;
                    CHECK(original.next() == paced.next());
                }
            }
    }
}
void unpaced() {
    for (const char *setting : {static_cast<const char *>(nullptr), "0"}) {
        Fixture fast("phase-half", false, setting);
        for (unsigned i = 0; i < 128; ++i)
            CHECK(fast.next().sequence == i);
        CHECK(fast.clock.waits == 0);
        CHECK(fast.clock.value == FakeTiming::Clock::time_point{});
        // Preserve the old cumulative schedule for startup stress fixtures.
        Fixture cumulative("startup-left", true, setting);
        CHECK(cumulative.next().sequence == 0);
        cumulative.clock.value += 1s;
        auto late = cumulative.clock.value;
        for (unsigned i = 1; i < 64; ++i)
            CHECK(cumulative.next().sequence == i);
        CHECK(cumulative.clock.value == late);
    }
}
template <class F> void rejects(F fn, const std::string &message) {
    try {
        fn();
    } catch (const std::exception &e) {
        CHECK(std::string(e.what()).find(message) != std::string::npos);
        return;
    }
    throw std::runtime_error("Expected failure: " + message);
}
void failures() {
    for (const char *setting : {"0", "1"}) {
        pace(setting);
        for (bool right : {false, true}) {
            Clock clock;
            auto backend = fake_backend(right ? "streamon-right" : "streamon-left", clock.timing());
            auto camera = backend->open(backend->discover().at(right ? 1 : 0), Mode{});
            rejects([&] { camera->start(); }, "STREAMON failed");
            Fixture off(right ? "streamoff-right" : "streamoff-left", right, setting);
            off.camera->stop();
            CHECK(off.camera->diagnostics().at("streamoff_error").find("STREAMOFF failed") !=
                  std::string::npos);
            Fixture stalled(right ? "stall-right" : "stall-left", right, setting);
            CHECK(!stalled.camera->next(100, [](const FrameView &) { CHECK(false); }));
            CHECK(stalled.clock.value.time_since_epoch() == 100ms);
        }
        Fixture disconnected("disconnect", true, setting);
        for (unsigned i = 0; i < 4; ++i)
            CHECK(disconnected.next().sequence == i);
        rejects([&] { (void)disconnected.next(); }, "disconnected");
        Fixture stopped("stop-right", true, setting);
        for (unsigned i = 0; i < 4; ++i)
            CHECK(stopped.next().sequence == i);
        CHECK(!stopped.camera->next(100, [](const FrameView &) { CHECK(false); }));
    }
    pace("invalid");
    rejects([] { (void)fake_backend(); }, "must be 0 or 1");
    pace("1");
    rejects([] { (void)fake_backend("normal", std::make_shared<FakeTiming>()); }, "Incomplete");
}
} // namespace
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        const std::string test = argv[1];
        if (test == "startup-left" || test == "startup-right")
            finite_rate(test);
        else if (test == "phase") {
            for (const auto &scenario : {"phase-half", "phase-drift-left", "phase-jitter-right"})
                finite_rate(scenario);
        } else if (test == "delayed")
            delayed();
        else if (test == "native")
            native_preservation();
        else if (test == "unpaced")
            unpaced();
        else if (test == "failures")
            failures();
        else
            throw std::runtime_error("Unknown test");
        std::cout << "Fake-camera " << test << " passed with an injected clock\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
