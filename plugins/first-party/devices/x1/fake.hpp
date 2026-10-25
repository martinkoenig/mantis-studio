#pragma once
#include "backend.hpp"
namespace x1 {
// Private fixture-only host scheduling seam. Native frame facts never use this clock.
struct FakeTiming {
    using Clock = std::chrono::steady_clock;
    std::function<Clock::time_point()> now;
    std::function<void(Clock::time_point)> wait_until;
};
std::unique_ptr<Backend> fake_backend(const std::string &scenario, std::shared_ptr<const FakeTiming>);
} // namespace x1
