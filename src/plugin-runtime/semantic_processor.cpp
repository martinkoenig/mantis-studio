#include <charconv>
#include <cstring>
#include <mantis/plugin_runtime.hpp>
#include <mantis/semantic_views.hpp>
namespace mantis::plugins {
namespace {
void timeout(uint32_t ms) {
    if (!ms || ms > 60000)
        fail(Status::invalid_argument, "ProcessorV2 timeout must be 1..60000 ms");
}
const MantisProcessorV2 *table(const Loaded &p) {
    auto api = p.query<MantisProcessorV2>(MANTIS_PROCESSOR_V2);
    if (!api->describe || !api->process)
        fail(Status::incompatible, "Incomplete ProcessorV2 table");
    return api;
}
std::string text(const char *p) {
    if (!p)
        fail(Status::incompatible, "Null ProcessorV2 descriptor string");
    size_t n = 0;
    while (n <= data::max_semantic_id && p[n])
        ++n;
    if (!n || n > data::max_semantic_id)
        fail(Status::incompatible, "Unbounded/empty ProcessorV2 descriptor string");
    return {p, n};
}
memory::BufferView pin(memory::BufferView buffer, const std::shared_ptr<const void> &library) {
    struct Owner {
        std::shared_ptr<const void> library;
        memory::BufferView buffer;
    };
    auto bytes = buffer.map_read();
    if (!bytes)
        throw Failure(bytes.error());
    auto storage = std::make_shared<memory::Storage>();
    storage->owner = std::make_shared<Owner>(Owner{library, buffer});
    storage->host = bytes->data();
    storage->size = bytes->size();
    storage->retained_extent = buffer.backing_size();
    storage->alignment = buffer.alignment();
    storage->domain = buffer.domain();
    return {std::move(storage), 0, buffer.size()};
}
data::Published pin_packet(const data::Published &p, const std::shared_ptr<const void> &library) {
    auto v = *p;
    for (auto &a : v.attributes)
        a.buffer = pin(a.buffer, library);
    for (auto &f : v.frames)
        f = pin_packet(f, library);
    return data::publish(std::move(v));
}
void pin_output(data::SemanticPacket &value, const std::shared_ptr<const void> &library) {
    std::visit(
        [&](auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, data::Published>)
                v = pin_packet(v, library);
            else if constexpr (std::is_same_v<T, data::AcquisitionBundle>) {
                if (v.frameset)
                    v.frameset = pin_packet(v.frameset, library);
            } else if constexpr (std::is_same_v<T, data::LaserObservation>)
                for (auto &a : v.attributes)
                    a.buffer = pin(a.buffer, library);
        },
        value);
}
void map_inputs(const data::SemanticPacket &value, const CancellationToken &token) {
    auto attributes = [&](const auto &list) {
        if (list.size() > 128)
            fail(Status::incompatible, "Excessive ProcessorV2 input attributes");
        for (const auto &a : list) {
            token.check();
            if (a.buffer.domain() == memory::MemoryDomain::device_local ||
                a.buffer.domain() == memory::MemoryDomain::external_device)
                fail(Status::unsupported, "ProcessorV2 host adapter requires an explicit host transfer");
            auto bytes = a.buffer.map_read(token);
            if (!bytes)
                throw Failure(bytes.error());
        }
    };
    auto packet = [&](const data::Published &p) {
        if (!p)
            fail(Status::incompatible, "Null semantic data packet");
        if (p->frames.size() > 16)
            fail(Status::incompatible, "Excessive FrameSet children");
        attributes(p->attributes);
        for (const auto &f : p->frames) {
            if (!f)
                fail(Status::incompatible, "Null FrameSet child");
            attributes(f->attributes);
        }
    };
    std::visit(
        [&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, data::Published>)
                packet(v);
            else if constexpr (std::is_same_v<T, data::AcquisitionBundle>) {
                if (v.frameset)
                    packet(v.frameset);
            } else if constexpr (std::is_same_v<T, data::LaserObservation>)
                attributes(v.attributes);
        },
        value);
}

