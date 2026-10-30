#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>

// A separate executable: this intentional leak never enters Studio or its suite.
// The compiler barrier makes the allocation observable even with optimization.
[[gnu::noinline]] static void leakForDetectorControl() {
    auto *allocation = static_cast<unsigned char *>(std::malloc(123));
    if (!allocation)
        std::abort();
    allocation[0] = 42;
    asm volatile("" : : "r"(allocation) : "memory");
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // LSan may terminate before buffered stdout is flushed.
    std::fprintf(stderr, "Intentional LSan control; runtime Qt %s\n", qVersion());
    leakForDetectorControl();
    return 0;
}
