#pragma once

namespace recorded_exposure_fixture {
using namespace data;

constexpr size_t selected_index = 1; // The decoy precedes the selected image and its optical evidence.
constexpr std::array<float, 3> columns{637.f, 638.f, 639.f};
constexpr float refined_y = 400.25f; // Vertex of the inherited exact quadratic RAW8 stripe.

enum class Case { optical_on, ambiguous_source, optical_off, optical_unknown };

void insert_frame(AcquisitionBundle &b, size_t index, const CameraFrameEvidence &frame, Published image) {
    auto set = *b.frameset;
    set.frames.insert(set.frames.begin() + index, std::move(image));
    b.frameset = publish(std::move(set));
    b.evidence.frames.insert(b.evidence.frames.begin() + index, frame);
    auto &effective = b.evidence.emitters.at(0).exposure_effective;
    auto on = *effective.at(0).state.get();
    on.frame = frame.frame;
    effective.insert(effective.begin() + index, {frame.frame, on});
}

AcquisitionBundle matched_bundle(const Graph &g, ProjectedCaptureHeader &header) {
    auto b = recorded_image(g, recorded_stripe);
    const auto &camera = g.right.value.solution.camera;
    const auto flat = l1_fixture::image_bundle(false, camera.image_width, camera.image_height,
                                               [](uint32_t, uint32_t) -> uint16_t { return 12; });
    auto frame = flat.evidence.frames.at(0);
    frame.frame.camera = {{"optical-decoy"}};
    frame.frame.stream.id = {{"optical-decoy-stream"}};
    frame.camera_role = camera.role;
    frame.camera_calibration = exact(g.right);
    frame.rig_calibration = exact(g.rig);
    auto exposure = *frame.exposure.get();
    exposure.evidence.source = frame.frame.camera;
    frame.exposure = exposure;
    auto image = *flat.frameset->frames.at(0);
    image.header.frame = camera.optical_frame;
    image.header.calibration = g.rig.value.revision;
    image.header.metadata.at("role") = frame.camera_role;
    image.header.metadata.at("component_id") = frame.frame.camera.id.value;
    image.header.metadata.at("stream_id") = frame.frame.stream.id.id.value;
    insert_frame(b, 0, frame, publish(std::move(image)));

    // A recorded diagnostic capture may retain pixels without optical agreement.
    // Declare a free-running pair gated by controller acknowledgement, not by
    // optical ON, and do not claim an unrecorded hardware trigger.
    auto &program = header.program;
    program.identity = {ProgramId{{"recorded-exposure-control"}}, Unknown{}, Unavailable{}};
    program.participants.cameras.push_back({frame.frame.camera, frame.frame.stream.id, frame.camera_role});
    program.repetitions = 1;
    program.steps.resize(1);
    auto &step = program.steps.front();
    step.capture = {CaptureMode::free_running, {storage_fixture::camera, frame.frame.camera}, std::nullopt};
    step.evidence_requirement = EvidenceRequirement::controller_acknowledged;
    step.required_scope = EvidenceScope::controller_register;
    b.evidence.participants = program.participants;
    b.evidence.program = program.identity;
    b.evidence.triggers.clear();
    return b;
}

void check_optical(const CameraEffectiveState &entry, const CameraFrameEvidence &frame, EmitterState state) {
    const auto *effective = entry.state.get();
    CHECK(entry.frame == frame.frame && effective && effective->frame == frame.frame);
    CHECK(effective->state == state && effective->scope == EvidenceScope::optical_emission);
    CHECK(effective->evidence.source == storage_fixture::emitter);
    CHECK(effective->evidence.method == EvidenceMethod::optical_sensor);
    const auto *exposure = frame.exposure.get();
    CHECK(exposure && exposure->interval.get());
    const auto &interval = *exposure->interval.get();
    const auto same_stamp = [](const SemanticTimestamp &a, const SemanticTimestamp &b) {
        return a.nanoseconds == b.nanoseconds && a.clock.domain.id == b.clock.domain.id &&
               a.clock.domain.name == b.clock.domain.name && a.clock.generation == b.clock.generation;
    };
    CHECK(same_stamp(effective->coverage.start, interval.start));
    CHECK(same_stamp(effective->coverage.end, interval.end));
}

AcquisitionBundle read_capture(const std::shared_ptr<artifact::Store> &store, const a3::RecordedRequest &r,
                               const ProjectedCaptureHeader &header) {
    artifact::BundleCaptureReader reader(store, r.extractor.raw_input.id);
    CHECK(storage_fixture::encode(reader.header()) == storage_fixture::encode(header));
    const auto &outcome = reader.final_outcome();
    CHECK(outcome && outcome->run == header.run && outcome->generation == header.generation);
    CHECK(outcome->disposition == RecordedRunDisposition::completed &&
          outcome->reason == AcquisitionReason::none);
    CHECK(r.record == 1);
    const auto startup = reader.next();
    CHECK(startup && !startup->frameset && startup->evidence.disposition == AcquisitionDisposition::startup);
    auto b = reader.next();
    CHECK(b && b->frameset && b->evidence.disposition == AcquisitionDisposition::captured);
    CHECK(!reader.next());
    return std::move(*b);
}

void check_capture(const AcquisitionBundle &b, const a3::RecordedRequest &r, Case scenario) {
    const auto &config = r.extractor;
    const auto &frames = b.evidence.frames;
    const auto &em = b.evidence.emitters.at(0);
    const auto &effective = em.exposure_effective;
    const bool ambiguous = scenario == Case::ambiguous_source;
    CHECK(frames.size() == (ambiguous ? 3u : 2u) && b.frameset->frames.size() == frames.size());
    CHECK(b.evidence.emitters.size() == 1 && em.emitter == config.emitter);
    CHECK(effective.size() == frames.size());
    CHECK(em.commanded.get() && em.commanded.get()->state == EmitterState::on);
    CHECK(em.acknowledged.get() && em.acknowledged.get()->result == AcknowledgementResult::success &&
          em.acknowledged.get()->scope == EvidenceScope::controller_register);
    CHECK(em.observed.get() && em.observed.get()->state == EmitterState::on &&
          em.observed.get()->scope == EvidenceScope::electrical_enable);
    const auto matches = [&](const CameraFrameEvidence &f) {
        return f.frame.camera == config.camera && f.frame.stream == config.stream;
    };
    const auto &selected = frames.at(selected_index);
    CHECK(matches(selected) && std::count_if(frames.begin(), frames.end(), matches) == (ambiguous ? 2 : 1));
    CHECK(frames[0].frame.camera != selected.frame.camera && frames[0].frame.stream != selected.frame.stream);
    CHECK(frames[0].frame.native_sequence == selected.frame.native_sequence);
    check_optical(effective[0], frames[0], EmitterState::on);
    for (size_t i = 0; i < frames.size(); ++i) {
        const auto &image = *b.frameset->frames.at(i);
        CHECK(image.header.sequence.value == frames[i].frame.native_sequence);
        CHECK(image.header.metadata.at("component_id") == frames[i].frame.camera.id.value);
        CHECK(image.header.metadata.at("stream_id") == frames[i].frame.stream.id.id.value);
        CHECK(image.header.metadata.at("role") == frames[i].camera_role);
    }
    if (scenario == Case::optical_unknown) {
        CHECK(effective[selected_index].frame == selected.frame);
        CHECK(effective[selected_index].state.presence() == Presence::unknown);
    } else {
        check_optical(effective[selected_index], selected,
                      scenario == Case::optical_off ? EmitterState::off : EmitterState::on);
    }
    if (ambiguous) {
        CHECK(matches(frames[2]) && frames[2].frame.native_sequence != selected.frame.native_sequence);
        check_optical(effective[2], frames[2], EmitterState::on);
    }
}

void check_derived(const std::shared_ptr<artifact::Store> &store, const Graph &g,
                   const a3::RecordedRequest &r, const AcquisitionBundle &b, bool unknown) {
    const auto before = store->list(); // Includes reservations in every state.
    const auto refs = get(a3::process_recorded(store, r));
    const auto od = store->get(refs.observation.id), rd = store->get(refs.result.id);
    CHECK(od.state == artifact::ArtifactState::finalized && rd.state == artifact::ArtifactState::finalized);
    CHECK(od.chunks == 1 && rd.chunks == 1);
    CHECK(od.type.name == "org.mantis.LaserObservation" && od.type.schema_version == 1);
    CHECK(rd.type.name == "org.mantis.Alg03Result" && rd.type.schema_version == 1);
    const ContentReference observation_ref{od.id, schema::laser_observation, od.hash, 1};
    CHECK(a3::detail::same(refs.observation, observation_ref) && refs.result.hash == rd.hash);
    CHECK(store->list().size() == before.size() + 2);
    CHECK(std::none_of(before.begin(), before.end(),
                       [&](const auto &d) { return d.id == od.id || d.id == rd.id; }));

    const auto obs = store->laser_observation(od.id);
    CHECK(obs.disposition == ObservationDisposition::success && obs.sample_count == columns.size());
    const auto &c = obs.context;
    const auto &source = b.evidence.frames.at(selected_index);
    CHECK(c.source.frame == source.frame);
    CHECK(c.raw_input.get() && a3::detail::same(*c.raw_input.get(), r.extractor.raw_input));
    CHECK(c.bundle == Evidence{b.key} && c.frameset == b.evidence.frameset);
    CHECK(c.acquisition_evidence == Evidence{b.evidence.key});
    CHECK(c.correlation.get() && same_program_reference(c.correlation.get()->program, b.evidence.program));
    CHECK(c.packet_quality_flags == (unknown ? pipeline::l1::attribution_unresolved : 0u));
    CHECK(c.emitter_evidence.size() == 1 && c.emitter_evidence[0].exposure_effective.size() == 1);
    const auto &effective = c.emitter_evidence[0].exposure_effective[0];
    if (unknown) {
        CHECK(effective.frame == source.frame && effective.state.presence() == Presence::unknown);
    } else {
        check_optical(effective, source, EmitterState::on);
    }
    // Compare the full source/exposure and emitter evidence (including clock,
    // provenance and presence) with the recorded values scoped to this frame only.
    auto expected = obs;
    expected.context.source = source;
    expected.context.emitter_evidence = b.evidence.emitters;
    expected.context.emitter_evidence[0].exposure_effective = {
        b.evidence.emitters[0].exposure_effective.at(selected_index)};
    CHECK(l1_fixture::encode(obs) == l1_fixture::encode(expected));
    CHECK(obs.emitter_dictionary == std::vector{r.extractor.emitter});
    const LineIdentity line{r.extractor.emitter, r.extractor.pattern, r.extractor.pattern_revision,
                            r.extractor.line};
    CHECK(obs.line_dictionary == std::vector{line});

    const auto result = get(a3::load_result(*store, rd.id));
    CHECK(result->type == schema::tensor && result->attributes.size() == 17);
    const auto &rig_frame = g.rig.value.solution.config.rig_frame;
    CHECK(result->header.frame.id == rig_frame.id && result->header.frame.name == rig_frame.name);
    const auto m = manifest(*result);
    CHECK(m.at("layout_version") == 1 && m.at("source_disposition") == "success");
    CHECK(m.at("inputs").at("observation") == a3::detail::content(observation_ref));
    CHECK(m.at("source_frame") == a3::detail::frame_key(source.frame));
    CHECK(m.at("source_key") == a3::detail::observation_key(obs.key));
    CHECK(m.at("outcome") == (unknown ? "all_rejected" : "all_accepted"));
    CHECK(m.at("sample_count") == columns.size());
    CHECK(m.at("accepted_count") == (unknown ? 0 : columns.size()));
    CHECK(m.at("rejected_count") == (unknown ? columns.size() : 0));
    const uint8_t valid = unknown ? 0 : 1;
    const uint32_t flags = unknown ? laser::emitter_unknown | laser::line_unknown : 0u;
    for (size_t row = 0; row < columns.size(); ++row) {
        const auto u = l1_fixture::value<float>(obs, laser::source_pixel, row, 0);
        const auto v = l1_fixture::value<float>(obs, laser::source_pixel, row, 1);
        CHECK(u == columns[row] && v == refined_y);
        CHECK(l1_fixture::value<uint8_t>(obs, pipeline::l1::refinement_valid, row) == 1);
        CHECK(l1_fixture::value<uint32_t>(obs, laser::quality_flags, row) == flags);
        CHECK(l1_fixture::value<uint8_t>(obs, laser::emitter_valid, row) == valid);
        CHECK(l1_fixture::value<uint8_t>(obs, laser::line_valid, row) == valid);
        CHECK(l1_fixture::value<uint32_t>(obs, laser::emitter_index, row) == 0);
        CHECK(l1_fixture::value<uint32_t>(obs, laser::line_index, row) == 0);
        CHECK(value<uint32_t>(*result, "source_row", row) == row);
        CHECK(value<float>(*result, "source_pixel", row, 0) == u);
        CHECK(value<float>(*result, "source_pixel", row, 1) == v);
        CHECK(value<uint32_t>(*result, "source_quality_flags", row) == flags);
        CHECK(value<uint8_t>(*result, "accepted", row) == valid);
        CHECK(value<uint32_t>(*result, "reason_bits", row) == (unknown ? a3::unresolved_attribution : 0u));
        const auto xyz = [&](size_t axis) { return value<double>(*result, "position", row, axis); };
        if (unknown) {
            CHECK(value<uint32_t>(*result, "diagnostic_valid", row) == 0);
            for (size_t axis = 0; axis < 3; ++axis) {
                auto zero = xyz(axis);
                CHECK(std::isfinite(zero) && zero == 0.0 && !std::signbit(zero));
            }
        } else {
            const auto point = oracle(g, columns[row], refined_y);
            CHECK(xyz(1) > 0.0);
            CHECK(std::hypot(xyz(0) - point[0], xyz(1) - point[1], xyz(2) - point[2]) < 2e-8L);
        }
    }
}

void run(const std::shared_ptr<artifact::Store> &store, const Graph &g) {
    auto header = storage_fixture::header();
    const auto matched = matched_bundle(g, header);
    const auto control = record_image(*store, g, matched, header);
    const auto exercise = [&](const a3::RecordedRequest &r, const AcquisitionBundle &fixture, Case scenario) {
        const auto &raw = r.extractor.raw_input;
        const auto before = store->get(raw.id);
        CHECK(before.state == artifact::ArtifactState::finalized);
        const ContentReference captured{
            before.id, {before.type.name, before.type.schema_version}, before.hash, 0};
        CHECK((captured.type == schema::DataTypeId{"org.mantis.RawCapture", 3} &&
               a3::detail::same(raw, captured)));
        const auto selected = read_capture(store, r, header);
        const auto bytes = storage_fixture::encode(selected);
        CHECK(bytes == storage_fixture::encode(fixture));
        check_capture(selected, r, scenario);

        if (scenario == Case::ambiguous_source || scenario == Case::optical_off) {
            const auto retained = refuse_recorded(store, r, Status::invalid_argument,
                                                  scenario == Case::ambiguous_source
                                                      ? "Multiple matching L1 source frames"
                                                      : "L1 target is optically OFF for this exposure");
            CHECK(retained.empty()); // Neither output may even be reserved before these L1 refusals.
        } else {
            check_derived(store, g, r, selected, scenario == Case::optical_unknown);
        }
        const auto after = store->get(raw.id);
        CHECK(after.id == before.id && after.state == before.state && after.hash == before.hash);
        CHECK(after.type.name == before.type.name && after.type.schema_version == before.type.schema_version);
        CHECK(after.chunks == before.chunks && after.bytes == before.bytes);
        CHECK(storage_fixture::encode(read_capture(store, r, header)) == bytes);
    };

    exercise(control, matched, Case::optical_on);
    for (const auto scenario : {Case::ambiguous_source, Case::optical_off, Case::optical_unknown}) {
        auto variant = matched;
        auto &effective = variant.evidence.emitters[0].exposure_effective.at(selected_index);
        if (scenario == Case::ambiguous_source) {
            auto frame = variant.evidence.frames.at(selected_index);
            ++frame.frame.native_sequence;
            auto image = *variant.frameset->frames.at(selected_index);
            image.header.sequence.value = frame.frame.native_sequence;
            insert_frame(variant, 2, frame, publish(std::move(image)));
        } else if (scenario == Case::optical_off) {
            auto off = *effective.state.get();
            off.state = EmitterState::off; // Do not change coverage, provenance, or non-optical indications.
            effective.state = off;
        } else {
            effective.state = Unknown{};
        }
        auto request = control; // Same extractor settings and exact geometry, policy and calibration refs.
        const auto raw = record_capture(*store, variant, header);
        request.extractor.raw_input.id = raw.id;
        request.extractor.raw_input.hash = raw.hash;
        exercise(request, variant, scenario);
    }
}
} // namespace recorded_exposure_fixture
