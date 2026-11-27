#pragma once

namespace {
// Reuse the accepted sensor-sized capture and frozen M4 fit. The mixed result
// uses the same recorded pixels with a distinct immutable incidence policy.
void ply_fixture(const std::filesystem::path &project, const std::filesystem::path &receipt,
                 const std::filesystem::path &calibration) {
    const auto store = std::make_shared<artifact::Store>(project);
    const auto g = fixed_graph(*store, calibration);
    constexpr std::array records{Pattern::stripe, Pattern::zero, Pattern::unresolved};
    const auto f = record(*store, g, records);
    const auto &t = g.rig.value.solution.rig.T_rig_from_left.matrix;
    std::array<std::array<long double, 3>, rows> expected{};
    std::array<double, rows> incidence{};
    for (size_t row = 0; row < rows; ++row) {
        const auto p = expected[row] = oracle(g, static_cast<float>(first + row), packed::vertex);
        const auto dy = p[1] - t[7];
        incidence[row] = static_cast<double>(std::abs(dy) / std::hypot(p[0] - t[3], dy, p[2] - t[11]));
    }
    auto sorted = incidence;
    std::sort(sorted.begin(), sorted.end());
    const auto middle = rows / 2;
    const auto threshold = (sorted[middle - 1] + sorted[middle]) / 2;
    CHECK(sorted[middle] - sorted[middle - 1] > 1e-8);

    Json out;
    auto process = [&](const char *label, a3::RecordedRequest req, std::string_view outcome, size_t samples,
                       size_t count, uint32_t rejected_reason) {
        const auto refs = get(a3::process_recorded(store, req));
        const auto p = get(a3::load_result(*store, refs.result.id));
        const auto bytes = get(p->attributes.at(0).buffer.map_read());
        const auto doc = Json::parse(reinterpret_cast<const char *>(bytes.data()),
                                     reinterpret_cast<const char *>(bytes.data() + bytes.size()));
        const auto &m = doc.at("payload");
        CHECK(m.at("outcome") == outcome && m.at("sample_count") == samples);
        CHECK(m.at("accepted_count") == count && m.at("rejected_count") == samples - count);
        CHECK(p->type == schema::tensor && p->attributes.size() == (samples ? 17 : 1));
        CHECK(p->header.frame.id == g.rig.value.solution.config.rig_frame.id &&
              p->header.frame.name == g.rig.value.solution.config.rig_frame.name);
        packed::check_logical(p->header.calibration, req.rig.calibration);
        CHECK(m.at("inputs").at("observation") == a3::detail::content(refs.observation));
        const auto o = store->laser_observation(refs.observation.id);
        packed::check_content(o.context.raw_input, req.extractor.raw_input);
        packed::check_calibration(o.context.source.rig_calibration, req.rig);
        const auto d = store->get(refs.result.id);
        CHECK(d.state == artifact::ArtifactState::finalized && d.hash == refs.result.hash);
        CHECK(
            (d.provenance.inputs == std::vector<Id>{refs.observation.id, req.camera.content.get()->id,
                                                    req.rig.content.get()->id, req.plane.id, req.policy.id}));
        Json xyz = Json::array();
        long double max_geometry_error{};
        for (size_t row = 0; row < samples; ++row) {
            const bool accepted =
                outcome == "all_accepted" || (outcome == "mixed" && incidence[row] > threshold);
            CHECK(value<uint32_t>(*p, "source_row", row) == row);
            CHECK(value<uint8_t>(*p, "accepted", row) == accepted);
            CHECK(value<uint32_t>(*p, "reason_bits", row) == (accepted ? 0u : rejected_reason));
            if (!accepted)
                continue;
            CHECK(value<float>(*p, "source_pixel", row) == static_cast<float>(first + row));
            CHECK(value<float>(*p, "source_pixel", row, 1) == packed::vertex);
            std::array<double, 3> point{};
            for (size_t axis = 0; axis < point.size(); ++axis) {
                point[axis] = value<double>(*p, "position", row, axis);
                auto error = std::abs(point[axis] - expected[row][axis]);
                CHECK(error < 2e-8L);
                max_geometry_error = std::max(max_geometry_error, error);
            }
            xyz.push_back(point);
        }
        CHECK(xyz.size() == count);
        out[label] = {{"id", d.id.value},
                      {"hash", d.hash.algorithm + ":" + d.hash.hex},
                      {"observation", refs.observation.id.value},
                      {"raw", req.extractor.raw_input.id.value},
                      {"bytes", d.bytes},
                      {"samples", samples},
                      {"rejected_reason", rejected_reason},
                      {"points_f64", xyz},
                      {"max_geometry_error_mm", max_geometry_error}};
        for (const auto &input : d.provenance.inputs)
            out[label]["inputs"].push_back(input.value);
        return p;
    };
    const auto sensor = process("sensor", f.request, "all_accepted", rows, rows, 0);
    auto mixed = f.request;
    auto pol = bounded_policy();
    pol.min_incidence_cosine = threshold;
    mixed.policy = get(a3::create_policy(*store, pol));
    process("mixed", mixed, "mixed", rows, rows - middle, a3::near_parallel);
    auto zero = f.request;
    zero.record = 2; // Literal zero, optical ON.
    process("empty_input", zero, "empty_input", 0, 0, 0);
    zero.record = 3; // Same stripe, optical Unknown.
    process("all_rejected", zero, "all_rejected", rows, 0, a3::unresolved_attribution);

    // Separate typed synthetic control: coincident valid coordinates are not
    // identities. ALG02 profiles in the recorded control have distinct pixels.
    auto duplicate = observation(g, 3);
    const std::array<float, 6> pixels{637, packed::vertex, 639, packed::vertex, 637, packed::vertex};
    replace(duplicate,
            {data::laser::source_pixel_descriptor(3), memory::copy(std::as_bytes(std::span(pixels)))});
    const auto duplicate_bound = get(a3::prepare(*store, bind(*store, g, duplicate, plane(g), policy())));
    const auto duplicate_result = get(a3::process(duplicate_bound, duplicate));
    auto duplicated = get(a3::persist_result(*store, duplicate_bound, *duplicate_result));
    const auto typed_duplicate = get(a3::load_result(*store, duplicated.id));
    out["duplicate_id"] = duplicated.id.value;
    Json duplicate_points = Json::array();
    for (size_t i = 0; i < 3; ++i) {
        const auto oracle_point = oracle(g, pixels[2 * i], pixels[2 * i + 1]);
        CHECK(value<uint8_t>(*typed_duplicate, "accepted", i) == 1);
        std::array<double, 3> point{};
        for (size_t axis = 0; axis < point.size(); ++axis) {
            point[axis] = value<double>(*typed_duplicate, "position", i, axis);
            CHECK(std::abs(point[axis] - oracle_point[axis]) < 2e-8L);
        }
        duplicate_points.push_back(point);
    }
    CHECK(duplicate_points[0] == duplicate_points[2] && duplicate_points[0] != duplicate_points[1]);
    out["duplicate_points_f64"] = std::move(duplicate_points);

    auto put = [&](const char *label, const artifact::ArtifactType &kind, const data::Packet &p,
                   const artifact::Provenance &provenance) {
        const auto id = store->begin(kind, provenance);
        store->append(id, p);
        store->finalize(id);
        out[label] = id.value;
        return id;
    };
    constexpr std::string_view legacy_type = "org.mantis.PointCloud";
    const std::array<float, 6> legacy_xyz{2, 4, 8, -16, -32, -64};
    data::Packet legacy;
    legacy.type = schema::points;
    legacy.attributes = {{{"org.mantis.position", schema::ScalarType::f32, {2, 3}, {12, 4}, "mm"},
                          memory::copy(std::as_bytes(std::span(legacy_xyz)))}};
    put("legacy", {std::string(legacy_type), 1}, legacy, {});
    auto lookalike = *sensor;
    // Include both the true ALG03 columns and a generic exporter-lookalike.
    lookalike.attributes.push_back(legacy.attributes.front());
    put("tensor", {schema::tensor.name, 1}, lookalike, {});
    const auto provenance = store->get({out.at("sensor").at("id").get<std::string>()}).provenance;
    // A recognized ALG03 name with an unsupported version must not fall through
    // to the legacy exporter even when its contents satisfy the PointCloud ABI.
    put("wrong_version", {a3::result_type.name, 2}, legacy, provenance);
    put("wrong_envelope", a3::result_type, legacy, provenance);
    out["unfinalized"] = store->begin(a3::result_type, provenance).value;
    auto incompatible = *sensor;
    auto manifest_bytes = get(incompatible.attributes.front().buffer.map_read());
    auto manifest =
        Json::parse(reinterpret_cast<const char *>(manifest_bytes.data()),
                    reinterpret_cast<const char *>(manifest_bytes.data() + manifest_bytes.size()));
    auto &stamp = manifest["payload"]["algorithm"]["build"].get_ref<std::string &>();
    stamp.back() = stamp.back() == '0' ? '1' : '0';
    const auto changed = manifest.dump();
    CHECK(changed.size() == manifest_bytes.size());
    incompatible.attributes.front().buffer = memory::copy(std::as_bytes(std::span(changed)));
    put("wrong_build", a3::result_type, incompatible, provenance);
    const auto corrupt = put("corrupt", a3::result_type, *sensor, provenance);
    {
        std::fstream object(store->object_path(corrupt), std::ios::in | std::ios::out | std::ios::binary);
        object.put('!'); // Real finalized object, invalid packet magic.
        object.flush();
        CHECK(object.good());
    }
    std::ofstream file(receipt, std::ios::binary);
    file << out.dump();
    file.close();
    CHECK(file.good());
} // Store owner is gone before the harness opens the project via mantisd.
} // namespace
