#pragma once
#include <mantis/l1_recorded.hpp>

inline thread_local CancellationToken *recorded_cancel_at_commit{};
inline void recorded_checkpoint(const Id &, artifact::Store::ObservationWriteStage stage) {
    if (recorded_cancel_at_commit && stage == artifact::Store::ObservationWriteStage::journal_published)
        recorded_cancel_at_commit->cancel();
}

data::AcquisitionBundle recorded_image(const Graph &g, const l1_fixture::Signal &signal) {
    const auto &camera = g.left.value.solution.camera;
    auto bundle = l1_fixture::image_bundle(false, camera.image_width, camera.image_height, signal);
    bundle.evidence.frames[0].camera_calibration = exact(g.left);
    bundle.evidence.frames[0].rig_calibration = exact(g.rig);
    bundle.evidence.rig_calibration = exact(g.rig);
    auto set = *bundle.frameset;
    auto image = *set.frames[0];
    set.header.frame = image.header.frame = camera.optical_frame;
    set.header.calibration = image.header.calibration = g.rig.value.revision;
    set.frames[0] = data::publish(std::move(image));
    bundle.frameset = data::publish(std::move(set));
    return bundle;
}

a3::RecordedRequest record_image(artifact::Store &s, const Graph &g, const data::AcquisitionBundle &bundle) {
    constexpr uint64_t selected_record = 1; // After a startup bundle with no FrameSet.
    auto raw = s.begin_projected_capture(storage_fixture::header());
    s.append_bundle(raw, storage_fixture::bundle(0));
    s.append_bundle(raw, bundle);
    s.record_run_outcome(raw, {2, storage_fixture::outcome(data::RecordedRunDisposition::completed,
                                                           data::AcquisitionReason::none)});
    auto descriptor = s.finalize(raw);
    auto config = l1_fixture::subpixel_config();
    config.optical_frame = g.left.value.solution.camera.optical_frame;
    config.roi = pipeline::L1CandidateConfig::Roi{637, 396, 3, 9};
    config.raw_input = {raw, {"org.mantis.RawCapture", 3}, descriptor.hash, 0};
    auto p = plane(g);
    p.line = {config.emitter, config.pattern, config.pattern_revision, config.line};
    p.cameras[0].component = config.camera;
    return {config,
            selected_record,
            get(a3::create_plane(s, p)),
            get(a3::create_policy(s, policy(a3::SourceContract::alg02_subpixel_1))),
            exact(g.left),
            exact(g.rig),
            a3::Selection::recorded_effective};
}

size_t recorded_result_count(const artifact::Store &s) {
    const auto descriptors = s.list();
    return std::count_if(descriptors.begin(), descriptors.end(),
                         [](const auto &d) { return d.type.name == a3::result_type.name; });
}

// Refusals must not create a metric result. Later failures can retain a valid
// observation with its own receipt, per the nontransactional artifact contract.
std::vector<Id> refuse_recorded(const std::shared_ptr<artifact::Store> &store, const a3::RecordedRequest &bad,
                                Status expected, std::string_view diagnostic,
                                const std::source_location &caller = std::source_location::current()) {
    auto before = recorded_result_count(*store);
    std::set<Id> existing;
    for (const auto &d : store->list())
        existing.insert(d.id);
    auto result = a3::process_recorded(store, bad);
    rejects(result, expected, caller);
    CHECK(result.error().message.find(diagnostic) != std::string::npos);
    CHECK(recorded_result_count(*store) == before);
    std::vector<Id> retained;
    for (const auto &d : store->list()) {
        if (!existing.contains(d.id)) {
            CHECK(d.type.name == schema::laser_observation.name);
            CHECK(d.state == artifact::ArtifactState::finalized);
            CHECK(result.error().message.find("ALG03 artifact " + d.id.value +
                                              "; observed state FINALIZED") != std::string::npos);
            retained.push_back(d.id);
        }
    }
    return retained;
}

