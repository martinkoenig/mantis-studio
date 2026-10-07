#pragma once
#include <algorithm>
#include <mantis/projected_light.h>
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


#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
namespace mantis::sdk {
inline void finite_timeout(uint32_t timeout_ms) {
    if (timeout_ms > MANTIS_MAX_TIMEOUT_MS)
        throw std::invalid_argument("Deadline must be finite (0..60000 ms)");
}
template <class T> const T *query_optional(const MantisPluginV1 *root, const char *id) {
    if (!compatible_table(root) || !root->query_interface || !id)
        throw std::invalid_argument("Invalid plugin query");
    auto *table = static_cast<const T *>(root->query_interface(id));
    if (table && !compatible_table(table))
        throw std::runtime_error("Incompatible queried interface");
    return table;
}
inline const MantisProcessorV2 *processor_v2(const MantisPluginV1 *root) {
    auto *api = query_optional<MantisProcessorV2>(root, MANTIS_PROCESSOR_V2);
    if (api && (!api->describe || !api->process))
        throw std::runtime_error("Incomplete processor v2 table");
    return api;
}
class RetainedBuffer {
    const MantisHostV1 *host_{};
    MantisBuffer *buffer_{};

  public:
    RetainedBuffer(const MantisHostV1 *host, MantisBuffer *buffer) : host_(host), buffer_(buffer) {
        if (!compatible(host) || !host->retain || !host->release || !buffer)
            throw std::invalid_argument("Invalid retained buffer");
        host_->retain(buffer_);
    }
    ~RetainedBuffer() {
        if (buffer_)
            host_->release(buffer_);
    }
    RetainedBuffer(const RetainedBuffer &) = delete;
    RetainedBuffer &operator=(const RetainedBuffer &) = delete;
    RetainedBuffer(RetainedBuffer &&v) noexcept
        : host_(v.host_), buffer_(std::exchange(v.buffer_, nullptr)) {}
    MantisBuffer *get() const {
        return buffer_;
    }
};
// Calls hold shared state. A close with an active call refuses within its deadline;
// state destruction is deferred until the last call returns, so callback storage
// and instance cannot race RAII destruction. Keep root/host alive for this object.
struct ContractFailure : std::runtime_error {
    int status;
    explicit ContractFailure(int rc) : std::runtime_error("Projected-light operation failed"), status(rc) {}
};
template <class View, class F> int borrowed_callback(void *, const View *) noexcept;
class ProjectedLight {
    struct State {
        const MantisProjectedLightV1 *api;
        void *instance{};
        std::shared_ptr<void> lifetime;
        std::mutex mutex;
        std::condition_variable idle;
        uint32_t active{}, cleanup_timeout{};
        bool normal{}, closing{}, destroying{};
        explicit State(const MantisProjectedLightV1 *v) : api(v) {}
        ~State() {
            if (instance) {
                // No calls remain. A violating plugin must never be unloaded
                // with live instance storage: quarantine its lifetime pin.
                int rc = MANTIS_PL_ERROR;
                try {
                    rc = api->destroy(instance, cleanup_timeout);
                } catch (...) {
                }
                if (rc && lifetime)
                    (void)new std::shared_ptr<void>(std::move(lifetime));
            }
        }
    };
    std::shared_ptr<State> state_;
    template <class F> int call(uint32_t timeout, bool side, F fn) const {
        finite_timeout(timeout);
        auto s = state_;
        {
            std::lock_guard lock(s->mutex);
            if (!s->instance || s->destroying || (!side && (s->closing || s->normal)))
                return MANTIS_PL_BUSY;
            ++s->active;
            if (!side)
                s->normal = true;
        }
        struct Guard {
            std::shared_ptr<State> state;
            bool side;
            ~Guard() {
                std::lock_guard lock(state->mutex);
                --state->active;
                if (!side)
                    state->normal = false;
                state->idle.notify_all();
            }
        } guard{s, side};
        try {
            return fn(*s);
        } catch (...) {
            return MANTIS_PL_ERROR;
        }
    }