struct Receiver {
    unsigned calls{};
    data::SemanticPublished result;
    std::optional<Error> error;
    schema::DataTypeId expected;
    std::shared_ptr<const void> library;
};
int receive(void *context, const MantisSemanticPacketV1 *v) noexcept {
    auto &r = *static_cast<Receiver *>(context);
    try {
        if (++r.calls != 1)
            fail(Status::incompatible, "ProcessorV2 emitted more than once");
        auto out = semantic::packet(v, host_api());
        if (data::semantic_type(out) != r.expected)
            fail(Status::incompatible, "ProcessorV2 output differs from descriptor");
        pin_output(out, r.library);
        // semantic::packet already validated every field and buffer before returning.
        r.result = std::make_shared<const data::SemanticPacket>(std::move(out));
        return 0;
    } catch (const Failure &e) {
        r.error = e.error;
    } catch (const std::exception &e) {
        r.error = Error{Status::plugin_failed, e.what(), "processor.v2"};
    } catch (...) {
        r.error = Error{Status::plugin_failed, "Unknown semantic emit exception", "processor.v2"};
    }
    r.result.reset();
    return 1;
}
} // namespace
pipeline::NodeDescriptor describe_semantic_node(const Loaded &p, uint32_t ms) {
    timeout(ms);
    auto api = table(p);
    MantisNodeDescriptorV1 d{};
    d.struct_size = sizeof(d);
    d.abi_version = 1;
    const auto begin = std::chrono::steady_clock::now();
    int rc;
    try {
        rc = api->describe(ms, &d);
    } catch (...) {
        fail(Status::plugin_failed, "ProcessorV2 describe crossed exception boundary");
    }
    if (rc || !sdk::compatible_table(&d) || !d.input_schema || !d.output_schema || d.deterministic > 1)
        fail(Status::incompatible, "Invalid ProcessorV2 node descriptor");
    if (std::chrono::steady_clock::now() - begin > std::chrono::milliseconds(ms))
        fail(Status::plugin_failed, "ProcessorV2 describe exceeded deadline");
    pipeline::NodeDescriptor out;
    out.id = text(d.id);
    out.algorithm_id = out.id;
    out.plugin_id = p.api()->id;
    auto version = text(p.api()->version);
    uint32_t *parts[]{&out.version.major, &out.version.minor, &out.version.patch};
    size_t begin_part = 0;
    for (unsigned i = 0; i < 3; ++i) {
        auto end = i == 2 ? version.size() : version.find('.', begin_part);
        if (end == std::string::npos)
            fail(Status::incompatible, "Invalid processor semantic version");
        auto [last, ec] = std::from_chars(version.data() + begin_part, version.data() + end, *parts[i]);
        if (ec != std::errc{} || last != version.data() + end)
            fail(Status::incompatible, "Invalid processor semantic version");
        begin_part = end + 1;
    }
    out.inputs = {{"input", {text(d.input_type), d.input_schema}}};
    out.outputs = {{"output", {text(d.output_type), d.output_schema}}};
    for (auto type : {out.inputs[0].type, out.outputs[0].type}) {
        if (type.name.find('.') == std::string::npos)
            fail(Status::incompatible, "Non-namespaced semantic type");
        for (auto known : {schema::image, schema::frameset, schema::acquisition_bundle,
                           schema::acquisition_evidence, schema::trigger_event, schema::laser_observation})
            if (type.name == known.name && type != known)
                fail(Status::incompatible, "Unsupported semantic schema");
    }
    out.deterministic = d.deterministic != 0;
    out.resources.backends = {text(d.backend)};
    return out;
}
data::SemanticPublished process_semantic(const Loaded &p, const data::SemanticPacket &input, uint32_t ms,
                                         const CancellationToken &token) {
    timeout(ms);
    token.check();
    auto descriptor = describe_semantic_node(p, ms);
    if (data::semantic_type(input) != descriptor.inputs[0].type)
        fail(Status::incompatible, "ProcessorV2 input type/schema differs from descriptor");
    map_inputs(input, token);
    auto valid = data::validate(input);
    if (!valid)
        throw Failure(valid.error());
    semantic::PacketView view(input, host_api());
    Receiver r;
    r.expected = descriptor.outputs[0].type;
    r.library = p.lifetime();
    const auto begin = std::chrono::steady_clock::now();
    int rc;
    try {
        rc = table(p)->process(host_api(), view.get(), ms, receive, &r);
    } catch (...) {
        fail(Status::plugin_failed, "ProcessorV2 crossed exception boundary");
    }
    token.check();
    if (std::chrono::steady_clock::now() - begin > std::chrono::milliseconds(ms))
        fail(Status::plugin_failed, "ProcessorV2 process exceeded deadline");
    if (r.error)
        throw Failure(*r.error);
    if (rc || r.calls != 1 || !r.result)
        fail(Status::plugin_failed, "ProcessorV2 requires exactly one emit on success and none on failure");
    return r.result; // own metadata and retained immutable buffers, never replace headers
}
} // namespace mantis::plugins
