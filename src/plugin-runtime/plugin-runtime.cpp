#include <algorithm>
#include <fstream>
#include <iostream>
#include <mantis/data_io.hpp>
#include <mantis/plugin_runtime.hpp>
#include <nlohmann/json.hpp>
struct MantisBuffer {
    std::atomic_uint refs{1};
    std::unique_ptr<mantis::memory::BufferBuilder> builder;
    mantis::memory::Buffer published;
    explicit MantisBuffer(uint64_t bytes, uint64_t alignment)
        : builder(std::make_unique<mantis::memory::BufferBuilder>(static_cast<size_t>(bytes),
                                                                  static_cast<size_t>(alignment))) {}
    explicit MantisBuffer(mantis::memory::Buffer b) : published(std::move(b)) {}
};
namespace mantis::plugins {
namespace {
MantisBuffer *allocate(uint64_t size, uint64_t alignment) noexcept {
    try {
        return new MantisBuffer(size, alignment);
    } catch (...) {
        return nullptr;
    }
}
void retain(MantisBuffer *b) {
    if (b)
        ++b->refs;
}
void release(MantisBuffer *b) {
    if (b && --b->refs == 0)
        delete b;
}
int write_map(MantisBuffer *b, void **p, uint64_t *size) noexcept {
    return sdk::boundary([&] {
        if (!b || !b->builder || !p || !size)
            throw std::runtime_error("Buffer is immutable");
        auto s = b->builder->writable();
        *p = s.data();
        *size = s.size();
    });
}
int read_map(const MantisBuffer *b, const void **p, uint64_t *size) noexcept {
    return sdk::boundary([&] {
        if (!b || b->builder || !p || !size)
            throw std::runtime_error("Unpublished buffer");
        auto r = b->published.map_read();
        if (!r)
            throw Failure(r.error());
        *p = r->data();
        *size = r->size();
    });
}
int publish(MantisBuffer *b) noexcept {
    return sdk::boundary([&] {
        if (!b || !b->builder)
            throw std::runtime_error("Already published");
        b->published = std::move(*b->builder).publish();
        b->builder.reset();
    });
}
void log(const char *level, const char *component, const char *message) {
    std::cerr << "[" << (level ? level : "info") << "] " << (component ? component : "") << ": "
              << (message ? message : "") << '\n';
}
const MantisHostV1 host{sizeof(host), 1, allocate, retain, release, write_map, read_map, publish, log};
struct PacketView {
    std::vector<MantisBuffer *> buffers;
    std::vector<MantisAttributeV1> attributes;
    MantisPacketV1 packet{};
    explicit PacketView(const data::Packet &p) {
        try {
            for (const auto &a : p.attributes) {
                auto *b = new MantisBuffer(a.buffer);
                buffers.push_back(b);
                MantisAttributeV1 attr{};
                attr.struct_size = sizeof(attr);
                attr.abi_version = 1;
                attr.name = a.descriptor.name.c_str();
                attr.unit = a.descriptor.unit.c_str();
                attr.scalar_type = static_cast<uint32_t>(a.descriptor.scalar);
                attr.rank = static_cast<uint32_t>(a.descriptor.shape.size());
                for (size_t i = 0; i < attr.rank; ++i) {
                    attr.shape[i] = a.descriptor.shape[i];
                    attr.stride[i] = a.descriptor.stride[i];
                }
                attr.buffer = b;
                attr.bytes = a.buffer.size();
                attributes.push_back(attr);
            }
        } catch (...) {
            for (auto *b : buffers)
                release(b);
            throw;
        }
        packet = {sizeof(packet),
                  1,
                  p.type.name.c_str(),
                  p.type.version,
                  p.header.sequence.value,
                  p.header.timestamp.nanoseconds,
                  p.header.timestamp.domain.id.value.c_str(),
                  p.header.calibration.id.value.c_str(),
                  p.header.calibration.revision,
                  p.header.frame.id.value.c_str(),
                  attributes.data(),
                  static_cast<uint32_t>(attributes.size())};
    }
    ~PacketView() {
        for (auto *b : buffers)
            release(b);
    }
};
struct Receiver {
    data::Published result;
    std::string error;
};
int receive(void *context, const MantisPacketV1 *p) noexcept {
    auto &receiver = *static_cast<Receiver *>(context);
    try {
        if (receiver.result || !p || p->struct_size < sizeof(*p) || p->abi_version != 1 || !p->type_id ||
            p->attribute_count > 128 || (!p->attributes && p->attribute_count))
            fail(Status::incompatible, "Invalid plugin packet");
        data::Packet out;
        out.type = {p->type_id, p->schema_version};
        out.header.sequence.value = p->sequence;
        out.header.timestamp = {p->device_time_ns, {{p->clock_id ? p->clock_id : ""}, "plugin clock"}};
        out.header.received = time::MonotonicTimestamp::now();
        out.header.calibration = {{p->calibration_id ? p->calibration_id : ""}, 1, p->calibration_revision};
        out.header.frame = {{p->coordinate_frame ? p->coordinate_frame : ""}, "plugin frame"};
        for (uint32_t i = 0; i < p->attribute_count; ++i) {
            const auto &a = p->attributes[i];
            if (a.struct_size < sizeof(a) || a.abi_version != 1 || a.rank < 1 || a.rank > 4 || !a.name ||
                !a.buffer || a.buffer->builder)
                fail(Status::incompatible, "Invalid/unpublished attribute");
            data::Attribute attr;
            attr.descriptor = {a.name,
                               static_cast<schema::ScalarType>(a.scalar_type),
                               {a.shape, a.shape + a.rank},
                               {a.stride, a.stride + a.rank},
                               a.unit ? a.unit : ""};
            attr.buffer =
                a.buffer->published.slice(static_cast<size_t>(a.offset), static_cast<size_t>(a.bytes));
            out.attributes.push_back(std::move(attr));
        }
        receiver.result = data::publish(std::move(out));
        return 0;
    } catch (const std::exception &e) {
        receiver.error = e.what();
        return 1;
    } catch (...) {
        receiver.error = "Unknown packet error";
        return 1;
    }
}
data::Metadata metadata(const char *json) {
    if (!json) return {};
    if (std::char_traits<char>::length(json) > 65536)
        fail(Status::incompatible, "Plugin metadata exceeds 64 KiB");
    auto values = nlohmann::json::parse(json).get<data::Metadata>();
    if (values.size() > 256) fail(Status::incompatible, "Too many metadata keys");
    return values;
}
data::Published observation(const MantisObservationV1 &o) {
    if (!sdk::compatible_table(&o) || o.sync_quality > 2)
        fail(Status::incompatible, "Invalid observation header");
    Receiver receiver;
    if (receive(&receiver, &o.packet)) fail(Status::incompatible, receiver.error);
    auto result = *receiver.result;
    result.header.received.nanoseconds = o.host_receive_ns;
    result.header.sync = {{o.sync_group ? o.sync_group : ""}, o.sync_trigger};
    result.header.sync_quality = static_cast<time::SyncQuality>(o.sync_quality);
    result.header.metadata = metadata(o.metadata_json);
    return data::publish(std::move(result));
}
int receive_set(void *ctx, const MantisFrameSetV1 *set) noexcept {
    auto &r = *static_cast<Receiver *>(ctx);
    try {
        if (r.result || !sdk::compatible_table(set) || !set->frames ||
            !set->frame_count || set->frame_count > 16)
            fail(Status::incompatible, "Invalid FrameSet");
        // Decode the parent as a header-only packet before attaching children.
        auto header = set->observation;
        if (!header.packet.type_id || std::strcmp(header.packet.type_id, MANTIS_FRAMESET) ||
            header.packet.attribute_count)
            fail(Status::incompatible, "Invalid FrameSet type/attributes");
        header.packet.type_id = MANTIS_IMAGE;
        auto out = *observation(header);
        out.type = schema::frameset;
        for (uint32_t i = 0; i < set->frame_count; ++i)
            out.frames.push_back(observation(set->frames[i]));
        r.result = data::publish(std::move(out));
        return 0;
    } catch (const std::exception &e) { r.error = e.what(); return 1; }
    catch (...) { r.error = "Unknown FrameSet error"; return 1; }
}
struct Discovery {
    std::vector<device::Descriptor> devices;
    std::string error;
};
int discovered(void *ctx, const MantisDiscoveredDeviceV1 *d) noexcept {
    auto &state = *static_cast<Discovery *>(ctx);
    try {
        if (!sdk::compatible_table(d) || !d->id || !*d->id || !d->name ||
            d->capability_count > 128 || (d->capability_count && !d->capabilities) ||
            state.devices.size() >= 256)
            fail(Status::incompatible, "Invalid discovered descriptor");
        device::Descriptor out;
        out.id = {d->id}; out.name = d->name; out.parent = {d->parent_id ? d->parent_id : ""};
        out.metadata = metadata(d->metadata_json);
        for (uint32_t i = 0; i < d->capability_count; ++i) {
            if (!d->capabilities[i]) fail(Status::incompatible, "Null capability");
            out.capabilities.emplace_back(d->capabilities[i]);
        }
        for (const auto &prior : state.devices)
            if (prior.id == out.id) fail(Status::incompatible, "Duplicate discovered ID");
        state.devices.push_back(std::move(out));
        return 0;
    } catch (const std::exception &e) { state.error = e.what(); return 1; }
    catch (...) { state.error = "Unknown enumeration error"; return 1; }
}
class Acquisition final : public device::ImageStream {
    std::shared_ptr<Loaded> loaded_;
    const MantisAcquisitionV1 *api_;
    device::Descriptor descriptor_;
    std::vector<device::Descriptor> children_;
    void *instance_{};
  public:
    Acquisition(std::shared_ptr<Loaded> loaded, device::Descriptor descriptor,
                std::vector<device::Descriptor> children)
        : loaded_(std::move(loaded)), api_(loaded_->query<MantisAcquisitionV1>(MANTIS_ACQUISITION_V1)),
          descriptor_(std::move(descriptor)), children_(std::move(children)) {}
    ~Acquisition() override {
        if (instance_) { api_->stop(instance_); api_->destroy(instance_); }
    }
    const device::Descriptor &descriptor() const override { return descriptor_; }
    std::vector<device::Descriptor> components() const override { return children_; }
    bool source_paced() const override { return true; }
    Result<void> start() override {
        if (instance_) { api_->stop(instance_); api_->destroy(instance_); instance_ = nullptr; }
        if (api_->open(&host, descriptor_.id.value.c_str(), &instance_) || !instance_)
            return std::unexpected(Error{Status::plugin_failed, "Acquisition open failed: " + diagnostics()["error"], "device"});
        if (api_->start(instance_))
            return std::unexpected(Error{Status::plugin_failed, diagnostics()["error"], "device"});
        return {};
    }
    Result<void> stop() override {
        if (instance_ && api_->stop(instance_))
            return std::unexpected(Error{Status::plugin_failed, "Acquisition stop failed: " + diagnostics()["error"], "device"});
        return {};
    }
    data::Metadata diagnostics() const override {
        data::Metadata result;
        auto emit = [](void *ctx, const char *json) noexcept {
            return sdk::boundary([&] { *static_cast<data::Metadata *>(ctx) = metadata(json); });
        };
        if (api_->diagnostics(instance_, emit, &result)) result["error"] = "Cannot read acquisition diagnostics";
        return result;
    }
    Result<data::Published> next() override {
        Receiver receiver;
        auto status = api_->next(instance_, 100, receive_set, &receiver);
        if (status == 2 && !receiver.result) return data::Published{};
        if (status || !receiver.result)
            return std::unexpected(Error{Status::plugin_failed,
                "Acquisition failed: " + diagnostics()["error"] + " " + receiver.error, "device"});
        return receiver.result;
    }
};
class Stream final : public device::ImageStream {
    std::shared_ptr<Loaded> loaded_;
    const MantisDeviceV1 *api_;
    void *instance_{};
    device::Descriptor descriptor_;

