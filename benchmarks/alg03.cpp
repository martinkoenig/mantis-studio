#include "../tests/support/alg03_values.hpp"
#include <chrono>
#include <iostream>
#include <set>
using namespace alg03_fixture;
int main() {
    try {
        Temp temp;
        artifact::Store store(temp.path);
        auto g = graph(store);
        // One finite software pass at each workload. No retries, device access,
        // clock/timing claim about physical acquisition or accuracy qualification.
        for (uint32_t n : {0u, 1u, 4096u, 65536u}) {
            auto input = observation(g, n);
            auto binding = bind(store, g, input, plane(g), policy());
            auto prepared = get(a3::prepare(store, binding));
            auto start = std::chrono::steady_clock::now();
            auto output = get(a3::process(prepared, input));
            auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
                    .count();
            std::set<const void *> roots;
            uint64_t backing = 0, payload = 0;
            for (auto &a : output->attributes) {
                payload += a.buffer.size();
                if (roots.insert(a.buffer.identity()).second)
                    backing += a.buffer.backing_size();
            }
            if (backing > a3::max_result_bytes + a3::max_source_bytes || payload > a3::max_result_bytes)
                throw std::runtime_error("ALG03 output backing cap");
            std::cout << "samples=" << n << " process_ns=" << elapsed << " output_payload_bytes=" << payload
                      << " engine_retained_bytes=" << backing << " output_backings=" << roots.size() << "\n";
            CancellationToken cancel;
            cancel.cancel();
            auto stopped = a3::process(prepared, input, cancel);
            if (stopped || stopped.error().code != Status::cancelled)
                throw std::runtime_error("ALG03 cancelled benchmark emitted output");
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
