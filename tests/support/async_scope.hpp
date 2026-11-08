#pragma once
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace test {
// Borrowed fixtures must be declared before Async. The destructor body unblocks
// work before future's destructor joins, including during assertion unwind.
template <class Cleanup, class Work> class Async {
    Cleanup cleanup_;

  public:
    std::future<std::invoke_result_t<Work>> future;
    Async(Cleanup cleanup, Work work)
        : cleanup_(std::move(cleanup)), future(std::async(std::launch::async, std::move(work))) {}
    Async(const Async &) = delete;
    Async &operator=(const Async &) = delete;
    ~Async() {
        cleanup_();
    }
};
template <class F> struct OnExit {
    F action;
    ~OnExit() {
        action();
    }
};
// Exercise an ordinary CHECK exception, not an alternate no-throw test path.
// Returning here also proves every future in work's scope has been retired.
template <class F>
void assertion_unwinds(std::string_view diagnostic, F work,
                       std::chrono::seconds bound = std::chrono::seconds{1}) {
    const auto began = std::chrono::steady_clock::now();
    bool caught = false;
    std::string primary;
    try {
        work();
    } catch (const std::runtime_error &e) {
        if (std::string_view{e.what()}.find(diagnostic) == std::string_view::npos)
            throw; // Keep unexpected primary diagnostics intact.
        caught = true;
        primary = e.what();
    }
    if (!caught)
        throw std::runtime_error("Missing assertion: " + std::string{diagnostic});
    if (std::chrono::steady_clock::now() - began >= bound)
        throw std::runtime_error("Assertion cleanup exceeded bound; primary: " + primary);
}
} // namespace test
