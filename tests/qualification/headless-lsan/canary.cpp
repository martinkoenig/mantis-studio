#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#ifndef __has_feature
#define __has_feature(feature) 0
#endif

#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
constexpr int instrumented = 1;
#else
// Also compiled without sanitizers by the verifier's negative control, in a temporary directory.
constexpr int instrumented = 0;
#endif

// No pointer escapes this frame. The compiler barrier keeps the real allocation and store at optimization.
[[gnu::noinline]] void mantisHeadlessLeakCanary(std::size_t bytes) {
    auto *allocation = static_cast<unsigned char *>(std::malloc(bytes));
    if (!allocation)
        std::abort();
    allocation[0] = 0x36;
    asm volatile("" : : "r"(allocation) : "memory");
}

int main() {
    std::printf("mantis-lsan-v1 pid=%ld asan=%d\n", static_cast<long>(getpid()), instrumented);
    std::fflush(stdout);
    char nonce[65]{};
    unsigned long bytes = 0;
    if (std::scanf("%64s %lu", nonce, &bytes) != 2 || std::strlen(nonce) != 64 || bytes < 12001 ||
        bytes > 16096)
        return 2;
    std::printf("allocation nonce=%s pid=%ld bytes=%lu\n", nonce, static_cast<long>(getpid()), bytes);
    std::fflush(stdout);
    mantisHeadlessLeakCanary(static_cast<std::size_t>(bytes));
    return 0;
}
