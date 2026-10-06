#pragma once
#include <filesystem>
#include <mantis/artifact_api.hpp>
#include <mantis/device_api.hpp>
#include <mantis/jobs.hpp>
#include <mantis/calibration_service.hpp>
namespace mantis::artifact { class Store; }
namespace mantis::services {
struct CaptureInfo {
    Id id;
    std::vector<Id> devices;
    Id raw_artifact;
    bool active{};
    uint64_t frames{}, dropped{}, queue_high_water{};
    std::string error;
    uint64_t produced{}, committed{}, queue_depth{}, queue_capacity{}, queue_saturation{}, preview_drops{}, total_bytes{};
    double duration{}, writer_mb_s{}, writer_mib_s{};
    data::Metadata diagnostics;
    Id finalization_job;
};
struct PreviewReference { Id lease; std::filesystem::path path; };
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
                      public DiagnosticsService,
                      public CalibrationService {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Runtime(Configuration);
    ~Runtime();
    // Internal persistence seam; intentionally absent from service/protocol/client APIs.
    std::shared_ptr<artifact::Store> project_store() const;
    std::vector<device::Descriptor> devices() const override;
    CaptureInfo start_capture(const std::vector<Id> &) override;
    CaptureInfo stop_capture(const Id &) override;
    std::vector<CaptureInfo> captures() const override;
    PreviewReference preview(const Id &);
    void release_preview(const Id &);
    Id replay_capture(const Id &, bool real_time, bool verify);
    Id run_pipeline(const Id &, const std::string &, const Id & = {}) override;
    std::string open_project(const std::filesystem::path &, bool) override;
    std::string project() const override;
    std::vector<artifact::ArtifactDescriptor> artifacts() const override;
    std::filesystem::path data_reference(const Id &) const override;
    Id export_artifact(const Id &, const std::filesystem::path &) override;
    artifact::ArtifactDescriptor recover_artifact(const Id &) override;
    Id recover_artifact_job(const Id &);
    std::vector<jobs::Snapshot> jobs() const override;
    void cancel_job(const Id &) override;
    std::vector<PluginInfo> plugins() const override;
    void enable_plugin(const std::string &, bool) override;
    std::vector<Event> events(uint64_t) const override;
    CalibrationInfo create_calibration_target(const TargetCreate &) override;
    Id build_calibration_dataset(const DatasetBuild &) override;
    Id solve_camera_calibration(const CameraSolve &) override;
    Id solve_rig_calibration(const RigSolve &) override;
    std::vector<CalibrationEntry> calibrations() const override;
    CalibrationInfo calibration_info(const Id &) const override;
    std::optional<ActiveCalibrationInfo> active_calibration(const Id &) const override;
    void activate_calibration(const Id &, const Id &) override;
    void clear_calibration(const Id &) override;

};
} // namespace mantis::services
