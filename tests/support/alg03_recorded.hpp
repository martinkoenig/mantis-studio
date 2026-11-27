#pragma once
#include <mantis/l1_recorded.hpp>

inline thread_local CancellationToken *recorded_cancel_at_commit{};
inline void recorded_checkpoint(const Id &, artifact::Store::ObservationWriteStage stage) {
    if (recorded_cancel_at_commit && stage == artifact::Store::ObservationWriteStage::journal_published)
        recorded_cancel_at_commit->cancel();
}

// One recorded source, including the control-only prefix; no fixture-side
// extraction or triangulation substitutes for the production composition.
void recorded_metric(artifact::Store &s, const Graph &g) {
    auto &camera = g.left.value.solution.camera;
    auto bundle = l1_fixture::image_bundle(
        false, camera.image_width, camera.image_height, [](uint32_t, uint32_t y) -> uint16_t {
            return y == 399 ? 155 : y == 400 ? 179 : y == 401 ? 171 : y == 398 ? 99 : y == 402 ? 131 : 12;
        });
    bundle.evidence.frames[0].camera_calibration = exact(g.left);
    bundle.evidence.frames[0].rig_calibration = exact(g.rig);
    bundle.evidence.rig_calibration = exact(g.rig);
    l1_fixture::edit_image(bundle, [&](data::Packet &image) {
        image.header.frame = camera.optical_frame;
        image.header.calibration = g.rig.value.revision;
    });
    auto raw = s.begin_projected_capture(storage_fixture::header());
    s.append_bundle(raw, storage_fixture::bundle(0));
    s.append_bundle(raw, bundle);
    s.record_run_outcome(raw, {2, storage_fixture::outcome(data::RecordedRunDisposition::completed,
                                                           data::AcquisitionReason::none)});
    auto descriptor = s.finalize(raw);
    auto config = l1_fixture::subpixel_config();
    config.optical_frame = camera.optical_frame;
    config.roi = pipeline::L1CandidateConfig::Roi{637, 396, 3, 9};
    config.raw_input = {raw, {"org.mantis.RawCapture", 3}, descriptor.hash, 0};
    auto p = plane(g);
    p.line = {config.emitter, config.pattern, config.pattern_revision, config.line};
    p.cameras[0].component = config.camera;
    a3::RecordedRequest request{config,
                                1,
                                get(a3::create_plane(s, p)),
                                get(a3::create_policy(s, policy(a3::SourceContract::alg02_subpixel_1))),
                                exact(g.left),
                                exact(g.rig)};
    // The shared pointer is synchronous/non-owning here; the Store outlives it.
    auto store = std::shared_ptr<artifact::Store>(&s, [](artifact::Store *) {});
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
    // Refusals must not create a metric result. Some later failures may retain
    // their valid observation, per the nontransactional artifact contract.
    auto result_count = [&] {
        size_t count{};
        for (const auto &d : s.list())
            if (d.type.name == a3::result_type.name)
                ++count;
        return count;
    };
    auto refuse = [&](const a3::RecordedRequest &bad, Status expected, std::string_view diagnostic) {
        auto before = result_count();
        std::set<Id> existing;
        for (const auto &d : s.list())
            existing.insert(d.id);
        auto result = a3::process_recorded(store, bad);
        rejects(result, expected);
        CHECK(result.error().message.find(diagnostic) != std::string::npos);
        CHECK(result_count() == before);
        for (const auto &d : s.list()) {
            if (!existing.contains(d.id)) {
                CHECK(d.type.name == schema::laser_observation.name);
                CHECK(d.state == artifact::ArtifactState::finalized);
                CHECK(result.error().message.find(d.id.value) != std::string::npos);
            }
        }
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