  public:
    ProjectedLight(const MantisProjectedLightV1 *api, const MantisHostV1 *host, const char *parent,
                   uint32_t timeout, std::shared_ptr<void> lifetime = {})
        : state_(std::make_shared<State>(api)) {
        finite_timeout(timeout);
        if (!compatible_table(api) || !compatible(host) || !parent || !api->enumerate || !api->open ||
            !api->validate || !api->prepare || !api->start || !api->next || !api->status || !api->abort ||
            !api->stop || !api->destroy || !api->diagnostics)
            throw std::invalid_argument("Invalid projected-light interface");
        state_->lifetime = std::move(lifetime);
        state_->cleanup_timeout = timeout;
        int rc = api->open(host, parent, timeout, &state_->instance);
        if (rc || !state_->instance)
            throw ContractFailure(rc ? rc : MANTIS_PL_INCOMPATIBLE);
    }
    ProjectedLight(const ProjectedLight &) = delete;
    ProjectedLight &operator=(const ProjectedLight &) = delete;
    ProjectedLight(ProjectedLight &&) = delete;
    int validate(const MantisAcquisitionProgramV1 *p, uint32_t t, MantisProgramValidationEmitV1 emit,
                 void *ctx) const {
        if (!compatible_table(p) || !emit)
            return MANTIS_PL_INCOMPATIBLE;
        return call(t, false, [&](State &s) { return s.api->validate(s.instance, p, t, emit, ctx); });
    }
    int prepare(const MantisAcquisitionProgramV1 *p, uint32_t t, MantisProgramValidationEmitV1 emit,
                void *ctx) const {
        if (!compatible_table(p) || !emit)
            return MANTIS_PL_INCOMPATIBLE;
        return call(t, false, [&](State &s) { return s.api->prepare(s.instance, p, t, emit, ctx); });
    }
    int start(const char *run, const char *generation, uint32_t t) const {
        return call(t, false, [&](State &s) { return s.api->start(s.instance, run, generation, t); });
    }
    int next(uint32_t t, MantisSemanticEmitV1 emit, void *ctx) const {
        return call(t, false, [&](State &s) { return s.api->next(s.instance, t, emit, ctx); });
    }
    template <class F> int next(uint32_t t, F &&fn) const {
        return next(t, borrowed_callback<MantisSemanticPacketV1, std::remove_reference_t<F>>, &fn);
    }
    template <class F> int status(uint32_t t, F &&fn) const {
        return status(t, borrowed_callback<MantisProjectedStatusV1, std::remove_reference_t<F>>, &fn);
    }
    template <class F> int abort(uint32_t reason, uint32_t t, F &&fn) const {
        return abort(reason, t, borrowed_callback<MantisAbortOutcomeV1, std::remove_reference_t<F>>, &fn);
    }
    int status(uint32_t t, MantisProjectedStatusEmitV1 emit, void *ctx) const {
        return call(t, false, [&](State &s) { return s.api->status(s.instance, t, emit, ctx); });
    }
    int abort(uint32_t reason, uint32_t t, MantisAbortEmitV1 emit, void *ctx) const {
        return call(t, true, [&](State &s) { return s.api->abort(s.instance, reason, t, emit, ctx); });
    }
    int stop(uint32_t t) const {
        return call(t, false, [&](State &s) { return s.api->stop(s.instance, t); });
    }
    int diagnostics(uint32_t t, MantisTextEmitV1 emit, void *ctx) const {
        return call(t, false, [&](State &s) { return s.api->diagnostics(s.instance, t, emit, ctx); });
    }
    int close(uint32_t t) const {
        finite_timeout(t);
        auto s = state_;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(t);
        std::unique_lock lock(s->mutex);
        if (!s->instance)
            return MANTIS_PL_OK;
        if (s->closing)
            return MANTIS_PL_BUSY;
        s->closing = true;
        if (!s->idle.wait_until(lock, deadline, [&] { return s->active == 0; })) {
            s->closing = false;
            return MANTIS_PL_BUSY;
        }
        auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())
                .count();
        const auto remaining = static_cast<uint32_t>(std::max<int64_t>(0, left));
        s->destroying = true;
        lock.unlock();
        int rc = MANTIS_PL_ERROR;
        try {
            rc = s->api->destroy(s->instance, remaining);
        } catch (...) {
        }
        lock.lock();
        if (!rc) {
            s->instance = nullptr;
            s->lifetime.reset();
        }
        s->closing = false;
        s->destroying = false;
        return rc;
    }
};
// Borrowed callbacks are exception-contained; no type conversion or runtime policy.
template <class View, class F> int borrowed_callback(void *ctx, const View *v) noexcept {
    return boundary([&] {
        if (!compatible_table(v))
            throw std::runtime_error("Invalid borrowed view prefix");
        (*static_cast<F *>(ctx))(*v);
    });
}
} // namespace mantis::sdk
