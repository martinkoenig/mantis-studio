#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>
#if defined(MANTIS_TEST_WAIT_WRAP)
#include <pthread.h>
#endif

namespace test {
// GNU/Linux link wrappers observe the REAL wait, including calls instantiated
// inside static runtime libraries. No production headers, layouts or hot paths
// change. Each observer is armed only on one selected worker thread.
class ObservedWait {
#if defined(MANTIS_TEST_WAIT_WRAP)
    std::mutex mutex_;
    std::condition_variable changed_;
    pthread_mutex_t *waiting_mutex_{};
    pthread_cond_t *condition_{};
    bool active_{};
    unsigned generation_{}, timeouts_{};
#endif
    static thread_local ObservedWait *current_;

  public:
    class Arm {
        ObservedWait *previous_;

      public:
        explicit Arm(ObservedWait &wait) : previous_(std::exchange(current_, &wait)) {}
        ~Arm() {
            current_ = previous_;
        }
    };
    bool blocked(std::chrono::milliseconds timeout = std::chrono::seconds{1});
    // Emergency notification AFTER cancellation/close/clock release. This is
    // cleanup only; positive wakeup assertions always precede it.
    void wake();
    unsigned timeouts();
#if defined(MANTIS_TEST_WAIT_WRAP)
    static ObservedWait *enter(pthread_cond_t *, pthread_mutex_t *);
    void leave(int result);
#endif
};
} // namespace test
