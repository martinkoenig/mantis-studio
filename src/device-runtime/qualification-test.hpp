#pragma once
#ifdef MANTIS_RECORDER_OBSERVER
#include "recorder_observer.hpp"
#define MANTIS_RECORDER(...) __VA_ARGS__
#define MANTIS_RECORDER_CALL(kind, item, expression)                                                         \
    ::mantis::recorder_observer::measured(::mantis::recorder_observer::Kind::kind, item,                     \
                                          [&] { return (expression); })
#else
#define MANTIS_RECORDER(...) ((void)0)
#define MANTIS_RECORDER_CALL(kind, item, expression) (expression)
#endif
// Private compile-time sampling seam, linked only by the Linux storage
// qualification executable. Product Session has no sampler or runtime selector.
#ifdef MANTIS_SESSION_QUALIFICATION
#include <cstdint>
namespace mantis::device::qualification_test {
void admission(int64_t began, int64_t ended, bool accepted) noexcept;
}
#endif

// Constructor scheduling only, in its isolated regression executable. The
// product still calls condition_variable::wait_for with the unchanged budget.
#ifdef MANTIS_SESSION_STARTUP_TESTING
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string_view>
namespace mantis::device {
class Session;
}
namespace mantis::device::startup_test {
bool wait(std::condition_variable &, std::unique_lock<std::mutex> &, std::chrono::seconds,
          const std::function<bool()> &);
void checkpoint(std::string_view, const Session &);
} // namespace mantis::device::startup_test
#define MANTIS_SESSION_WAIT(cv, lock, budget, predicate)                                                     \
    ::mantis::device::startup_test::wait(cv, lock, budget, predicate)
#define MANTIS_SESSION_STARTUP_POINT(point, session)                                                         \
    ::mantis::device::startup_test::checkpoint(point, session)
#else
#define MANTIS_SESSION_WAIT(cv, lock, budget, predicate) cv.wait_for(lock, budget, predicate)
#define MANTIS_SESSION_STARTUP_POINT(point, session) ((void)0)
#endif
