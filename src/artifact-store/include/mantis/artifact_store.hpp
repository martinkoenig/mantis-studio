#pragma once
#include <filesystem>
#include <mantis/artifact_api.hpp>
namespace mantis::artifact {
class Store {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Store(std::filesystem::path);
    ~Store();
    Store(const Store &) = delete;
    const std::filesystem::path &root() const;
    ArtifactId begin(ArtifactType, Provenance);
    void append(const ArtifactId &, const data::Packet &);
    // RawCapture v2: sequential records, segment-batched durability/SQLite commits.
    // Failed/incomplete live writers must be abandoned before explicit recovery.
    void abandon(const ArtifactId &);
    void prepare_finalize(const ArtifactId &);
    void replay(const ArtifactId &, const std::function<void(data::Published)> &, const CancellationToken & = {}) const;
    uint64_t record_count(const ArtifactId &) const;
    ArtifactDescriptor finalize(const ArtifactId &);
    std::vector<ArtifactDescriptor> list() const;
    ArtifactDescriptor get(const ArtifactId &) const;
    data::Published packet(const ArtifactId &, uint64_t chunk = 0) const;
    std::filesystem::path object_path(const ArtifactId &, uint64_t chunk = 0) const;
    // Startup classifies provisional state; explicit recovery finalizes only verified, committed chunks.
    ArtifactDescriptor recover(const ArtifactId &);
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
