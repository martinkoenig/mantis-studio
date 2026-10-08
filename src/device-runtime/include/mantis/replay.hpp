#pragma once
#include <mantis/artifact_store.hpp>
#include <mantis/device_api.hpp>
namespace mantis::device {
// Live and recorded sources use the same semantic contract; no algorithm parses storage.
std::unique_ptr<ImageStream> recorded_source(std::shared_ptr<const artifact::Store>, Id, bool real_time);
} // namespace mantis::device

namespace mantis::device {
// Independent evidence publication source; never an ImageStream or executor.
class BundleReplay {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    BundleReplay(std::shared_ptr<const artifact::Store>, Id, bool receive_paced, Clock = {});
    ~BundleReplay();
    const data::ProjectedCaptureHeader &header() const;
    const std::optional<data::ProjectedRunOutcome> &final_outcome() const;
    // One next caller at a time; concurrent calls report busy.
    // Empty on bounded poll timeout or EOF; finished() distinguishes EOF/stop.
    Result<std::optional<data::AcquisitionBundle>> next(uint32_t timeout_ms = 0);
    void stop(); // concurrent with next; wakes paced waits and cancels segment scans
    bool finished() const;
    void notify_clock_advanced(); // injectable clock owner wakes deterministic pacing
};
} // namespace mantis::device
