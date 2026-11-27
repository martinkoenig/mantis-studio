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
uint16_t signal(uint32_t x, uint32_t y) {
    const auto d = static_cast<int>(y) - static_cast<int>(peak_row);
    if (x < first_column || x >= first_column + profiles || d < -2 || d > 2)
        return 48;
    return static_cast<uint16_t>(723 + static_cast<int>(x - first_column) + 17 * d - 34 * d * d);
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
    const data::ProducerStreamId producer_stream{{"packed-alg02"}};
    data::BundleKey bundle() const {
        return {run, {bundle_counter}};
    }
    data::EvidenceKey evidence() const {
        return {run, {4}};
    }
    data::FrameSetKey frameset() const {
        return {run, {{{"set"}}, capture}, frameset_counter};
    }
    data::SourceFrameKey frame() const {
        return {storage_fixture::camera, {{{"stream"}}, capture}, native_frame};
    }
    data::TriggerKey trigger() const {
        return {run, storage_fixture::controller, capture, {2}};
    }
    data::StepInstance step() const {
        return {run, 0, 9};
    }
    data::ObservationKey observation() const {
        return {run, producer_stream, producer, {bundle_counter}};
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

struct Recording {
    a3::RecordedRequest request;
    artifact::ArtifactDescriptor raw;
    data::ProjectedCaptureHeader header;
    data::ProjectedCaptureOutcome outcome;
    data::AcquisitionBundle startup, selected;
};

Recording record(artifact::Store &store, const Graph &g, const Generation &identity,
                 data::AcquisitionBundle image, const data::ContentReference &policy_ref) {
    auto header = storage_fixture::header();
    header.run = identity.run;
    header.generation = identity.capture;
    auto startup = storage_fixture::bundle(0);
    startup.key = {identity.run, {bundle_counter - 1}};
    startup.evidence.key = {identity.run, {0}};
    startup.published = identity.host(0);
    auto outcome =
        storage_fixture::outcome(data::RecordedRunDisposition::completed, data::AcquisitionReason::none);
    outcome.run = identity.run;
    outcome.generation = identity.capture;

    image.key = identity.bundle();
    image.published = identity.host(1'000'000);
    auto &e = image.evidence;
    e.key = identity.evidence();
    e.causal_predecessors = {startup.evidence.key};
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

    const auto raw_id = store.begin_projected_capture(header);
    store.append_bundle(raw_id, startup);
    store.append_bundle(raw_id, image);
    data::ProjectedCaptureOutcome final_outcome{2, outcome};
    store.record_run_outcome(raw_id, final_outcome);
    auto raw = store.finalize(raw_id);
    auto config = l1_fixture::subpixel_config(true);
    config.run = identity.run;
    config.camera = identity.frame().camera;
    config.stream = identity.frame().stream;
    config.producer_stream = identity.producer_stream;
    config.producer_generation = identity.producer;
    config.optical_frame = g.left.value.solution.camera.optical_frame;
    config.roi = pipeline::L1CandidateConfig::Roi{first_column, peak_row - 4, profiles, 9};
    config.raw_input = {raw_id, {"org.mantis.RawCapture", 3}, raw.hash, 0};
    const auto plane_ref = get(a3::create_plane(
        store,
        plane(g, config.camera, {config.emitter, config.pattern, config.pattern_revision, config.line})));
    return {{config, selected_record, plane_ref, policy_ref, exact(g.left), exact(g.rig),
             a3::Selection::recorded_effective},
            std::move(raw),
            std::move(header),
            std::move(final_outcome),
            std::move(startup),
            std::move(image)};
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

void check_recording(const std::shared_ptr<artifact::Store> &store, const Recording &r,
                     const Generation &identity) {
    const auto &raw = r.request.extractor.raw_input;
    const auto d = check_finalized(*store, raw, {"org.mantis.RawCapture", 3}, 0);
    CHECK(d.id == r.raw.id && d.hash == r.raw.hash && d.bytes == r.raw.bytes && d.chunks == r.raw.chunks);
    artifact::BundleCaptureReader reader(store, raw.id);
    const auto &header = reader.header();
    CHECK(header.run == identity.run && header.generation == identity.capture);
    CHECK(storage_fixture::encode(header) == storage_fixture::encode(r.header));
    const auto &outcome = reader.final_outcome();
    CHECK(outcome && outcome->run == identity.run && outcome->generation == identity.capture);
    CHECK(outcome->disposition == data::RecordedRunDisposition::completed);
    CHECK(storage_fixture::encode(data::ProjectedCaptureOutcome{2, *outcome}) ==
          storage_fixture::encode(r.outcome));
    const auto startup = reader.next();
    CHECK(startup && storage_fixture::encode(*startup) == storage_fixture::encode(r.startup));
    const auto selected = reader.next();
    CHECK(selected && storage_fixture::encode(*selected) == storage_fixture::encode(r.selected));
    CHECK(!reader.next());
}

struct Readback {
    a3::RecordedResult refs;
    data::LaserObservation observation;
    data::Published result;
};

Readback replay_and_check(const std::shared_ptr<artifact::Store> &store, const Graph &g, const Recording &r,
                          const Generation &identity) {
    const auto &request = r.request;
    const auto &raw = request.extractor.raw_input;
    const auto &camera = g.left.value.solution.camera;
    const auto camera_ref = exact(g.left), rig_ref = exact(g.rig);
    check_finalized(*store, request.plane, {"org.mantis.Alg03DeclaredPlane", 1}, 1);
    check_finalized(*store, request.policy, {"org.mantis.Alg03Policy", 1}, 1);
    const auto refs = get(a3::process_recorded(store, request));
    const auto od = check_finalized(*store, refs.observation, {"org.mantis.LaserObservation", 1}, 1);
    const auto rd = store->get(refs.result.id);
    CHECK(rd.state == artifact::ArtifactState::finalized && rd.id == refs.result.id);
    CHECK(rd.type.name == "org.mantis.Alg03Result" && rd.type.schema_version == 1);
    CHECK(rd.hash == refs.result.hash && !rd.hash.hex.empty());
    CHECK(od.chunks == 1 && rd.chunks == 1);
    CHECK((od.provenance.inputs == std::vector<Id>{r.raw.id, g.left.descriptor.id, g.rig.descriptor.id}));
    CHECK((rd.provenance.inputs == std::vector<Id>{od.id, g.left.descriptor.id, g.rig.descriptor.id,
                                                   request.plane.id, request.policy.id}));
    check_logical(rd.provenance.calibration, rig_ref.calibration);

    auto o = store->laser_observation(od.id);
    const auto &c = o.context;
    const auto key = identity.observation();
    CHECK(o.key.run_id == key.run_id && o.key.producer_stream == key.producer_stream);
    CHECK(o.key.producer_generation == key.producer_generation && o.key.sequence == key.sequence);
    check_content(c.raw_input, raw);
    CHECK(c.exact_inputs.size() == 1);
    check_content(c.exact_inputs[0], raw);
    CHECK(c.bundle == data::Evidence{identity.bundle()});
    CHECK(c.acquisition_evidence == data::Evidence{identity.evidence()});
    CHECK(c.frameset == data::Evidence{identity.frameset()});
    CHECK(c.source.frame == identity.frame());
    CHECK(c.correlation.get() && c.correlation.get()->step == identity.step());
    CHECK(data::same_program_reference(c.correlation.get()->program, r.header.program.identity));
    CHECK(c.triggers == std::vector{identity.trigger()});
    CHECK(c.source.source_timestamp.get() &&
          c.source.source_timestamp.get()->clock.generation == identity.capture);
    CHECK(c.source.host_received.get() && c.source.host_received.get()->clock.generation == identity.capture);
    CHECK(c.emitter_evidence.size() == 1 && c.emitter_evidence[0].exposure_effective.size() == 1);
    const auto &effective = c.emitter_evidence[0].exposure_effective[0];
    CHECK(effective.frame == identity.frame() && effective.state.get());
    CHECK(effective.state.get()->frame == identity.frame());
    CHECK(effective.state.get()->scope == data::EvidenceScope::optical_emission);
    CHECK(effective.state.get()->state == data::EmitterState::on);
    CHECK(effective.state.get()->coverage.start.clock.generation == identity.capture);
    CHECK(effective.state.get()->coverage.end.clock.generation == identity.capture);
    CHECK(c.source.camera_role == camera.role && c.source.width == camera.image_width &&
          c.source.height == camera.image_height);
    CHECK(c.source.original_calibration.presence() == data::Presence::unavailable);
    check_calibration(c.source.camera_calibration, camera_ref);
    check_calibration(c.source.rig_calibration, rig_ref);
    CHECK(c.producer.implementation.value == "org.mantis.l1-subpixel");
    CHECK(o.disposition == data::ObservationDisposition::success && o.sample_count == profiles);
    CHECK(o.diagnostic == "profiles=4;missing=0;rejected=0;saturated=0" && c.packet_quality_flags == 0);

    auto result = get(a3::load_result(*store, rd.id));
    const auto m = manifest(*result);
    CHECK(result->type == schema::tensor && result->attributes.size() == 17);
    check_logical(result->header.calibration, rig_ref.calibration);
    CHECK(result->header.frame.id == g.rig.value.solution.config.rig_frame.id);
    CHECK(result->header.frame.name == g.rig.value.solution.config.rig_frame.name);
    const auto &source_key = m.at("source_key");
    CHECK(source_key.at("run_id").at("presence") == "established");
    CHECK(source_key.at("run_id").at("value").at("id").at("value") == identity.run.id.value);
    CHECK(source_key.at("producer_stream").at("id").at("value") == identity.producer_stream.id.value);
    CHECK(source_key.at("producer_generation").at("id").at("value") == identity.producer.id.value);
    CHECK(source_key.at("sequence").at("value") == bundle_counter);
    const auto &source_frame = m.at("source_frame");
    CHECK(source_frame.at("camera").at("id").at("value") == identity.frame().camera.id.value);
    CHECK(source_frame.at("stream").at("id").at("id").at("value") == identity.frame().stream.id.id.value);
    CHECK(source_frame.at("stream").at("generation").at("id").at("value") == identity.capture.id.value);
    CHECK(source_frame.at("native_sequence") == native_frame);
    CHECK(m.at("camera_role") == camera.role && m.at("image_width") == camera.image_width &&
          m.at("image_height") == camera.image_height);
    const auto &bindings = m.at("inputs");
    CHECK(bindings.at("observation") == a3::detail::content(refs.observation));
    CHECK(bindings.at("camera") == a3::detail::exact(camera_ref));
    CHECK(bindings.at("rig") == a3::detail::exact(rig_ref));
    CHECK(bindings.at("plane") == a3::detail::content(request.plane));
    CHECK(bindings.at("policy") == a3::detail::content(request.policy));
    CHECK(bindings.at("calibration_selection") == "recorded_effective");
    const auto &recorded = m.at("recorded_calibration");
    CHECK(recorded.at("camera").at("presence") == "established");
    CHECK(recorded.at("camera").at("value") == a3::detail::exact(camera_ref));
    CHECK(recorded.at("rig").at("presence") == "established");
    CHECK(recorded.at("rig").at("value") == a3::detail::exact(rig_ref));
    CHECK(recorded.at("original").at("presence") == "unavailable");
    CHECK(m.at("layout_version") == 1 && m.at("source_disposition") == "success");
    CHECK(m.at("sample_count") == profiles && m.at("accepted_count") == profiles &&
          m.at("rejected_count") == 0);
    CHECK(m.at("outcome") == "all_accepted");
    CHECK(m.at("origin") == "synthetic" && m.at("validity_scope") == "declared_geometry_numeric_only");

    long double worst_error_mm = 0;
    for (size_t row = 0; row < profiles; ++row) {
        const auto u = static_cast<float>(first_column + row);
        CHECK(l1_fixture::value<float>(o, data::laser::source_pixel, row) == u);
        CHECK(l1_fixture::value<float>(o, data::laser::source_pixel, row, 1) == vertex);
        CHECK(l1_fixture::value<uint8_t>(o, l1::refinement_valid, row) == 1);
        CHECK(l1_fixture::value<uint32_t>(o, l1::curvature, row) == 68);
        CHECK(l1_fixture::value<uint32_t>(o, l1::peak, row) == 723 + row);
        CHECK(l1_fixture::value<uint32_t>(o, l1::width, row) == 5);
        CHECK(l1_fixture::value<uint32_t>(o, data::laser::quality_flags, row) == 0);
        CHECK(value<uint32_t>(*result, "source_row", row) == row);
        CHECK(value<float>(*result, "source_pixel", row) == u);
        CHECK(value<float>(*result, "source_pixel", row, 1) == vertex);
        CHECK(value<uint8_t>(*result, "accepted", row) == 1);
        CHECK(value<uint32_t>(*result, "reason_bits", row) == 0);
        const auto expected = oracle(g, u, vertex);
        const auto error = std::hypot(value<double>(*result, "position", row, 0) - expected[0],
                                      value<double>(*result, "position", row, 1) - expected[1],
                                      value<double>(*result, "position", row, 2) - expected[2]);
        CHECK(error < 2e-8L);
        worst_error_mm = std::max(worst_error_mm, error);
    }
    // The forward solve terminates at <1e-14 px in long double. Use the existing
    // near-centre RAW8 geometry/policy budget in L2, not per axis: this allows
    // binary64 inverse residual/roundoff, not a change in localization.
    std::cout << identity.run.id.value << " rig_revision=" << rig_ref.calibration.revision
              << " max_L2_error_mm=" << worst_error_mm << '\n';
    return {refs, std::move(o), std::move(result)};
}

void same_rows(const std::vector<data::Attribute> &a, const std::vector<data::Attribute> &b, size_t start) {
    CHECK(a.size() == b.size());
    for (size_t i = start; i < a.size(); ++i) {
        CHECK(a[i].descriptor.name == b[i].descriptor.name);
        CHECK(std::ranges::equal(get(a[i].buffer.map_read()), get(b[i].buffer.map_read())));
    }
}

void run(const std::shared_ptr<artifact::Store> &store, const Graph &old_geometry) {
    const Generation first{{{"packed-run-1"}}, {{"packed-capture-1"}}, {{"packed-producer-1"}}},
        second{{{"packed-run-2"}}, {{"packed-capture-2"}}, {{"packed-producer-2"}}};
    auto &s = *store;
    const Id device{"packed-synthetic-device"};
    const auto &camera = old_geometry.left.value.solution.camera;
    const auto image =
        l1_fixture::image_bundle(true, camera.image_width, camera.image_height, signal, padding);
    const auto policy_ref = get(a3::create_policy(s, policy(a3::SourceContract::alg02_subpixel_1)));
    get(ca::activate_rig_calibration(s, device, old_geometry.rig.descriptor.id));
    const auto old_recording = record(s, old_geometry, first, image, policy_ref);
    check_packed_bytes(s.bundle(old_recording.raw.id, selected_record), camera);
    const auto old = replay_and_check(store, old_geometry, old_recording, first);

    // Publish actual next revisions of the same logical series. Reuse the
    // solutions, not the content references: the numerical model is invariant.
    auto revised = old_geometry;
    revised.left = get(ca::create_camera_calibration(
        s, revised.left.value.solution, revised.dataset.reference(), revised.target.reference(),
        revised.left.value.implementation, revised.left.value.revision.id));
    revised.rig = get(ca::create_rig_calibration(s, revised.rig.value.solution, revised.dataset.reference(),
                                                 revised.target.reference(), revised.left.reference(),
                                                 revised.right.reference(), revised.rig.value.implementation,
                                                 revised.rig.value.revision.id));
    const auto check_revision = [&](const auto &v1, const auto &v2, const schema::DataTypeId &type) {
        const auto a = exact(v1), b = exact(v2);
        CHECK(a.calibration.id == b.calibration.id && a.calibration.id != a.content.get()->id);
        CHECK(a.calibration.schema_version == 1 && b.calibration.schema_version == 1);
        CHECK(a.calibration.revision == 1 && b.calibration.revision == 2);
        CHECK(a.content.get()->id != b.content.get()->id && a.content.get()->hash != b.content.get()->hash);
        check_finalized(s, *a.content.get(), type, 1);
        check_finalized(s, *b.content.get(), type, 2);
    };
    check_revision(old_geometry.left, revised.left, {"org.mantis.CameraCalibration", 1});
    check_revision(old_geometry.rig, revised.rig, {"org.mantis.RigCalibration", 1});
    get(ca::activate_rig_calibration(s, device, revised.rig.descriptor.id));
    const auto active = s.active_calibration(device);
    CHECK(active);
    check_logical(active->reference, revised.rig.value.revision);
    CHECK(active->artifact.id == revised.rig.descriptor.id &&
          active->artifact.hash == revised.rig.descriptor.hash);

    const auto new_recording = record(s, revised, second, image, policy_ref);
    const auto current = replay_and_check(store, revised, new_recording, second);
    CHECK(old.observation.key.run_id != current.observation.key.run_id);
    CHECK(old.observation.key.producer_generation != current.observation.key.producer_generation);
    const auto &old_frame = old.observation.context.source.frame;
    const auto &new_frame = current.observation.context.source.frame;
    CHECK(old_frame.camera == new_frame.camera && old_frame.stream.id == new_frame.stream.id);
    CHECK(old_frame.native_sequence == new_frame.native_sequence);
    CHECK(old_frame.stream.generation != new_frame.stream.generation);
    CHECK(old.observation.key.sequence == current.observation.key.sequence);
    CHECK(old_recording.raw.id != new_recording.raw.id && old_recording.raw.hash != new_recording.raw.hash);
    CHECK(old_recording.request.plane.id != new_recording.request.plane.id);
    CHECK(old_recording.request.plane.hash != new_recording.request.plane.hash);
    CHECK(old.refs.observation.id != current.refs.observation.id &&
          old.refs.observation.hash != current.refs.observation.hash);
    CHECK(old.refs.result.id != current.refs.result.id && old.refs.result.hash != current.refs.result.hash);
    same_rows(old.observation.attributes, current.observation.attributes, 0);
    same_rows(old.result->attributes, current.result->attributes, 1);

    // The exact old request must give the same durable content after activation.
    // Verify the new IDs before removing just the observation ID from the manifest comparison.
    const auto repeated = replay_and_check(store, old_geometry, old_recording, first);
    CHECK(repeated.refs.observation.id != old.refs.observation.id &&
          repeated.refs.observation.id != current.refs.observation.id);
    CHECK(repeated.refs.result.id != old.refs.result.id && repeated.refs.result.id != current.refs.result.id);
    CHECK(repeated.refs.observation.hash == old.refs.observation.hash);
    CHECK(l1_fixture::encode(old.observation) == l1_fixture::encode(repeated.observation));
    same_rows(old.result->attributes, repeated.result->attributes, 1);
    auto before = manifest(*old.result), after = manifest(*repeated.result);
    before.at("inputs").at("observation").erase("id");
    after.at("inputs").at("observation").erase("id");
    CHECK(before == after);
    const auto still_active = s.active_calibration(device);
    CHECK(still_active && still_active->artifact.id == active->artifact.id &&
          still_active->artifact.hash == active->artifact.hash);
    check_logical(still_active->reference, active->reference);
    check_recording(store, old_recording, first);
    check_recording(store, new_recording, second);
}
} // namespace packed_recorded_fixture
