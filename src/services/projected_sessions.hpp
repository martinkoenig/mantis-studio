#pragma once
#include <mantis/plugin_runtime.hpp>
#include <mantis/services.hpp>
namespace mantis::services {
// Control operations are dispatched serially. Session status/latest publication and
// recorder/finalization callbacks have their own short locks; no lock crosses hardware/storage calls.
class ProjectedSessions {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    using EventSink = std::function<void(std::string, std::string, std::string)>;
    ProjectedSessions(plugins::Registry &, jobs::Manager &, std::shared_ptr<artifact::Store>, EventSink);
    ~ProjectedSessions();
    std::vector<ProjectedDeviceInfo> devices() const;
    device::ProjectedValidation validate(const ProjectedCaptureRequest &);
    ProjectedCaptureInfo start(const ProjectedCaptureRequest &, const std::string &);
    ProjectedCaptureInfo status(const Id &) const;
    std::vector<ProjectedCaptureInfo> list() const;
    ProjectedCaptureInfo stop(const ProjectedStopRequest &);
    bool active(const std::string &plugin = {}) const;
    void check_camera(const std::vector<device::Descriptor> &) const;
    void set_camera_resources(std::vector<device::Descriptor>);
    std::shared_ptr<const data::AcquisitionBundle> latest(const Id &) const;
    Id replay(const Id &, bool paced, bool verify);
    std::function<void(const artifact::ArtifactDescriptor &)> artifact_update(const Id &) const;
};
} // namespace mantis::services
