#include "observed_wait.hpp"
#include <cerrno>
#include <thread>
#include <utility>

namespace test {
thread_local ObservedWait *ObservedWait::current_ = nullptr;
#if defined(MANTIS_TEST_WAIT_WRAP)
ObservedWait *ObservedWait::enter(pthread_cond_t *condition, pthread_mutex_t *mutex) {
    auto *observer = current_;
    if (observer) {
        std::lock_guard lock(observer->mutex_);
        observer->waiting_mutex_ = mutex;
        observer->condition_ = condition;
        observer->active_ = true;
        ++observer->generation_;
        observer->changed_.notify_all();
    }
    return observer;
}
void ObservedWait::leave(int result) {
    std::lock_guard lock(mutex_);
    active_ = false;
    if (result == ETIMEDOUT)
        ++timeouts_;
    changed_.notify_all();
}
bool ObservedWait::blocked(std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock lock(mutex_);
    for (;;) {
        if (!changed_.wait_until(lock, deadline, [&] { return active_; }))
            return false;
        const auto generation = generation_;
        auto *mutex = waiting_mutex_;
        lock.unlock();
        // enter() runs with the wait mutex HELD. Acquiring that same mutex
        // proves the underlying condition wait has atomically released it.
        // Merely reaching the wrapper or starting the worker is insufficient.
        const auto acquired = pthread_mutex_trylock(mutex) == 0;
        lock.lock();
        const bool blocked = acquired && active_ && generation_ == generation;
        if (acquired)
            pthread_mutex_unlock(mutex);
        if (blocked)
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        lock.unlock();
        std::this_thread::yield();
        lock.lock();
    }
}
void ObservedWait::wake() {
    std::lock_guard lock(mutex_);
    if (active_)
        pthread_cond_broadcast(condition_);
}
unsigned ObservedWait::timeouts() {
    std::lock_guard lock(mutex_);
    return timeouts_;
}
#else
// The project's qualified matrix is GNU/Linux with libstdc++. Unsupported
// harnesses fail the entry assertion explicitly instead of claiming a proof.
bool ObservedWait::blocked(std::chrono::milliseconds) {
    return false;
}
void ObservedWait::wake() {}
unsigned ObservedWait::timeouts() {
    return 0;
}
#endif
} // namespace test

#if defined(MANTIS_TEST_WAIT_WRAP)
// condition_variable::wait is out of line in libstdc++; wrapping pthread only
// would miss its call inside that DSO. Timed waits are inlined and use pthread.
extern "C" void real_wait(std::condition_variable *, std::unique_lock<std::mutex> &) asm(
    "__real__ZNSt18condition_variable4waitERSt11unique_lockISt5mutexE");
extern "C" void wrapped_wait(std::condition_variable *, std::unique_lock<std::mutex> &) asm(
    "__wrap__ZNSt18condition_variable4waitERSt11unique_lockISt5mutexE");
extern "C" void wrapped_wait(std::condition_variable *condition, std::unique_lock<std::mutex> &lock) {
    auto *observer = test::ObservedWait::enter(condition->native_handle(), lock.mutex()->native_handle());
    real_wait(condition, lock);
    if (observer)
        observer->leave(0);
}
extern "C" int __real_pthread_cond_clockwait(pthread_cond_t *, pthread_mutex_t *, clockid_t,
                                             const timespec *);
extern "C" int __wrap_pthread_cond_clockwait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                                             clockid_t clock, const timespec *deadline) {
    auto *observer = test::ObservedWait::enter(condition, mutex);
    auto result = __real_pthread_cond_clockwait(condition, mutex, clock, deadline);
    if (observer)
        observer->leave(result);
    return result;
}
extern "C" int __real_pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const timespec *);
extern "C" int __wrap_pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                                             const timespec *deadline) {
    auto *observer = test::ObservedWait::enter(condition, mutex);
    auto result = __real_pthread_cond_timedwait(condition, mutex, deadline);
    if (observer)
        observer->leave(result);
    return result;
}
#endif