void recorded_no_line(const std::shared_ptr<artifact::Store> &store, const Graph &g) {
    auto &s = *store;
    // Uniform grey at the normal 12 DN background: no stripe, not a disabled
    // extractor. Use the same ALG-02 settings and exposure evidence as the stripe case.
    const auto bundle = recorded_image(g, [](uint32_t, uint32_t) -> uint16_t { return 12; });
    const auto request = record_image(s, g, bundle);
    const auto &raw = request.extractor.raw_input;
    const auto descriptor = s.get(raw.id);
    CHECK(descriptor.state == artifact::ArtifactState::finalized);
    const data::ContentReference reread_raw{
        descriptor.id, {descriptor.type.name, descriptor.type.schema_version}, descriptor.hash, 0};
    CHECK(a3::detail::same(raw, reread_raw));
    const auto run_outcome = s.run_outcome(raw.id);
    CHECK(run_outcome && run_outcome->disposition == data::RecordedRunDisposition::completed);
    const auto selected = s.bundle(raw.id, request.record);
    const auto selected_bytes = storage_fixture::encode(selected);
    CHECK(selected_bytes == storage_fixture::encode(bundle));
    const auto &frame = selected.evidence.frames.at(0);

    const auto refs = get(a3::process_recorded(store, request));
    const auto observation_descriptor = s.get(refs.observation.id);
    CHECK(observation_descriptor.state == artifact::ArtifactState::finalized);
    CHECK(observation_descriptor.chunks == 1);
    CHECK(observation_descriptor.type.name == "org.mantis.LaserObservation");
    CHECK(observation_descriptor.type.schema_version == 1);
    const data::ContentReference reread_observation{observation_descriptor.id, schema::laser_observation,
                                                    observation_descriptor.hash, 1};
    CHECK(a3::detail::same(refs.observation, reread_observation));
    const auto observation = s.laser_observation(refs.observation.id);
    CHECK(observation.disposition == data::ObservationDisposition::success);
    CHECK(observation.sample_count == 0);
    CHECK(observation.attributes.empty());
    CHECK(observation.context.producer.implementation.value == "org.mantis.l1-subpixel");
    CHECK(observation.diagnostic == "profiles=3;missing=3;rejected=0;saturated=0");
    const auto &context = observation.context;
    CHECK(context.raw_input.get() && a3::detail::same(*context.raw_input.get(), reread_raw));
    CHECK(context.exact_inputs.size() == 1 && a3::detail::same(context.exact_inputs[0], reread_raw));
    CHECK(context.bundle.get() && *context.bundle.get() == selected.key);
    CHECK(context.frameset.get() && context.frameset == selected.evidence.frameset);
    CHECK(context.acquisition_evidence.get() && *context.acquisition_evidence.get() == selected.evidence.key);
    CHECK(context.source.frame == frame.frame);
    CHECK(context.source.camera_calibration.get() && frame.camera_calibration.get() &&
          a3::detail::same(*context.source.camera_calibration.get(), *frame.camera_calibration.get()));
    CHECK(context.source.rig_calibration.get() && frame.rig_calibration.get() &&
          a3::detail::same(*context.source.rig_calibration.get(), *frame.rig_calibration.get()));

    const auto result_descriptor = s.get(refs.result.id);
    CHECK(result_descriptor.state == artifact::ArtifactState::finalized);
    CHECK(result_descriptor.type.name == "org.mantis.Alg03Result");
    CHECK(result_descriptor.type.schema_version == 1);
    CHECK(result_descriptor.hash == refs.result.hash);
    const auto result = get(a3::load_result(s, refs.result.id));
    // The manifest is the entire tensor: no source/point rows or XYZ column.
    CHECK(result->type == schema::tensor);
    CHECK(result->attributes.size() == 1);
    CHECK(result->attributes[0].descriptor.name == "org.mantis.alg03.manifest");
    const auto m = manifest(*result);
    CHECK(m.at("layout_version") == 1);
    CHECK(m.at("outcome") == "empty_input");
    CHECK(m.at("source_disposition") == "success");
    CHECK(m.at("sample_count") == 0 && m.at("accepted_count") == 0 && m.at("rejected_count") == 0);
    CHECK(m.at("source_frame") == a3::detail::frame_key(frame.frame));
    CHECK(m.at("source_key") == a3::detail::observation_key(observation.key));
    CHECK(m.at("camera_role") == frame.camera_role);
    CHECK(m.at("image_width") == frame.width && m.at("image_height") == frame.height);
    const auto &recorded = m.at("recorded_calibration");
    CHECK(recorded.at("camera").at("presence") == "established");
    CHECK(recorded.at("rig").at("presence") == "established");
    CHECK(recorded.at("camera").at("value") == a3::detail::exact(exact(g.left)));
    CHECK(recorded.at("rig").at("value") == a3::detail::exact(exact(g.rig)));
    const auto &inputs = m.at("inputs");
    CHECK(inputs.at("observation") == a3::detail::content(reread_observation));
    CHECK(inputs.at("calibration_selection") == "recorded_effective");
    CHECK(inputs.at("camera") == a3::detail::exact(request.camera));
    CHECK(inputs.at("rig") == a3::detail::exact(request.rig));
    CHECK(inputs.at("plane") == a3::detail::content(request.plane));
    CHECK(inputs.at("policy") == a3::detail::content(request.policy));

    // Same pixels and extraction; a mismatched requested rig revision must still refuse
    // after observation persistence, not turn the binding error into empty_input.
    auto stale = request;
    ++stale.rig.calibration.revision;
    const auto retained =
        refuse_recorded(store, stale, Status::incompatible, "Calibration reference version mismatch");
    CHECK(retained.size() == 1);
    CHECK(l1_fixture::encode(s.laser_observation(retained[0])) == l1_fixture::encode(observation));
    CHECK(s.get(raw.id).hash == descriptor.hash);
    CHECK(storage_fixture::encode(s.bundle(raw.id, request.record)) == selected_bytes);
}

