#include "domain_fixture.hpp"
#include "fixture.h"
#include <future>
#include <iostream>
#include <mantis/capture_calibration.hpp>
#include <mantis/plugin_runtime.hpp>
#include <mantis/semantic_views.hpp>
#include <thread>
using namespace mantis;
using namespace std::chrono_literals;
namespace {
unsigned checks{};
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                   \
    } while (false)
template <class F> void incompatible(F fn, Status status = Status::incompatible) {
    bool rejected = false;
    try {
        fn();
    } catch (const Failure &e) {
        CHECK(e.error.code == status);
        rejected = true;
    }
    CHECK(rejected);
}
template <class T> T get(Result<T> r) {
    if (!r)
        throw Failure(r.error());
    return std::move(*r);
}
void get(Result<void> r) {
    if (!r)
        throw Failure(r.error());
}
const data::RunId run{{"contract-run"}};
const data::GenerationId generation{{"contract-generation"}};
const TestProjectedControl *control(const plugins::Loaded &p) {
    auto *v = static_cast<const TestProjectedControl *>(p.api()->query_interface(TEST_PROJECTED_CONTROL));
    CHECK(v);
    return v;
}
data::AcquisitionProgram program() {
    auto p = domain_fixture::off_program();
    p.identity.id = {{"program-alpha"}};
    p.participants.cameras = {{{{"camera-alpha"}}, {{"image-stream"}}, "imaging"}};
    p.participants.emitters = {{{"emitter-alpha"}}};
    p.participants.controllers = {{{"controller-alpha"}}};
    p.steps[0].emitters = {{{{"emitter-alpha"}}, data::EmitterState::off}};
    CHECK(data::validate(p));
    return p;
}
std::unique_ptr<device::ProjectedExecutor> opened(const std::shared_ptr<plugins::Loaded> &loaded) {
    return plugins::open_projected_light(loaded, {"parent-alpha"}, 100);
}
void start(device::ProjectedExecutor &v) {
    auto p = program();
    CHECK(get(v.validate(p, 100)).accepted);
    CHECK(get(v.prepare(p, 100)).accepted);
    get(v.start(run, generation, 100));
}
void views() {
    namespace d = domain_fixture;
    auto p = d::off_program();
    p.participants.cameras = {{{d::camera}, {{"image-stream"}}, "left"}};
    p.participants.controllers = {d::controller};
    p.identity.hash = Hash{"sha256", "ab12"};
    p.identity.content =
        data::ContentReference{p.identity.id.id, schema::acquisition_program, Hash{"sha256", "ab12"}, 17};
    p.steps[0].capture.mode = data::CaptureMode::hardware_trigger;
    p.steps[0].capture.cameras = {d::camera};
    p.steps[0].capture.trigger = data::TriggerIntent{d::controller, {{"request"}}, {d::camera}};
    p.steps[0].settle = 3ns;
    p.steps[0].evidence_requirement = data::EvidenceRequirement::controller_acknowledged;
    plugins::semantic::ProgramView view(p);
    auto round = plugins::semantic::program(view.get());
    CHECK(round.identity.hash.get() && *round.identity.hash.get() == *p.identity.hash.get());
    CHECK(round.identity.content.get()->revision == 17 &&
          round.identity.content.get()->type == schema::acquisition_program);
    CHECK(round.steps[0].capture.trigger->request == p.steps[0].capture.trigger->request);
    CHECK(round.steps[0].capture.trigger->endpoints == p.steps[0].capture.trigger->endpoints);
    CHECK(round.steps[0].emitters[0].state == p.steps[0].emitters[0].state && round.steps[0].settle == 3ns);
    CHECK(round.bounds.max_duration == p.bounds.max_duration &&
          round.bounds.max_on_duration == p.bounds.max_on_duration);
    CHECK(round.identity.content.get()->hash.presence() == data::Presence::established);
    auto bad = *view.get();
    incompatible([&] { plugins::semantic::program(nullptr); });
    bad.struct_size = 0;
    incompatible([&] { plugins::semantic::program(&bad); });
    bad = *view.get();
    bad.abi_version = 2;
    incompatible([&] { plugins::semantic::program(&bad); });
    bad = *view.get();
    bad.steps_count = 257;
    incompatible([&] { plugins::semantic::program(&bad); });
    bad = *view.get();
    bad.terminal_policy = 99;
    incompatible([&] { plugins::semantic::program(&bad); });
    bad = *view.get();
    bad.identity.hash.presence = 99;
    incompatible([&] { plugins::semantic::program(&bad); });
    bad = *view.get();
    bad.identity.hash.presence = MANTIS_PRESENCE_UNAVAILABLE;
    incompatible([&] { plugins::semantic::program(&bad); });
    bad = *view.get();
    auto missing_step = bad.steps[0];
    bad.steps = &missing_step;
    missing_step.capture.mode.presence = MANTIS_PRESENCE_UNKNOWN;
    missing_step.capture.mode.value = nullptr;
    incompatible([&] { plugins::semantic::program(&bad); });
    missing_step = view.get()->steps[0];
    missing_step.evidence_requirement.value = nullptr;
    incompatible([&] { plugins::semantic::program(&bad); });
    bad = *view.get();
    bad.repetitions = 0;
    incompatible([&] { plugins::semantic::program(&bad); });
    auto o = d::observation();
    o.context.source.camera_calibration = data::ExactCalibrationReference{
        {{"logical-camera"}, 1, 7},
        data::ContentReference{
            {"immutable-camera"}, {"org.mantis.CameraCalibration", 1}, Hash{"sha256", "abcd"}, 7}};
    o.context.preprocessing = data::PreprocessingTransform{};
    o.context.correlation = data::ProgramCorrelation{d::program_ref(), {d::run, 0, 0}};
    o.context.emitter_patterns = {{d::emitter_a, {{"pattern"}}, 1}};
    o.context.producer_completed = d::host_ts(234);
    plugins::semantic::PacketView laser(o, plugins::host_api());
    auto copied =
        std::get<data::LaserObservation>(plugins::semantic::packet(laser.get(), plugins::host_api()));
    CHECK(copied.key.run_id.presence() == data::Presence::established && *copied.key.run_id.get() == d::run);
    CHECK(copied.context.source.source_timestamp.get()->nanoseconds == 0);
    CHECK(copied.context.source.exposure.get()->integration_duration.get()->count() == 10);
    CHECK(copied.context.source.exposure.get()->requested_duration.presence() == data::Presence::unknown);
    CHECK(copied.context.source.camera_calibration.get()->calibration.revision == 7);
    CHECK(copied.context.source.camera_calibration.get()->content.get()->hash.get()->hex == "abcd");
    CHECK(copied.context.preprocessing.get()->original_from_processed ==
          o.context.preprocessing.get()->original_from_processed);
    CHECK(copied.context.producer_completed.get()->time.nanoseconds == 234);
    CHECK(copied.line_dictionary == o.line_dictionary && copied.emitter_dictionary == o.emitter_dictionary);
    CHECK(copied.attributes.size() == o.attributes.size());
    for (size_t i = 0; i < o.attributes.size(); ++i) {
        auto a = get(copied.attributes[i].buffer.map_read()), b = get(o.attributes[i].buffer.map_read());
        CHECK(a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin()));
        CHECK(copied.attributes[i].descriptor.shape == o.attributes[i].descriptor.shape);
    }
    auto t = d::observed();
    plugins::semantic::PacketView trigger(t, plugins::host_api());
    auto ct = std::get<data::TriggerEvent>(plugins::semantic::packet(trigger.get(), plugins::host_api()));
    CHECK(ct.native_trigger.get()->value == 0 && ct.native_trigger.get()->generation == d::generation);
    CHECK(ct.actual_endpoints.get() && *ct.actual_endpoints.get() == *t.actual_endpoints.get());
    CHECK(ct.exposure_association.get()->frame == t.exposure_association.get()->frame);
    auto packet = *laser.get();
    packet.kind = MANTIS_SEMANTIC_TRIGGER;
    incompatible([&] { plugins::semantic::packet(&packet, plugins::host_api()); });
    packet = *laser.get();
    packet.trigger = trigger.get()->trigger;
    incompatible([&] { plugins::semantic::packet(&packet, plugins::host_api()); });
}
void descriptors(const std::shared_ptr<plugins::Loaded> &loaded) {
    auto *c = control(*loaded);
    c->fault(TEST_NORMAL);
    const auto graphs = plugins::discover_projected_light(*loaded, 100);
    CHECK(graphs.size() == 1);
    CHECK(graphs[0].components.size() == 4 && graphs[0].image_participants().size() == 1);
    CHECK(graphs[0].image_participants()[0].id.value == "camera-alpha");
    CHECK(graphs[0].limits.watchdog.presence() == data::Presence::unavailable &&
          graphs[0].limits.interlock.presence() == data::Presence::unknown);
    // The same generic image selection protects existing calibration discovery.
    auto parent = graphs[0].components[0].descriptor;
    std::vector<device::Descriptor> components;
    for (const auto &component : graphs[0].components) {
        auto descriptor = component.descriptor;
        if (descriptor.id.value == "camera-alpha")
            descriptor.metadata = {
                {"role", "imaging"}, {"identity", "camera-alpha"}, {"width", "2"}, {"height", "2"}};
        components.push_back(std::move(descriptor));
    }
    auto cameras = get(services::discovered_activation_components(parent, components));
    CHECK(cameras.size() == 1 && cameras[0].role == "imaging");
    for (uint32_t f = TEST_GRAPH_NULL; f <= TEST_GRAPH_PRESENCE; ++f) {
        c->fault(f);
        incompatible([&] { plugins::discover_projected_light(*loaded, 100); });
    }
    c->fault(TEST_NORMAL);
}
void lifecycle(const std::shared_ptr<plugins::Loaded> &loaded) {
    auto *c = control(*loaded);
    c->fault(TEST_NORMAL);
    auto executor = opened(loaded);
    CHECK(c->live_instances() == 1);
    incompatible([&] { (void)opened(loaded); }, Status::busy);
    const auto initialized = c->initializations(), shutdown = c->shutdowns();
    auto second_loaded = std::make_shared<plugins::Loaded>(loaded->path());
    CHECK(c->initializations() == initialized);
    incompatible([&] { (void)opened(second_loaded); }, Status::busy);
    second_loaded.reset();
    CHECK(c->shutdowns() == shutdown && c->live_instances() == 1);
    CHECK(get(executor->status(100)).state == device::ProjectedRunState::open);
    auto bad = program();
    bad.repetitions = 0;
    auto validation = executor->prepare(bad, 100);
    CHECK(!validation && validation.error().code == Status::invalid_argument);
    c->fault(TEST_REJECT_PROGRAM);
    CHECK(!get(executor->prepare(program(), 100)).accepted);
    c->fault(TEST_NORMAL);
    start(*executor);
    auto status = get(executor->status(100));
    CHECK(status.state == device::ProjectedRunState::started && *status.run.get() == run &&
          *status.generation.get() == generation);
    auto first = get(executor->next(100));
    CHECK(first && !first->frameset && first->triggers.empty());
    CHECK(first->evidence.emitters[0].commanded.presence() == data::Presence::unknown &&
          first->evidence.emitters[0].observed.presence() == data::Presence::unavailable);
    auto second = get(executor->next(100));
    CHECK(second && second->frameset && second->frameset->type == schema::frameset &&
          second->frameset->frames.size() == 1);
    CHECK(second->frameset->header.sequence.value == 9 &&
          second->frameset->frames[0]->header.sequence.value == 7);
    CHECK(second->evidence.emitters[0].exposure_effective[0].state.presence() == data::Presence::unknown);
    auto third = get(executor->next(100));
    CHECK(third && third->triggers.size() == 1 &&
          third->triggers[0].kind == data::TriggerEvent::Kind::requested);
    CHECK(!get(executor->next(0)));
    // Processor v2 uses full semantic views, without v1/header substitution.
    auto *processor = mantis::sdk::processor_v2(loaded->api());
    CHECK(processor && processor->abi_version == 1);
    MantisNodeDescriptorV1 description{};
    description.struct_size = sizeof(description);
    description.abi_version = 1;
    CHECK(!processor->describe(100, &description) &&
          std::string(description.input_type) == MANTIS_ACQUISITION_BUNDLE);
    plugins::semantic::PacketView input(*second, plugins::host_api());
    std::optional<data::AcquisitionBundle> output;
    auto emit = [](void *ctx, const MantisSemanticPacketV1 *p) noexcept {
        return sdk::boundary([&] {
            *static_cast<std::optional<data::AcquisitionBundle> *>(ctx) =
                std::get<data::AcquisitionBundle>(plugins::semantic::packet(p, plugins::host_api()));
        });
    };
    CHECK(!processor->process(plugins::host_api(), input.get(), 100, emit, &output));
    CHECK(output && output->key == second->key && output->frameset->header.sequence.value == 9);
    CHECK(get(executor->diagnostics(100)).find("fixture") != std::string::npos);
    auto abort = get(executor->abort(data::AcquisitionReason::user_cancel, 100));
    CHECK(*abort.inhibited.get() && *abort.stale_work_fenced.get() && *abort.off_requested.get());
    CHECK(abort.emitters[0].commanded.get()->state == data::EmitterState::off &&
          abort.emitters[0].observed.presence() == data::Presence::unavailable);
    c->fault(TEST_DESTROY_REFUSE);
    auto refused = executor->close(10);
    CHECK(!refused && refused.error().code == Status::busy && c->live_instances() == 1);
    c->fault(TEST_NORMAL);
    get(executor->stop(100));
    get(executor->close(100));
    CHECK(c->live_instances() == 0);
    auto bytes = get(second->frameset->frames[0]->attributes[0].buffer.map_read());
    CHECK(bytes.size() == 4 && bytes[0] == std::byte{42});
    auto replacement = opened(loaded);
    get(replacement->close(100));
    sdk::ProjectedLight client(loaded->query<MantisProjectedLightV1>(MANTIS_PROJECTED_LIGHT_V1),
                               plugins::host_api(), "parent-alpha", 100, loaded);
    auto program_value = program();
    plugins::semantic::ProgramView program_view(program_value);
    auto bad_view = *program_view.get();
    bad_view.struct_size = 0;
    uint32_t emitted = 0;
    auto validated = [](void *context, const MantisProgramValidationV1 *) {
        ++*static_cast<uint32_t *>(context);
        return 0;
    };
    CHECK(client.validate(&bad_view, 100, validated, &emitted) == MANTIS_PL_INCOMPATIBLE && emitted == 0);
    CHECK(client.prepare(nullptr, 100, validated, &emitted) == MANTIS_PL_INCOMPATIBLE && emitted == 0);
    CHECK(!client.close(100));
}
void malformed(const std::shared_ptr<plugins::Loaded> &loaded) {
    auto *c = control(*loaded);
    for (auto f :
         {TEST_ZERO_EMIT, TEST_DOUBLE_EMIT, TEST_WRONG_MEMBER, TEST_TOO_MANY_MEMBERS, TEST_BAD_FRAMESET,
          TEST_BAD_EVIDENCE, TEST_RUN_MISMATCH, TEST_EMIT_AFTER_FAILURE, TEST_BUNDLE_NULL, TEST_BUNDLE_SIZE,
          TEST_BUNDLE_VERSION, TEST_BUNDLE_PRESENCE, TEST_ACTIVE_RUN, TEST_PROGRAM_MISMATCH}) {
        c->fault(TEST_NORMAL);
        auto executor = opened(loaded);
        start(*executor);
        c->fault(static_cast<uint32_t>(f));
        auto next = executor->next(100);
        CHECK(!next && next.error().code == Status::incompatible);
        c->fault(TEST_NORMAL);
        get(executor->close(100));
    }
    auto executor = opened(loaded);
    start(*executor);
    c->fault(TEST_NOT_READY);
    CHECK(!get(executor->next(0)));
    c->fault(TEST_FAILURE);
    auto failed = executor->next(100);
    CHECK(!failed && failed.error().code == Status::plugin_failed);
    c->fault(TEST_BAD_STATUS);
    auto status = executor->status(100);
    CHECK(!status && status.error().code == Status::incompatible);
    c->fault(TEST_BAD_ABORT);
    auto abort = executor->abort(data::AcquisitionReason::user_cancel, 100);
    CHECK(!abort && abort.error().code == Status::incompatible);
    c->fault(TEST_NORMAL);
    get(executor->close(100));
    CHECK(sdk::boundary([] { throw std::runtime_error("contained"); }) != 0);
    auto callback = [](const MantisSemanticPacketV1 &) { throw std::runtime_error("callback failure"); };
    MantisSemanticPacketV1 packet{};
    packet.struct_size = sizeof(packet);
    packet.abi_version = 1;
    CHECK((sdk::borrowed_callback<MantisSemanticPacketV1, decltype(callback)>(&callback, &packet)) != 0);
}
void concurrency(const std::shared_ptr<plugins::Loaded> &loaded) {
    auto *c = control(*loaded);
    c->fault(TEST_NORMAL);
    auto executor = opened(loaded);
    start(*executor);
    c->fault(TEST_PENDING);
    auto future = std::async(std::launch::async, [&] { return executor->next(1000); });
    auto until = std::chrono::steady_clock::now() + 1s;
    while (!c->pending() && std::chrono::steady_clock::now() < until)
        std::this_thread::yield();
    CHECK(c->pending() == 1);
    const auto shutdown = c->shutdowns();
    {
        plugins::Loaded alias(loaded->path());
        CHECK(c->initializations() == 1);
    }
    CHECK(c->shutdowns() == shutdown); // pending next still owns the shared root
    const auto destroyed = c->destroyed();
    auto closed = executor->close(5);
    CHECK(!closed && closed.error().code == Status::busy && c->destroyed() == destroyed);
    auto concurrent = executor->next(0);
    CHECK(!concurrent && concurrent.error().code == Status::busy);
    auto begin = std::chrono::steady_clock::now();
    auto abort = get(executor->abort(data::AcquisitionReason::user_cancel, 100));
    CHECK(std::chrono::steady_clock::now() - begin < 250ms && *abort.inhibited.get());
    CHECK(future.wait_for(250ms) == std::future_status::ready && !get(future.get()));
    c->fault(TEST_NORMAL);
    get(executor->close(100));
    CHECK(c->destroyed() == destroyed + 1);
    // Waiting close fences ordinary calls but must keep the abort side path available.
    executor = opened(loaded);
    start(*executor);
    c->fault(TEST_PENDING);
    auto pending = std::async(std::launch::async, [&] { return executor->next(1000); });
    until = std::chrono::steady_clock::now() + 1s;
    while (!c->pending() && std::chrono::steady_clock::now() < until)
        std::this_thread::yield();
    CHECK(c->pending() == 1);
    auto closing = std::async(std::launch::async, [&] { return executor->close(1000); });
    CHECK(closing.wait_for(10ms) == std::future_status::timeout);
    CHECK(*get(executor->abort(data::AcquisitionReason::user_cancel, 100)).inhibited.get());
    CHECK(!get(pending.get()));
    get(closing.get());
    c->fault(TEST_NORMAL);
    // Timeout/poll are finite even if no abort arrives.
    executor = opened(loaded);
    start(*executor);
    c->fault(TEST_PENDING);
    begin = std::chrono::steady_clock::now();
    CHECK(!get(executor->next(15)));
    CHECK(std::chrono::steady_clock::now() - begin < 250ms);
    auto timeout = executor->next(UINT32_MAX);
    CHECK(!timeout && timeout.error().code == Status::invalid_argument);
    c->fault(TEST_NORMAL);
    get(executor->close(100));
    // Raw table's destroy must refuse an active synchronous callback.
    auto *api = loaded->query<MantisProjectedLightV1>(MANTIS_PROJECTED_LIGHT_V1);
    void *instance{};
    CHECK(!api->open(plugins::host_api(), "parent-alpha", 100, &instance));
    auto p = program();
    plugins::semantic::ProgramView pv(p);
    auto valid = [](void *, const MantisProgramValidationV1 *) { return 0; };
    CHECK(!api->prepare(instance, pv.get(), 100, valid, nullptr));
    CHECK(!api->start(instance, "raw-run", "raw-generation", 100));
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    struct Callback {
        std::promise<void> &entered;
        std::shared_future<void> released;
    } ctx{entered, released};
    auto emit = [](void *ptr, const MantisSemanticPacketV1 *) {
        auto &o = *static_cast<Callback *>(ptr);
        o.entered.set_value();
        o.released.wait();
        return 0;
    };
    auto raw = std::async(std::launch::async, [&] { return api->next(instance, 1000, emit, &ctx); });
    entered.get_future().wait();
    CHECK(api->destroy(instance, 5) == MANTIS_PL_BUSY && c->live_instances() == 1);
    auto aborted = [](void *, const MantisAbortOutcomeV1 *out) {
        return out->inhibited.presence == MANTIS_PRESENCE_ESTABLISHED ? 0 : 1;
    };
    CHECK(!api->abort(instance, MANTIS_ACQUISITION_REASON_USER_CANCEL, 100, aborted, nullptr));
    release.set_value();
    CHECK(!raw.get());
    CHECK(!api->destroy(instance, 100));
}
std::atomic_uint retains{}, releases{};
void buffer_ownership() {
    MantisHostV1 host = *plugins::host_api();
    host.retain = [](MantisBuffer *b) {
        ++retains;
        plugins::host_api()->retain(b);
    };
    host.release = [](MantisBuffer *b) {
        ++releases;
        plugins::host_api()->release(b);
    };
    data::Published retained;
    {
        auto original = domain_fixture::frameset_packet();
        plugins::semantic::PacketView view(original, plugins::host_api());
        retained = std::get<data::Published>(plugins::semantic::packet(view.get(), &host));
        CHECK(retains == 1 && releases == 0);
    }
    auto bytes = get(retained->frames[0]->attributes[0].buffer.map_read());
    CHECK(bytes[0] == std::byte{7});
    retained.reset();
    CHECK(retains == releases);
}
void registry(const char *directory) {
    plugins::Registry registry("", std::filesystem::path(directory) / "scratch", {});
    registry.discover(directory, {"org.example.projected-contract"});
    auto parents = registry.projected_light_parents(100);
    CHECK(parents.size() == 1);
    auto streams = registry.devices();
    CHECK(streams.size() == 1 && streams[0]->descriptor().id.value == "parent-alpha");
    get(streams[0]->start());
    auto packet = get(streams[0]->next());
    CHECK(packet->type == schema::frameset && packet->frames[0]->attributes[0].buffer.size() == 4);
    incompatible(
        [&] { registry.open_projected_light("org.example.projected-contract", {"parent-alpha"}, 100); },
        Status::busy);
    get(streams[0]->stop());
    streams.clear();
    auto executor = registry.open_projected_light("org.example.projected-contract", {"parent-alpha"}, 100);
    CHECK(executor->graph().parent.value == "parent-alpha");
    get(executor->close(100));
}
} // namespace
int main(int argc, char **argv) {
    try {
        CHECK(argc == 3);
        views();
        auto loaded = std::make_shared<plugins::Loaded>(argv[1]);
        CHECK(loaded->api()->abi_version == 1 && !loaded->api()->query_interface("unknown.interface"));
        descriptors(loaded);
        lifecycle(loaded);
        malformed(loaded);
        concurrency(loaded);
        registry(argv[2]);
        // Buffer ownership has its own host retain/release counters.
        // Null query is a caller error and must throw, never dereference.
        bool caught = false;
        try {
            sdk::query_optional<MantisProjectedLightV1>(nullptr, MANTIS_PROJECTED_LIGHT_V1);
        } catch (const std::invalid_argument &) {
            caught = true;
        }
        CHECK(caught);
        buffer_ownership();
        CHECK(control(*loaded)->live_instances() == 0);
        std::cout << checks << " projected-light ABI/adapter checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
