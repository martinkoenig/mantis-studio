#pragma once
#include <mantis/plugin.h>
#include <span>
#include <stdexcept>
#include <utility>
namespace mantis::sdk {
template <class T> inline bool compatible_table(const T *p) {
    return p && p->struct_size >= sizeof(T) && p->abi_version == MANTIS_ABI_V1;
}
inline void check(int status) {
    if (status)
        throw std::runtime_error("Plugin host operation failed");
}
template <class F> int boundary(F &&fn) noexcept {
    try {
        fn();
        return 0;
    } catch (...) {
        return 1;
    }
}
class Buffer {
    const MantisHostV1 *host_;
    MantisBuffer *buffer_;

  public:
    Buffer(const MantisHostV1 *host, uint64_t size) : host_(host), buffer_(host->allocate(size, 64)) {
        if (!buffer_)
            throw std::bad_alloc();
    }
    ~Buffer() {
        if (buffer_)
            host_->release(buffer_);
    }
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    Buffer(Buffer &&other) noexcept : host_(other.host_), buffer_(std::exchange(other.buffer_, nullptr)) {}
    MantisBuffer *get() const {
        return buffer_;
    }
    std::span<std::byte> writable() {
        void *data{};
        uint64_t size{};
        check(host_->write_map(buffer_, &data, &size));
        return {static_cast<std::byte *>(data), static_cast<size_t>(size)};
    }
    void publish() {
        check(host_->publish(buffer_));
    }
};
inline std::span<const std::byte> read(const MantisHostV1 *host, const MantisAttributeV1 &a) {
    const void *p{};
    uint64_t size{};
    check(host->read_map(a.buffer, &p, &size));
    if (a.offset > size || a.bytes > size - a.offset)
        throw std::out_of_range("Attribute");
    return {static_cast<const std::byte *>(p) + a.offset, static_cast<size_t>(a.bytes)};
}
inline bool compatible(const MantisHostV1 *host) {
    return host && host->struct_size >= sizeof(MantisHostV1) && host->abi_version == MANTIS_ABI_V1;
}
} // namespace mantis::sdk
