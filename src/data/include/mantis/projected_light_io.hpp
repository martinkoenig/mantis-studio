#pragma once
#include <mantis/projected_light.hpp>
#include <ostream>
namespace mantis::data {
// Storage value below device-runtime. Convert explicitly from ProjectedRunConfig.
struct RecordedRunConfig {
    uint32_t queue_capacity{}, operation_timeout_ms{}, abort_timeout_ms{}, cleanup_timeout_ms{};
    uint32_t publication_timeout_ms{}, max_correlation_entries{};
};
struct ProjectedCaptureHeader {
    AcquisitionProgram program;
    RunId run;
    GenerationId generation;
    RecordedRunConfig config;
};
// Daemon cleanup result, deliberately separate from executor bundle dispositions.
enum class RecordedRunDisposition { completed, cancelled, failed };
struct RecordedExecutorError {
    uint32_t category{}, code{};
};
struct RecordedAbortOutcome {
    Evidence<RunId> run;
    Evidence<GenerationId> fenced_generation;
    Evidence<bool> inhibited, stale_work_fenced, off_requested;
    std::vector<EmitterEvidence> emitters;
    RecordedExecutorError error;
};
struct ProjectedRunOutcome {
    RunId run;
    GenerationId generation;
    RecordedRunDisposition disposition{RecordedRunDisposition::failed};
    AcquisitionReason reason{AcquisitionReason::none};
    std::optional<Error> initiating_error, abort_error, stop_error, close_error;
    std::optional<RecordedAbortOutcome> abort_outcome;
    std::string diagnostic;
};
// L3 queue.produced supplies the expected authoritative prefix; Store must not invent this count.
struct ProjectedCaptureOutcome {
    uint64_t bundle_count{};
    ProjectedRunOutcome outcome;
};
inline constexpr uint64_t max_run_outcome_bytes = 1024 * 1024;
void validate_run_outcome(const ProjectedRunOutcome &);
void write_run_outcome(std::ostream &, const ProjectedCaptureOutcome &);
// Only a provisional .part may opt into incomplete-envelope detection.
std::optional<ProjectedCaptureOutcome> read_run_outcome(memory::BufferView, bool allow_incomplete = false);
void validate_capture_header(const ProjectedCaptureHeader &);
void write_capture_header(std::ostream &, const ProjectedCaptureHeader &);
ProjectedCaptureHeader read_capture_header(memory::BufferView);
void write_bundle(std::ostream &, const AcquisitionBundle &);
AcquisitionBundle read_bundle(memory::BufferView);
// Structural sizing: never maps/traverses pixels.
uint64_t bundle_encoded_size(const AcquisitionBundle &);
// Canonical typed values, useful for exact storage identity comparisons.
bool same_program_reference(const ProgramReference &, const ProgramReference &);
bool same_emitter_command(const EmitterCommand &, const EmitterCommand &);
bool same_participants(const Participants &, const Participants &);
} // namespace mantis::data
