#pragma once
// Internal qualification only. No product selector, ABI field or installed header.
#ifdef MANTIS_RECORDER_DIAGNOSTICS
#include <cstddef>
#include <mantis/device_runtime.hpp>
#include <mutex>
#include <string_view>
namespace mantis::recorder_diagnostics {
struct Trace;
Trace *open(const void *, const device::CaptureDescriptor &) noexcept;
class Worker {
    Trace *previous_;
    unsigned lane_;

  public:
    Worker(Trace *, unsigned) noexcept;
    ~Worker();
};
void event(const char *, const data::Packet * = nullptr, int64_t = -1, std::string_view = {}) noexcept;
void source(const data::Packet &, const device::ImageStream &) noexcept;
Result<data::Published> next(device::ImageStream &);
std::optional<data::Published> pop(pipeline::BoundedQueue<data::Published> &);
void cleanup(const void *, bool) noexcept;
void raw_stage(size_t, bool) noexcept;
class StoreSpan {
    int exceptions_;

  public:
    explicit StoreSpan(std::string_view) noexcept;
    ~StoreSpan();
};
// Deterministic gate used only by the isolated regression, never by the daemon.
using Boundary = void (*)(std::mutex &);
void boundary_hook(Boundary) noexcept;
void boundary(std::mutex &) noexcept;
} // namespace mantis::recorder_diagnostics
#define MANTIS_RECORDER_OPEN(owner, descriptor)                                                              \
    auto *observation = ::mantis::recorder_diagnostics::open(owner, descriptor)
#define MANTIS_RECORDER_CAPTURE , observation
#define MANTIS_RECORDER_RELEASE(frame) frame.reset()
#define MANTIS_RECORDER_WORKER(lane)                                                                         \
    ::mantis::recorder_diagnostics::Worker diagnostic_worker(observation, lane)
#define MANTIS_RECORDER_EVENT(...) ::mantis::recorder_diagnostics::event(__VA_ARGS__)
#define MANTIS_RECORDER_SOURCE(packet, stream) ::mantis::recorder_diagnostics::source(packet, stream)
#define MANTIS_RECORDER_NEXT(stream) ::mantis::recorder_diagnostics::next(stream)
#define MANTIS_RECORDER_POP(queue) ::mantis::recorder_diagnostics::pop(queue)
#define MANTIS_RECORDER_CLEANUP(owner, done) ::mantis::recorder_diagnostics::cleanup(owner, done)
#define MANTIS_RECORDER_BOUNDARY() ::mantis::recorder_diagnostics::boundary(mutex_)
#define MANTIS_RECORDER_STORE(id) ::mantis::recorder_diagnostics::StoreSpan diagnostic_store_span(id)
#define MANTIS_RECORDER_RAW(index, end) ::mantis::recorder_diagnostics::raw_stage(index, end)
#else
#define MANTIS_RECORDER_OPEN(owner, descriptor) ((void)0)
#define MANTIS_RECORDER_CAPTURE
#define MANTIS_RECORDER_RELEASE(frame) ((void)0)
#define MANTIS_RECORDER_WORKER(lane) ((void)0)
#define MANTIS_RECORDER_EVENT(...) ((void)0)
#define MANTIS_RECORDER_SOURCE(packet, stream) ((void)0)
#define MANTIS_RECORDER_NEXT(stream) (stream).next()
#define MANTIS_RECORDER_POP(queue) queue.pop()
#define MANTIS_RECORDER_CLEANUP(owner, done) ((void)0)
#define MANTIS_RECORDER_BOUNDARY() ((void)0)
#define MANTIS_RECORDER_STORE(id) ((void)0)
#define MANTIS_RECORDER_RAW(index, end) ((void)0)
#endif
