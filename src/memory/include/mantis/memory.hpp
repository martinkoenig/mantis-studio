#pragma once
#include <cstring>
#include <mantis/base.hpp>
#include <new>
#include <numeric>
#include <optional>
namespace mantis::memory {
enum class MemoryDomain { host_pageable, host_pinned, shared_memory, device_local, external_device, unified };
enum class Access { read_only, read_write };
struct Fence {
    virtual ~Fence() = default;
    virtual Result<void> wait(const CancellationToken &) const = 0;
};
struct Storage {
    std::shared_ptr<const void> owner;
    const std::byte *host{};
    size_t size{}, alignment{};
    size_t retained_extent{}; // conservative backing extent for owner-retaining host wrappers
    MemoryDomain domain{MemoryDomain::host_pageable};
    std::string backend, device_id;
    std::shared_ptr<const Fence> ready;
};
class BufferView {
    std::shared_ptr<const Storage> storage_;
    size_t offset_{}, size_{};

  public:
    BufferView() = default;
    BufferView(std::shared_ptr<const Storage> storage, size_t offset, size_t size)
        : storage_(std::move(storage)), offset_(offset), size_(size) {
        if (!storage_ || offset > storage_->size || size > storage_->size - offset)
            fail(Status::invalid_argument, "Buffer view out of range");
    }
    size_t size() const {
        return size_;
    }
    size_t alignment() const {
        return storage_ ? std::gcd(storage_->alignment, offset_) : 0;
    }
    MemoryDomain domain() const {
        return storage_ ? storage_->domain : MemoryDomain::host_pageable;
    }
    Access access() const {
        return Access::read_only;
    }
    BufferView slice(size_t offset, size_t size) const {
        if (offset > size_ || size > size_ - offset)
            fail(Status::invalid_argument, "Slice out of range");
        return {storage_, offset_ + offset, size};
    }
    Result<std::span<const std::byte>> map_read(const CancellationToken &token = {}) const {
        if (!storage_)
            return std::unexpected(Error{Status::invalid_argument, "Empty buffer", "memory"});
        if (storage_->ready) {
            auto r = storage_->ready->wait(token);
            if (!r)
                return std::unexpected(r.error());
        }
        if (!storage_->host)
            return std::unexpected(Error{Status::unsupported,
                                         "Buffer is not host-addressable; request an explicit transfer",
                                         "memory"});
        return std::span<const std::byte>{storage_->host + offset_, size_};
    }
    size_t backing_size() const noexcept {
        return storage_ ? std::max(storage_->size, storage_->retained_extent) : 0;
    }
    const void *identity() const noexcept {
        return storage_.get();
    }
};
using Buffer = BufferView;
class BufferBuilder {
    std::shared_ptr<void> owner_;
    std::byte *data_{};
    size_t size_{}, alignment_{};

  public:
    explicit BufferBuilder(size_t size, size_t alignment = 64) : size_(size), alignment_(alignment) {
        if (alignment < alignof(void *) || (alignment & (alignment - 1)) || size > 256 * 1024 * 1024)
            fail(Status::invalid_argument, "Invalid buffer size/alignment");
        data_ = static_cast<std::byte *>(::operator new(size ? size : 1, std::align_val_t(alignment)));
        owner_ = {data_, [alignment](void *p) { ::operator delete(p, std::align_val_t(alignment)); }};
    }
    BufferBuilder(const BufferBuilder &) = delete;
    BufferBuilder &operator=(const BufferBuilder &) = delete;
    BufferBuilder(BufferBuilder &&) = default;
    BufferBuilder &operator=(BufferBuilder &&) = default;
    std::span<std::byte> writable() {
        if (!owner_)
            fail(Status::invalid_argument, "Buffer already published");
        return {data_, size_};
    }
    Buffer publish() && {
        if (!owner_)
            fail(Status::invalid_argument, "Buffer already published");
        auto storage = std::make_shared<Storage>();
        storage->owner = std::move(owner_);
        storage->host = data_;
        storage->size = size_;
        storage->alignment = alignment_;
        data_ = nullptr;
        return {std::move(storage), 0, size_};
    }
};
inline Buffer copy(std::span<const std::byte> bytes) {
    BufferBuilder b(bytes.size());
    std::memcpy(b.writable().data(), bytes.data(), bytes.size());
    return std::move(b).publish();
}
} // namespace mantis::memory