  public:
    explicit Stream(std::shared_ptr<Loaded> p)
        : loaded_(std::move(p)), api_(loaded_->query<MantisDeviceV1>(MANTIS_DEVICE_V1)) {
        if (!api_->describe || !api_->create || !api_->destroy || !api_->start || !api_->next || !api_->stop)
            fail(Status::incompatible, "Incomplete device table");
        MantisDeviceDescriptorV1 d{};
        d.struct_size = sizeof(d);
        d.abi_version = 1;
        if (api_->describe(&d) || !d.id || !d.name || d.capability_count > 128)
            fail(Status::plugin_failed, "Device descriptor failed");
        descriptor_.id = {d.id};
        descriptor_.name = d.name;
        descriptor_.plugin_id = loaded_->api()->id;
        for (uint32_t i = 0; i < d.capability_count; ++i)
            descriptor_.capabilities.emplace_back(d.capabilities[i]);
        if (api_->create(&host, &instance_) || !instance_)
            fail(Status::plugin_failed, "Device creation failed");
    }
    ~Stream() override {
        if (instance_) {
            api_->stop(instance_);
            api_->destroy(instance_);
        }
    }
    const device::Descriptor &descriptor() const override {
        return descriptor_;
    }
    Result<void> start() override {
        if (api_->start(instance_))
            return std::unexpected(Error{Status::plugin_failed, "Device start failed", "device"});
        return {};
    }
    Result<void> stop() override {
        if (api_->stop(instance_))
            return std::unexpected(Error{Status::plugin_failed, "Device stop failed", "device"});
        return {};
    }
    Result<data::Published> next() override {
        Receiver r;
        if (api_->next(instance_, receive, &r) || !r.result)
            return std::unexpected(Error{Status::plugin_failed, "Device frame failed: " + r.error, "device"});
        return r.result;
    }
};
} // namespace
const MantisHostV1 *host_api() {
    return &host;
}
Loaded::Loaded(const std::filesystem::path &path) : library_(path) {
    auto entry = reinterpret_cast<MantisPluginEntryV1>(library_.symbol("mantis_plugin_entry"));
    api_ = entry(1);
    if (!api_ || api_->struct_size < sizeof(MantisPluginV1) || api_->abi_version != 1 || !api_->id ||
        !api_->version || !api_->initialize || !api_->shutdown || !api_->query_interface)
        fail(Status::incompatible, "Plugin ABI incompatible");
    if (api_->initialize(&host)) {
        api_ = nullptr;
        fail(Status::plugin_failed, "Plugin initialization failed");
    }
}
Loaded::~Loaded() {
    if (api_)
        api_->shutdown();
}
data::Published process(const Loaded &p, const data::Packet &input) {
    auto api = p.query<MantisProcessorV1>(MANTIS_PROCESSOR_V1);
    if (!api->process)
        fail(Status::incompatible, "Missing process function");
    PacketView view(input);
    Receiver r;
    if (api->process(&host, &view.packet, receive, &r) || !r.result)
        fail(Status::plugin_failed, "Plugin processing failed: " + r.error);
    auto output = *r.result;
    output.header = input.header;
    return data::publish(std::move(output));
}
void export_data(const Loaded &p, const data::Packet &data, const std::filesystem::path &path) {
    auto api = p.query<MantisExporterV1>(MANTIS_EXPORTER_V1);
    if (!api->input_type || data.type.name != api->input_type || data.type.version != api->input_schema ||
        !api->export_file)
        fail(Status::incompatible, "Exporter input type mismatch");
    PacketView view(data);
    auto utf = path.u8string();
    if (api->export_file(&host, &view.packet, reinterpret_cast<const char *>(utf.c_str())))
        fail(Status::plugin_failed, "Export failed");
}
pipeline::NodeDescriptor describe_node(const Loaded &p) {
    auto api = p.query<MantisProcessorV1>(MANTIS_PROCESSOR_V1);
    MantisNodeDescriptorV1 d{};
    d.struct_size = sizeof(d);
    d.abi_version = 1;
    if (!api->describe || api->describe(&d) || !d.id || !d.input_type || !d.output_type || !d.backend)
        fail(Status::incompatible, "Invalid node descriptor");
    pipeline::NodeDescriptor out;
    out.id = d.id;
    out.plugin_id = p.api()->id;
    out.algorithm_id = d.id;
    out.inputs = {{"input", {d.input_type, d.input_schema}}};
    out.outputs = {{"output", {d.output_type, d.output_schema}}};
    out.deterministic = d.deterministic != 0;
    out.resources.backends = {d.backend};
    return out;
}
Registry::Registry(std::filesystem::path host, std::filesystem::path scratch, LogSink logger)
    : host_(std::move(host)), scratch_(std::move(scratch)), logger_(std::move(logger)) {
    std::filesystem::create_directories(scratch_);
}
std::shared_ptr<Registry::Entry> Registry::entry(const std::string &id) const {
    std::lock_guard lock(mutex_);
    auto it = entries_.find(id);
    if (it == entries_.end())
        fail(Status::not_found, "Unknown plugin: " + id);
    return it->second;
}
void Registry::discover(const std::filesystem::path &directory, const std::vector<std::string> &approved) {
    for (const auto &file : std::filesystem::directory_iterator(directory)) {
        if (file.path().extension() != ".json")
            continue;
        auto e = std::make_shared<Entry>();
        e->manifest.id = file.path().stem().string();
        try {
            std::ifstream in(file.path());
            auto j = nlohmann::json::parse(in);
            if (j.at("manifest_version") != 1 || j.at("abi_version") != 1)
                fail(Status::incompatible, "Manifest/ABI version unsupported");
            e->manifest.id = j.at("id");
            e->manifest.version = j.at("version");
            e->manifest.abi_version = j.at("abi_version");
            e->manifest.kind = j.at("kind");
            e->manifest.execution = j.at("execution");
            e->manifest.permissions = j.at("permissions").get<std::vector<std::string>>();
            auto lib = std::filesystem::path(j.at("library").get<std::string>());
            if (lib.has_parent_path() || lib.empty())
                fail(Status::invalid_argument, "Plugin library must be a sibling filename");
            e->manifest.library = std::filesystem::absolute(file.path().parent_path() / lib);
            if (e->manifest.execution != "in_process" && e->manifest.execution != "isolated")
                fail(Status::invalid_argument, "Unknown execution mode");
            bool trusted = e->manifest.execution == "in_process" &&
                           std::find(approved.begin(), approved.end(), e->manifest.id) != approved.end();
            if (trusted) {
                e->loaded = std::make_shared<Loaded>(e->manifest.library);
                if (e->manifest.id != e->loaded->api()->id ||
                    e->manifest.version != e->loaded->api()->version)
                    fail(Status::incompatible, "Manifest/binary identity mismatch");
                if (e->manifest.kind == "processor")
                    e->node = describe_node(*e->loaded);
            } else {
                e->manifest.execution = "isolated";
                auto probe = scratch_ / (Id::random().value + ".json");
                auto rc =
                    platform::run_process(host_, {"probe", e->manifest.library.string(), probe.string()});
                if (rc)
                    fail(Status::plugin_failed, "Plugin probe host exited " + std::to_string(rc));
                std::ifstream result(probe);
                auto info = nlohmann::json::parse(result);
                result.close();
                std::filesystem::remove(probe);
                if (info.at("id") != e->manifest.id || info.at("version") != e->manifest.version)
                    fail(Status::incompatible, "Plugin identity mismatch");
                if (e->manifest.kind == "device")
                    fail(Status::unsupported, "Isolated device stream transport is reserved; v0.1 supports "
                                              "isolated processors/exporters");
                if (e->manifest.kind == "processor") {
                    e->node.id = info.at("node");
                    e->node.plugin_id = e->manifest.id;
                    e->node.algorithm_id = e->node.id;
                    e->node.deterministic = info.at("deterministic");
                    e->node.resources.backends = {info.at("backend")};
                    e->node.inputs = {{"input", {info.at("input"), info.at("input_schema")}}};
                    e->node.outputs = {{"output", {info.at("output"), info.at("output_schema")}}};
                }
            }
        } catch (const std::exception &ex) {
            e->state = "failed";
            e->diagnostic = ex.what();
            if (logger_)
                logger_({"error", "plugin", e->manifest.id + ": " + e->diagnostic});
        }
        std::lock_guard lock(mutex_);
        if (entries_.contains(e->manifest.id))
            fail(Status::invalid_argument, "Duplicate plugin ID");
        entries_.emplace(e->manifest.id, e);
    }
}
std::vector<PluginStatus> Registry::statuses() const {
    std::lock_guard lock(mutex_);
    std::vector<PluginStatus> out;
    for (auto &[id, e] : entries_)
        out.push_back({e->manifest, e->state, e->diagnostic});
    return out;
}
std::vector<std::unique_ptr<device::ImageStream>> Registry::devices() {
    std::vector<std::unique_ptr<device::ImageStream>> out;
    for (auto &[id, e] : entries_) {
        if (e->manifest.kind != "device" || !e->loaded || e->state != "registered") continue;
        try {
            if (!e->loaded->api()->query_interface(MANTIS_ACQUISITION_V1)) {
                out.push_back(std::make_unique<Stream>(e->loaded));
                continue;
            }
            auto api = e->loaded->query<MantisAcquisitionV1>(MANTIS_ACQUISITION_V1);
            if (!api->enumerate || !api->open || !api->destroy || !api->start || !api->next ||
                !api->stop || !api->diagnostics) fail(Status::incompatible, "Incomplete acquisition table");
            Discovery discovery;
            if (api->enumerate(discovered, &discovery)) {
                data::Metadata diagnostic;
                auto emit = [](void *ctx, const char *json) noexcept {
                    return sdk::boundary([&] { *static_cast<data::Metadata *>(ctx) = metadata(json); });
                };
                (void)api->diagnostics(nullptr, emit, &diagnostic);
                fail(Status::plugin_failed, "Acquisition discovery failed: " + discovery.error + " " + diagnostic["error"]);
            }
            e->diagnostic.clear();
            for (auto &d : discovery.devices) d.plugin_id = id;
            for (auto &d : discovery.devices) {
                if (std::find(d.capabilities.begin(), d.capabilities.end(), device::frameset_stream) == d.capabilities.end()) continue;
                std::vector<device::Descriptor> children;
                for (const auto &child : discovery.devices) if (child.parent == d.id) {
                    d.children.push_back(child.id); children.push_back(child);
                }
                out.push_back(std::make_unique<Acquisition>(e->loaded, d, std::move(children)));
            }
        } catch (const std::exception &ex) {
            e->diagnostic = ex.what();
            if (logger_) logger_({"error", "discovery", id + ": " + ex.what()});
        }
    }
    return out;
}
void Registry::isolated(const std::shared_ptr<Entry> &e, const std::string &operation,
                        const std::filesystem::path &input, const std::filesystem::path &output,
                        const CancellationToken &token) {
    try {
        int status = platform::run_process(
            host_, {operation, e->manifest.library.string(), input.string(), output.string()}, token);
        if (status)
            fail(Status::plugin_failed, "Plugin host exited with status " + std::to_string(status),
                 e->manifest.id);
    } catch (const Failure &ex) {
        if (ex.error.code == Status::cancelled)
            throw;
        {
            std::lock_guard lock(mutex_);
            e->state = "failed";
            e->diagnostic = ex.what();
        }
        if (logger_)
            logger_({"error", "plugin", e->manifest.id + ": " + ex.what()});
        throw;
    }
}
namespace {
class FunctionNode : public pipeline::NodeInstance {
    std::function<Result<data::Published>(std::span<const data::Published>, const CancellationToken &)> f_;

