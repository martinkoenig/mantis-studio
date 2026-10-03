#pragma once
#include <filesystem>
#include <mantis/artifact_api.hpp>
#include <mantis/device_api.hpp>
#include <mantis/jobs.hpp>
namespace mantis::services {
struct CaptureInfo {
    Id id;
    std::vector<Id> devices;
    Id raw_artifact;
    bool active{};
    uint64_t frames{}, dropped{}, queue_high_water{};
    std::string error;
};
struct PluginInfo {
    std::string id, version, kind, execution, state, diagnostic;
    std::vector<std::string> permissions;
};
struct Event {
    uint64_t sequence{};
    std::string kind, component, message;
};
class DeviceService {
  public:
    virtual ~DeviceService() = default;
    virtual std::vector<device::Descriptor> devices() const = 0;
};
class CaptureService {
  public:
    virtual ~CaptureService() = default;
    virtual CaptureInfo start_capture(const std::vector<Id> &) = 0;
    virtual CaptureInfo stop_capture(const Id &) = 0;
    virtual std::vector<CaptureInfo> captures() const = 0;
};
class PipelineService {
  public:
    virtual ~PipelineService() = default;
    virtual Id run_pipeline(const Id &capture, const std::string &recipe, const Id &artifact = {}) = 0;
};
class ProjectService {
  public:
    virtual ~ProjectService() = default;
    virtual std::string open_project(const std::filesystem::path &, bool create) = 0;
    virtual std::string project() const = 0;
};
class ArtifactService {
  public:
    virtual ~ArtifactService() = default;
    virtual std::vector<artifact::ArtifactDescriptor> artifacts() const = 0;
    virtual std::filesystem::path data_reference(const Id &) const = 0;
    virtual Id export_artifact(const Id &, const std::filesystem::path &) = 0;
    virtual artifact::ArtifactDescriptor recover_artifact(const Id &) = 0;
};
class JobService {
  public:
    virtual ~JobService() = default;
    virtual std::vector<jobs::Snapshot> jobs() const = 0;
    virtual void cancel_job(const Id &) = 0;
};
class PluginService {
  public:
    virtual ~PluginService() = default;
    virtual std::vector<PluginInfo> plugins() const = 0;
    virtual void enable_plugin(const std::string &, bool) = 0;
};
class DiagnosticsService {
  public:
    virtual ~DiagnosticsService() = default;
    virtual std::vector<Event> events(uint64_t after) const = 0;
};
struct Configuration {
    std::filesystem::path plugins, plugin_host, project, recipes;
    std::vector<std::string> approved_in_process;
};
class Runtime final : public DeviceService,
                      public CaptureService,
                      public PipelineService,
                      public ProjectService,
                      public ArtifactService,
                      public JobService,
                      public PluginService,
                      public DiagnosticsService {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Runtime(Configuration);
    ~Runtime();
    std::vector<device::Descriptor> devices() const override;
    CaptureInfo start_capture(const std::vector<Id> &) override;
    CaptureInfo stop_capture(const Id &) override;
    std::vector<CaptureInfo> captures() const override;
    Id run_pipeline(const Id &, const std::string &, const Id & = {}) override;
    std::string open_project(const std::filesystem::path &, bool) override;
    std::string project() const override;
    std::vector<artifact::ArtifactDescriptor> artifacts() const override;
    std::filesystem::path data_reference(const Id &) const override;
    Id export_artifact(const Id &, const std::filesystem::path &) override;
    artifact::ArtifactDescriptor recover_artifact(const Id &) override;
    std::vector<jobs::Snapshot> jobs() const override;
    void cancel_job(const Id &) override;
    std::vector<PluginInfo> plugins() const override;
    void enable_plugin(const std::string &, bool) override;
    std::vector<Event> events(uint64_t) const override;
};
} // namespace mantis::services
