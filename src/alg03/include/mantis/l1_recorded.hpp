#pragma once
#include <mantis/l1_candidates.hpp>
#include <mantis/l1_ray_plane.hpp>

namespace mantis::alg03 {
// Internal synchronous offline operation, not a service/SDK or a job runner.
// extractor.raw_input must identify an exact finalized local RawCapture-3.
// record is its zero-based bundle ordinal (including control-only records).
struct RecordedRequest {
    pipeline::L1CandidateConfig extractor;
    uint64_t record{};
    data::ContentReference plane, policy;
    data::ExactCalibrationReference camera, rig;
    Selection calibration_selection{Selection::recorded_effective};
};
struct RecordedResult {
    data::ContentReference observation;
    artifact::ArtifactReference result;
};
// Reads only through the selected record, with bounded reader/DAG ownership.
// Each call creates distinct derived artifacts. Failure/cancellation never returns
// success; a finalized observation or recoverable reservation can remain if a
// later phase fails. No rollback/deletion or active calibration lookup occurs.
Result<RecordedResult> process_recorded(std::shared_ptr<artifact::Store>, const RecordedRequest &,
                                        const CancellationToken & = {});
} // namespace mantis::alg03
