#pragma once
#include <array>
#include <cstdint>
#include <sys/types.h>

namespace alg04_test {
inline constexpr size_t bulk_min = 4096;
struct FileIdentity {
    dev_t device{};
    ino_t inode{};
};
struct BulkIO {
    uint64_t mappings{}, mapping_bytes{}, source_maps{}, source_map_bytes{};
    uint64_t copies{}, copy_bytes{}, largest_copy{};
    uint64_t source_copies{}, source_copy_bytes{};
    bool source_tracking_incomplete{};
};
// A single synchronous test sample. Nothing is active on other threads or
// outside this scope; wrapped functions only update fixed-size counters.
class Probe {
  public:
    const FileIdentity source;
    BulkIO io;
    struct Range {
        uintptr_t start{};
        size_t bytes{};
    };
    std::array<Range, 2> live_source_mappings{};
    explicit Probe(FileIdentity = {});
    ~Probe();
    Probe(const Probe &) = delete;
    Probe &operator=(const Probe &) = delete;
};
// Not an algorithm copy. An explicit linker-resolved memcpy checks probe liveness.
void copy_control(void *out, const void *in, size_t bytes);
} // namespace alg04_test
