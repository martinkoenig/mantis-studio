#include <fstream>
#include <future>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/data_io.hpp>
#include <mantis/jobs.hpp>
#include <mantis/pipeline_runtime.hpp>
#include <mantis/plugin_runtime.hpp>
using namespace mantis;
namespace {
int checks = 0;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string("Check failed: ") + #x + " at line " +                      \
                                     std::to_string(__LINE__));                                              \
    } while (false)
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const Failure &) {
        rejected = true;
    }
    CHECK(rejected);
}
data::Published image() {
    memory::BufferBuilder builder(4);
    auto bytes = builder.writable();
    bytes[0] = std::byte{1};
    bytes[1] = std::byte{2};
    bytes[2] = std::byte{3};
    bytes[3] = std::byte{4};
    data::Packet p;
    p.type = schema::image;
    p.header.timestamp.domain = {{"test-clock"}, "test"};
    p.attributes.push_back({{"org.mantis.pixels", schema::ScalarType::u8, {2, 2}, {2, 1}, "intensity"},
                            std::move(builder).publish()});
    return data::publish(std::move(p));
}
void queues() {
    using namespace pipeline;
    for (auto policy : {QueuePolicy::block, QueuePolicy::lossless}) {
        BoundedQueue<int> q(1, policy);
        CHECK(q.push(1));
        auto pending = std::async(std::launch::async, [&] { return q.push(2); });
        CHECK(pending.wait_for(std::chrono::milliseconds(25)) == std::future_status::timeout);
        CHECK(q.pop() == 1);
        CHECK(pending.get());
        CHECK(q.pop() == 2);
        CHECK(q.metrics().dropped == 0);
        q.close();
        CHECK(!q.pop());
        CHECK(!q.push(3));
    }
    BoundedQueue<int> oldest(2, QueuePolicy::drop_oldest);
    oldest.push(1);
    oldest.push(2);
    oldest.push(3);
    CHECK(oldest.pop() == 2);
    CHECK(oldest.pop() == 3);
    CHECK(oldest.metrics().dropped == 1);
    BoundedQueue<int> latest(8, QueuePolicy::latest_only);
    latest.push(1);
    latest.push(2);
    latest.push(3);
    CHECK(latest.pop() == 3);
    CHECK(latest.metrics().high_water == 1);
    CHECK(latest.metrics().dropped == 2);
    BoundedQueue<int> newest(1, QueuePolicy::drop_newest);
    newest.push(1);
    newest.push(2);
    CHECK(newest.pop() == 1);
    CHECK(newest.metrics().dropped == 1);
    BoundedQueue<int> stopped(1, QueuePolicy::lossless);
    stopped.push(1);
    std::stop_source stop;
    auto pending = std::async(std::launch::async, [&] { return stopped.push(2, stop.get_token()); });
    stop.request_stop();
    CHECK(!pending.get());
    BoundedQueue<int> stress(4, QueuePolicy::lossless);
    std::thread producer([&] {
        for (int i = 0; i < 2000; ++i)
            stress.push(i);
        stress.close();
    });
    int expected = 0;
    while (auto n = stress.pop())
        CHECK(*n == expected++);
    producer.join();
    CHECK(expected == 2000);
    CHECK(stress.metrics().high_water <= 4);
    rejects([] { BoundedQueue<int> invalid(0, QueuePolicy::block); });
}
} // namespace
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        queues();
        auto input = image();
        auto view = input->attributes[0].buffer.slice(1, 2);
        CHECK(view.identity() == input->attributes[0].buffer.identity());
        CHECK(view.map_read()->size() == 2);
        CHECK(view.alignment() == 1);
        CHECK(std::to_integer<int>((*view.map_read())[0]) == 2);
        rejects([&] { view.slice(2, 1); });
        memory::BufferBuilder published(8);
        auto frozen = std::move(published).publish();
        rejects([&] { published.writable(); });
        CHECK(frozen.access() == memory::Access::read_only);
        auto opaque = std::make_shared<memory::Storage>();
        opaque->domain = memory::MemoryDomain::device_local;
        opaque->size = 100;
        memory::Buffer device_buffer{opaque, 0, 100};
        CHECK(!device_buffer.map_read());
        CHECK(!schema::validate({"bad", schema::ScalarType::f32, {3}, {4}, ""}, 12));
        CHECK(!schema::validate({"a.b", schema::ScalarType::f64, {UINT64_MAX}, {8}, ""}, 8));
        spatial::CoordinateFrame a{{"a"}, "a"}, b{{"b"}, "b"}, c{{"c"}, "c"};
        spatial::Transform ab;
        ab.target = b;
        ab.source = a;
        ab.matrix[3] = 2;
        spatial::Transform bc;
        bc.target = c;
        bc.source = b;
        bc.matrix[7] = 3;
        spatial::TransformGraph graph;
        auto next = graph.with(ab).with(bc);
        CHECK(graph.version() == 0);
        CHECK(next.version() == 2);
        auto transform = next.resolve(c, a);
        CHECK(transform);
        CHECK((transform->apply({1, 1, 1}) == std::array<double, 3>{3, 4, 1}));
        auto tmp = std::filesystem::temp_directory_path() / Id::random().value;
        std::filesystem::create_directories(tmp);
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(p, ec);
            }
        } cleanup{tmp};
        // Host allocation ownership remains entirely on the host side of the C ABI.
        const auto *host_api = plugins::host_api();
        auto *abi_buffer = host_api->allocate(16, 64);
        CHECK(abi_buffer);
        const void *read_pointer = nullptr;
        void *write_pointer = nullptr;
        uint64_t abi_size = 0;
        CHECK(host_api->read_map(abi_buffer, &read_pointer, &abi_size) != 0);
        CHECK(host_api->write_map(abi_buffer, &write_pointer, &abi_size) == 0);
        CHECK(abi_size == 16);
        CHECK(host_api->publish(abi_buffer) == 0);
        CHECK(host_api->write_map(abi_buffer, &write_pointer, &abi_size) != 0);
        CHECK(host_api->read_map(abi_buffer, &read_pointer, &abi_size) == 0);
        host_api->retain(abi_buffer);
        host_api->release(abi_buffer);
        host_api->release(abi_buffer);
        auto plugin_dir = std::filesystem::path(argv[1]);
        auto host = plugin_dir.parent_path() / "bin" / "mantis-plugin-host";
