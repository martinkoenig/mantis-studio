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
    ArtifactDescriptor finalize(const ArtifactId &);
    std::vector<ArtifactDescriptor> list() const;
    ArtifactDescriptor get(const ArtifactId &) const;
    data::Published packet(const ArtifactId &, uint64_t chunk = 0) const;
    std::filesystem::path object_path(const ArtifactId &, uint64_t chunk = 0) const;
    // Startup classifies provisional state; explicit recovery finalizes only verified, committed chunks.
    ArtifactDescriptor recover(const ArtifactId &);
};
} // namespace mantis::artifact
