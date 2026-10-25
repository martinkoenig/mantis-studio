#pragma once
#include <mantis/calibration_dataset.hpp>
#include <mantis/artifact_store.hpp>

namespace mantis::calibration {
// Streaming, synchronous analysis of finalized schema-2 RawCaptures. The returned
// dataset owns source references/correspondences only; neither pixels nor Store.
Result<CalibrationDataset> build_calibration_dataset(
    std::shared_ptr<const artifact::Store> store, std::vector<Id> raw_capture_ids,
    CalibrationTarget target, DatasetAnalysisConfig config, const CancellationToken &token = {});
} // namespace mantis::calibration
