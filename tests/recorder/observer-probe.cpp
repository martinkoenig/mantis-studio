#include "../../src/device-runtime/recorder-diagnostics.hpp"
#include <cstdlib>
#include <iostream>
#include <new>
#include <nlohmann/json.hpp>
namespace {
thread_local bool counting{};
thread_local size_t allocations{};
void counted() noexcept {
    if (counting)
        ++allocations;
}
} // namespace
void *operator new(std::size_t n) {
    counted();
    if (auto *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void *p) noexcept {
    std::free(p);
}
void operator delete[](void *p) noexcept {
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept {
    std::free(p);
}
void *operator new(std::size_t n, std::align_val_t alignment) {
    counted();
    void *p{};
    if (posix_memalign(&p, static_cast<size_t>(alignment), n ? n : 1) == 0)
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n, std::align_val_t a) {
    return ::operator new(n, a);
}
void operator delete(void *p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void *p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete(void *p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
int main() {
    using namespace mantis;
    try {
        counting = true;
        auto *sensitivity = ::operator new(17);
        counting = false;
        ::operator delete(sensitivity);
        if (allocations != 1)
            throw std::runtime_error("allocation counter insensitive");
        constexpr unsigned iterations = 4096;
        data::Packet packet;
        packet.header.timestamp.domain.id = {"probe-clock"};
        auto measure = [&] {
            allocations = 0;
            const auto began = time::MonotonicTimestamp::now().nanoseconds;
            counting = true;
            for (unsigned i = 0; i < iterations; ++i)
                recorder_diagnostics::event("probe.record", &packet, i, "probe-source");
            counting = false;
            return time::MonotonicTimestamp::now().nanoseconds - began;
        };
        const auto disabled_ns = measure();
        if (allocations != 0)
            throw std::runtime_error("disabled event allocated");
        auto *trace = recorder_diagnostics::open(&packet, {{"probe-capture"}, {{"probe-source"}}, {}});
        if (!trace)
            throw std::runtime_error("probe observer unavailable");
        int64_t enabled_ns{};
        {
            recorder_diagnostics::Worker worker(trace, 0);
            enabled_ns = measure();
            if (allocations != 0)
                throw std::runtime_error("enabled event allocated");
        }
        recorder_diagnostics::cleanup(&packet, false);
        recorder_diagnostics::cleanup(&packet, true);
        for (unsigned i = 1; i < 8; ++i) {
            auto *additional =
                recorder_diagnostics::open(&packet, {{"bounded-capture"}, {{"probe-source"}}, {}});
            if (!additional)
                throw std::runtime_error("unexpected session admission failure");
            recorder_diagnostics::cleanup(&packet, false);
            recorder_diagnostics::cleanup(&packet, true);
        }
        if (recorder_diagnostics::open(&packet, {{"ninth-capture"}, {{"probe-source"}}, {}}))
            throw std::runtime_error("session/output retention bound not enforced");
        std::cout << nlohmann::json({{"iterations", iterations},
                                     {"disabled_ns", disabled_ns},
                                     {"enabled_ns", enabled_ns},
                                     {"allocations_per_record", 0},
                                     {"scope", "fixed diagnostic record microprobe; excludes "
                                               "setup/serialization; not pipeline speedup"}})
                         .dump()
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        counting = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
