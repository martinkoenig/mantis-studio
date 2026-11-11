#include "../tests/support/l1_candidate_values.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mantis/pipeline_runtime.hpp>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif

// Executable-only instrumentation; product allocation/ABI behavior is unchanged.
namespace counters {
thread_local bool enabled{};
thread_local uint64_t allocations{}, allocated{}, maximum_allocation{}, copied{}, source_copied{};
thread_local uintptr_t source_begin{}, source_end{};
void allocation(size_t n) {
    if (enabled) {
        ++allocations;
        allocated += n;
        maximum_allocation = std::max<uint64_t>(maximum_allocation, n);
    }
}
void copy(const void *source, size_t n) {
    if (!enabled)
        return;
    copied += n;
    auto start = reinterpret_cast<uintptr_t>(source);
    if (start < source_end && (start >= source_begin || n > source_begin - start)) {
        const auto prefix = start < source_begin ? source_begin - start : 0;
        source_copied += std::min<uint64_t>(n - prefix, source_end - std::max(start, source_begin));
    }
}
} // namespace counters
void *operator new(size_t n) {
    counters::allocation(n);
    if (auto p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](size_t n) {
    return ::operator new(n);
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
void *operator new(size_t n, std::align_val_t a) {
    counters::allocation(n);
    auto alignment = static_cast<size_t>(a);
    if (n > SIZE_MAX - alignment)
        throw std::bad_alloc();
    auto rounded = ((n ? n : 1) + alignment - 1) / alignment * alignment;
#ifdef _WIN32
    if (auto p = _aligned_malloc(rounded, alignment))
        return p;
#else
    if (auto p = std::aligned_alloc(alignment, rounded))
        return p;
#endif
    throw std::bad_alloc();
}
void *operator new[](size_t n, std::align_val_t a) {
    return ::operator new(n, a);
}
void operator delete(void *p, std::align_val_t) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void *p, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
void operator delete(void *p, size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
void operator delete[](void *p, size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
#ifdef MANTIS_L1_COPY_PROBE
extern "C" void *__real_memcpy(void *, const void *, size_t);
extern "C" void *__real_memmove(void *, const void *, size_t);
extern "C" void *__wrap_memcpy(void *destination, const void *source, size_t n) {
    counters::copy(source, n);
    return __real_memcpy(destination, source, n);
}
extern "C" void *__wrap_memmove(void *destination, const void *source, size_t n) {
    counters::copy(source, n);
    return __real_memmove(destination, source, n);
}
#endif
using namespace l1_fixture;
using namespace mantis::pipeline;
template <class F> void measure(std::string_view label, uint32_t count, F invoke) {
    for (unsigned i = 0; i < 3; ++i)
        invoke();
    std::vector<double> microseconds(count);
    counters::allocations = counters::allocated = counters::maximum_allocation = counters::copied =
        counters::source_copied = 0;
    counters::enabled = true;
    try {
        for (uint32_t i = 0; i < count; ++i) {
            auto begin = std::chrono::steady_clock::now();
            invoke();
            microseconds[i] =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
        }
    } catch (...) {
        counters::enabled = false;
        throw;
    }
    counters::enabled = false;
    std::sort(microseconds.begin(), microseconds.end());
    auto percentile = [&](unsigned p) { return microseconds[(uint64_t(count - 1) * p + 99) / 100]; };
    std::cout << label << " iterations=" << count << " p50_us=" << percentile(50)
              << " p95_us=" << percentile(95) << " p99_us=" << percentile(99)
              << " max_us=" << microseconds.back()
              << " allocations_per_call=" << double(counters::allocations) / count
              << " allocated_bytes_per_call=" << double(counters::allocated) / count
              << " largest_allocation_bytes=" << counters::maximum_allocation;
#ifdef MANTIS_L1_COPY_PROBE
    std::cout << " interposed_copy_bytes_per_call=" << double(counters::copied) / count
              << " interposed_source_copy_bytes_per_call=" << double(counters::source_copied) / count;
#else
    std::cout << " interposed_copies=UNAVAILABLE";
#endif
    std::cout << '\n';
}
int main(int argc, char **argv) {
    try {
        uint32_t repetitions = 32;
        if (argc > 2)
            fail(Status::invalid_argument, "Usage: mantis-l1-candidate-benchmark [1..1000]");
        if (argc == 2) {
            std::string argument = argv[1];
            size_t consumed{};
            auto n = std::stoul(argument, &consumed);
            if (consumed != argument.size() || n == 0 || n > 1000)
                fail(Status::invalid_argument, "Invalid iteration count");
            repetitions = static_cast<uint32_t>(n);
        }
        std::cout
            << "Synthetic host CPU 1280x720; no sensor/Q6A FPS claim. Copy probe observes linker-visible "
               "memcpy/memmove only; compiler-inlined/manual/DSO-internal copies are outside its scope.\n";
        for (bool packed : {false, true}) {
            auto b = line(packed, 1280, 720);
            const auto &buffer = b.frameset->frames[0]->attributes[0].buffer;
            auto original = buffer.map_read();
            if (!original)
                throw Failure(original.error());
            const auto hash = content_hash(*original);
            counters::source_begin = reinterpret_cast<uintptr_t>(original->data());
            counters::source_end = counters::source_begin + original->size();
            auto c = config(packed);
            auto node = l1_candidate_extractor(c);
            auto instance = node.factory();
            std::array<SemanticPublished, 1> input{data::publish(SemanticPacket{b})};
            SemanticPublished retained;
            auto direct = [&] {
                auto r = instance->process(input, {});
                if (!r)
                    throw Failure(r.error());
                retained = std::move(*r);
            };
            auto verify = [&] {
                const auto &o = std::get<LaserObservation>(*retained);
                if (o.sample_count != 1280 || value<float>(o, laser::source_pixel, 1279) != 1279 ||
                    value<float>(o, laser::source_pixel, 1279, 1) != 2 ||
                    value<uint32_t>(o, l1::peak, 1279) != (packed ? 701u : 181u) ||
                    value<uint32_t>(o, laser::quality_flags, 1279) != 0)
                    fail(Status::corrupt, "Benchmark production-path oracle failed");
                for (const auto &a : o.attributes)
                    if (a.buffer.identity() != o.attributes[0].buffer.identity())
                        fail(Status::corrupt, "Output unexpectedly uses separate column allocations");
                std::cout << " input_backing_bytes=" << buffer.backing_size()
                          << " output_backing_bytes=" << o.attributes[0].buffer.backing_size() << '\n';
            };
            measure(packed ? "Y10P node" : "RAW8 node", repetitions, direct);
            verify();
            NodeDescriptor source;
            source.id = "recorded-input";
            source.outputs = {{"bundle", schema::acquisition_bundle}};
            SemanticGraph graph;
            graph.nodes = {{source, {}}, node, observation_consumer()};
            graph.connections = {{0, 0, 1, 0, 1, QueuePolicy::lossless},
                                 {1, 0, 2, 0, 1, QueuePolicy::lossless}};
            auto plan = compile(graph);
            if (!plan)
                throw Failure(plan.error());
            uint64_t high_water{};
            measure(packed ? "Y10P DAG" : "RAW8 DAG", repetitions, [&] {
                auto r = execute(*plan, input[0]);
                if (!r)
                    throw Failure(r.error());
                high_water = r->retained_payload_high_water;
                retained = std::move(r->output);
            });
            verify();
            std::cout << " retained_payload_high_water_bytes=" << high_water << '\n';
            if (content_hash(*buffer.map_read()) != hash)
                fail(Status::corrupt, "Benchmark source pixels changed");
            counters::source_begin = counters::source_end = 0;
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
