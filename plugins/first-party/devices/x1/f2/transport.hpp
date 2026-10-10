#pragma once
#include "codec.hpp"
#include <chrono>

namespace x1::f2 {
using Deadline = std::chrono::steady_clock::time_point;
enum class TransportStatus { ready, timeout, interrupted, closed, unavailable, invalid };
struct FrameRead {
    TransportStatus status = TransportStatus::timeout;
    std::optional<Frame> frame;
    explicit operator bool() const {
        return status == TransportStatus::ready && frame.has_value();
    }
    const Frame &operator*() const {
        return frame.value();
    }
    const Frame *operator->() const {
        return &frame.value();
    }
    bool operator==(const FrameRead &) const = default;
};
inline bool matches_response(const Frame &reply, const Frame &request) {
    return reply.kind == Class::response && reply.message == request.message &&
           reply.request == request.request && reply.session == request.session;
}
// Private framed-byte boundary, not an OS I/O framework or public plugin ABI.
// Implementations MUST transmit the supplied complete wire bytes unchanged and
// incrementally COBS/CRC/envelope-validate received bytes before returning frames.
// No direct peer-state calls may substitute for this byte boundary.
//
// submit is bounded (at most max_wire bytes), never waits for a response/queue
// drain, and reports admission failure. One ordinary transaction, one KEEPALIVE
// and priority STOP may operate concurrently. STOP admission and its response
// slot MUST be independent of ordinary/event saturation and heartbeat waits.
// receive matches RESPONSE class + session/request/message exactly, rejecting
// stale/unrelated replies; event returns EVENT frames through a separate bounded
// queue. These waits release all locks needed by submit/interrupt and respect the
// total steady-clock deadline (including zero-budget polling).
//
// interrupt wakes current ordinary/heartbeat/event reads with interrupted; it
// does not close the link or interrupt priority STOP reception. Closure wakes ALL
// reads with closed. Timeout/absence is distinct from disconnect/interruption.
// All collectors, queues and diagnostics must have explicit finite capacities.
//
// Caller owns the transport. It must outlive Host, its joined heartbeat worker,
// and every Host call. Quiesce callers before destroying Host/transport. No
// detached operation or callback may retain either object after destruction.
class Transport {
  public:
    virtual ~Transport() = default;
    virtual TransportStatus submit(const Wire &) = 0;
    virtual FrameRead receive(const Frame &request, Deadline) = 0;
    virtual FrameRead event(Deadline) = 0;
    virtual void interrupt() noexcept = 0;
};
} // namespace x1::f2
