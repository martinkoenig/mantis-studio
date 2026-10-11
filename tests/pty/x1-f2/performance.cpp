#include "common.hpp"
#include <cstdlib>
#include <new>
#include <time.h>

namespace {
std::atomic<bool> measuring{};
std::atomic<uint64_t> allocations{};
} // namespace
void *operator new(size_t n) {
    if (measuring.load(std::memory_order_relaxed))
        ++allocations;
    if (auto p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](size_t n) {
    return ::operator new(n);
}
void *operator new(size_t n, std::align_val_t alignment) {
    if (measuring.load(std::memory_order_relaxed))
        ++allocations;
    void *p;
    if (::posix_memalign(&p, static_cast<size_t>(alignment), n ? n : 1) != 0)
        throw std::bad_alloc();
    return p;
}
void *operator new[](size_t n, std::align_val_t a) {
    return ::operator new(n, a);
}
void operator delete(void *p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void *p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete(void *p, size_t, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void *p, size_t, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete(void *p) noexcept {
    std::free(p);
}
void operator delete[](void *p) noexcept {
    std::free(p);
}
void operator delete(void *p, size_t) noexcept {
    std::free(p);
}
void operator delete[](void *p, size_t) noexcept {
    std::free(p);
}
using namespace ptytest;
int main() {
    try {
        Pty p;
        measuring = true;
        FdTransport t(p.take_slave());
        measuring = false;
        auto startup = allocations.exchange(0);
        const size_t count = 1000;
        std::array<int64_t, count> latency{};
        std::exception_ptr error;
        std::jthread peer([&] {
            try {
                for (size_t i = 0; i < count; ++i) {
                    auto w = read_wire(p.master);
                    auto f = decode(w.data().subspan(1, w.size - 2));
                    CHECK(f);
                    write_bytes(p.master, encode(reply(*f))->data());
                }
            } catch (...) {
                error = std::current_exception();
            }
        });
        auto q = unknown(0x1234, 132);
        auto w = *encode(q);
        measuring = true;
        auto begin = Clock::now();
        for (size_t i = 0; i < count; ++i) {
            auto start = Clock::now();
            CHECK(t.submit(w) == TransportStatus::ready);
            CHECK(t.receive(q, deadline()));
            latency[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        }
        auto duration = Clock::now() - begin;
        measuring = false;
        peer.join();
        if (error)
            std::rethrow_exception(error);
        CHECK(allocations == 0);
        std::sort(latency.begin(), latency.end());
        auto m = t.metrics();
        std::cout << "sizeof(FdTransport)=" << sizeof(FdTransport)
                  << "; worker startup C++ allocations=" << startup
                  << "; capacities TX/RX/events=" << FdTransport::tx_capacity << '/'
                  << FdTransport::response_capacity << '/' << FdTransport::event_capacity
                  << "; high-water=" << m.tx_high << '/' << m.response_high << '/' << m.event_high
                  << "; representative C++ new allocations=" << allocations << "\nPTY " << count
                  << " maximum REQUEST roundtrips; wire bytes/s="
                  << double(m.tx_bytes + m.rx_bytes) / std::chrono::duration<double>(duration).count()
                  << "; ns p50/p95/p99=" << latency[500] << '/' << latency[950] << '/' << latency[990]
                  << '\n';
        // Idle measurements use a timed wait as observation, not a race synchronizer.
        CHECK(Access::await(t, [](auto v) { return !v.active_size; }, deadline()));
        auto before = t.metrics();
        auto cpu_clock = Access::cpu_clock(t);
        timespec cpu_before{}, cpu_after{};
        CHECK(::clock_gettime(cpu_clock, &cpu_before) == 0);
        auto idle_start = Clock::now();
        ::poll(nullptr, 0, 100);
        auto idle = t.metrics();
        CHECK(::clock_gettime(cpu_clock, &cpu_after) == 0);
        std::cout << "Idle ms="
                  << std::chrono::duration<double, std::milli>(Clock::now() - idle_start).count()
                  << "; worker poll wake delta=" << idle.wakes - before.wakes << "; worker CPU ns="
                  << (int64_t(cpu_after.tv_sec) - cpu_before.tv_sec) * 1'000'000'000 + cpu_after.tv_nsec -
                         cpu_before.tv_nsec
                  << '\n';
        CHECK(idle.wakes - before.wakes <= 1);
        Access::gate(t, true);
        CHECK(t.submit(w) == TransportStatus::ready);
        CHECK(Access::await(t, [](auto v) { return v.active_size; }, deadline()));
        for (size_t i = 0; i < FdTransport::tx_capacity; ++i)
            CHECK(t.submit(w) == TransportStatus::ready);
        auto stop = *encode(request(Message::stop));
        auto start = Clock::now();
        CHECK(t.submit(stop) == TransportStatus::ready);
        auto admitted = Clock::now();
        Access::gate(t, false);
        CHECK(read_wire(p.master) == stop);
        std::cout << "Congested STOP enqueue ns="
                  << std::chrono::duration_cast<std::chrono::nanoseconds>(admitted - start).count()
                  << "; first complete wire delivery ns="
                  << std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()
                  << '\n';
    } catch (const std::exception &e) {
        measuring = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