#ifdef _WIN32
        host += ".exe";
#endif
        plugins::Registry plugins(host, tmp / "host", {});
        plugins.discover(plugin_dir,
                         {"org.mantis.virtual-scanner", "org.mantis.example-points", "org.mantis.ply"});
        auto streams = plugins.devices();
        CHECK(streams.size() == 1);
        CHECK(streams[0]->descriptor().id.value == "virtual-scanner");
        CHECK(streams[0]->start());
        auto frame = streams[0]->next();
        CHECK(frame);
        CHECK(streams[0]->stop());
        CHECK(!streams[0]->next());
        CHECK(streams[0]->start());
        auto repeat = streams[0]->next();
        CHECK(repeat);
        CHECK(content_hash(*(*frame)->attributes[0].buffer.map_read()) ==
              content_hash(*(*repeat)->attributes[0].buffer.map_read()));
        CHECK(streams[0]->stop());
        pipeline::PipelineRecipe recipe;
        recipe.id = "test";
        pipeline::NodeDescriptor source;
        source.id = "source";
        source.outputs = {{"out", schema::image}};
        recipe.graph.nodes = {{source, {}}, plugins.node("org.mantis.example-points")};
        recipe.graph.connections = {{0, 0, 1, 0, 2, pipeline::QueuePolicy::block}};
        auto plan = pipeline::compile(recipe);
        CHECK(plan);
        auto result = pipeline::execute(*plan, *frame, {});
        CHECK(result);
        CHECK(result->output->type == schema::points);
        CHECK(result->output->attributes[0].descriptor.shape[0] == 3072);
        auto result2 = pipeline::execute(*plan, *frame, {});
        CHECK(result2);
        CHECK(content_hash(*result->output->attributes[0].buffer.map_read()) ==
              content_hash(*result2->output->attributes[0].buffer.map_read()));
        CHECK(result->timings.size() == 2);
        auto branched = recipe;
        branched.graph.nodes.push_back(plugins.node("org.mantis.example-points"));
        branched.graph.nodes.back().descriptor.id = "second-branch";
        branched.graph.connections.push_back({0, 0, 2, 0, 1, pipeline::QueuePolicy::latest_only});
        auto branch_plan = pipeline::compile(branched);
        CHECK(branch_plan);
        auto branch_result = pipeline::execute(*branch_plan, *frame, {});
        CHECK(branch_result);
        CHECK(branch_result->outputs.size() == 2);
        // Streaming instances must retain their state across input frames.
        class CounterNode final : public pipeline::NodeInstance {
            uint64_t count_{};
            Result<data::Published> process(std::span<const data::Published> values,
                                            const CancellationToken &) override {
                auto packet = *values[0];
                packet.header.sequence.value = ++count_;
                return data::publish(std::move(packet));
            }
        };
        auto stateful = recipe;
        stateful.graph.nodes[1].descriptor.outputs[0].type = schema::image;
        stateful.graph.nodes[1].descriptor.stateful = true;
        stateful.graph.nodes[1].factory = []() { return std::make_unique<CounterNode>(); };
        auto stateful_plan = pipeline::compile(stateful);
        CHECK(stateful_plan);
        pipeline::BoundedQueue<data::Published> stateful_source(2, pipeline::QueuePolicy::lossless);
        stateful_source.push(*frame);
        stateful_source.push(*frame);
        stateful_source.close();
        uint64_t seen = 0;
        CHECK(pipeline::execute_stream(
            *stateful_plan, stateful_source,
            [&](auto item) { CHECK(item.output->header.sequence.value == ++seen); }, {}));
        CHECK(seen == 2);
        auto incompatible = recipe;
        incompatible.graph.nodes[1].descriptor.inputs[0].type = schema::mesh;
        CHECK(!pipeline::compile(incompatible));
        auto version = recipe;
        version.graph.nodes[1].descriptor.inputs[0].type.version = 2;
        CHECK(!pipeline::compile(version));
        auto gpu = recipe;
        gpu.graph.nodes[1].descriptor.resources.backends = {"cuda"};
        CHECK(!pipeline::compile(gpu));
        auto cycle = recipe;
        cycle.graph.nodes[0].factory = recipe.graph.nodes[1].factory;
        cycle.graph.nodes[0].descriptor.inputs = {{"in", schema::points}};
        cycle.graph.connections.push_back({1, 0, 0, 0, 1, pipeline::QueuePolicy::block});
        cycle.graph.nodes.push_back({source, {}});
        cycle.graph.nodes.back().descriptor.id = "external";
        CHECK(!pipeline::compile(cycle));
        pipeline::BoundedQueue<data::Published> feed(2, pipeline::QueuePolicy::lossless);
        feed.push(*frame);
        feed.push(*frame);
        feed.close();
        size_t outputs = 0;
        CHECK(pipeline::execute_stream(*plan, feed,
                                       [&](auto out) {
                                           CHECK(out.output->type == schema::points);
                                           ++outputs;
                                       },
                                       {}));
        CHECK(outputs == 2);
        auto packet_path = tmp / "test.packet";
        data::write_packet(packet_path, *result->output);
        auto loaded = data::read_packet(packet_path);
        CHECK(loaded->attributes[0].buffer.domain() == memory::MemoryDomain::shared_memory);
        CHECK(content_hash(*loaded->attributes[0].buffer.map_read()) ==
              content_hash(*result->output->attributes[0].buffer.map_read()));
        Id provisional, finalized;
        {
            artifact::Store store(tmp / "project");
            rejects([&] { artifact::Store second(tmp / "project"); });
            artifact::Provenance prov;
            prov.producer = "test";
            finalized = store.begin({schema::points.name, 1}, prov);
            store.append(finalized, *result->output);
            auto finalized_descriptor = store.finalize(finalized);
            CHECK(finalized_descriptor.state == artifact::ArtifactState::finalized);
            CHECK(!finalized_descriptor.hash.hex.empty());
            rejects([&] { store.append(finalized, *result->output); });
            rejects([&] { store.finalize(finalized); });
            provisional = store.begin({"org.mantis.RawCapture", 1}, prov);
            store.append(provisional, **frame);
            std::ofstream corrupt(tmp / "project" / "objects" / provisional.value / "1.packet.part");
            corrupt << "interrupted trailing frame";
        }
        {
            artifact::Store store(tmp / "project");
            CHECK(store.get(finalized).state == artifact::ArtifactState::finalized);
            CHECK(store.get(provisional).state == artifact::ArtifactState::recoverable);
            CHECK(store.recover(provisional).chunks == 1);
            CHECK(store.packet(provisional)->type == schema::image);
        }
        // Successful operations, not just crashes, must work in an isolated host.
        auto isolated_dir = tmp / "isolated-plugins";
        std::filesystem::create_directories(isolated_dir);
        for (const auto &status : plugins.statuses()) {
            if (status.manifest.kind != "processor" && status.manifest.kind != "exporter")
                continue;
            if (status.manifest.id == "org.mantis.crash-test")
                continue;
            auto filename = status.manifest.library.filename();
            std::filesystem::copy_file(status.manifest.library, isolated_dir / filename);
            std::ofstream manifest(isolated_dir / (status.manifest.id + ".json"));
            manifest << "{\"manifest_version\":1,\"abi_version\":1,\"id\":\"" << status.manifest.id
                     << "\",\"version\":\"0.1.0\",\"kind\":\"" << status.manifest.kind
                     << "\",\"execution\":\"isolated\",\"library\":\"" << filename.string()
                     << "\",\"permissions\":[]}";
        }
        plugins::Registry isolated(host, tmp / "isolated-host", {});
        isolated.discover(isolated_dir, {});
        auto isolated_node = isolated.node("org.mantis.example-points");
        auto isolated_instance = isolated_node.factory();
        std::array<data::Published, 1> isolated_input{*frame};
        auto isolated_output = isolated_instance->process(isolated_input, {});
        CHECK(isolated_output);
        CHECK(content_hash(*(*isolated_output)->attributes[0].buffer.map_read()) ==
              content_hash(*result->output->attributes[0].buffer.map_read()));
        isolated.export_file("org.mantis.ply", **isolated_output, tmp / "isolated.ply", {});
        CHECK(std::filesystem::file_size(tmp / "isolated.ply") > 10000);
        plugins.export_file("org.mantis.ply", *result->output, tmp / "points.ply", {});
        CHECK(std::filesystem::file_size(tmp / "points.ply") > 10000);
        {
            jobs::Manager jobs;
            auto id = jobs.submit("cancel",
                                  [](jobs::Context &context) -> std::optional<artifact::ArtifactReference> {
                                      while (!context.cancellation.cancelled())
                                          std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                      context.cancellation.check();
                                      return {};
                                  });
            jobs.cancel(id);
            for (int i = 0; i < 100 && jobs.busy(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            CHECK(jobs.get(id).state == jobs::State::cancelled);
        }
        std::cout << checks << " architecture assertions passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
