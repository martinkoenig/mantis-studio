#include <iostream>
#include <mantis/pipeline_api.hpp>
#include <thread>
int main() {
    mantis::pipeline::BoundedQueue<uint64_t> queue(64, mantis::pipeline::QueuePolicy::lossless);
    constexpr uint64_t count = 100000;
    auto start = std::chrono::steady_clock::now();
    std::thread producer([&] {
        for (uint64_t i = 0; i < count; ++i)
            queue.push(i);
        queue.close();
    });
    uint64_t sum = 0;
    while (auto n = queue.pop())
        sum += *n;
    producer.join();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
                  .count();
    std::cout << "items=" << count << " ns/item=" << ns / static_cast<int64_t>(count)
              << " high_water=" << queue.metrics().high_water << " dropped=" << queue.metrics().dropped
              << " checksum=" << sum << '\n';
    return sum == count * (count - 1) / 2 ? 0 : 1;
}
