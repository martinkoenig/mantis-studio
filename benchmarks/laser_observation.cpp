#include "../tests/unit/laser_observation_values.hpp"
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <mantis/pipeline_runtime.hpp>
#include <mantis/plugin_runtime.hpp>
#include <new>
#include <thread>
// Benchmark-only replaceable allocation instrumentation, outside production libraries.
namespace allocation {
thread_local bool enabled = false;
thread_local uint64_t calls = 0, bytes = 0;
void count(size_t n) {
    if (enabled) {
        ++calls;
        bytes += n;
    }
}
} // namespace allocation
void *operator new(size_t n) {
    allocation::count(n);
    if (auto *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }
void *operator new(size_t n, std::align_val_t a) {
    allocation::count(n);
    auto alignment = static_cast<size_t>(a);
    auto rounded = ((n ? n : 1) + alignment - 1) / alignment * alignment;
    if (auto *p = std::aligned_alloc(alignment, rounded))
        return p;
    throw std::bad_alloc();
}
void *operator new[](size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void *p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t, std::align_val_t) noexcept { std::free(p); }
using namespace observation_fixture;
struct NullWriter : std::streambuf {
    uint64_t bytes{};
    std::streamsize xsputn(const char *, std::streamsize n) override {
        bytes += static_cast<uint64_t>(n);
        return n;
    }
    int_type overflow(int_type c) override {
        if (!traits_type::eq_int_type(c, traits_type::eof()))
            ++bytes;
        return traits_type::not_eof(c);
    }
};
struct Measurement {
    double us{}, allocations{}, bytes{};
};
template <class F> Measurement measure(unsigned repetitions, F f) {
    allocation::calls = 0;
    allocation::bytes = 0;
    auto begin = std::chrono::steady_clock::now();
    allocation::enabled = true;
    try {
        for (unsigned i = 0; i < repetitions; ++i)
            f();
    } catch (...) {
        allocation::enabled = false;
        throw;
    }
    allocation::enabled = false;
    return {std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count() /
                repetitions,
            double(allocation::calls) / repetitions, double(allocation::bytes) / repetitions};
}
class Producer : public pipeline::SemanticNodeInstance {
    plugins::Loaded &plugin;

  public:
    explicit Producer(plugins::Loaded &p) : plugin(p) {}
    Result<data::SemanticPublished> process(std::span<const data::SemanticPublished> input,
                                            const CancellationToken &token) override {
        try {
            return plugins::process_semantic(plugin, *input[0], 60000, token);
        } catch (const Failure &e) {
            return std::unexpected(e.error);
        }
    }
};
int main(int argc, char **argv) {
    try {
        if (argc != 2 && argc != 3)
            fail(Status::invalid_argument,
                 "Usage: mantis-observation-benchmark SYNTHETIC_PLUGIN [C_PASSTHROUGH_PLUGIN]");
        auto dir = std::filesystem::temp_directory_path() / Id::random().value;
        std::filesystem::create_directory(dir);
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(p, ec);
            }
        } cleanup{dir};
        if (argc == 3) {
#ifdef _WIN32
            _putenv_s("MANTIS_PROCESSOR_TEST_MODE", "0");
#else
            if (setenv("MANTIS_PROCESSOR_TEST_MODE", "0", 1))
                fail(Status::io, "Contract setup failed");
#endif
            plugins::Loaded contract(argv[2]);
            auto image = SemanticPacket{images()->frames[0]};
            auto overhead = measure(100, [&] {
                auto output = plugins::process_semantic(contract, image);
                if (!output)
                    fail(Status::corrupt, "Missing pass-through output");
            });
            std::cout << "native_passthrough_us=" << overhead.us << ",allocations=" << overhead.allocations
                      << ",allocation_bytes=" << overhead.bytes << '\n';
        }
        plugins::Loaded plugin(argv[1]);
        auto input = data::publish(SemanticPacket{bundle(1)});
        std::cout
            << "N,encoded_bytes,serialize_us,serialize_MiB_s,serialize_allocs,mapped_decode_us,decode_allocs,"
               "v2_us,v2_allocs,v2_alloc_bytes,pipeline_us,pipeline_allocs,application_bulk_copies\n";
        for (uint64_t n : {0ull, 2ull, 4096ull, 65536ull}) {
            auto obs = observation(0, n);
            auto path = dir / (std::to_string(n) + ".observation");
            write_laser_observation(path, obs);
#ifdef _WIN32
            _putenv_s("MANTIS_SYNTHETIC_OBSERVATION_FIXTURE", path.string().c_str());
#else
            if (setenv("MANTIS_SYNTHETIC_OBSERVATION_FIXTURE", path.c_str(), 1))
                fail(Status::io, "Fixture setup failed");
#endif
            auto bytes = encode(obs);
            auto storage = view(bytes);
            unsigned repetitions = n >= 65536 ? 5 : 20;
            auto serialization = measure(repetitions, [&] {
                NullWriter sink;
                std::ostream out(&sink);
                write_laser_observation(out, obs);
            });
            auto decode = measure(repetitions, [&] {
                auto value = read_laser_observation(storage);
                for (const auto &a : value.attributes)
                    if (a.buffer.identity() != storage.identity())
                        fail(Status::corrupt, "Unexpected bulk copy");
            });
            auto call = measure(repetitions, [&] {
                auto value = plugins::process_semantic(plugin, *input, 60000);
                if (!value)
                    fail(Status::corrupt, "Missing output");
            });
            pipeline::NodeDescriptor source;
            source.id = "source";
            source.outputs = {{"bundle", schema::acquisition_bundle}};
            pipeline::SemanticGraph graph;
            graph.nodes = {
                {source, {}},
                {plugins::describe_semantic_node(plugin), [&] { return std::make_unique<Producer>(plugin); }},
                pipeline::observation_consumer()};
            graph.connections = {{0, 0, 1, 0, 2, pipeline::QueuePolicy::lossless},
                                 {1, 0, 2, 0, 2, pipeline::QueuePolicy::lossless}};
            auto plan = pipeline::compile(graph);
            if (!plan)
                throw Failure(plan.error());
            auto pipeline = measure(repetitions, [&] {
                auto r = pipeline::execute(*plan, input);
                if (!r)
                    throw Failure(r.error());
            });
            std::cout << n << ',' << bytes.size() << ',' << serialization.us << ','
                      << double(bytes.size()) / serialization.us * 1e6 / (1024 * 1024) << ','
                      << serialization.allocations << ',' << decode.us << ',' << decode.allocations << ','
                      << call.us << ',' << call.allocations << ',' << call.bytes << ',' << pipeline.us << ','
                      << pipeline.allocations << ",0\n";
            pipeline::BoundedQueue<data::SemanticPublished> in(2, pipeline::QueuePolicy::lossless);
            pipeline::BoundedQueue<pipeline::SemanticExecutionResult> out(2, pipeline::QueuePolicy::lossless);
            std::stop_source stop;
            auto started = std::chrono::steady_clock::now();
            auto run = std::async(std::launch::async,
                                  [&] { return pipeline::execute_stream(*plan, in, out, stop.get_token()); });
            std::thread producer([&] {
                for (unsigned i = 0; i < 20; ++i)
                    in.push(input);
                in.close();
            });
            uint64_t count = 0;
            while (out.pop())
                ++count;
            producer.join();
            auto r = run.get();
            if (!r)
                throw Failure(r.error());
            double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            std::cout << "stream,N=" << n << ",observations_s=" << double(count) / seconds
                      << ",input_hwm=" << in.metrics().high_water
                      << ",output_hwm=" << out.metrics().high_water
                      << ",dropped=" << in.metrics().dropped + out.metrics().dropped << '\n';
        }
        pipeline::BoundedQueue<data::SemanticPublished> in(1, pipeline::QueuePolicy::lossless);
        pipeline::BoundedQueue<pipeline::SemanticExecutionResult> out(1, pipeline::QueuePolicy::lossless);
        pipeline::NodeDescriptor source;
        source.id = "source";
        source.outputs = {{"bundle", schema::acquisition_bundle}};
        pipeline::SemanticGraph graph;
        graph.nodes = {{source, {}}, {plugins::describe_semantic_node(plugin), [&] {
                                          return std::make_unique<Producer>(plugin);
                                      }}};
        graph.connections = {{0, 0, 1, 0, 1, pipeline::QueuePolicy::lossless}};
        auto plan = pipeline::compile(graph);
        if (!plan)
            throw Failure(plan.error());
        std::stop_source stop;
        auto pending = std::async(std::launch::async,
                                  [&] { return pipeline::execute_stream(*plan, in, out, stop.get_token()); });
        auto begin = std::chrono::steady_clock::now();
        stop.request_stop();
        auto r = pending.get();
        if (r)
            fail(Status::corrupt, "Cancellation falsely succeeded");
        std::cout
            << "cancellation_us="
            << std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count()
            << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
