#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>

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