void recorded_metric(const std::shared_ptr<artifact::Store> &store, const Graph &g) {
    auto &s = *store;
    const auto bundle = recorded_image(g, [](uint32_t, uint32_t y) -> uint16_t {
        return y == 399 ? 155 : y == 400 ? 179 : y == 401 ? 171 : y == 398 ? 99 : y == 402 ? 131 : 12;
    });
    const auto request = record_image(s, g, bundle);
    const auto &config = request.extractor;
    const auto &raw = config.raw_input.id;
    const auto descriptor = s.get(raw);
    const auto raw_bytes = storage_fixture::encode(s.bundle(raw, 1));
    std::string previous;
    data::Published previous_result;
    for (unsigned pass = 0; pass < 2; ++pass) {
        auto refs = get(a3::process_recorded(store, request));
        auto observation = s.laser_observation(refs.observation.id);
        CHECK(observation.sample_count == 3);
        CHECK(observation.context.bundle.get() && *observation.context.bundle.get() == bundle.key);
        CHECK(a3::detail::same(*observation.context.raw_input.get(), config.raw_input));
        CHECK(a3::detail::same(*observation.context.source.camera_calibration.get(), exact(g.left)));
        CHECK(a3::detail::same(*observation.context.source.rig_calibration.get(), exact(g.rig)));
        auto bytes = l1_fixture::encode(observation);
        if (pass)
            CHECK(bytes == previous);
        previous = bytes;
        auto result = get(a3::load_result(s, refs.result.id));
        CHECK(manifest(*result).at("accepted_count") == 3);
        CHECK(manifest(*result).at("inputs").at("observation") == a3::detail::content(refs.observation));
        CHECK(manifest(*result).at("inputs").at("camera") == a3::detail::exact(exact(g.left)));
        CHECK(manifest(*result).at("inputs").at("rig") == a3::detail::exact(exact(g.rig)));
        CHECK(manifest(*result).at("layout_version") == 1);
        CHECK(manifest(*result).at("reason_version") == 1);
        CHECK(manifest(*result).at("diagnostic_version") == 1);
        if (pass) {
            CHECK(result->attributes.size() == previous_result->attributes.size());
            for (size_t i = 1; i < result->attributes.size(); ++i) {
                auto a = get(result->attributes[i].buffer.map_read());
                auto b = get(previous_result->attributes[i].buffer.map_read());
                CHECK(a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin()));
            }
            auto current_manifest = manifest(*result), old_manifest = manifest(*previous_result);
            // New immutable observation IDs are the only run-varying member.
            current_manifest["inputs"]["observation"]["id"] = "distinct-derived-id";
            old_manifest["inputs"]["observation"]["id"] = "distinct-derived-id";
            CHECK(current_manifest == old_manifest);
        }
        for (size_t row = 0; row < 3; ++row) {
            auto expected = oracle(g, float(637 + row), 400.25f);
            if (!pass) {
                std::cout << "recorded row=" << row << " raw_hash=" << descriptor.hash.hex;
                for (size_t axis = 0; axis < 3; ++axis)
                    std::cout << " position_mm=" << std::setprecision(17)
                              << value<double>(*result, "position", row, axis)
                              << " expected_mm=" << expected[axis];
                std::cout << '\n';
            }
            for (size_t axis = 0; axis < 3; ++axis) {
                CHECK(std::abs(value<double>(*result, "position", row, axis) - expected[axis]) < 2e-8);
                if (pass)
                    CHECK(value<double>(*result, "position", row, axis) ==
                          value<double>(*previous_result, "position", row, axis));
            }
        }
        previous_result = result;
    }
    auto result_count = [&] { return recorded_result_count(s); };
    auto refuse = [&](const a3::RecordedRequest &bad, Status expected, std::string_view diagnostic,
                      const std::source_location &caller = std::source_location::current()) {
        refuse_recorded(store, bad, expected, diagnostic, caller);
    };
    auto bad = request;
    bad.extractor.raw_input.id = {"missing-recorded-source"};
    refuse(bad, Status::not_found, "Artifact");
    bad = request;
    bad.extractor.raw_input.hash = Hash{"fnv1a64", "0000000000000000"};
    refuse(bad, Status::incompatible, "hash mismatch");
    bad = request;
    bad.record = 99;
    refuse(bad, Status::not_found, "Selected bundle not found");
    bad = request;
    bad.record = 0;
    refuse(bad, Status::incompatible, "not a captured image bundle");
    bad = request;
    auto missing_camera = *bad.camera.content.get();
    missing_camera.id = {"missing-camera-calibration"};
    bad.camera.content = missing_camera;
    refuse(bad, Status::not_found, "Artifact");
    bad = request;
    bad.camera = exact(g.right);
    refuse(bad, Status::incompatible, "camera/component");
    bad = request;
    bad.rig.calibration.revision += 1;
    refuse(bad, Status::incompatible, "Calibration reference version mismatch");
    bad = request;
    bad.policy.id = {"missing-policy"};
    refuse(bad, Status::not_found, "Artifact");
    CancellationToken cancelled;
    cancelled.cancel();
    auto before_cancel = result_count();
    rejects(a3::process_recorded(store, request, cancelled), Status::cancelled);
    CHECK(result_count() == before_cancel);
    // Real entered-state cancellation after durable observation journaling.
    CancellationToken committing;
    std::set<Id> before_commit;
    for (const auto &d : s.list())
        before_commit.insert(d.id);
    struct CommitCancellation {
        explicit CommitCancellation(CancellationToken &t) {
            recorded_cancel_at_commit = &t;
        }
        ~CommitCancellation() {
            recorded_cancel_at_commit = nullptr;
        }
    };
    {
        CommitCancellation gate(committing);
        rejects(a3::process_recorded(store, request, committing), Status::cancelled);
    }
    CHECK(committing.cancelled());
    CHECK(result_count() == before_cancel);
    size_t reservations{};
    for (const auto &d : s.list()) {
        if (before_commit.contains(d.id))
            continue;
        ++reservations;
        CHECK(d.type.name == schema::laser_observation.name);
        CHECK(d.state == artifact::ArtifactState::recoverable);
        auto recovered = s.recover(d.id);
        CHECK(recovered.state == artifact::ArtifactState::finalized);
        CHECK(l1_fixture::encode(s.laser_observation(d.id)) == previous);
    }
    CHECK(reservations == 1);
    // Flip a byte in the actual segment, not a mock reader or metadata row.
    auto object = s.object_path(raw);
    struct RestoreByte {
        std::filesystem::path path;
        char original{};
        explicit RestoreByte(std::filesystem::path p) : path(std::move(p)) {
            std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
            CHECK(file.get(original));
            file.seekp(0);
            file.put(char(original ^ 1));
            file.flush();
            CHECK(file.good());
        }
        ~RestoreByte() {
            std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
            file.put(original);
        }
    };
    {
        RestoreByte fault(object);
        refuse(request, Status::corrupt, "");
    }
    auto restored = get(a3::process_recorded(store, request));
    CHECK(l1_fixture::encode(s.laser_observation(restored.observation.id)) == previous);
    CHECK(manifest(*get(a3::load_result(s, restored.result.id))).at("accepted_count") == 3);
    CHECK(s.get(raw).hash == descriptor.hash);
    CHECK(storage_fixture::encode(s.bundle(raw, 1)) == raw_bytes);
}
