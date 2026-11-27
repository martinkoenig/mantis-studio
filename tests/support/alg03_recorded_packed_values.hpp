#pragma once

namespace packed_recorded_fixture {
namespace l1 = pipeline::l1;
constexpr uint32_t first_column = 637, peak_row = 400, profiles = 4, padding = 3;
constexpr float vertex = 400.25f;
constexpr uint64_t selected_record = 1, native_frame = 7;
constexpr uint64_t bundle_counter = 30, frameset_counter = 17;

// Within x=637..640 and d=y-400 in [-2,2], the DN is 723 + (x-637) + 17d - 34d²;
// everywhere else it is 48. The vertex is y=400+17/68 and the second difference
// is -68. All four expected pixels are exactly representable as binary32.
constexpr uint16_t background_dn = 48;
constexpr uint16_t stripe_sample(uint32_t phase, uint32_t y) {
    const auto d = static_cast<int>(y) - static_cast<int>(peak_row);
    if (d < -2 || d > 2)
        return background_dn;
    return static_cast<uint16_t>(723 + static_cast<int>(phase) + 17 * d - 34 * d * d);
}
uint16_t signal(uint32_t x, uint32_t y) {
    return x >= first_column && x < first_column + profiles ? stripe_sample(x - first_column, y)
                                                            : background_dn;
}

void check_packed_bytes(const data::AcquisitionBundle &bundle, const calibration::DatasetCamera &camera) {
    const auto &image = *bundle.frameset->frames.at(0);
    const size_t row_bytes = size_t(camera.image_width) * 5 / 4, stride = row_bytes + padding;
    CHECK(image.header.metadata.at("fourcc") == "Y10P");
    CHECK(image.header.metadata.at("org.mantis.image.row_stride_bytes") == std::to_string(stride));
    CHECK(image.attributes.size() == 1);
    CHECK(image.attributes[0].descriptor.name == "org.mantis.image.packed_bytes");
    const auto bytes = get(image.attributes[0].buffer.map_read());
    CHECK(bytes.size() == stride * camera.image_height);
    // Frozen MIPI bytes for groups x=636..639 | 640..643, with the four upper-byte
    // values followed by a byte of 2-bit tails (least significant pair first).
    // Include both adjacent background rows and check padding independently.
    constexpr uint8_t groups[][10] = {
        {0x0c, 0x0c, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x0c, 0x0c, 0x00},
        {0x0c, 0x8a, 0x8a, 0x8a, 0xe4, 0x8b, 0x0c, 0x0c, 0x0c, 0x00},
        {0x0c, 0xa8, 0xa8, 0xa8, 0x90, 0xa8, 0x0c, 0x0c, 0x0c, 0x03},
        {0x0c, 0xb4, 0xb5, 0xb5, 0x4c, 0xb5, 0x0c, 0x0c, 0x0c, 0x02},
        {0x0c, 0xb0, 0xb0, 0xb1, 0x38, 0xb1, 0x0c, 0x0c, 0x0c, 0x01},
        {0x0c, 0x9b, 0x9b, 0x9b, 0xe4, 0x9c, 0x0c, 0x0c, 0x0c, 0x00},
        {0x0c, 0x0c, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x0c, 0x0c, 0x00},
    };
    CHECK(camera.image_width >= 644 && camera.image_height > peak_row + 3);
    for (size_t i = 0; i < std::size(groups); ++i) {
        const auto row = bytes.subspan((peak_row - 3 + i) * stride, stride);
        CHECK(std::ranges::equal(row.subspan(636 / 4 * 5, 10), std::as_bytes(std::span(groups[i]))));
        CHECK(std::ranges::all_of(row.last(padding), [](std::byte b) { return b == std::byte{0xe3}; }));
    }
}

struct Generation {
    data::RunId run;
    data::GenerationId capture, producer;
    uint64_t offset{};
    const data::ProducerStreamId producer_stream{{"packed-alg02"}};
    data::BundleKey bundle() const {
        return {run, {bundle_counter + offset}};
    }
    data::EvidenceKey evidence() const {
        return {run, {4 + offset}};
    }
    data::FrameSetKey frameset() const {
        return {run, {{{"set"}}, capture}, frameset_counter + offset};
    }
    data::SourceFrameKey frame() const {
        return {storage_fixture::camera, {{{"stream"}}, capture}, native_frame + offset};
    }
    data::TriggerKey trigger() const {
        return {run, storage_fixture::controller, capture, {2 + offset}};
    }
    data::StepInstance step() const {
        return {run, 0, 9};
    }
    data::ObservationKey observation() const {
        return {run, producer_stream, producer, {bundle_counter + offset}};
    }
    data::RuntimeTimestamp host(int64_t n) const {
        auto t = storage_fixture::host(n);
        t.clock.generation = capture;
        return t;
    }
    data::SemanticTimestamp sensor(int64_t n) const {
        auto t = storage_fixture::sensor(n);
        t.clock.generation = capture;
        return t;
    }
};

void tag_image(data::AcquisitionBundle &image, const Graph &g, const Generation &identity,
               const data::EvidenceKey &predecessor) {
    image.key = identity.bundle();
    image.published = identity.host(1'000'000 + identity.offset * 1'000'000);
    auto &e = image.evidence;
    e.key = identity.evidence();
    e.causal_predecessors = {predecessor};
    e.step = identity.step();
    e.frameset = identity.frameset();
    e.triggers = {identity.trigger()};
    e.clock_mappings[0].source_generation = e.clock_mappings[0].target_generation = identity.capture;
    e.rig_calibration = exact(g.rig);
    auto &f = e.frames.at(0);
    f.frame = identity.frame();
    f.source_timestamp = identity.sensor(0);
    f.host_received = identity.host(9);
    const data::TimeInterval interval{identity.sensor(0), identity.sensor(10)};
    auto exposure = *f.exposure.get();
    exposure.interval = interval;
    f.exposure = exposure;
    f.camera_calibration = exact(g.left);
    f.rig_calibration = exact(g.rig);
    auto &emitter = e.emitters.at(0);
    auto command = *emitter.commanded.get();
    command.dispatched = identity.host(0);
    emitter.commanded = command;
    auto acknowledgement = *emitter.acknowledged.get();
    acknowledgement.time = identity.sensor(0);
    emitter.acknowledged = acknowledgement;
    auto observed = *emitter.observed.get();
    observed.time = identity.sensor(0);
    observed.coverage = interval;
    emitter.observed = observed;
    auto effective = *emitter.exposure_effective.at(0).state.get();
    effective.frame = f.frame;
    effective.coverage = interval;
    emitter.exposure_effective = {{f.frame, effective}};

    auto set = *image.frameset;
    auto child = *set.frames.at(0);
    set.header.sequence.value = identity.frameset().sequence;
    child.header.sequence.value = identity.frame().native_sequence;
    set.header.frame = child.header.frame = g.left.value.solution.camera.optical_frame;
    set.header.calibration = child.header.calibration = g.rig.value.revision;
    set.frames[0] = data::publish(std::move(child));
    image.frameset = data::publish(std::move(set));
}

pipeline::L1CandidateConfig extractor(const Graph &g, const Generation &identity,
                                      const artifact::ArtifactDescriptor &raw) {
    auto config = l1_fixture::subpixel_config(true);
    config.run = identity.run;
    config.camera = identity.frame().camera;
    config.stream = identity.frame().stream;
    config.producer_stream = identity.producer_stream;
    config.producer_generation = identity.producer;
    config.optical_frame = g.left.value.solution.camera.optical_frame;
    config.raw_input = {raw.id, {"org.mantis.RawCapture", 3}, raw.hash, 0};
    return config;
}

// Compare concrete references, including evidence presence, not only logical revisions.
void check_content(const data::Evidence<data::ContentReference> &actual,
                   const data::ContentReference &expected) {
    CHECK(actual.get());
    const auto &ref = *actual.get();
    CHECK(ref.id == expected.id && ref.type == expected.type);
    CHECK(ref.hash == expected.hash && ref.revision == expected.revision);
}
void check_logical(const calibration::Reference &actual, const calibration::Reference &expected) {
    CHECK(actual.id == expected.id && actual.schema_version == expected.schema_version);
    CHECK(actual.revision == expected.revision);
}
void check_calibration(const data::Evidence<data::ExactCalibrationReference> &actual,
                       const data::ExactCalibrationReference &expected) {
    CHECK(actual.get());
    check_logical(actual.get()->calibration, expected.calibration);
    check_content(actual.get()->content, *expected.content.get());
}

artifact::ArtifactDescriptor check_finalized(const artifact::Store &store, const data::ContentReference &ref,
                                             const schema::DataTypeId &type, uint64_t revision) {
    CHECK(ref.type == type && ref.revision == revision);
    auto descriptor = store.get(ref.id);
    CHECK(descriptor.state == artifact::ArtifactState::finalized);
    CHECK(descriptor.id == ref.id && descriptor.type.name == type.name &&
          descriptor.type.schema_version == type.version);
    CHECK(ref.hash.get() && descriptor.hash == *ref.hash.get() && !descriptor.hash.hex.empty());
    return descriptor;
}

} // namespace packed_recorded_fixture
