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
void validate_capture_header(const ProjectedCaptureHeader &);
void write_capture_header(std::ostream &, const ProjectedCaptureHeader &);
ProjectedCaptureHeader read_capture_header(memory::BufferView);
void write_bundle(std::ostream &, const AcquisitionBundle &);
AcquisitionBundle read_bundle(memory::BufferView);
// Structural sizing: never maps/traverses pixels.
uint64_t bundle_encoded_size(const AcquisitionBundle &);
// Canonical typed values, useful for exact storage identity comparisons.
bool same_program_reference(const ProgramReference &, const ProgramReference &);
bool same_participants(const Participants &, const Participants &);
} // namespace mantis::data
