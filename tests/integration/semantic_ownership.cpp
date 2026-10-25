#include "../unit/laser_observation_values.hpp"
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <mantis/artifact_store.hpp>
#include <mantis/pipeline_runtime.hpp>
#include <mantis/plugin_runtime.hpp>
#include <thread>
using namespace observation_fixture;
using namespace std::chrono_literals;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__));                 \
    } while (false)
struct Temporary {
    std::filesystem::path path = std::filesystem::temp_directory_path() / Id::random().value;
    Temporary() {
        std::filesystem::create_directories(path);
    }
    ~Temporary() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
template <class F> void rejects(F f, Status status) {
    try {
        f();
    } catch (const Failure &e) {
        CHECK(e.error.code == status);
        return;
    }
    CHECK(false);
}
void env(const char *key, const std::string &value) {
#ifdef _WIN32
    CHECK(_putenv_s(key, value.c_str()) == 0);
#else
    CHECK(setenv(key, value.c_str(), 1) == 0);
#endif
}
void mode(unsigned n) {
    env("MANTIS_PROCESSOR_TEST_MODE", std::to_string(n));
}
// Always release a checkpoint before an async future is destroyed, including on failed assertions.
struct Gate {
    std::promise<void> entered, released;
    std::shared_future<void> release = released.get_future().share();
    bool open{};
    void unblock() {
        if (!open) {
            open = true;
            released.set_value();
        }
    }
    ~Gate() {
        unblock();
    }
    void hold() {
        entered.set_value();
        release.wait();
    }
};
void store_tests(artifact::Store::ObservationWriteStage checkpoint) {
    Temporary tmp;
    auto value = observation(4);
    auto canonical = encode(value);
    Id held;
    Gate gate;
    artifact::Store store(tmp.path / "project", [&](const Id &id, auto stage) {
        if (id == held && stage == checkpoint)
            gate.hold();
    });
    held = store.begin_laser_observation();
    auto other = store.begin_laser_observation();
    auto raw = store.begin_projected_capture(header());
    auto entered = gate.entered.get_future();
    auto writing = std::async(std::launch::async, [&] { store.append_laser_observation(held, value); });
    struct Release {
        Gate &gate;
        ~Release() {
            gate.unblock();
        }
    } release{gate};
    CHECK(entered.wait_for(2s) == std::future_status::ready);
    auto independent = std::async(std::launch::async, [&] {
        CHECK(store.get(held).state == artifact::ArtifactState::open);
        CHECK(store.list().size() == 3);
        store.append_bundle(raw, bundle(0));
        store.append_laser_observation(other, value);
        CHECK(store.finalize(other).chunks == 1);
    });
    auto responsive = independent.wait_for(2s) == std::future_status::ready;
    if (!responsive)
        gate.unblock();
    CHECK(responsive);
    independent.get();
    CHECK(writing.wait_for(0s) == std::future_status::timeout);
    rejects([&] { store.append_laser_observation(held, value); }, Status::busy);
    rejects([&] { store.append(held, *images()); }, Status::busy);
    rejects([&] { store.finalize(held); }, Status::busy);
    rejects([&] { store.prepare_finalize(held); }, Status::busy);
    rejects([&] { store.abandon(held); }, Status::busy);
    rejects([&] { store.recover(held); }, Status::busy);
    gate.unblock();
    writing.get();
    auto descriptor = store.finalize(held);
    CHECK(descriptor.chunks == 1 && descriptor.bytes == canonical.size());
    CHECK(encode(store.laser_observation(held)) == canonical);
    store.record_run_outcome(raw, {1, outcome(RecordedRunDisposition::completed, AcquisitionReason::none)});
    CHECK(store.finalize(raw).state == artifact::ArtifactState::finalized);
}
void store_recovery_tests() {
    Temporary tmp;
    auto value = observation(4);
    auto canonical = encode(value);
    Id failed;
    {
        artifact::Store store(tmp.path / "failure", [](const Id &, auto stage) {
            if (stage == artifact::Store::ObservationWriteStage::journal_published)
                fail(Status::io, "Injected journal publication failure");
        });
        failed = store.begin_laser_observation();
        rejects([&] { store.append_laser_observation(failed, value); }, Status::io);
        CHECK(store.get(failed).state == artifact::ArtifactState::recoverable);
    }
    {
        artifact::Store store(tmp.path / "failure");
        CHECK(store.get(failed).chunks == 1);
        auto recovered = store.recover(failed);
        CHECK(recovered.chunks == 1 && recovered.bytes == canonical.size());
        CHECK(encode(store.laser_observation(failed)) == canonical);
        auto ordinary = store.begin_laser_observation();
        store.append_laser_observation(ordinary, value);
        CHECK(store.finalize(ordinary).hash == recovered.hash);
        std::vector<std::future<void>> writers;
        for (unsigned n = 0; n < 8; ++n) {
            auto id = store.begin_laser_observation();
            writers.push_back(std::async(std::launch::async, [&, id] {
                store.append_laser_observation(id, value);
                CHECK(store.finalize(id).hash == recovered.hash);
                CHECK(encode(store.laser_observation(id)) == canonical);
            }));
        }
        for (auto &writer : writers)
            writer.get();
    }
    // In-flight append retains project lock/database after the Store wrapper is destroyed.
    Gate gate;
    auto store = std::make_unique<artifact::Store>(tmp.path / "teardown", [&](const Id &, auto stage) {
        if (stage == artifact::Store::ObservationWriteStage::before_encode)
            gate.hold();
    });
    auto id = store->begin_laser_observation();
    auto entered = gate.entered.get_future();
    auto writing = std::async(std::launch::async,
                              [ptr = store.get(), id, &value] { ptr->append_laser_observation(id, value); });
    struct Release {
        Gate &gate;
        ~Release() {
            gate.unblock();
        }
    } release{gate};
    CHECK(entered.wait_for(2s) == std::future_status::ready);
    store.reset();
    gate.unblock();
    writing.get();
    artifact::Store reopened(tmp.path / "teardown");
    CHECK(reopened.recover(id).chunks == 1);
    CHECK(encode(reopened.laser_observation(id)) == canonical);
}
constexpr const char *contract = "org.example.processor-contract";
auto status(plugins::Registry &registry, const std::string &id = contract) {
    auto statuses = registry.statuses();
    auto it =
        std::find_if(statuses.begin(), statuses.end(), [&](const auto &s) { return s.manifest.id == id; });
    CHECK(it != statuses.end());
    return *it;
}
void started(const std::filesystem::path &path) {
    auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!std::filesystem::exists(path) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    CHECK(std::filesystem::exists(path));
}
void lifetime_tests(const std::filesystem::path &plugins_dir, const std::filesystem::path &host) {
    Temporary tmp;
    auto input = data::publish(SemanticPacket{images()->frames[0]});
    std::array<data::SemanticPublished, 1> inputs{input};
    for (bool native : {false, true}) {
        for (unsigned n = 0; n < 4; ++n) {
            mode(0);
            auto registry =
                std::make_unique<plugins::Registry>(host, tmp.path / Id::random().value, LogSink{});
            registry->discover(plugins_dir,
                               native ? std::vector<std::string>{contract} : std::vector<std::string>{});
            auto node = registry->semantic_node(contract);
            auto instance = node.factory();
            CHECK(instance->process(inputs, {}));
            registry.reset();
            rejects([&] { (void)node.factory(); }, Status::cancelled);
            auto failed = instance->process(inputs, {});
            CHECK(!failed && failed.error().code == Status::cancelled);
        }
    }
    mode(0);
    data::SemanticPublished retained;
    {
        plugins::Registry registry(host, tmp.path / "buffers", {});
        registry.discover(plugins_dir, {contract});
        auto instance = registry.semantic_node(contract).factory();
        mode(26);
        auto result = instance->process(inputs, {});
        CHECK(result);
        retained = *result;
    }
    CHECK((*std::get<Published>(*retained)->attributes[0].buffer.map_read())[0] == std::byte{47});
    retained.reset();
    // Teardown kills and reaps an isolated callback; caller cancellation stays independent.
    for (bool cancel : {false, true}) {
        mode(0);
        auto scratch = tmp.path / Id::random().value;
        auto registry = std::make_unique<plugins::Registry>(host, scratch, LogSink{});
        registry->discover(plugins_dir, {});
        auto instance = registry->semantic_node(contract, 6000).factory();
        auto checkpoint = tmp.path / Id::random().value;
        env("MANTIS_PROCESSOR_STARTED_FILE", checkpoint.string());
        mode(16);
        CancellationToken token;
        auto pending = std::async(std::launch::async, [&] { return instance->process(inputs, token); });
        started(checkpoint);
        if (cancel)
            token.cancel();
        registry.reset();
        CHECK(pending.wait_for(2s) == std::future_status::ready);
        auto result = pending.get();
        CHECK(!result && result.error().code == Status::cancelled);
        CHECK(token.cancelled() == cancel);
        CHECK(std::filesystem::is_empty(scratch));
    }
    // A trusted bounded call may finish after teardown, but its execution context/library stay alive.
    mode(0);
    auto native = std::make_unique<plugins::Registry>(host, tmp.path / "pending-native", LogSink{});
    native->discover(plugins_dir, {contract});
    auto native_instance = native->semantic_node(contract, 6000).factory();
    auto native_checkpoint = tmp.path / "native.started";
    auto native_release = tmp.path / "native.release";
    env("MANTIS_PROCESSOR_RELEASE_FILE", native_release.string());
    env("MANTIS_PROCESSOR_STARTED_FILE", native_checkpoint.string());
    mode(37);
    auto pending_native =
        std::async(std::launch::async, [&] { return native_instance->process(inputs, {}); });
    started(native_checkpoint);
    native.reset();
    {
        std::ofstream out(native_release);
        out << '1';
        CHECK(out.good());
    }
    CHECK(pending_native.wait_for(2s) == std::future_status::ready);
    auto late = pending_native.get();
    CHECK(!late && late.error().code == Status::cancelled);
    env("MANTIS_PROCESSOR_STARTED_FILE", "");
    env("MANTIS_PROCESSOR_RELEASE_FILE", "");
    mode(0);
}
void isolation_tests(const std::filesystem::path &plugins_dir, const std::filesystem::path &host) {
    Temporary tmp;
    mode(0);
    plugins::Registry registry(host, tmp.path / "scratch", {});
    registry.discover(plugins_dir, {});
    auto node = registry.semantic_node(contract, 100);
    auto instance = node.factory();
    auto input = data::publish(SemanticPacket{images()->frames[0]});
    std::array<data::SemanticPublished, 1> inputs{input};
    CHECK(instance->process(inputs, {}));
    auto wrong = data::publish(SemanticPacket{observation()});
    std::array<data::SemanticPublished, 1> wrong_inputs{wrong};
    CHECK(!instance->process(wrong_inputs, {}));
    CHECK(status(registry).state == "registered");
    auto malformed = *images()->frames[0];
    malformed.attributes[0].descriptor.shape = {999999};
    std::array<data::SemanticPublished, 1> malformed_inputs{
        std::make_shared<const SemanticPacket>(Published{std::make_shared<const Packet>(malformed)})};
    CHECK(!instance->process(malformed_inputs, {}));
    CHECK(status(registry).state == "registered");
    for (auto n : {15u, 10u, 13u, 16u}) {
        registry.set_enabled(contract, true);
        mode(n);
        auto failed = instance->process(inputs, {});
        CHECK(!failed);
        auto s = status(registry);
        CHECK(s.state == "failed" && !s.diagnostic.empty());
        if (n == 15)
            CHECK(s.diagnostic.find("host exited") != std::string::npos);
        if (n == 10)
            CHECK(s.diagnostic.find("one emit") != std::string::npos);
        if (n == 16)
            CHECK(s.diagnostic.find("timed out") != std::string::npos);
        mode(0);
        CHECK(!instance->process(inputs, {}));
        rejects([&] { (void)registry.semantic_node(contract); }, Status::incompatible);
        CHECK(status(registry).diagnostic == s.diagnostic);
        CHECK(status(registry, "org.example.synthetic-observation").state == "registered");
    }
    // Another plugin remains usable while contract plugin is quarantined.
    auto fixture = tmp.path / "fixture";
    data::write_laser_observation(fixture, observation(4));
    env("MANTIS_SYNTHETIC_OBSERVATION_FIXTURE", fixture.string());
    std::array<data::SemanticPublished, 1> bundles{data::publish(SemanticPacket{bundle(1)})};
    CHECK(registry.semantic_node("org.example.synthetic-observation").factory()->process(bundles, {}));
    registry.set_enabled(contract, true);
    mode(0);
    CHECK(instance->process(inputs, {}));
    auto checkpoint = tmp.path / "cancel.started";
    env("MANTIS_PROCESSOR_STARTED_FILE", checkpoint.string());
    mode(16);
    CancellationToken token;
    auto pending = std::async(std::launch::async, [&] { return instance->process(inputs, token); });
    started(checkpoint);
    token.cancel();
    CHECK(pending.wait_for(2s) == std::future_status::ready);
    auto cancelled = pending.get();
    CHECK(!cancelled && cancelled.error().code == Status::cancelled);
    CHECK(status(registry).state == "registered");
    env("MANTIS_PROCESSOR_STARTED_FILE", "");
    mode(0);
    CHECK(instance->process(inputs, {}));
}
using Callback = std::function<Result<data::SemanticPublished>(std::span<const data::SemanticPublished>,
                                                               const CancellationToken &)>;
class FunctionNode final : public pipeline::SemanticNodeInstance {
    Callback callback;

