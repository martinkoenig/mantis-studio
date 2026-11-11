#include "../support/l1_candidate_values.hpp"
#include <condition_variable>
#include <future>
#include <iostream>
#include <limits>
#include <mantis/artifact_store.hpp>
#include <mantis/pipeline_runtime.hpp>
#include <mantis/replay.hpp>
#include <mutex>
#include <source_location>

using namespace l1_fixture;
using namespace mantis::pipeline;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__));                 \
    } while (false)
data::SemanticPublished invoke(SemanticNodeInstance &node, const AcquisitionBundle &b,
                               const CancellationToken &token = {}) {
    std::array<SemanticPublished, 1> inputs{data::publish(SemanticPacket{b})};
    auto r = node.process(inputs, token);
    if (!r)
        throw Failure(r.error());
    return *r;
}
LaserObservation extract(const AcquisitionBundle &b, L1CandidateConfig c = config()) {
    auto instance = l1_candidate_extractor(std::move(c)).factory();
    return std::get<LaserObservation>(*invoke(*instance, b));
}
template <class F>
void rejects(F f, std::optional<Status> status = {},
             const std::source_location location = std::source_location::current()) {
    bool bad = false;
    try {
        f();
    } catch (const Failure &e) {
        bad = true;
        if (status && e.error.code != *status)
            throw std::runtime_error("Expected error code " + std::to_string(static_cast<int>(*status)) +
                                     ", actual " + std::to_string(static_cast<int>(e.error.code)) + ": " +
                                     e.error.message + " (component=" + e.error.component + ") at " +
                                     location.file_name() + ":" + std::to_string(location.line()));
    }
    CHECK(bad);
}
void assert_line(const LaserObservation &o, uint32_t first, uint32_t count, uint32_t peak,
                 bool refined = false) {
    CHECK(o.sample_count == count);
    CHECK(o.disposition == ObservationDisposition::success);
    for (uint32_t i = 0; i < count; ++i) {
        CHECK(value<float>(o, laser::source_pixel, i) == first + i);
        const auto expected_y = refined ? float(2.0 + double(peak - 11) / (2 * double(peak - 9))) : 2.0f;
        CHECK(value<float>(o, laser::source_pixel, i, 1) == expected_y);
        if (refined) {
            CHECK(value<uint8_t>(o, l1::refinement_valid, i) == 1);
            CHECK(value<uint32_t>(o, l1::curvature, i) == peak - 9);
        }
        CHECK(value<uint32_t>(o, laser::quality_flags, i) == 0);
        CHECK(value<uint32_t>(o, l1::reason, i) == 0);
        CHECK(value<uint32_t>(o, l1::peak, i) == peak);
        CHECK(value<uint32_t>(o, l1::width, i) == 2);
        CHECK(value<uint8_t>(o, laser::emitter_valid, i) == 1);
        CHECK(value<uint8_t>(o, laser::line_valid, i) == 1);
        CHECK(value<uint32_t>(o, laser::line_index, i) == 0);
    }
    const auto identity = o.attributes[0].buffer.identity();
    for (const auto &a : o.attributes)
        CHECK((refined && a.descriptor.name == l1::curvature) || a.buffer.identity() == identity);
    CHECK(o.confidence_interpretation.presence() == Presence::unavailable);
}
void positives() {
    for (bool packed : {false, true}) {
        auto b = line(packed);
        auto before = b.frameset->frames[0]->attributes[0].buffer;
        const auto hash = content_hash(*before.map_read());
        auto c = config(packed);
        auto o = extract(b, c);
        assert_line(o, 0, 8, packed ? 701 : 181);
        CHECK(encode(o) == encode(extract(b, c)));
        CHECK(content_hash(*before.map_read()) == hash);
        CHECK(o.context.packet_quality_flags == 0);
        CHECK(o.key.sequence.value == 1 && o.key.producer_stream == c.producer_stream &&
              o.key.producer_generation == c.producer_generation && *o.key.run_id.get() == run);
        CHECK(o.context.producer.implementation.value == "org.mantis.l1-candidates");
        CHECK((o.context.producer.version == SemanticVersion{1, 0, 0}));
        CHECK(o.context.producer_completed.presence() == Presence::unavailable);
        CHECK(o.context.preprocessing.presence() == Presence::unavailable);
        CHECK(o.context.origin == ObservationOrigin::synthetic);
        CHECK(o.context.parameters.get() && o.context.parameters.get()->hash.get());
        CHECK(o.context.parameters.get()->hash.get()->algorithm == "fnv1a64");
        // Canonical comparison detects changes in every nested source/evidence field,
        // including clock generations, exposure, sync and exact calibration references.
        auto expected = o;
        expected.context.source = b.evidence.frames[0];
        expected.context.frameset = b.evidence.frameset;
        expected.context.bundle = b.key;
        expected.context.raw_input = c.raw_input;
        expected.context.correlation = ProgramCorrelation{b.evidence.program, *b.evidence.step.get()};
        expected.context.acquisition_evidence = b.evidence.key;
        expected.context.triggers = b.evidence.triggers;
        expected.context.clock_mappings = b.evidence.clock_mappings;
        expected.context.emitter_evidence = b.evidence.emitters;
        expected.context.requested_emitters = {emitter};
        expected.context.emitter_patterns = {{emitter, c.pattern, 7}};
        expected.context.exact_inputs = {c.raw_input};
        expected.emitter_dictionary = {emitter};
        expected.line_dictionary = {{emitter, c.pattern, 7, c.line}};
        CHECK(encode(o) == encode(expected));
        c.roi = L1CandidateConfig::Roi{2, 1, 4, 5};
        auto cropped = extract(b, c);
        assert_line(cropped, 2, 4, packed ? 701 : 181);
        CHECK(*cropped.context.parameters.get()->hash.get() != *o.context.parameters.get()->hash.get());
        // Exercise the actual typed compiler/executor and consumer, not just a helper.
        NodeDescriptor source;
        source.id = "recorded-input";
        source.outputs = {{"bundle", schema::acquisition_bundle}};
        SemanticGraph graph;
        graph.nodes = {{source, {}}, l1_candidate_extractor(c), observation_consumer()};
        graph.connections = {{0, 0, 1, 0, 1, QueuePolicy::lossless}, {1, 0, 2, 0, 1, QueuePolicy::lossless}};
        auto plan = compile(graph);
        CHECK(plan);
        auto result = execute(*plan, data::publish(SemanticPacket{b}));
        CHECK(result);
        CHECK(encode(std::get<LaserObservation>(*result->output)) == encode(cropped));
        CHECK(result->retained_payload_high_water >= before.backing_size());
    }
    // Equal-height plateau: topmost original pixel, with no fractional localization.
    auto plateau =
        image_bundle(false, 8, 8, [](auto, auto y) { return uint16_t(y == 2 || y == 3 ? 180 : 5); });
    assert_line(extract(plateau), 0, 8, 180);
}
void profiles() {
    auto b = image_bundle(false, 8, 8, [](auto x, auto y) -> uint16_t {
        if (x == 1)
            return 8; // absent/possibly occluded, never synthesize a coordinate
        if (x == 2 && (y == 2 || y == 3))
            return 255;
        if (x == 3 && (y == 5 || y == 6))
            return 179; // competing near-equal stripe
        if (x == 4)
            return y == 2 ? 200 : 5; // narrow outlier
        return y == 2 || y == 3 ? 181 : 5;
    });
    auto o = extract(b);
    CHECK(o.sample_count == 7);
    CHECK(o.diagnostic == "profiles=8;missing=1;rejected=3;saturated=1");
    CHECK(o.context.packet_quality_flags ==
          (l1::missing_profiles | l1::rejected_profiles | l1::saturation_observed));
    CHECK(value<float>(o, laser::source_pixel, 1) == 2);
    CHECK(value<uint32_t>(o, l1::reason, 1) == l1::saturated);
    CHECK(value<uint32_t>(o, laser::quality_flags, 1) == laser::producer_rejected);
    CHECK(value<float>(o, laser::source_pixel, 2, 1) == 2);
    CHECK(value<uint32_t>(o, l1::reason, 2) == l1::competing_peaks);
    CHECK(value<uint32_t>(o, laser::quality_flags, 2) == (laser::ambiguous | laser::producer_rejected));
    CHECK(value<uint32_t>(o, l1::reason, 3) == l1::width_rejected);
    auto equal = image_bundle(
        false, 8, 8, [](auto, auto y) { return uint16_t(y == 2 || y == 3 || y == 5 || y == 6 ? 180 : 5); });
    auto tie = extract(equal);
    CHECK(value<float>(tie, laser::source_pixel, 0, 1) == 2);
    CHECK(value<uint32_t>(tie, l1::reason, 0) == l1::competing_peaks);
    auto later = image_bundle(false, 8, 8, [](auto, auto y) {
        return uint16_t(y == 5 || y == 6 ? 200 : y == 2 || y == 3 ? 180 : 5);
    });
    CHECK(value<float>(extract(later), laser::source_pixel, 0, 1) == 5);
    for (uint16_t background : {0, 10, 90}) {
        auto empty =
            image_bundle(false, 8, 8, [=](auto x, auto y) { return uint16_t(background + (x + y) % 3); });
        auto none = extract(empty);
        CHECK(none.sample_count == 0 && none.attributes.empty());
        CHECK(none.disposition == ObservationDisposition::success);
        CHECK(none.context.packet_quality_flags == l1::missing_profiles);
    }
    auto c = config();
    c.background_level = 100;
    auto none = extract(line(), c);
    CHECK(none.sample_count == 0 && none.attributes.empty());
}
void evidence() {
    for (Presence presence : {Presence::unknown, Presence::unavailable}) {
        auto b = line();
        b.evidence.emitters[0].exposure_effective[0].state =
            presence == Presence::unknown ? Evidence<ExposureEffectiveState>{Unknown{}}
                                          : Evidence<ExposureEffectiveState>{Unavailable{}};
        auto o = extract(b);
        CHECK(o.sample_count == 8);
        CHECK(o.context.emitter_evidence[0].exposure_effective[0].state.presence() == presence);
        CHECK(value<uint8_t>(o, laser::emitter_valid, 0) == 0);
        CHECK(value<uint8_t>(o, laser::line_valid, 0) == 0);
        CHECK(value<uint32_t>(o, laser::quality_flags, 0) == (laser::emitter_unknown | laser::line_unknown));
        CHECK(o.context.packet_quality_flags == l1::attribution_unresolved);
    }
    auto b = line();
    auto state = *b.evidence.emitters[0].exposure_effective[0].state.get();
    state.scope = EvidenceScope::electrical_enable;
    state.evidence = proof(EvidenceMethod::electrical_readback);
    b.evidence.emitters[0].exposure_effective[0].state = state;
    CHECK(value<uint8_t>(extract(b), laser::emitter_valid, 0) == 0);
    b = line();
    state = *b.evidence.emitters[0].exposure_effective[0].state.get();
    state.state = EmitterState::off;
    b.evidence.emitters[0].exposure_effective[0].state = state;
    rejects([&] { extract(b); });
    auto c = config();
    c.emitter = {{"not-the-emitter"}};
    rejects([&] { extract(line(), c); });
    b = line();
    b.evidence.emitters.clear();
    rejects([&] { extract(b); });
    b = line();
    auto other = b.evidence.emitters[0];
    other.emitter = {{"other-emitter"}};
    other.commanded = Unavailable{};
    other.acknowledged = Unavailable{};
    other.observed = Unavailable{};
    b.evidence.participants.emitters.push_back(other.emitter);
    b.evidence.emitters.push_back(other);
    rejects([&] { extract(b); });
    b.evidence.emitters.back().exposure_effective[0].state = Unknown{};
    CHECK(value<uint8_t>(extract(b), laser::emitter_valid, 0) == 0);
    b = line();
    state = *b.evidence.emitters[0].exposure_effective[0].state.get();
    state.coverage.end.nanoseconds = 9;
    b.evidence.emitters[0].exposure_effective[0].state = state;
    rejects([&] { extract(b); });
    b = line();
    auto exposure = *b.evidence.frames[0].exposure.get();
    exposure.uncertainty_ns = std::numeric_limits<double>::quiet_NaN();
    b.evidence.frames[0].exposure = exposure;
    rejects([&] { extract(b); });
    // Explicitly select the second camera; vector order cannot select the decoy.
    b = line();
    auto decoy = b.evidence.frames[0];
    decoy.frame.camera = {{"decoy"}};
    decoy.frame.stream.id = {{"decoy-stream"}};
    decoy.camera_role = "right";
    b.evidence.frames.insert(b.evidence.frames.begin(), decoy);
    b.evidence.participants.cameras.insert(b.evidence.participants.cameras.begin(),
                                           {decoy.frame.camera, decoy.frame.stream.id, "right"});
    b.evidence.emitters[0].exposure_effective.insert(b.evidence.emitters[0].exposure_effective.begin(),
                                                     {decoy.frame, Unknown{}});
    auto set = *b.frameset;
    auto decoy_image = *set.frames[0];
    decoy_image.header.metadata["role"] = "right";
    decoy_image.header.metadata["component_id"] = "decoy";
    decoy_image.header.metadata["stream_id"] = "decoy-stream";
    set.frames.insert(set.frames.begin(), data::publish(std::move(decoy_image)));
    b.frameset = data::publish(std::move(set));
    assert_line(extract(b), 0, 8, 181);
    CHECK(extract(b).context.source.frame.camera == camera);
    CHECK(extract(b).context.emitter_evidence[0].exposure_effective.size() == 1);
}
void invalid_inputs() {
    auto instance = l1_candidate_extractor(config()).factory();
    auto bad = [&](AcquisitionBundle b) {
        // Bypass the convenient publication helper to exercise node-side validation.
        std::array<SemanticPublished, 1> in{std::make_shared<const SemanticPacket>(std::move(b))};
        CHECK(!instance->process(in, {}));
    };
    auto b = line();
    edit_image(b, [](auto &p) { p.attributes[0].descriptor.stride[0] = 1; });
    bad(b);
    b = line(true);
    edit_image(b, [](auto &p) { p.header.metadata["org.mantis.image.row_stride_bytes"] = "9"; });
    bad(b);
    b = line(true);
    edit_image(b, [](auto &p) { p.header.metadata["org.mantis.image.width"] = "7"; });
    bad(b);
    b = line();
    edit_image(b, [](auto &p) { p.attributes[0].descriptor.shape[0] = 0; });
    bad(b);
    b = line();
    edit_image(b, [](auto &p) { p.attributes[0].buffer = p.attributes[0].buffer.slice(0, 10); });
    bad(b);
    b = line();
    edit_image(b, [](auto &p) { p.attributes[0].descriptor.shape = {UINT64_MAX, UINT64_MAX}; });
    bad(b);
    b = line();
    b.evidence.frames[0].width = UINT32_MAX;
    bad(b);
    b = line();
    b.evidence.frames[0].height = 0;
    bad(b);
    b = line();
    b.evidence.frames[0].width = 7;
    bad(b);
    b = line();
    edit_image(b, [](auto &p) { p.header.metadata["role"] = "right"; });
    bad(b);
    b = line();
    edit_image(b, [](auto &p) { p.header.metadata["component_id"] = "other-camera"; });
    bad(b);
    b = line();
    edit_image(b, [](auto &p) { p.header.sequence.value = 9; });
    bad(b);
    b = line();
    edit_image(b, [](auto &p) { p.header.timestamp.nanoseconds = 9; });
    bad(b);
    b = line();
    edit_image(b, [](auto &p) { p.header.calibration.revision = 99; });
    bad(b);
    b = line();
    edit_image(b, [](auto &p) { p.header.metadata["fourcc"] = "RGB3"; });
    bad(b);
    b = line();
    b.evidence.frames[0].frame.stream.generation = {{"stale"}};
    bad(b);
    b = line();
    b.evidence.disposition = AcquisitionDisposition::failed;
    bad(b);
    b = line();
    b.frameset.reset();
    bad(b);
    for (unsigned mode = 0; mode < 10; ++mode) {
        auto c = config();
        switch (mode) {
        case 0:
            c.minimum_signal = 0;
            break;
        case 1:
            c.maximum_width = 0;
            break;
        case 2:
            c.maximum_dimension = UINT32_MAX;
            break;
        case 3:
            c.maximum_pixels = UINT64_MAX;
            break;
        case 4:
            c.emitter = {};
            break;
        case 5:
            c.raw_input.hash = Unknown{};
            break;
        case 6:
            c.roi = L1CandidateConfig::Roi{0, 0, 0, 2};
            break;
        case 7:
            c.minimum_signal = 1024;
            break;
        case 8:
            c.dark = L1DarkReference{};
            break;
        case 9:
            c.origin.reset();
            break;
        }
        rejects([&] { l1_candidate_extractor(c); });
    }
    auto c = config();
    c.roi = L1CandidateConfig::Roi{UINT32_MAX, 1, 8, 8};
    rejects([&] { extract(line(), c); });
    c = config();
    c.minimum_signal = 256;
    rejects([&] { extract(line(), c); });
    c = config();
    c.maximum_pixels = 63;
    rejects([&] { extract(line(), c); });
    CHECK(std::get<LaserObservation>(*invoke(*instance, line())).sample_count ==
          8); // recovery after rejection
    std::array<SemanticPublished, 0> empty;
    CHECK(!instance->process(empty, {}));
    std::array<SemanticPublished, 1> wrong{data::publish(SemanticPacket{images()})};
    CHECK(!instance->process(wrong, {}));
    CancellationToken cancelled;
    cancelled.cancel();
    rejects([&] { invoke(*instance, line(), cancelled); }, Status::cancelled);
}
void dark_reference() {
    auto b = image_bundle(false, 8, 8, [](auto, auto y) { return uint16_t(y == 2 || y == 3 ? 181 : 120); });
    auto dark = image_bundle(false, 8, 8, [](auto, auto) { return uint16_t(120); }, 3, true);
    auto c = config();
    c.minimum_signal = 50;
    c.dark = L1DarkReference{std::make_shared<const AcquisitionBundle>(dark), c.raw_input};
    assert_line(extract(b, c), 0, 8, 181);
    CHECK(extract(b, c).context.exact_inputs.size() == 1);
    c.dark->raw_input.id.value = "dark-raw-fixture";
    CHECK(extract(b, c).context.exact_inputs.size() == 2);
    auto original = *extract(b, c).context.parameters.get()->hash.get();
    dark.evidence.emitters[0].exposure_effective[0].state = Unknown{};
    c.dark->bundle = std::make_shared<const AcquisitionBundle>(dark);
    rejects([&] { extract(b, c); });
    c.dark->bundle = std::make_shared<const AcquisitionBundle>(b);
    rejects([&] { extract(b, c); });
    dark = image_bundle(false, 8, 8, [](auto, auto) { return uint16_t(120); }, 3, true);
    auto e = *dark.evidence.frames[0].exposure.get();
    e.integration_duration = Duration{9};
    dark.evidence.frames[0].exposure = e;
    c.dark->bundle = std::make_shared<const AcquisitionBundle>(dark);
    rejects([&] { extract(b, c); });
    c.dark.reset();
    CHECK(*extract(b, c).context.parameters.get()->hash.get() != original);
}
struct Gate final : memory::Fence {
    mutable std::mutex mutex;
    mutable std::condition_variable cv;
    mutable std::promise<void> entered;
    mutable bool announced{}, released{};
    Result<void> wait(const CancellationToken &token) const override {
        std::unique_lock lock(mutex);
        if (!announced) {
            announced = true;
            entered.set_value();
        }
        cv.wait(lock, [&] { return released; });
        if (token.cancelled())
            return std::unexpected(Error{Status::cancelled, "cancelled at input fence", "test"});
        return {};
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        cv.notify_all();
    }
};
void cancellation_lifetime(bool refined = false) {
    for (bool packed : {false, true}) {
        auto b = line(packed);
        auto source = b.frameset->frames[0]->attributes[0].buffer;
        auto owner =
            std::make_shared<std::vector<std::byte>>(source.map_read()->begin(), source.map_read()->end());
        std::weak_ptr<std::vector<std::byte>> weak = owner;
        auto gate = std::make_shared<Gate>();
        auto storage = std::make_shared<memory::Storage>();
        storage->owner = owner;
        storage->host = owner->data();
        storage->size = owner->size();
        storage->ready = gate;
        edit_image(b, [&](auto &p) { p.attributes[0].buffer = {storage, 0, storage->size}; });
        std::array<SemanticPublished, 1> inputs{std::make_shared<const SemanticPacket>(b)};
        auto c = config(packed);
        if (refined)
            c.subpixel = L1SubpixelConfig{};
        auto instance = l1_candidate_extractor(c).factory();
        CancellationToken token;
        auto entry = gate->entered.get_future();
        auto pending = std::async(std::launch::async,
                                  [&, retained = inputs] { return instance->process(retained, token); });
        // Declared after future: releases the gate even when an assertion unwinds.
        struct Release {
            std::shared_ptr<Gate> gate;
            ~Release() {
                gate->release();
            }
        } release{gate};
        CHECK(entry.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        b.frameset.reset();
        inputs[0].reset();
        storage.reset();
        owner.reset();
        CHECK(!weak.expired());
        token.cancel();
        gate->release();
        CHECK(pending.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        auto r = pending.get();
        CHECK(!r && r.error().code == Status::cancelled);
        CHECK(weak.expired());
        auto retained = invoke(*instance, line(packed));
        instance.reset();
        assert_line(std::get<LaserObservation>(*retained), 0, 8, packed ? 701 : 181, refined);
    }
}
void recorded_path(bool refined = false) {
    const auto path = std::filesystem::temp_directory_path() / ("mantis-l1-" + Id::random().value);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{path};
    LaserObservation retained;
    std::string expected_bytes;
    {
        auto store = std::make_shared<artifact::Store>(path);
        auto raw = store->begin_projected_capture(header());
        store->append_bundle(raw, bundle(0));
        store->append_bundle(raw, line(true));
        store->record_run_outcome(raw,
                                  {2, outcome(RecordedRunDisposition::completed, AcquisitionReason::none)});
        auto descriptor = store->finalize(raw);
        auto c = config(true);
        if (refined)
            c.subpixel = L1SubpixelConfig{};
        c.raw_input = {raw, {"org.mantis.RawCapture", 3}, descriptor.hash, 0};
        auto original = store->bundle(raw, 1);
        std::ostringstream raw_bytes;
        write_bundle(raw_bytes, original);
        auto node = l1_candidate_extractor(c);
        auto instance = node.factory();
        for (unsigned pass = 0; pass < 2; ++pass) {
            device::BundleReplay replay(store, raw, false);
            auto startup = replay.next();
            CHECK(startup && startup->has_value());
            auto captured = replay.next();
            CHECK(captured && captured->has_value());
            auto result = invoke(*instance, **captured);
            const auto &o = std::get<LaserObservation>(*result);
            assert_line(o, 0, 8, 701, refined);
            CHECK(o.context.raw_input.get()->id == raw &&
                  *o.context.raw_input.get()->hash.get() == descriptor.hash);
            CHECK(o.context.exact_inputs.size() == 1 && o.context.exact_inputs[0].id == raw);
            if (pass)
                CHECK(encode(o) == expected_bytes);
            else
                expected_bytes = encode(o);
            auto derived = store->begin_laser_observation();
            store->append_laser_observation(derived, o);
            store->finalize(derived);
            retained = store->laser_observation(derived);
            CHECK(encode(retained) == expected_bytes);
        }
        std::ostringstream after;
        write_bundle(after, store->bundle(raw, 1));
        CHECK(after.str() == raw_bytes.str());
        auto wrong = retained;
        auto ref = *wrong.context.raw_input.get();
        auto different_hash = *ref.hash.get();
        CHECK(!different_hash.hex.empty());
        different_hash.hex.front() = different_hash.hex.front() == '0' ? '1' : '0';
        CHECK(different_hash != *ref.hash.get());
        ref.hash = different_hash;
        CHECK(ref.id == raw);
        wrong.context.raw_input = ref;
        for (auto &input : wrong.context.exact_inputs)
            if (input.id == ref.id)
                input = ref;
        CHECK(data::validate(wrong)); // Reach the Store identity guard with a canonical observation.
        auto rejected = store->begin_laser_observation();
        rejects([&] { store->append_laser_observation(rejected, wrong); }, Status::incompatible);
    }
    CHECK(encode(retained) == expected_bytes);
    assert_line(retained, 0, 8, 701, refined);
}
int main() {
    try {
        positives();
        profiles();
        evidence();
        invalid_inputs();
        dark_reference();
        cancellation_lifetime();
        cancellation_lifetime(true);
        recorded_path();
        recorded_path(true);
        std::cout << "L1 candidates: extraction, typed DAG, lineage, rejection, dark evidence and "
                     "cancellation passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
