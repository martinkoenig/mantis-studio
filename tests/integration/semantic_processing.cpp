#include "../unit/laser_observation_values.hpp"
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/pipeline_runtime.hpp>
#include <mantis/plugin_runtime.hpp>
#include <mantis/replay.hpp>
#include <mantis/semantic_views.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace observation_fixture;
using namespace std::chrono_literals;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__));                 \
    } while (false)
template <class F> void rejects(F f) {
    bool bad = false;
    try {
        f();
    } catch (const std::exception &) {
        bad = true;
    }
    CHECK(bad);
}
void env(const char *key, std::string value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    CHECK(setenv(key, value.c_str(), 1) == 0);
#endif
}
void mode(unsigned n) { env("MANTIS_PROCESSOR_TEST_MODE", std::to_string(n)); }
struct Temporary {
    std::filesystem::path path = std::filesystem::temp_directory_path() / Id::random().value;
    Temporary() { std::filesystem::create_directories(path); }
    ~Temporary() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
void write(const std::filesystem::path &p, std::string_view bytes) {
    std::ofstream f(p, std::ios::binary);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    CHECK(f.good());
}
void processor_tests(const std::filesystem::path &library, const std::filesystem::path &plugins_dir,
                     const std::filesystem::path &host) {
    Temporary tmp;
    mode(0);
    data::SemanticPublished retained, borrowed_retained;
    auto image = images()->frames[0];
    auto original = image->attributes[0].buffer.map_read();
    CHECK(original);
    {
        plugins::Loaded plugin(library);
        CHECK(plugin.api()->query_interface(MANTIS_PROCESSOR_V1) == nullptr);
        CHECK(plugin.api()->query_interface("org.example.unknown") == nullptr);
        auto d = plugins::describe_semantic_node(plugin);
        CHECK(d.inputs[0].type == schema::image);
        CHECK((d.version == SemanticVersion{1, 0, 0}));
        retained = plugins::process_semantic(plugin, SemanticPacket{image});
        borrowed_retained = retained;
        auto out = std::get<Published>(*retained);
        CHECK(out->attributes[0].buffer.map_read()->data() == original->data());
        CHECK(out->header.received.nanoseconds == image->header.received.nanoseconds);
        for (unsigned n : {1, 2, 3, 4, 5, 6, 7, 27, 28}) {
            mode(n);
            rejects([&] { (void)plugins::describe_semantic_node(plugin); });
        }
        for (unsigned n : {10, 11, 12, 13, 14, 17, 18, 19, 20, 21, 22, 23, 24}) {
            mode(n);
            rejects([&] { (void)plugins::process_semantic(plugin, SemanticPacket{image}); });
        }
        for (unsigned n = 40; n <= 49; ++n) {
            mode(n);
            rejects([&] { (void)plugins::process_semantic(plugin, SemanticPacket{observation()}); });
        }
        mode(8);
        rejects([&] { (void)plugins::process_semantic(plugin, SemanticPacket{image}); });
        mode(25);
        auto independent = plugins::process_semantic(plugin, SemanticPacket{image});
        CHECK(std::get<Published>(*independent)->header.sequence.value == 991);
        CHECK(std::get<Published>(*independent)->header.received.nanoseconds == 771);
        mode(26);
        retained = plugins::process_semantic(plugin, SemanticPacket{image});
        CHECK((*std::get<Published>(*retained)->attributes[0].buffer.map_read())[0] == std::byte{47});
        for (auto pair : std::vector<std::pair<unsigned, SemanticPacket>>{{9, bundle(1)},
                                                                          {30, images()},
                                                                          {31, observation(4)},
                                                                          {32, bundle(2).triggers[0]},
                                                                          {33, bundle(0).evidence}}) {
            mode(pair.first);
            auto out2 = plugins::process_semantic(plugin, pair.second);
            CHECK(semantic_bytes(*out2) == semantic_bytes(pair.second));
        }
        mode(0);
        CancellationToken cancelled;
        cancelled.cancel();
        rejects([&] { (void)plugins::process_semantic(plugin, SemanticPacket{image}, 1000, cancelled); });
        rejects([&] { (void)plugins::process_semantic(plugin, SemanticPacket{image}, 0); });
        mode(0);
        auto opaque = std::make_shared<memory::Storage>();
        opaque->size = 4;
        opaque->domain = memory::MemoryDomain::device_local;
        auto unmapped = *image;
        unmapped.attributes[0].buffer = {opaque, 0, 4};
        rejects([&] { (void)plugins::process_semantic(plugin, SemanticPacket{data::publish(unmapped)}); });
        // Concurrent stateless calls retain independent output ownership; no shared invocation buffer.
        std::vector<std::future<data::SemanticPublished>> calls;
        for (unsigned n = 0; n < 8; ++n)
            calls.push_back(std::async(std::launch::async, [&] {
                return plugins::process_semantic(plugin, SemanticPacket{image});
            }));
        for (auto &call : calls)
            CHECK(semantic_bytes(*call.get()) == semantic_bytes(SemanticPacket{image}));
    }
    image.reset();
    CHECK((*std::get<Published>(*retained)->attributes[0].buffer.map_read())[3] == std::byte{47});
    CHECK((*std::get<Published>(*borrowed_retained)->attributes[0].buffer.map_read())[3] == std::byte{255});
    mode(0);
    plugins::Registry isolated(host, tmp.path / "scratch", {});
    isolated.discover(plugins_dir, {});
    auto node = isolated.semantic_node("org.example.processor-contract", 100);
    auto instance = node.factory();
    auto input = data::publish(SemanticPacket{images()->frames[0]});
    std::array<data::SemanticPublished, 1> inputs{input};
    auto good = instance->process(inputs, {});
    CHECK(good);
    CHECK(semantic_bytes(**good) == semantic_bytes(*input));
    plugins::Registry trusted(host, tmp.path / "trusted", {});
    trusted.discover(plugins_dir, {"org.example.processor-contract"});
    auto native_instance = trusted.semantic_node("org.example.processor-contract").factory();
    mode(34); // callback advertises/emits a valid value that differs from its registered node descriptor
    auto changed = native_instance->process(inputs, {});
    CHECK(!changed && changed.error().code == Status::incompatible);
    CHECK(!instance->process(inputs, {}));
    isolated.set_enabled("org.example.processor-contract", true);
    mode(0);
    CHECK(native_instance->process(inputs, {}));
    {
        mode(35);
        plugins::Registry both(host, tmp.path / "both", {});
        both.discover(plugins_dir, {"org.example.processor-contract"});
        CHECK(both.semantic_node("org.example.processor-contract").factory()->process(inputs, {}));
        std::array<data::Published, 1> flat{std::get<Published>(*input)};
        CHECK(!both.node("org.example.processor-contract").factory()->process(flat, {}));
    }
    mode(0);
    for (unsigned n : {10, 11, 12, 13, 14, 15, 17, 18, 20, 24}) {
        isolated.set_enabled("org.example.processor-contract", true);
        mode(n);
        auto result = instance->process(inputs, {});
        CHECK(!result);
        auto statuses = isolated.statuses();
        auto failed = std::find_if(statuses.begin(), statuses.end(), [](const auto &s) {
            return s.manifest.id == "org.example.processor-contract";
        });
        CHECK(failed != statuses.end() && failed->state == "failed" && !failed->diagnostic.empty());
        if (n == 15)
            CHECK(result.error().code == Status::plugin_failed);
        CHECK(std::filesystem::is_empty(tmp.path / "scratch"));
    }
    mode(16);
    isolated.set_enabled("org.example.processor-contract", true);
    auto begin = std::chrono::steady_clock::now();
    auto timed = instance->process(inputs, {});
    CHECK(!timed);
    CHECK(std::chrono::steady_clock::now() - begin < 3s);
    isolated.set_enabled("org.example.processor-contract", true);
    CancellationToken token;
    auto pending = std::async(std::launch::async, [&] { return instance->process(inputs, token); });
    std::this_thread::sleep_for(50ms);
    begin = std::chrono::steady_clock::now();
    token.cancel();
    CHECK(pending.wait_for(1s) == std::future_status::ready);
    auto cancelled = pending.get();
    CHECK(!cancelled && cancelled.error().code == Status::cancelled);
    CHECK(std::chrono::steady_clock::now() - begin < 1s);
    CHECK(std::filesystem::is_empty(tmp.path / "scratch"));
    mode(0);
    CHECK(instance->process(inputs, {}));
    // Explicit flat bridge allows a V2/V1 graph without changing the legacy plugin's behavior.
    plugins::Registry legacy(host, tmp.path / "legacy", {});
    legacy.discover(plugins_dir.parent_path() / "plugins", {"org.mantis.example-points"});
    rejects([&] { (void)legacy.semantic_node("org.mantis.example-points"); });
    auto pixels = *images()->frames[0];
    pixels.attributes[0].descriptor.shape = {2, 2};
    pixels.attributes[0].descriptor.stride = {2, 1};
    pipeline::NodeDescriptor source;
    source.id = "mixed-source";
    source.outputs = {{"image", schema::image}};
    pipeline::SemanticGraph mixed;
    mixed.nodes = {{source, {}},
                   isolated.semantic_node("org.example.processor-contract"),
                   pipeline::semantic_bridge(legacy.node("org.mantis.example-points"))};
    mixed.connections = {{0, 0, 1, 0, 1, pipeline::QueuePolicy::lossless},
                         {1, 0, 2, 0, 1, pipeline::QueuePolicy::lossless}};
    auto mixed_plan = pipeline::compile(mixed);
    CHECK(mixed_plan);
    auto mixed_result = pipeline::execute(*mixed_plan, data::publish(SemanticPacket{data::publish(pixels)}));
    CHECK(mixed_result);
    CHECK(data::semantic_type(*mixed_result->output) == schema::points);
    CHECK(std::get<Published>(*mixed_result->output)->header.sequence.value == pixels.header.sequence.value);
    auto bad_bridge = legacy.node("org.mantis.example-points");
    bad_bridge.descriptor.inputs[0].type = schema::acquisition_bundle;
    rejects([&] { (void)pipeline::semantic_bridge(std::move(bad_bridge)); });
    // Input lifetime/presence contradictions rejected before publication.
    auto obs = observation();
    plugins::semantic::PacketView view(SemanticPacket{obs}, plugins::host_api());
    auto bad = *view.get();
    bad.kind = 99;
    rejects([&] { (void)plugins::semantic::packet(&bad, plugins::host_api()); });
    bad = *view.get();
    bad.data = reinterpret_cast<const MantisDataPacketV1 *>(bad.laser);
    rejects([&] { (void)plugins::semantic::packet(&bad, plugins::host_api()); });
    auto laser = *view.get()->laser;
    bad = *view.get();
    bad.laser = &laser;
    laser.sample_count = 3;
    rejects([&] { (void)plugins::semantic::packet(&bad, plugins::host_api()); });
    laser = *view.get()->laser;
    laser.context.source.rig_calibration.presence = 99;
    rejects([&] { (void)plugins::semantic::packet(&bad, plugins::host_api()); });
}
void artifact_tests() {
    Temporary tmp;
    const auto oversized = tmp.path / "oversized.semantic";
    write(oversized, "");
    std::filesystem::resize_file(oversized, data::max_observation_record_bytes + 1);
    rejects([&] { (void)data::read_laser_observation(oversized); });
    rejects([&] { (void)data::read_semantic_packet(oversized); });
    Id id;
    memory::BufferView retained;
    const auto original = observation(4);
    const auto canonical = encode(original);
    {
        artifact::Store store(tmp.path / "project");
        id = store.begin_laser_observation();
        rejects([&] { store.append(id, *images()); });
        store.append_laser_observation(id, original);
        CHECK(store.get(id).bytes == canonical.size());
        rejects([&] { store.append_laser_observation(id, original); });
        auto descriptor = store.finalize(id);
        CHECK(descriptor.state == artifact::ArtifactState::finalized);
        CHECK(descriptor.provenance.producer == original.context.producer.implementation.value);
        CHECK(std::find(descriptor.provenance.inputs.begin(), descriptor.provenance.inputs.end(),
                        Id{"raw-art"}) != descriptor.provenance.inputs.end());
        auto restored = store.laser_observation(id);
        CHECK(encode(restored) == canonical);
        retained = restored.attributes.back().buffer;
        CHECK(retained.domain() == memory::MemoryDomain::shared_memory);
        rejects([&] { (void)store.packet(id); });
        rejects([&] { store.append_laser_observation(id, original); });
        rejects([&] { (void)store.begin({"org.mantis.LaserObservation", 2}, {}); });
        auto bad = store.begin_laser_observation();
        auto object = tmp.path / "project/objects" / bad.value / "0.observation.part";
        std::filesystem::create_directory(object);
        rejects([&] { store.append_laser_observation(bad, original); });
        CHECK(store.get(bad).state == artifact::ArtifactState::recoverable);
        rejects([&] { (void)store.recover(bad); });
    }
    CHECK(retained.map_read());
    CHECK(retained.size() == 16);
    {
        artifact::Store store(tmp.path / "project");
        CHECK(encode(store.laser_observation(id)) == canonical);
        auto object = store.object_path(id);
        auto bad = canonical;
        bad[32] ^= 1;
        write(object, bad);
        rejects(
            [&] { (void)store.laser_observation(id); }); // persisted chunk hash catches complete corruption
    }
    // Complete journaled observation is recoverable; incomplete/unpromised data is not finalized.
    for (unsigned n = 0; n < 4; ++n) {
        auto project = tmp.path / ("recovery" + std::to_string(n));
        Id provisional;
        {
            artifact::Store store(project);
            provisional = store.begin_laser_observation();
        }
        auto rel = "objects/" + provisional.value + "/0.observation";
        auto bytes = canonical;
        if (n == 1)
            bytes.resize(bytes.size() / 2);
        if (n == 2)
            bytes[32] ^= 1;
        write(project / (rel + ".part"), bytes);
        auto hash = content_hash({reinterpret_cast<const std::byte *>(bytes.data()), bytes.size()});
        auto journal = nlohmann::json{{"id", provisional.value},
                                      {"index", 0},
                                      {"path", rel},
                                      {"hash", hash.hex},
                                      {"bytes", bytes.size() + (n == 3 ? 1 : 0)}}
                           .dump();
        write(project / "journal" / (provisional.value + "_0.json"), journal);
        artifact::Store store(project);
        CHECK(store.get(provisional).state == artifact::ArtifactState::recoverable);
        if (n == 0) {
            CHECK(store.recover(provisional).state == artifact::ArtifactState::finalized);
            CHECK(encode(store.laser_observation(provisional)) == canonical);
        } else {
            rejects([&] { (void)store.recover(provisional); });
            CHECK(store.get(provisional).state == artifact::ArtifactState::recoverable);
        }
    }
#ifndef _WIN32
    // Process death after complete commit, before explicit finalization.
    auto project = tmp.path / "killed";
    Id killed;
    {
        artifact::Store store(project);
        killed = store.begin_laser_observation();
    }
    int ready[2];
    CHECK(pipe(ready) == 0);
    auto pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        close(ready[0]);
        try {
            artifact::Store store(project);
            // Reopen marks provisional recoverable; create a fresh live artifact in child.
            auto fresh = store.begin_laser_observation();
            store.append_laser_observation(fresh, original);
            std::string name = fresh.value;
            (void)::write(ready[1], name.data(), name.size());
            for (;;)
                pause();
        } catch (...) {
            _exit(2);
        }
    }
    close(ready[1]);
    char name[128];
    auto count = read(ready[0], name, sizeof(name));
    CHECK(count > 0);
    close(ready[0]);
    CHECK(kill(pid, SIGKILL) == 0);
    int status;
    CHECK(waitpid(pid, &status, 0) == pid);
    {
        artifact::Store store(project);
        Id fresh{std::string(name, static_cast<size_t>(count))};
        CHECK(store.get(fresh).state == artifact::ArtifactState::recoverable);
        store.recover(fresh);
        CHECK(encode(store.laser_observation(fresh)) == canonical);
    }
#endif
}
pipeline::SemanticExecutionPlan plan(plugins::Registry &registry) {
    pipeline::NodeDescriptor source;
    source.id = "raw-source";
    source.outputs = {{"bundle", schema::acquisition_bundle}};
    pipeline::SemanticGraph graph;
    graph.nodes = {{source, {}},
                   registry.semantic_node("org.example.synthetic-observation"),
                   pipeline::observation_consumer()};
    graph.connections = {{0, 0, 1, 0, 2, pipeline::QueuePolicy::lossless},
                         {1, 0, 2, 0, 2, pipeline::QueuePolicy::lossless}};
    auto result = pipeline::compile(graph);
    CHECK(result);
    auto mismatch = graph;
    mismatch.nodes[1].descriptor.inputs[0].type.version = 2;
    CHECK(!pipeline::compile(mismatch));
    auto cycle = graph;
    cycle.connections.push_back({2, 0, 1, 0, 1, pipeline::QueuePolicy::lossless});
    CHECK(!pipeline::compile(cycle));
    auto gpu = graph;
    gpu.nodes[1].descriptor.resources.backends = {"missing"};
    CHECK(!pipeline::compile(gpu));
    auto lossy = graph;
    lossy.connections[0].policy = pipeline::QueuePolicy::drop_oldest;
    CHECK(!pipeline::compile(lossy));
    return *result;
}
void stream_tests(const std::filesystem::path &plugin_dir, const std::filesystem::path &host) {
    Temporary tmp;
    mode(0);
    auto store = std::make_shared<artifact::Store>(tmp.path / "project");
    auto raw = store->begin_projected_capture(header());
    store->append_bundle(raw, bundle(0));
    store->append_bundle(raw, bundle(1));
    store->record_run_outcome(raw, {2, outcome(RecordedRunDisposition::completed, AcquisitionReason::none)});
    auto raw_descriptor = store->finalize(raw);
    const auto input_bytes = storage_fixture::encode(store->bundle(raw, 1));
    auto fixture = observation(4);
    fixture.context.raw_input = ContentReference{raw, {"org.mantis.RawCapture", 3}, raw_descriptor.hash, 0};
    fixture.context.exact_inputs.push_back(*fixture.context.raw_input.get());
    auto fixture_path = tmp.path / "synthetic-fixture.observation";
    write_laser_observation(fixture_path, fixture);
    env("MANTIS_SYNTHETIC_OBSERVATION_FIXTURE", fixture_path.string());
    data::SemanticPublished retained_native;
    std::weak_ptr<const void> producing_library;
    {
        plugins::Loaded scoped(plugin_dir / "libmantis-synthetic-observation.so");
        producing_library = scoped.lifetime();
        retained_native = plugins::process_semantic(scoped, SemanticPacket{store->bundle(raw, 1)});
    }
    // This assertion checks the pin independently of loader-specific NODELETE behavior.
    CHECK(!producing_library.expired());
    CHECK(std::get<LaserObservation>(*retained_native).attributes[0].buffer.map_read());
    const auto native_bytes = encode(std::get<LaserObservation>(*retained_native));
    retained_native.reset(); // release plugin-defined backing deleters before unloading the library
    CHECK(producing_library.expired());
    plugins::Registry registry(host, tmp.path / "native", {});
    registry.discover(plugin_dir, {"org.example.synthetic-observation", "org.example.processor-contract"});
    auto compiled = plan(registry);
    std::string previous;
    Id derived;
    for (unsigned pass = 0; pass < 2; ++pass) {
        device::BundleReplay replay(store, raw, false);
        auto first = replay.next();
        CHECK(first && first->has_value());
        auto captured = replay.next();
        CHECK(captured && captured->has_value());
        auto input = data::publish(SemanticPacket{std::move(**captured)});
        auto result = pipeline::execute(compiled, input);
        CHECK(result);
        CHECK(result->outputs.size() == 1);
        CHECK(result->output == result->outputs.begin()->second);
        const auto &o = std::get<LaserObservation>(*result->output);
        CHECK(o.context.origin == ObservationOrigin::synthetic);
        CHECK(o.context.raw_input.get()->id == raw &&
              *o.context.raw_input.get()->hash.get() == raw_descriptor.hash);
        CHECK(o.context.source.rig_calibration.get()->content.get()->id == Id{"rig-art"});
        CHECK(o.context.source.frame == frame());
        CHECK(o.context.bundle.get()->sequence.value == 1);
        auto summary = pipeline::summarize(o);
        CHECK(summary && summary->known_emitters == 2 && summary->known_lines == 2);
        auto bytes = encode(o);
        if (pass)
            CHECK(bytes == previous);
        else
            previous = bytes;
        CHECK(bytes == native_bytes);
        derived = store->begin_laser_observation();
        store->append_laser_observation(derived, o);
        store->finalize(derived);
        CHECK(encode(store->laser_observation(derived)) == previous);
        CHECK(storage_fixture::encode(store->bundle(raw, 1)) == input_bytes);
    }
    // Isolated synthetic producer produces exactly the same semantic output.
    plugins::Registry isolated(host, tmp.path / "isolated", {});
    isolated.discover(plugin_dir, {});
    auto isolated_plan = plan(isolated);
    auto source = data::publish(SemanticPacket{store->bundle(raw, 1)});
    auto isolated_result = pipeline::execute(isolated_plan, source);
    CHECK(isolated_result);
    CHECK(encode(std::get<LaserObservation>(*isolated_result->output)) == previous);
    // Typed fixture outcomes including no attributes and unresolved attribution remain distinct.
    for (unsigned n : {1, 2, 3, 5}) {
        auto value = observation(n);
        value.context.raw_input = *fixture.context.raw_input.get();
        auto path = tmp.path / ("fixture-" + std::to_string(n));
        write_laser_observation(path, value);
        env("MANTIS_SYNTHETIC_OBSERVATION_FIXTURE", path.string());
        auto result = pipeline::execute(compiled, source);
        CHECK(result);
        const auto &o = std::get<LaserObservation>(*result->output);
        CHECK(o.disposition == value.disposition);
        CHECK(o.sample_count == value.sample_count);
        if (n != 1)
            CHECK(o.attributes.empty());
        if (n == 1) {
            auto s = pipeline::summarize(o);
            CHECK(s && s->unknown_emitters == 2 && s->unknown_lines == 2);
        }
    }
    env("MANTIS_SYNTHETIC_OBSERVATION_FIXTURE", fixture_path.string());
    // Explicit selected camera in a multi-camera bundle is supported; no arbitrary first-image fallback.
    auto multi = bundle(1);
    auto other = multi.evidence.frames[0];
    other.frame.camera = {{"other-camera"}};
    other.frame.stream.id = {{"other-stream"}};
    other.camera_role = "other";
    other.exposure = Unavailable{};
    other.sync = Unavailable{};
    multi.evidence.participants.cameras.push_back(
        {other.frame.camera, other.frame.stream.id, other.camera_role});
    multi.evidence.frames.insert(multi.evidence.frames.begin(), other);
    auto frame_set = *multi.frameset;
    frame_set.frames.insert(frame_set.frames.begin(), frame_set.frames[0]);
    multi.frameset = data::publish(frame_set);
    for (auto &e : multi.evidence.emitters)
        e.exposure_effective.insert(e.exposure_effective.begin(), {other.frame, Unknown{}});
    CHECK(data::validate(multi));
    auto result = pipeline::execute(compiled, data::publish(SemanticPacket{multi}));
    CHECK(result);
    CHECK(std::get<LaserObservation>(*result->output).context.source.frame == frame());
    CHECK(!pipeline::execute(compiled, data::publish(SemanticPacket{bundle(
                                           0)}))); // evidence-only is explicit unsupported, no fake image
    // Missing explicit camera selection and an SDK-contained fixture failure publish no output.
    auto unavailable_source = fixture;
    unavailable_source.context.source.frame.stream.id = {{"missing-source-stream"}};
    unavailable_source.context.source.sync = Unavailable{};
    for (auto &e : unavailable_source.context.emitter_evidence)
        for (auto &effective : e.exposure_effective) {
            effective.frame = unavailable_source.context.source.frame;
            if (effective.state.get()) {
                auto state = *effective.state.get();
                state.frame = effective.frame;
                effective.state = state;
            }
        }
    auto unavailable_path = tmp.path / "missing-source-fixture";
    write_laser_observation(unavailable_path, unavailable_source);
    env("MANTIS_SYNTHETIC_OBSERVATION_FIXTURE", unavailable_path.string());
    CHECK(!pipeline::execute(compiled, source));
    env("MANTIS_SYNTHETIC_OBSERVATION_FIXTURE", (tmp.path / "nonexistent-fixture").string());
    CHECK(!pipeline::execute(compiled, source));
    env("MANTIS_SYNTHETIC_OBSERVATION_FIXTURE", fixture_path.string());
    store->clear_active_calibration({"logical-device"});
    CHECK(storage_fixture::encode(store->bundle(raw, 1)) == input_bytes);
    // Real local lossless stream, finite source/sink queues, deterministic order and closure.
    pipeline::BoundedQueue<data::SemanticPublished> in(2, pipeline::QueuePolicy::lossless);
    pipeline::BoundedQueue<pipeline::SemanticExecutionResult> out(2, pipeline::QueuePolicy::lossless);
    std::stop_source stop;
    auto processing = std::async(
        std::launch::async, [&] { return pipeline::execute_stream(compiled, in, out, stop.get_token()); });
    std::thread producer([&] {
        for (unsigned i = 0; i < 64; ++i) {
            auto b = bundle(1);
            b.key.sequence = {i};
            b.evidence.key.ordinal = {i};
            b.evidence.causal_predecessors.clear();
            CHECK(in.push(data::publish(SemanticPacket{b}), stop.get_token()));
        }
        in.close();
    });
    uint64_t received = 0;
    while (auto v = out.pop()) {
        auto &o = std::get<LaserObservation>(*v->output);
        CHECK(o.key.sequence.value == received++);
        CHECK(o.context.bundle.get()->sequence.value == o.key.sequence.value);
    }
    producer.join();
    CHECK(processing.get());
    CHECK(received == 64);
    CHECK(in.metrics().dropped == 0 && out.metrics().dropped == 0);
    CHECK(in.metrics().high_water <= 2 && out.metrics().high_water <= 2);
    // Cancellation while sink is blocked and with queued publications wakes every participant.
    pipeline::BoundedQueue<data::SemanticPublished> pending_in(2, pipeline::QueuePolicy::lossless);
    pipeline::BoundedQueue<pipeline::SemanticExecutionResult> pending_out(1, pipeline::QueuePolicy::lossless);
    std::stop_source cancellation;
    CHECK(pending_in.push(source));
    CHECK(pending_in.push(source));
    auto pending = std::async(std::launch::async, [&] {
        return pipeline::execute_stream(compiled, pending_in, pending_out, cancellation.get_token());
    });
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (pending_out.metrics().occupancy != 1 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    CHECK(pending_out.metrics().occupancy == 1);
    auto begin = std::chrono::steady_clock::now();
    cancellation.request_stop();
    CHECK(pending.wait_for(1s) == std::future_status::ready);
    auto cancelled = pending.get();
    CHECK(!cancelled && cancelled.error().code == Status::cancelled);
    CHECK(std::chrono::steady_clock::now() - begin < 1s);
    pipeline::BoundedQueue<data::SemanticPublished> no_input(1, pipeline::QueuePolicy::lossless);
    pipeline::BoundedQueue<pipeline::SemanticExecutionResult> no_output(1, pipeline::QueuePolicy::lossless);
    std::stop_source pre_cancel;
    pre_cancel.request_stop();
    CHECK(!pipeline::execute_stream(compiled, no_input, no_output, pre_cancel.get_token()));
    // Failed node closes output and source without a valid-looking publication.
    pipeline::BoundedQueue<data::SemanticPublished> failed_in(1, pipeline::QueuePolicy::lossless);
    pipeline::BoundedQueue<pipeline::SemanticExecutionResult> failed_out(1, pipeline::QueuePolicy::lossless);
    failed_in.push(data::publish(SemanticPacket{bundle(0)}));
    auto failed = pipeline::execute_stream(compiled, failed_in, failed_out, {});
    CHECK(!failed);
    CHECK(!failed_out.pop());
    // Retained mapped columns outlive readers, pipelines, plugin views and Store.
    auto retained = store->laser_observation(derived).attributes[0].buffer;
    store.reset();
    CHECK(retained.map_read());
    CHECK(retained.size() == 16);
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 5);
        std::string operation = argv[1];
        if (operation == "processor")
            processor_tests(argv[2], argv[3], argv[4]);
        else if (operation == "artifact")
            artifact_tests();
        else if (operation == "stream")
            stream_tests(argv[3], argv[4]);
        else
            CHECK(false);
        std::cout << operation << " semantic boundary checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
