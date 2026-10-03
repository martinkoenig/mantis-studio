#pragma once
#include <filesystem>
#include <mantis/device_api.hpp>
#include <mantis/pipeline_api.hpp>
#include <mantis/platform.hpp>
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
    platform::Library library_;
    const MantisPluginV1 *api_{};

  public:
    explicit Loaded(const std::filesystem::path &);
    ~Loaded();
    Loaded(const Loaded &) = delete;
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
data::Published process(const Loaded &, const data::Packet &);
void export_data(const Loaded &, const data::Packet &, const std::filesystem::path &);
pipeline::NodeDescriptor describe_node(const Loaded &);
class Registry {
    struct Entry {
        Manifest manifest;
        std::string state{"registered"}, diagnostic;
        std::shared_ptr<Loaded> loaded;
        pipeline::NodeDescriptor node;
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
    pipeline::Node node(const std::string &);
    void export_file(const std::string &, const data::Packet &, const std::filesystem::path &,
                     const CancellationToken &);
    void set_enabled(const std::string &, bool);
};
} // namespace mantis::plugins