  public:
    explicit FunctionNode(Callback f) : callback(std::move(f)) {}
    Result<data::SemanticPublished> process(std::span<const data::SemanticPublished> in,
                                            const CancellationToken &token) override {
        return callback(in, token);
    }
};
pipeline::SemanticNode node(std::string id, Callback callback = {}) {
    pipeline::NodeDescriptor d;
    d.id = std::move(id);
    if (callback)
        d.inputs = {{"in", schema::laser_observation}};
    d.outputs = {{"out", schema::laser_observation}};
    return {d, callback ? std::function<std::unique_ptr<pipeline::SemanticNodeInstance>()>{[callback] {
                return std::make_unique<FunctionNode>(callback);
            }}
                        : nullptr};
}
constexpr size_t bulk_bytes = 128 * 1024;
data::SemanticPublished tracked(std::weak_ptr<const void> &weak, const void **identity = nullptr) {
    auto owner = std::make_shared<std::vector<std::byte>>(bulk_bytes);
    weak = owner;
    auto storage = std::make_shared<memory::Storage>();
    storage->owner = owner;
    storage->host = owner->data();
    storage->size = owner->size();
    storage->alignment = 1;
    memory::BufferView buffer{storage, 0, owner->size()};
    if (identity)
        *identity = buffer.identity();
    auto o = observation();
    o.attributes.push_back(
        {{"org.example.retained", schema::ScalarType::u8, {2}, {bulk_bytes / 2}, ""}, buffer});
    CHECK(data::validate(o));
    return data::publish(SemanticPacket{std::move(o)});
}
pipeline::SemanticExecutionPlan compiled(const pipeline::SemanticGraph &graph) {
    auto p = pipeline::compile(graph);
    CHECK(p);
    return *p;
}
void pipeline_tests() {
    // Long chain: each independent bulk allocation expires immediately after its final consumer.
    std::vector<std::weak_ptr<const void>> owners(25);
    pipeline::SemanticGraph graph;
    graph.retained_payload_limit_bytes = bulk_bytes * 3;
    graph.nodes.push_back(node("source"));
    for (size_t n = 1; n < owners.size(); ++n) {
        graph.nodes.push_back(node("node" + std::to_string(n), [&, n](auto in, const auto &) {
            CHECK(in.size() == 1 && !owners[n - 1].expired());
            if (n > 1)
                CHECK(owners[n - 2].expired());
            return tracked(owners[n]);
        }));
        graph.connections.push_back({n - 1, 0, n, 0, 1, pipeline::QueuePolicy::lossless});
    }
    auto plan = compiled(graph);
    for (unsigned pass = 0; pass < 8; ++pass) {
        auto result = pipeline::execute(plan, tracked(owners[0]));
        CHECK(result);
        CHECK(result->outputs.size() == 1);
        CHECK(result->retained_payload_high_water < bulk_bytes * 3);
        for (size_t i = 0; i + 1 < owners.size(); ++i)
            CHECK(owners[i].expired());
        CHECK(!owners.back().expired());
        result = std::unexpected(Error{Status::cancelled, "release", "test"});
        CHECK(owners.back().expired());
    }
    std::array<std::weak_ptr<const void>, 4> branch;
    std::array<const void *, 4> identities{};
    pipeline::SemanticGraph fan;
    fan.retained_payload_limit_bytes = bulk_bytes * 4;
    fan.nodes = {
        node("source"), node("shared", [&](auto, const auto &) { return tracked(branch[1]); }),
        node("left",
             [&](auto in, const auto &) {
                 CHECK(!branch[1].expired());
                 identities[1] = std::get<LaserObservation>(*in[0]).attributes.back().buffer.identity();
                 return tracked(branch[2], &identities[2]);
             }),
        node("right", [&](auto in, const auto &) {
            CHECK(!branch[1].expired() && !branch[2].expired());
            CHECK(std::get<LaserObservation>(*in[0]).attributes.back().buffer.identity() == identities[1]);
            return tracked(branch[3], &identities[3]);
        })};
    fan.connections = {{0, 0, 1, 0, 1, pipeline::QueuePolicy::lossless},
                       {1, 0, 2, 0, 1, pipeline::QueuePolicy::lossless},
                       {1, 0, 3, 0, 1, pipeline::QueuePolicy::lossless}};
    {
        auto result = pipeline::execute(compiled(fan), tracked(branch[0]));
        CHECK(result);
        CHECK(result->outputs.size() == 2 && branch[1].expired());
        for (auto pair : {std::pair{"left", 2u}, std::pair{"right", 3u}}) {
            auto &o = std::get<LaserObservation>(*result->outputs.at(pair.first));
            CHECK(o.attributes.back().buffer.identity() == identities[pair.second]);
            CHECK(o.attributes.back().buffer.map_read());
        }
    }
    CHECK(branch[2].expired() && branch[3].expired());
    auto small = graph;
    small.retained_payload_limit_bytes = bulk_bytes + bulk_bytes / 2;
    auto exhausted = pipeline::execute(compiled(small), tracked(owners[0]));
    CHECK(!exhausted && exhausted.error().code == Status::busy);
    CHECK(owners[0].expired() && owners[1].expired());
    auto invalid = graph;
    invalid.retained_payload_limit_bytes = 0;
    CHECK(!pipeline::compile(invalid));
    invalid.retained_payload_limit_bytes = 2ull * 1024 * 1024 * 1024 + 1;
    CHECK(!pipeline::compile(invalid));
    // Charge a full backing allocation even when an attribute uses a tiny slice.
    auto tiny = observation();
    auto backing = tracked(owners[0]);
    auto buffer = std::get<LaserObservation>(*backing).attributes.back().buffer.slice(0, 2);
    tiny.attributes.push_back({{"org.example.slice", schema::ScalarType::u8, {2}, {1}, ""}, buffer});
    CHECK(buffer.backing_size() == bulk_bytes);
    auto limited = graph;
    limited.retained_payload_limit_bytes = bulk_bytes - 1;
    CHECK(!pipeline::execute(compiled(limited), data::publish(SemanticPacket{tiny})));
    // Overflow in backing accounting is rejected without reading or allocating fictitious extents.
    auto overflow = observation();
    for (unsigned i = 0; i < 2; ++i) {
        auto storage = std::make_shared<memory::Storage>();
        auto bytes = std::make_shared<std::array<std::byte, 2>>();
        storage->owner = bytes;
        storage->host = bytes->data();
        storage->size = 2;
        storage->retained_extent = std::numeric_limits<size_t>::max();
        overflow.attributes.push_back(
            {{"org.example.overflow" + std::to_string(i), schema::ScalarType::u8, {2}, {1}, ""},
             {storage, 0, 2}});
    }
    auto excessive = pipeline::execute(plan, data::publish(SemanticPacket{overflow}));
    CHECK(!excessive && excessive.error().code == Status::invalid_argument);
    CHECK(excessive.error().message.find("overflow") != std::string::npos);
    // Cancellation after a node returns releases the candidate and consumed intermediates.
    pipeline::SemanticGraph cancelled;
    cancelled.nodes = {node("source"), node("cancel", [&](auto, const CancellationToken &token) {
                           token.cancel();
                           return tracked(owners[1]);
                       })};
    cancelled.connections = {{0, 0, 1, 0, 1, pipeline::QueuePolicy::lossless}};
    auto r = pipeline::execute(compiled(cancelled), tracked(owners[0]));
    CHECK(!r && r.error().code == Status::cancelled);
    CHECK(owners[0].expired() && owners[1].expired());
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 4);
        std::string operation = argv[1];
        if (operation == "store") {
            store_tests(artifact::Store::ObservationWriteStage::before_encode);
            store_tests(artifact::Store::ObservationWriteStage::journal_published);
            store_recovery_tests();
        } else if (operation == "lifetime")
            lifetime_tests(argv[2], argv[3]);
        else if (operation == "isolation")
            isolation_tests(argv[2], argv[3]);
        else if (operation == "pipeline")
            pipeline_tests();
        else
            CHECK(false);
        std::cout << operation << " ownership checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
