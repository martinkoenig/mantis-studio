#pragma once
#include <chrono>
#include <mantis/projected_light_device.hpp>
#include <mantis/projected_light_io.hpp>

namespace mantis::device {
// Daemon state, deliberately separate from the executor's resource lifecycle.
enum class ProjectedState { validating, ready, running, stopping, completed, cancelled, failed };
struct ProjectedIdentity {
    data::RunId run;
    data::GenerationId generation;
};
using ProjectedIdentitySource = std::function<ProjectedIdentity()>;
using ProjectedClock = std::function<std::chrono::steady_clock::time_point()>;
struct ProjectedRunConfig {
    uint32_t queue_capacity{16};
    uint32_t operation_timeout_ms{100}, abort_timeout_ms{100}, cleanup_timeout_ms{100};
    uint32_t publication_timeout_ms{100};
    // Explicit bounded correlation reservation; exhaustion is a resource fault.
    uint32_t max_correlation_entries{4096};
};
inline data::RecordedRunConfig recorded_run_config(const ProjectedRunConfig &c) {
    return {c.queue_capacity,     c.operation_timeout_ms,   c.abort_timeout_ms,
            c.cleanup_timeout_ms, c.publication_timeout_ms, c.max_correlation_entries};
}
struct ProjectedQueueMetrics {
    size_t capacity{}, occupancy{}, high_water{};
    uint64_t produced{}, consumed{}, saturation_failures{};
};
struct ProjectedTerminal {
    data::AcquisitionReason reason{data::AcquisitionReason::none};
    std::optional<Error> initiating_error, abort_error, stop_error, close_error;
    std::optional<AbortOutcome> abort_outcome;
    // Original terminal and unqueued/faulting publication, retaining shared pixels.
    std::shared_ptr<const data::AcquisitionBundle> executor_terminal, unqueued_bundle;
};
struct ProjectedRunSnapshot {
    ProjectedState state{ProjectedState::validating};
    ProjectedIdentity identity;
    std::vector<ProjectedState> transitions{ProjectedState::validating};
    std::optional<ProgramValidation> validation, preparation;
    ProjectedQueueMetrics queue;
    uint64_t covered_steps{}, expected_steps{}, late_evidence{};
    ProjectedTerminal terminal;
    // FAILED may be visible during preflight cleanup; state alone is not completion.
    bool cleanup_resolved{};
};
// Rejects non-final snapshots; performs no cleanup, reason/state inference or hardware work.
data::ProjectedRunOutcome recorded_run_outcome(const ProjectedRunSnapshot &);
// Owns one executor and an immutable program snapshot. Only its worker makes ordinary
// executor calls. Abort bypasses that worker and the authoritative queue.
class ProjectedRun {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    ProjectedRun(std::unique_ptr<ProjectedExecutor>, data::AcquisitionProgram, ProjectedRunConfig = {},
                 ProjectedIdentitySource = {}, ProjectedClock = {});
    ~ProjectedRun();
    ProjectedRun(const ProjectedRun &) = delete;
    ProjectedRun &operator=(const ProjectedRun &) = delete;
    Result<void> prepare();
    Result<void> start();
    void stop();
    void cancel();
    // Drains existing immutable publications, including after terminal cleanup.
    Result<std::optional<std::shared_ptr<const data::AcquisitionBundle>>> next(uint32_t timeout_ms);
    bool wait_terminal(uint32_t timeout_ms) const;
    ProjectedRunSnapshot snapshot() const;
    // An injected monotonic clock owner calls this after advancing fake time.
    void notify_clock_advanced();
};
} // namespace mantis::device
