#pragma once
#include <filesystem>
#include <mantis/artifact_api.hpp>
#include <mantis/projected_light_io.hpp>
namespace mantis::artifact {
struct CalibrationRevision {
    calibration::Reference reference;
    ArtifactId artifact_id;
    std::string kind;
};
struct ActiveCalibration {
    calibration::Reference reference;
    ArtifactReference artifact;
};
class Store {
    friend class BundleCaptureReader;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ArtifactDescriptor finalize_impl(const Id &, const CancellationToken &, bool recovery);

  public:
    explicit Store(std::filesystem::path);
    ~Store();
    Store(const Store &) = delete;
    const std::filesystem::path &root() const;
    ArtifactId begin(ArtifactType, Provenance);
    void append(const ArtifactId &, const data::Packet &);
    // Synchronous durable pre-run initialization; never starts hardware.
    ArtifactId begin_projected_capture(data::ProjectedCaptureHeader, Provenance = {});
    void append_bundle(const ArtifactId &, const data::AcquisitionBundle &);
    // Call only with the final daemon snapshot, after cleanup and draining bundles.
    void record_run_outcome(const ArtifactId &, const data::ProjectedRunOutcome &);
    std::optional<data::ProjectedRunOutcome> run_outcome(const ArtifactId &) const;
    data::ProjectedCaptureHeader capture_header(const ArtifactId &) const;
    data::AcquisitionBundle bundle(const ArtifactId &, uint64_t record = 0) const;
    void replay_bundles(const ArtifactId &, const std::function<void(data::AcquisitionBundle)> &,
                        const CancellationToken & = {}) const;
    struct BundleSummary {
        uint64_t records{};
        std::optional<data::ProjectedRunOutcome> final_outcome; // absent = unknown/incomplete
        std::optional<data::AcquisitionDisposition> last_executor_disposition;
    };
    BundleSummary bundle_summary(const ArtifactId &, const CancellationToken & = {}) const;
    // Reservation and artifact creation are one transaction; reserved revisions are never reused.
    CalibrationRevision begin_calibration(ArtifactType, Provenance, std::optional<Id> series = {});
    CalibrationRevision calibration_revision(const Id &, uint64_t revision) const;
    CalibrationRevision artifact_revision(const ArtifactId &) const;
    std::optional<ActiveCalibration> active_calibration(const Id &logical_device_id) const;
    void activate_calibration(const Id &logical_device_id, const ArtifactId &rig_artifact_id);
    void clear_active_calibration(const Id &logical_device_id);
    // Capture audit initialization only, before any packet is appended.
    void initialize_provenance(const ArtifactId &, Provenance);
    // RawCapture v2: sequential records, segment-batched durability/SQLite commits.
    // Failed/incomplete live writers must be abandoned before explicit recovery.
    void abandon(const ArtifactId &);
    void prepare_finalize(const ArtifactId &);
    void replay(const ArtifactId &, const std::function<void(data::Published)> &,
                const CancellationToken & = {}) const;
    uint64_t record_count(const ArtifactId &) const;
    ArtifactDescriptor finalize(const ArtifactId &, const CancellationToken & = {});
    std::vector<ArtifactDescriptor> list() const;
    ArtifactDescriptor get(const ArtifactId &) const;
    data::Published packet(const ArtifactId &, uint64_t chunk = 0) const;
    std::filesystem::path object_path(const ArtifactId &, uint64_t chunk = 0) const;
    // Startup classifies provisional state; explicit recovery finalizes only verified, committed chunks.
    ArtifactDescriptor recover(const ArtifactId &, const CancellationToken & = {});
};
class BundleCaptureReader {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    BundleCaptureReader(std::shared_ptr<const Store>, Id);
    ~BundleCaptureReader();
    const data::ProjectedCaptureHeader &header() const;
    const std::optional<data::ProjectedRunOutcome> &final_outcome() const;
    std::optional<data::AcquisitionBundle> next(const CancellationToken & = {});
};
// Generic reader/source seam: parser remains in storage, callers receive Published.
class CaptureReader {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    CaptureReader(std::shared_ptr<const Store>, Id);
    ~CaptureReader();
    data::Published next(); // null at EOF
};
} // namespace mantis::artifact
