#pragma once
#include <filesystem>
#include <mantis/device_api.hpp>
#include <mantis/pipeline_api.hpp>
#include <mantis/platform.hpp>
#include <mantis/projected_light_device.hpp>
#include <mantis/sdk.hpp>
#include <mutex>
namespace mantis::plugins {
struct Manifest {
    std::string id, version, kind, execution;
    uint32_t abi_version{};
    std::filesystem::path library;
    std::vector<std::string> permissions;
};
struct PluginStatus {
    Manifest manifest;
    std::string state, diagnostic;
};
const MantisHostV1 *host_api();
class Loaded {
    struct SharedLibrary;
    std::shared_ptr<SharedLibrary> library_;
    std::filesystem::path path_;
    const MantisPluginV1 *api_{};

  public:
    explicit Loaded(const std::filesystem::path &);
    ~Loaded();
    Loaded(const Loaded &) = delete;
    const std::filesystem::path &path() const {
        return path_;
    }
    std::shared_ptr<const void> lifetime() const { return library_; }
    const MantisPluginV1 *api() const {
        return api_;
    }
    template <class T> const T *query(const char *id) const {
        auto p = static_cast<const T *>(api_->query_interface(id));
        if (!p || p->struct_size < sizeof(T) || p->abi_version != 1)
            fail(Status::incompatible, "Missing/incompatible interface: " + std::string(id));
        return p;
    }
};
std::vector<device::ProjectedGraph> discover_projected_light(const Loaded &, uint32_t timeout_ms);
std::unique_ptr<device::ProjectedExecutor> open_projected_light(std::shared_ptr<Loaded>, const Id &parent,
                                                                uint32_t timeout_ms);
data::Published process(const Loaded &, const data::Packet &);
void export_data(const Loaded &, const data::Packet &, const std::filesystem::path &);
pipeline::NodeDescriptor describe_node(const Loaded &);
pipeline::NodeDescriptor describe_semantic_node(const Loaded &, uint32_t timeout_ms = 1000);
data::SemanticPublished process_semantic(const Loaded &, const data::SemanticPacket &,
                                         uint32_t timeout_ms = 1000, const CancellationToken & = {});
class Registry {
    struct Entry {
        Manifest manifest;
        std::string state{"registered"}, diagnostic;
        std::shared_ptr<Loaded> loaded;
        pipeline::NodeDescriptor node;
        std::optional<pipeline::NodeDescriptor> semantic_node;
    };
    std::map<std::string, std::shared_ptr<Entry>> entries_;
    mutable std::mutex mutex_;
    std::filesystem::path host_, scratch_;
    LogSink logger_;
    std::shared_ptr<Entry> entry(const std::string &) const;
    void isolated(const std::shared_ptr<Entry> &, const std::string &, const std::filesystem::path &,
                  const std::filesystem::path &, const CancellationToken &);

  public:
    Registry(std::filesystem::path host, std::filesystem::path scratch, LogSink logger);
    void discover(const std::filesystem::path &, const std::vector<std::string> &approved_in_process);
    std::vector<PluginStatus> statuses() const;
    std::vector<std::unique_ptr<device::ImageStream>> devices();
    struct ProjectedParent {
        std::string plugin_id;
        device::ProjectedGraph graph;
    };
    std::vector<ProjectedParent> projected_light_parents(uint32_t timeout_ms);
    std::unique_ptr<device::ProjectedExecutor> open_projected_light(const std::string &plugin_id,
                                                                    const Id &parent, uint32_t timeout_ms);
    pipeline::Node node(const std::string &);
    pipeline::SemanticNode semantic_node(const std::string &, uint32_t timeout_ms = 1000);
    void export_file(const std::string &, const data::Packet &, const std::filesystem::path &,
                     const CancellationToken &);
    void set_enabled(const std::string &, bool);
};
} // namespace mantis::plugins
