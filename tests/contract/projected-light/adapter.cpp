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
std::atomic_uint checks{};
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
    const auto &image = *graphs[0].components[1].image_source;
    CHECK(image.stream.id.value == "image-stream" && image.physical_identity == "physical-camera-alpha" &&
          image.width == 2 && image.height == 2);
    CHECK(graphs[0].frameset_stream.get()->id.value == "frameset-stream");
    CHECK(graphs[0].limits.watchdog.presence() == data::Presence::unavailable &&
          graphs[0].limits.interlock.presence() == data::Presence::unknown);
    // The same generic image selection protects existing calibration discovery.
    auto parent = graphs[0].components[0].descriptor;
    std::vector<device::Descriptor> components;
    for (const auto &component : graphs[0].components) {
        auto descriptor = component.descriptor;
        if (component.image_source)
            descriptor.metadata = {{"role", component.role},
                                   {"identity", component.image_source->physical_identity},
                                   {"width", std::to_string(component.image_source->width)},
                                   {"height", std::to_string(component.image_source->height)}};
        components.push_back(std::move(descriptor));
    }
    auto cameras = get(services::discovered_activation_components(parent, components));
    CHECK(cameras.size() == 1 && cameras[0].role == "imaging");
    for (auto f : {TEST_GRAPH_NULL, TEST_GRAPH_SIZE, TEST_GRAPH_VERSION, TEST_GRAPH_COUNT,
                   TEST_GRAPH_DUPLICATE, TEST_GRAPH_PARENT, TEST_GRAPH_RELATION, TEST_GRAPH_PRESENCE,
                   TEST_IMAGE_NULL, TEST_IMAGE_STREAM, TEST_IMAGE_IDENTITY, TEST_IMAGE_WIDTH,
                   TEST_IMAGE_HEIGHT, TEST_IMAGE_PREFIX, TEST_FRAMESET_STREAM}) {
        c->fault(f);
        incompatible([&] { plugins::discover_projected_light(*loaded, 100); });
    }
    c->fault(TEST_NORMAL);
}
void successors(const std::shared_ptr<plugins::Loaded> &loaded) {
    auto *c = control(*loaded);
    for (auto f :
         {TEST_SUCCESSOR_DUPLICATE, TEST_SUCCESSOR_BACKWARD, TEST_SUCCESSOR_ORDINAL, TEST_SUCCESSOR_CLOCK,
          TEST_SUCCESSOR_CLOCK_GENERATION, TEST_SUCCESSOR_TIME, TEST_SUCCESSOR_TRIGGER_REUSE,
          TEST_SUCCESSOR_TRIGGER_BACKWARD, TEST_SUCCESSOR_CONTROLLER, TEST_SUCCESSOR_STREAM}) {
        c->fault(TEST_NORMAL);
        c->shape(f == TEST_SUCCESSOR_STREAM ? 1u
                 : (f == TEST_SUCCESSOR_CONTROLLER || f == TEST_SUCCESSOR_TRIGGER_REUSE ||
                    f == TEST_SUCCESSOR_TRIGGER_BACKWARD)
                     ? 2u
                     : 0u);
        auto executor = opened(loaded);
        start(*executor);
        c->publication(1);
        const auto first = *get(executor->next(100));
        c->publication(2);
        c->fault(f);
        auto rejected = executor->next(100);
        CHECK(!rejected && rejected.error().code == Status::incompatible);
        // Rejection must not advance the previous accepted publication.
        c->fault(TEST_NORMAL);
        c->publication(2);
        auto second = get(executor->next(100));
        CHECK(second && data::validate_successor(first, *second));
        c->publication(2);
        auto duplicate = executor->next(100);
        CHECK(!duplicate && duplicate.error().code == Status::incompatible);
        c->fault(TEST_NOT_READY);
        c->publication(3);
        CHECK(!get(executor->next(0)));
        c->fault(TEST_NORMAL);
        c->publication(3);
        CHECK(get(executor->next(100))->key.sequence.value == 3);
        get(executor->stop(100));
        get(executor->start({{"new-run"}}, {{"new-generation"}}, 100));
        c->publication(0);
        CHECK(get(executor->next(100))->key.sequence.value == 0);
        get(executor->close(100));
        c->shape(UINT32_MAX);
    }
}
void prepared_program(const std::shared_ptr<plugins::Loaded> &loaded) {
    auto *c = control(*loaded);
    c->fault(TEST_NORMAL);
    auto executor = opened(loaded);
    auto unprepared = executor->start(run, generation, 100);
    CHECK(!unprepared && unprepared.error().code == Status::invalid_argument);
    CHECK(get(executor->prepare(program(), 100)).accepted);
    c->fault(TEST_REJECT_PROGRAM);
    CHECK(!get(executor->prepare(program(), 100)).accepted);
    c->fault(TEST_NORMAL);
    auto stale = executor->start(run, generation, 100);
    CHECK(!stale && stale.error().code == Status::invalid_argument);
    CHECK(get(executor->prepare(program(), 100)).accepted);
    c->fault(TEST_FAILURE);
    auto failed = executor->prepare(program(), 100);
    CHECK(!failed && failed.error().code == Status::plugin_failed);
    c->fault(TEST_NORMAL);
    stale = executor->start(run, generation, 100);
    CHECK(!stale && stale.error().code == Status::invalid_argument);
    CHECK(get(executor->prepare(program(), 100)).accepted);
    auto invalid = program();
    invalid.repetitions = 0;
    CHECK(!executor->prepare(invalid, 100));
    stale = executor->start(run, generation, 100);
    CHECK(!stale && stale.error().code == Status::invalid_argument);
    // Graph identity validation happens before plugin acceptance.
    for (bool stream : {true, false}) {
        auto wrong = program();
        if (stream)
            wrong.participants.cameras[0].stream = {{"wrong-stream"}};
        else
            wrong.participants.cameras[0].role = "wrong-role";
        auto validated = executor->validate(wrong, 100);
        CHECK(!validated && validated.error().code == Status::invalid_argument);
        auto prepared = executor->prepare(wrong, 100);
        CHECK(!prepared && prepared.error().code == Status::invalid_argument);
    }
    get(executor->close(100));
    for (auto f : {TEST_HASH_MISMATCH, TEST_HASH_DOWNGRADE, TEST_CONTENT_MISMATCH, TEST_CONTENT_DOWNGRADE,
                   TEST_CONTENT_HASH, TEST_EVIDENCE_STEP, TEST_EVIDENCE_REPETITION, TEST_TRIGGER_STEP,
                   TEST_TRIGGER_REPETITION, TEST_STATUS_STEP, TEST_STATUS_REPETITION}) {
        c->fault(TEST_NORMAL);
        c->shape(f == TEST_TRIGGER_STEP || f == TEST_TRIGGER_REPETITION ? 2u : 1u);
        executor = opened(loaded);
        auto prepared = program();
        prepared.steps[0].index = 42; // Declared index is deliberately not vector position.
        prepared.repetitions = 3;
        prepared.identity.hash = Hash{"sha256", "abcd"};
        prepared.identity.content = data::ContentReference{
            prepared.identity.id.id, schema::acquisition_program, Hash{"sha256", "abcd"}, 7};
        if (f == TEST_CONTENT_HASH)
            prepared.identity.hash = data::Unknown{};
        CHECK(get(executor->prepare(prepared, 100)).accepted);
        get(executor->start(run, generation, 100));
        auto status = get(executor->status(100));
        CHECK(status.step.get()->step_index == 42);
        auto first = get(executor->next(100));
        CHECK(first && first->evidence.program.content.get()->revision == 7);
        if (f == TEST_TRIGGER_STEP || f == TEST_TRIGGER_REPETITION)
            CHECK(first->triggers[0].step.step_index == 42);
        else
            CHECK(first->evidence.step.get()->step_index == 42);
        c->fault(f);
        if (f == TEST_STATUS_STEP || f == TEST_STATUS_REPETITION) {
            auto output = executor->status(100);
            CHECK(!output && output.error().code == Status::incompatible);
        } else {
            auto output = executor->next(100);
            CHECK(!output && output.error().code == Status::incompatible);
        }
        c->fault(TEST_NORMAL);
        c->publication(1); // Failed correlation must not advance successor state either.
        CHECK(get(executor->next(100))->key.sequence.value == 1);
        get(executor->close(100));
        c->shape(UINT32_MAX);
    }
}
void exact_reference(const std::shared_ptr<plugins::Loaded> &loaded) {
    auto *c = control(*loaded);
    for (auto presence :
         {data::Presence::unknown, data::Presence::unavailable, data::Presence::established}) {
        c->fault(TEST_NORMAL);
        c->shape(0);
        auto executor = opened(loaded);
        auto prepared = program();
        if (presence == data::Presence::established) {
            prepared.identity.hash = Hash{"sha256", "abcd"};
            prepared.identity.content = data::ContentReference{
                prepared.identity.id.id, schema::acquisition_program, Hash{"sha256", "abcd"}, 7};
        } else if (presence == data::Presence::unknown) {
            prepared.identity.hash = data::Unknown{};
            prepared.identity.content = data::Unknown{};
        } else {
            prepared.identity.hash = data::Unavailable{};
            prepared.identity.content = data::Unavailable{};
        }
        CHECK(get(executor->prepare(prepared, 100)).accepted);
        get(executor->start(run, generation, 100));
        auto first = get(executor->next(100));
        CHECK(first && first->evidence.program.hash == prepared.identity.hash &&
              first->evidence.program.content.presence() == presence);
        if (presence == data::Presence::established) {
            CHECK(first->evidence.program.content.get()->revision == 7 &&
                  first->evidence.program.content.get()->hash == prepared.identity.content.get()->hash);
        } else {
            for (auto f : {TEST_HASH_ESTABLISHED, TEST_HASH_UNAVAILABLE, TEST_HASH_DOWNGRADE,
                           TEST_CONTENT_ESTABLISHED, TEST_CONTENT_UNAVAILABLE, TEST_CONTENT_DOWNGRADE}) {
                if ((presence == data::Presence::unknown &&
                     (f == TEST_HASH_DOWNGRADE || f == TEST_CONTENT_DOWNGRADE)) ||
                    (presence == data::Presence::unavailable &&
                     (f == TEST_HASH_UNAVAILABLE || f == TEST_CONTENT_UNAVAILABLE)))
                    continue; // unchanged absence is tested by the successful first publication
                c->fault(f);
                c->publication(1);
                auto rejected = executor->next(100);
                CHECK(!rejected && rejected.error().code == Status::incompatible);
            }
        }
        c->fault(TEST_NORMAL);
        c->publication(1);
        CHECK(get(executor->next(100))->key.sequence.value == 1);
        get(executor->close(100));
        c->shape(UINT32_MAX);
    }
    // Exact presence also applies to the hash nested inside established content.
    for (auto presence :
         {data::Presence::unknown, data::Presence::unavailable, data::Presence::established}) {
        c->fault(TEST_NORMAL);
        c->shape(0);
        auto executor = opened(loaded);
        auto prepared = program();
        data::Evidence<Hash> hash = data::Unknown{};
        if (presence == data::Presence::unavailable)
            hash = data::Unavailable{};
        if (presence == data::Presence::established)
            hash = Hash{"sha256", "abcd"};
        prepared.identity.hash = data::Unknown{};
        prepared.identity.content =
            data::ContentReference{prepared.identity.id.id, schema::acquisition_program, hash, 7};
        CHECK(get(executor->prepare(prepared, 100)).accepted);
        get(executor->start(run, generation, 100));
        CHECK(get(executor->next(100))->evidence.program.content.get()->hash == hash);
        for (auto f : {TEST_CONTENT_HASH, TEST_CONTENT_HASH_UNKNOWN, TEST_CONTENT_HASH_UNAVAILABLE}) {
            if ((presence == data::Presence::unknown && f == TEST_CONTENT_HASH_UNKNOWN) ||
                (presence == data::Presence::unavailable && f == TEST_CONTENT_HASH_UNAVAILABLE))
                continue;
            c->fault(f);
            c->publication(1);
            auto rejected = executor->next(100);
            CHECK(!rejected && rejected.error().code == Status::incompatible);
        }
        c->fault(TEST_NORMAL);
        c->publication(1);
        CHECK(get(executor->next(100))->key.sequence.value == 1);
        get(executor->close(100));
        c->shape(UINT32_MAX);
    }
}
void capture_graph(const std::shared_ptr<plugins::Loaded> &loaded) {
    auto *c = control(*loaded);
    for (auto f :
         {TEST_SOURCE_WIDTH, TEST_SOURCE_HEIGHT, TEST_OUTPUT_FRAMESET_STREAM, TEST_GRAPH_NO_FRAMESET}) {
        c->fault(f == TEST_GRAPH_NO_FRAMESET ? f : TEST_NORMAL);
        c->shape(f == TEST_GRAPH_NO_FRAMESET ? 0u : 1u);
        auto executor = opened(loaded);
        c->fault(TEST_NORMAL);
        start(*executor);
        CHECK(get(executor->next(100)));
        c->shape(1);
        c->fault(f == TEST_GRAPH_NO_FRAMESET ? TEST_NORMAL : f);
        auto rejected = executor->next(100);
        CHECK(!rejected && rejected.error().code == Status::incompatible);
        c->fault(TEST_NORMAL);
        if (f == TEST_GRAPH_NO_FRAMESET)
            c->shape(0);
        c->publication(1);
        auto accepted = get(executor->next(100));
        CHECK(accepted && accepted->key.sequence.value == 1);
        if (f != TEST_GRAPH_NO_FRAMESET) {
            const auto &source = *executor->graph().components[1].image_source;
            CHECK(accepted->evidence.frames[0].width == source.width &&
                  accepted->evidence.frames[0].height == source.height &&
                  accepted->evidence.frameset.get()->stream.id == *executor->graph().frameset_stream.get());
        }
        get(executor->close(100));
        c->shape(UINT32_MAX);
    }
}
void abort_emitters(const std::shared_ptr<plugins::Loaded> &loaded) {
    auto *c = control(*loaded);
    for (auto f : {TEST_ABORT_EMPTY, TEST_ABORT_MISSING, TEST_ABORT_DUPLICATE, TEST_ABORT_FOREIGN,
                   TEST_ABORT_UNKNOWN, TEST_ABORT_REVERSE, TEST_NORMAL}) {
        c->fault(TEST_TWO_EMITTERS);
        auto executor = opened(loaded);
        CHECK(executor->graph().components.size() == 5);
        c->fault(f);
        auto outcome = executor->abort(data::AcquisitionReason::user_cancel, 100);
        if (f == TEST_ABORT_UNKNOWN || f == TEST_ABORT_REVERSE || f == TEST_NORMAL) {
            CHECK(outcome && outcome->emitters.size() == 2);
            CHECK(outcome->emitters[0].observed.presence() == data::Presence::unavailable);
            if (f == TEST_ABORT_UNKNOWN)
                CHECK(outcome->emitters[0].commanded.presence() == data::Presence::unknown);
            else
                CHECK(outcome->emitters[0].commanded.get()->state == data::EmitterState::off);
            if (f == TEST_ABORT_UNKNOWN)
                CHECK(outcome->emitters[1].commanded.presence() == data::Presence::unavailable);
            else
                CHECK(outcome->emitters[1].commanded.get()->state == data::EmitterState::off);
            CHECK(outcome->emitters[1].observed.presence() == data::Presence::unavailable);
            if (f == TEST_ABORT_REVERSE)
                CHECK(outcome->emitters[0].emitter.id.value == "emitter-beta");
        } else
            CHECK(!outcome && outcome.error().code == Status::incompatible);
        c->fault(TEST_NORMAL);
        get(executor->close(100));
    }
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

    // The SDK admits one ordinary call and one abort, but never two aborts.
    sdk::ProjectedLight client(api, plugins::host_api(), "parent-alpha", 100, loaded);
    CHECK(!client.prepare(pv.get(), 100, valid, nullptr));
    CHECK(!client.start("sdk-run", "sdk-generation", 100));
    std::promise<void> normal_entered, normal_release, abort_entered, abort_release;
    auto normal_released = normal_release.get_future().share();
    auto abort_released = abort_release.get_future().share();
    auto ordinary = std::async(std::launch::async, [&] {
        return client.next(1000, [&](const MantisSemanticPacketV1 &) {
            normal_entered.set_value();
            CHECK(normal_released.wait_for(800ms) == std::future_status::ready);
        });
    });
    CHECK(normal_entered.get_future().wait_for(250ms) == std::future_status::ready);
    auto side = std::async(std::launch::async, [&] {
        return client.abort(MANTIS_ACQUISITION_REASON_USER_CANCEL, 1000, [&](const MantisAbortOutcomeV1 &) {
            abort_entered.set_value();
            CHECK(abort_released.wait_for(800ms) == std::future_status::ready);
        });
    });
    CHECK(abort_entered.get_future().wait_for(250ms) == std::future_status::ready);
    unsigned second_callbacks = 0;
    auto second_abort = [&](const MantisAbortOutcomeV1 &) { ++second_callbacks; };
    CHECK(client.abort(MANTIS_ACQUISITION_REASON_USER_CANCEL, 0, second_abort) == MANTIS_PL_BUSY &&
          second_callbacks == 0);
    const auto before_destroy = c->destroyed();
    CHECK(client.close(5) == MANTIS_PL_BUSY && c->destroyed() == before_destroy);
    normal_release.set_value();
    CHECK(!ordinary.get());
    CHECK(client.close(5) == MANTIS_PL_BUSY && c->destroyed() == before_destroy);
    auto waiting_close = std::async(std::launch::async, [&] { return client.close(1000); });
    CHECK(waiting_close.wait_for(10ms) == std::future_status::timeout);
    CHECK(client.abort(MANTIS_ACQUISITION_REASON_USER_CANCEL, 0, second_abort) == MANTIS_PL_BUSY);
    abort_release.set_value();
    CHECK(!side.get() && !waiting_close.get() && c->destroyed() == before_destroy + 1);
}
std::atomic_uint retains{}, releases{}, allocations{};
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
    MantisHostV1 producer = *plugins::host_api();
    producer.allocate = [](uint64_t size, uint64_t alignment) {
        ++allocations;
        return plugins::host_api()->allocate(size, alignment);
    };
    data::Published retained;
    const void *backing{};
    std::optional<sdk::RetainedBuffer> plugin_retained;
    {
        auto full = domain_fixture::frameset_packet();
        auto image = *full->frames[0];
        image.attributes[0].buffer = image.attributes[0].buffer.slice(32, 4);
        image.attributes[0].descriptor.shape = {2, 2};
        image.attributes[0].descriptor.stride = {2, 1};
        auto frameset = *full;
        frameset.frames = {data::publish(std::move(image))};
        auto original = data::publish(std::move(frameset));
        auto original_bytes = get(original->frames[0]->attributes[0].buffer.map_read());
        backing = original_bytes.data();
        plugins::semantic::PacketView view(original, &producer);
        const auto &attribute = view.get()->data->frames[0].attributes[0];
        const void *abi_backing{};
        uint64_t size{};
        CHECK(!producer.read_map(attribute.buffer, &abi_backing, &size));
        CHECK(allocations == 0 && abi_backing == backing && size == original_bytes.size() && size == 4);
        plugin_retained.emplace(&producer, attribute.buffer);
        retained = std::get<data::Published>(plugins::semantic::packet(view.get(), &host));
        CHECK(retains == 1 && releases == 0);
    }
    auto bytes = get(retained->frames[0]->attributes[0].buffer.map_read());
    CHECK(bytes[0] == std::byte{7} && bytes.data() == backing);
    const void *plugin_backing{};
    uint64_t size{};
    CHECK(!producer.read_map(plugin_retained->get(), &plugin_backing, &size) && plugin_backing == backing);
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
        successors(loaded);
        prepared_program(loaded);
        exact_reference(loaded);
        capture_graph(loaded);
        abort_emitters(loaded);
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
        std::cout << checks.load() << " projected-light ABI/adapter checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
