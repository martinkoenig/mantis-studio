#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>

// PR #4's deliberate allocation stays separate from every passing workload.
[[gnu::noinline]] static void leakForDetectorControl() {
    auto *allocation = static_cast<unsigned char *>(std::malloc(123));
    if (!allocation)
        std::abort();
    allocation[0] = 42;
    asm volatile("" : : "r"(allocation) : "memory");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::fprintf(stderr, "Intentional LSan control; runtime Qt %s\n", qVersion());
    leakForDetectorControl();
    return 0;
}
