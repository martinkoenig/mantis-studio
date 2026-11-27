#include "probe.hpp"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <sys/mman.h>
#include <sys/stat.h>

namespace {
thread_local alg04_test::Probe *current{};
void copy(const void *src, size_t n) {
    if (current && n >= alg04_test::bulk_min) {
        ++current->io.copies;
        current->io.copy_bytes += n;
        current->io.largest_copy = std::max<uint64_t>(current->io.largest_copy, n);
        const auto p = reinterpret_cast<uintptr_t>(src);
        size_t overlap{};
        for (const auto &r : current->live_source_mappings) {
            if (!r.bytes)
                continue;
            if (p < r.start && n > r.start - p)
                overlap += std::min(n - (r.start - p), r.bytes);
            else if (p >= r.start && p - r.start < r.bytes)
                overlap += std::min(n, r.bytes - (p - r.start));
        }
        if (overlap) {
            ++current->io.source_copies;
            current->io.source_copy_bytes += overlap;
        }
    }
}
void mapping(void *address, int fd, size_t bytes) {
    if (!current || address == MAP_FAILED || fd < 0)
        return;
    ++current->io.mappings;
    current->io.mapping_bytes += bytes;
    // Classify the actual source by device and inode, not by mapping size.
    // This probe is separate from timing; fstat does not affect the production result.
    const int saved_errno = errno;
    struct stat info {};
    if (fstat(fd, &info) != 0) {
        current->io.source_tracking_incomplete = true;
    } else if (info.st_dev == current->source.device && info.st_ino == current->source.inode) {
        ++current->io.source_maps;
        current->io.source_map_bytes += bytes;
        auto free = std::ranges::find_if(current->live_source_mappings, [](auto r) { return !r.bytes; });
        if (free != current->live_source_mappings.end())
            *free = {reinterpret_cast<uintptr_t>(address), bytes};
        else
            current->io.source_tracking_incomplete = true;
    }
    errno = saved_errno;
}
} // namespace

alg04_test::Probe::Probe(FileIdentity f) : source(f) {
    assert(!current);
    current = this;
}
alg04_test::Probe::~Probe() {
    current = nullptr;
}

extern "C" void *__real_memcpy(void *, const void *, size_t);
extern "C" void *__real_memmove(void *, const void *, size_t);
extern "C" void *__wrap_memcpy(void *out, const void *in, size_t n) {
    copy(in, n);
    return __real_memcpy(out, in, n);
}
extern "C" void *__wrap_memmove(void *out, const void *in, size_t n) {
    copy(in, n);
    return __real_memmove(out, in, n);
}
extern "C" void *copy_symbol(void *, const void *, size_t) asm("memcpy");
void alg04_test::copy_control(void *out, const void *in, size_t n) {
    (void)copy_symbol(out, in, n);
}
extern "C" void *__real_mmap(void *, size_t, int, int, int, off_t);
extern "C" void *__real_mmap64(void *, size_t, int, int, int, off64_t);
extern "C" int __real_munmap(void *, size_t);
extern "C" void *__wrap_mmap(void *hint, size_t bytes, int prot, int flags, int fd, off_t offset) {
    auto *p = __real_mmap(hint, bytes, prot, flags, fd, offset);
    mapping(p, fd, bytes);
    return p;
}
extern "C" void *__wrap_mmap64(void *hint, size_t bytes, int prot, int flags, int fd, off64_t offset) {
    auto *p = __real_mmap64(hint, bytes, prot, flags, fd, offset);
    mapping(p, fd, bytes);
    return p;
}
extern "C" int __wrap_munmap(void *address, size_t bytes) {
    const auto status = __real_munmap(address, bytes);
    if (current && !status) {
        for (auto &r : current->live_source_mappings)
            if (r.bytes && r.start == reinterpret_cast<uintptr_t>(address)) {
                if (r.bytes != bytes)
                    current->io.source_tracking_incomplete = true;
                r = {};
            }
    }
    return status;
}
