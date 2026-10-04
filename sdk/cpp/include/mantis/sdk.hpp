#pragma once
#include <mantis/plugin.h>
#include <span>
#include <stdexcept>
#include <type_traits>
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

namespace mantis::sdk {
// RAII client of the public acquisition table, usable by plugin development tools.
class Acquisition {
    const MantisAcquisitionV1 *api_;
    void *instance_{};
  public:
    Acquisition(const MantisAcquisitionV1 *api, const MantisHostV1 *host, const char *id) : api_(api) {
        if (!compatible_table(api_) || !compatible(host) || !api_->open || !api_->destroy ||
            !api_->start || !api_->next || !api_->stop || !id) throw std::runtime_error("Invalid acquisition interface");
        check(api_->open(host, id, &instance_));
        if (!instance_) throw std::runtime_error("Acquisition returned null instance");
    }
    ~Acquisition() { if (instance_) { api_->stop(instance_); api_->destroy(instance_); } }
    Acquisition(const Acquisition &) = delete;
    Acquisition &operator=(const Acquisition &) = delete;
    Acquisition(Acquisition &&other) noexcept : api_(other.api_), instance_(std::exchange(other.instance_, nullptr)) {}
    void start() { check(api_->start(instance_)); }
    void stop() { check(api_->stop(instance_)); }
    int next(uint32_t timeout_ms, MantisFrameSetEmitV1 emit, void *context) { return api_->next(instance_, timeout_ms, emit, context); }
};
template<class F> void enumerate(const MantisAcquisitionV1 *api, F &&fn) {
    if (!compatible_table(api) || !api->enumerate) throw std::runtime_error("Invalid acquisition enumeration interface");
    auto callback = [](void *context, const MantisDiscoveredDeviceV1 *descriptor) noexcept {
        return boundary([&] {
            if (!compatible_table(descriptor)) throw std::runtime_error("Invalid discovered descriptor");
            (*static_cast<std::remove_reference_t<F> *>(context))(*descriptor);
        });
    };
    check(api->enumerate(callback, &fn));
}
} // namespace mantis::sdk