  public:
    explicit FunctionNode(decltype(f_) f) : f_(std::move(f)) {}
    Result<data::Published> process(std::span<const data::Published> p, const CancellationToken &c) override {
        return f_(p, c);
    }
};
} // namespace
pipeline::Node Registry::node(const std::string &id) {
    auto e = entry(id);
    {
        std::lock_guard lock(mutex_);
        if (e->state != "registered" || e->manifest.kind != "processor")
            fail(Status::plugin_failed, "Processor unavailable: " + id);
    }
    return {e->node, [this, e] {
                return std::make_unique<FunctionNode>(
                    [this, e](std::span<const data::Published> inputs,
                              const CancellationToken &token) -> Result<data::Published> {
                        try {
                            token.check();
                            if (inputs.size() != 1)
                                fail(Status::invalid_argument, "Example processor expects one input");
                            if (e->loaded)
                                return plugins::process(*e->loaded, *inputs[0]);
                            auto dir = scratch_ / Id::random().value;
                            std::filesystem::create_directory(dir);
                            struct Cleanup {
                                std::filesystem::path p;
                                ~Cleanup() {
                                    std::error_code ec;
                                    std::filesystem::remove_all(p, ec);
                                }
                            } cleanup{dir};
                            data::write_packet(dir / "input.packet", *inputs[0]);
                            isolated(e, "process", dir / "input.packet", dir / "output.packet", token);
                            return data::read_packet(dir / "output.packet");
                        } catch (const Failure &ex) {
                            return std::unexpected(ex.error);
                        } catch (const std::exception &ex) {
                            return std::unexpected(Error{Status::plugin_failed, ex.what(), e->manifest.id});
                        }
                    });
            }};
}
void Registry::export_file(const std::string &id, const data::Packet &input,
                           const std::filesystem::path &output, const CancellationToken &token) {
    auto e = entry(id);
    {
        std::lock_guard lock(mutex_);
        if (e->state != "registered" || e->manifest.kind != "exporter")
            fail(Status::plugin_failed, "Exporter unavailable");
    }
    token.check();
    if (e->loaded) {
        export_data(*e->loaded, input, output);
        return;
    }
    auto path = scratch_ / (Id::random().value + ".packet");
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(p, ec);
        }
    } cleanup{path};
    data::write_packet(path, input);
    isolated(e, "export", path, output, token);
}
void Registry::set_enabled(const std::string &id, bool enabled) {
    auto e = entry(id);
    std::lock_guard lock(mutex_);
    if (e->manifest.kind == "device")
        fail(Status::busy, "Device plugin lifecycle belongs to active device runtime");
    e->state = enabled ? "registered" : "disabled";
    e->diagnostic.clear();
}
} // namespace mantis::plugins
