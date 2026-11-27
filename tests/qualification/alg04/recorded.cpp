#include "../../../src/alg03/private.hpp"
#include "../../../src/artifact-store/segments.hpp"
#include "../../support/alg03_values.hpp"
#include "../../support/async_scope.hpp"
#include "../../support/l1_candidate_values.hpp"
#include "../../support/observed_wait.hpp"
#include "probe.hpp"
#include <fstream>
#include <iostream>
#include <mantis/image_layout.hpp>
#include <mantis/l1_recorded.hpp>
#include <mantis/plugin_runtime.hpp>

#include <source_location>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/vfs.h>

namespace {
using namespace alg03_fixture;
using namespace mantis::data;
using namespace std::chrono_literals;
using a3::detail::Json;

void check(bool ok, const char *predicate, std::source_location at = std::source_location::current()) {
    if (!ok)
        throw std::runtime_error("line " + std::to_string(at.line()) + ": " + predicate);
}
#define CHECK(x) check(bool(x), #x)
} // namespace
#include "../../support/alg03_recorded_packed_values.hpp"

namespace {
namespace packed = packed_recorded_fixture;
namespace l1 = pipeline::l1;
namespace seg = artifact::segments;
constexpr uint32_t width = 1280, height = 960, stride = 1603, first = 512, rows = 256;
constexpr size_t pixel_bytes = size_t(stride) * height;
constexpr auto empty_mask = l1::missing_profiles;
constexpr uint64_t max_raw_bytes = 10 << 20;
constexpr uint64_t observation_file_cap = 128 << 10, source_cap = 2 << 20, result_cap = 64 << 10,
                   geometry_cap = 32 << 20;
constexpr size_t in_cap = 2, out_cap = 1;
constexpr auto wait_bound = 5s;
const packed::Generation run{{{"alg04-run"}}, {{"alg04-capture"}}, {{"alg04-extractor"}}};

enum class Pattern { stripe, zero, flat, unresolved };
constexpr std::array patterns{Pattern::stripe, Pattern::zero, Pattern::flat,
                              Pattern::stripe, Pattern::zero, Pattern::unresolved};
bool has_line(Pattern p) {
    return p == Pattern::stripe || p == Pattern::unresolved;
}
std::string_view name(Pattern p) {
    switch (p) {
    case Pattern::stripe:
        return "stripe";
    case Pattern::zero:
        return "zero";
    case Pattern::flat:
        return "flat48";
    case Pattern::unresolved:
        return "stripe_optical_unknown";
    }
    throw std::logic_error("Unknown fixture pattern");
}
packed::Generation identity(size_t i) {
    auto g = run;
    g.offset = i;
    return g;
}
uint16_t signal(Pattern p, uint32_t x, uint32_t y) {
    if (has_line(p) && x >= first && x < first + rows)
        return packed::stripe_sample((x + 3) % 4, y); // Same phase at x=637..640 as #151.
    return p == Pattern::zero ? 0 : packed::background_dn;
}

a3::Policy bounded_policy() {
    auto p = policy(a3::SourceContract::alg02_subpixel_1);
    p.max_samples = rows;
    p.max_source_bytes = source_cap; // Readback may conservatively charge eleven alias wrappers.
    p.max_result_bytes = result_cap;
    p.max_geometry_bytes = geometry_cap;
    p.max_dependency_nodes = 32;
    return p;
}

// Freeze the inherited M4 synthetic fit across runner architectures. Rebind
// only Store-allocated IDs; the actual typed create/load path owns validation,
// revisions, dependency descriptors and all new checksums.
Graph fixed_graph(artifact::Store &s, const std::filesystem::path &path) {
    CHECK(std::filesystem::file_size(path) <= (512u << 10));
    std::ifstream input(path, std::ios::binary);
    auto values = Json::parse(input);
    a3::detail::fields(values, {"dataset", "left", "right", "rig", "solver"});
    auto [target, raw] = calibration_sources(s);
    const std::map<std::string, Id> ids{
        {"@target", target.value.target.identity.id}, {"@raw-a", raw.at(0)}, {"@raw-b", raw.at(1)}};
    auto rebind = [&](auto &&visit, Json &v) -> void {
        if (v.is_structured()) {
            for (auto &child : v)
                visit(visit, child);
        } else if (v.is_string()) {
            const auto found = ids.find(v.get_ref<const std::string &>());
            if (found != ids.end())
                v = found->second.value;
        }
    };
    rebind(rebind, values);
    auto d = get(ca::create_calibration_dataset(
        s, a3::detail::dec<calibration::CalibrationDataset>(values.at("dataset")), target.reference()));
    const ca::SolverImplementation solver{values.at("solver").get<std::string>()};
    auto camera = [&](std::string_view role) {
        auto c = get(ca::create_camera_calibration(
            s, a3::detail::dec<calibration::CameraCalibrationSolution>(values.at(role)), d.reference(),
            target.reference(), solver));
        CHECK(a3::detail::enc(c.value.solution) == values.at(role));
        return c;
    };
    auto left = camera("left"), right = camera("right");
    auto rig = get(ca::create_rig_calibration(
        s, a3::detail::dec<calibration::StereoCalibrationSolution>(values.at("rig")), d.reference(),
        target.reference(), left.reference(), right.reference(), solver));
    CHECK(a3::detail::enc(rig.value.solution) == values.at("rig"));
    CHECK(a3::detail::enc(d.value.dataset) == values.at("dataset"));
    return {std::move(target), std::move(d), std::move(left), std::move(right), std::move(rig)};
}

struct Fixture {
    artifact::ArtifactDescriptor raw;
    uint64_t segment_bytes;
    ProjectedCaptureHeader header;
    a3::RecordedRequest request;
    ContentReference tight;
};
Fixture record(artifact::Store &s, const Graph &g) {
    const auto &cam = g.left.value.solution.camera;
    CHECK(cam.image_width == width && cam.image_height == height);
    auto header = storage_fixture::header();
    header.run = run.run;
    header.generation = run.capture;
    const auto raw_id = s.begin_projected_capture(header);
    auto startup = storage_fixture::bundle(0);
    startup.key = {run.run, {packed::bundle_counter - 1}};
    startup.evidence.key = {run.run, {0}};
    startup.published = run.host(0);
    s.append_bundle(raw_id, startup);
    auto predecessor = startup.evidence.key;
    for (size_t i = 0; i < patterns.size(); ++i) {
        const auto pattern = patterns[i];
        auto b = l1_fixture::image_bundle(
            true, width, height, [=](uint32_t x, uint32_t y) { return signal(pattern, x, y); },
            packed::padding);
        packed::tag_image(b, g, identity(i), predecessor);
        if (pattern == Pattern::unresolved)
            b.evidence.emitters.at(0).exposure_effective.at(0).state = Unknown{};
        s.append_bundle(raw_id, b);
        predecessor = b.evidence.key;
    }
    auto outcome = storage_fixture::outcome(RecordedRunDisposition::completed, AcquisitionReason::none);
    outcome.run = run.run;
    outcome.generation = run.capture;
    s.record_run_outcome(raw_id, {patterns.size() + 1, outcome});
    auto raw = s.finalize(raw_id);
    auto config = packed::extractor(g, run, raw);
    CHECK(config.background_level == 48 && config.minimum_signal == 64 && !config.dark);
    config.roi = pipeline::L1CandidateConfig::Roi{0, 0, width, height};
    config.maximum_dimension = width;
    config.maximum_pixels = uint64_t(width) * height;
    const auto plane_ref = get(a3::create_plane(
        s, plane(g, config.camera, {config.emitter, config.pattern, config.pattern_revision, config.line})));
    auto p = bounded_policy();
    const auto normal = get(a3::create_policy(s, p));
    --p.max_samples;
    const auto tight = get(a3::create_policy(s, p));
    return {raw,
            std::filesystem::file_size(s.object_path(raw.id)),
            std::move(header),
            {config, 1, plane_ref, normal, exact(g.left), exact(g.rig)},
            tight};
}

auto image_bytes(const AcquisitionBundle &bundle) {
    return get(bundle.frameset->frames.at(0)->attributes.at(0).buffer.map_read());
}
void check_identity(const AcquisitionBundle &b, size_t i) {
    const auto keys = identity(i);
    CHECK(b.key == keys.bundle());
    CHECK(b.evidence.key == keys.evidence());
    CHECK(b.evidence.frameset == Evidence{keys.frameset()});
    CHECK(b.evidence.frames.size() == 1 && b.evidence.frames[0].frame == keys.frame());
    CHECK(b.evidence.triggers == std::vector{keys.trigger()});
    CHECK(b.frameset && b.frameset->frames.size() == 1);
    CHECK(b.frameset->header.sequence.value == keys.frameset().sequence);
    CHECK(b.frameset->frames[0]->header.sequence.value == keys.frame().native_sequence);
}

std::string check_capture(const std::shared_ptr<artifact::Store> &store, const Fixture &f, bool print) {
    const auto &s = *store;
    const auto descriptor =
        packed::check_finalized(s, f.request.extractor.raw_input, {"org.mantis.RawCapture", 3}, 0);
    CHECK(descriptor.chunks == 1 && descriptor.bytes <= max_raw_bytes && descriptor.bytes == f.raw.bytes);
    const auto path = s.object_path(f.raw.id);
    const auto header_bytes = std::filesystem::file_size(path.parent_path() / "run.header");
    const auto outcome_bytes = std::filesystem::file_size(path.parent_path() / "run.outcome");
    const auto scan = seg::scan(path, 3); // Existing product index; no second decoder/replay.
    CHECK(!scan.incomplete && scan.corruption.empty() && scan.records.size() == patterns.size() + 1);
    CHECK(scan.complete_bytes == f.segment_bytes && std::filesystem::file_size(path) == f.segment_bytes);
    CHECK(descriptor.bytes == f.segment_bytes + header_bytes + outcome_bytes);
    const auto segment_digest = content_hash(get(scan.mapping.map_read())).hex;
    if (print)
        std::cout << "raw id=" << f.raw.id.value << " descriptor_hash=" << f.raw.hash.hex
                  << " segment_fnv1a64=" << segment_digest << " records=" << scan.records.size()
                  << " segments/chunks=" << descriptor.chunks << " stored_bytes=" << descriptor.bytes
                  << " segment_bytes=" << f.segment_bytes << " header_bytes=" << header_bytes
                  << " outcome_bytes=" << outcome_bytes << " startup_payload_bytes=" << scan.records[0].bytes
                  << '\n';

    artifact::BundleCaptureReader reader(store, f.raw.id);
    CHECK(storage_fixture::encode(reader.header()) == storage_fixture::encode(f.header));
    CHECK(reader.final_outcome() && reader.final_outcome()->disposition == RecordedRunDisposition::completed);
    CHECK(reader.final_outcome()->run == run.run && reader.final_outcome()->generation == run.capture);
    auto startup = reader.next();
    CHECK(startup && !startup->frameset &&
          startup->key == (BundleKey{run.run, {packed::bundle_counter - 1}}));
    std::optional<memory::BufferView> retained;
    // Independently packed full-image FNV-1a64 goldens, including 0xe3 padding.
    const std::array<std::string_view, 3> expected_hashes{"290e6de172c53e25", "e17ec31aa0b2b0e5",
                                                          "4ed51f2aa952b0e5"};
    for (size_t i = 0; i < patterns.size(); ++i) {
        const auto p = patterns[i];
        auto b = reader.next();
        CHECK(b);
        check_identity(*b, i);
        const auto &frame = *b->frameset->frames.at(0);
        const auto layout = image_layout(frame);
        CHECK(frame.header.metadata.at("fourcc") == "Y10P");
        CHECK(layout.width == width && layout.height == height && layout.row_stride == stride &&
              layout.packing == ImagePacking::mipi_raw10);
        packed::check_calibration(b->evidence.frames.at(0).camera_calibration, f.request.camera);
        packed::check_calibration(b->evidence.frames.at(0).rig_calibration, f.request.rig);
        const auto &effective = b->evidence.emitters.at(0).exposure_effective.at(0);
        CHECK(effective.frame == identity(i).frame());
        if (p == Pattern::unresolved)
            CHECK(effective.state.presence() == Presence::unknown);
        else
            CHECK(effective.state.get() && effective.state.get()->scope == EvidenceScope::optical_emission &&
                  effective.state.get()->state == EmitterState::on);
        const auto &buffer = frame.attributes.at(0).buffer;
        CHECK(buffer.domain() == memory::MemoryDomain::shared_memory &&
              buffer.backing_size() == f.segment_bytes);
        if (!retained)
            retained = buffer;
        CHECK(buffer.identity() == retained->identity()); // One segment backing across all records.
        const auto bytes = image_bytes(*b);
        CHECK(bytes.size() == pixel_bytes);
        if (p == Pattern::zero || p == Pattern::flat) {
            for (size_t y = 0; y < height; ++y) {
                auto row = bytes.subspan(y * stride, stride);
                for (size_t j = 0; j < width / 4; ++j) {
                    const auto upper = p == Pattern::zero ? std::byte{0} : std::byte{12};
                    CHECK(row[j * 5] == upper && row[j * 5 + 1] == upper && row[j * 5 + 2] == upper &&
                          row[j * 5 + 3] == upper && row[j * 5 + 4] == std::byte{0});
                }
                CHECK(std::ranges::all_of(row.last(packed::padding),
                                          [](auto v) { return v == std::byte{0xe3}; }));
            }
        }
        const auto hash = content_hash(bytes);
        CHECK(hash.hex == expected_hashes[has_line(p) ? 0 : p == Pattern::zero ? 1 : 2]);
        if (print)
            std::cout << "record=" << i + 1 << " pattern=" << name(p) << " run=" << b->key.run_id.id.value
                      << " capture_generation=" << run.capture.id.value << " bundle=" << b->key.sequence.value
                      << " evidence=" << b->evidence.key.ordinal.value
                      << " frameset=" << identity(i).frameset().sequence
                      << " image=" << identity(i).frame().native_sequence
                      << " trigger=" << identity(i).trigger().sequence.value
                      << " bytes=" << scan.records[i + 1].bytes << " image_fnv1a64=" << hash.hex << '\n';
    }
    CHECK(!reader.next());
    CHECK(retained && content_hash(get(retained->map_read())).hex == expected_hashes[0]);
    return segment_digest;
}

void check_observation(const LaserObservation &o, const Fixture &f, size_t i) {
    const auto p = patterns.at(i);
    const auto keys = identity(i);
    const bool unknown = p == Pattern::unresolved;
    const auto n = has_line(p) ? rows : 0u;
    const auto &c = o.context;
    CHECK(o.key.run_id == keys.observation().run_id &&
          o.key.producer_stream == keys.observation().producer_stream);
    CHECK(o.key.producer_generation == keys.observation().producer_generation &&
          o.key.sequence == keys.observation().sequence);
    CHECK(c.source.frame == keys.frame() && c.source.width == width && c.source.height == height &&
          c.source.camera_role == "left");
    CHECK(c.bundle == Evidence{keys.bundle()} && c.frameset == Evidence{keys.frameset()} &&
          c.acquisition_evidence == Evidence{keys.evidence()} && c.triggers == std::vector{keys.trigger()});
    CHECK(c.correlation.get() && c.correlation.get()->step == keys.step() &&
          same_program_reference(c.correlation.get()->program, f.header.program.identity));
    CHECK(c.source.original_calibration.presence() == Presence::unavailable);
    packed::check_content(c.raw_input, f.request.extractor.raw_input);
    CHECK(c.exact_inputs.size() == 1);
    packed::check_content(c.exact_inputs[0], f.request.extractor.raw_input);
    packed::check_calibration(c.source.camera_calibration, f.request.camera);
    packed::check_calibration(c.source.rig_calibration, f.request.rig);
    CHECK(c.producer.implementation.value == "org.mantis.l1-subpixel" && !c.producer.build.empty());
    CHECK(c.parameters.get() && c.parameters.get()->hash.get() && c.producer.configuration.get() &&
          a3::detail::same(*c.parameters.get(), *c.producer.configuration.get()));
    const auto &effective = c.emitter_evidence.at(0).exposure_effective.at(0);
    CHECK(effective.frame == keys.frame());
    CHECK(unknown
              ? effective.state.presence() == Presence::unknown
              : (effective.state.get() && effective.state.get()->scope == EvidenceScope::optical_emission &&
                 effective.state.get()->state == EmitterState::on));
    const auto summary = get(pipeline::summarize(o));
    CHECK(summary.disposition == ObservationDisposition::success && summary.samples == n);
    CHECK(summary.known_emitters == (unknown ? 0 : n) && summary.known_lines == summary.known_emitters);
    CHECK(summary.unknown_emitters == (unknown ? n : 0) && summary.unknown_lines == summary.unknown_emitters);
    CHECK(o.sample_count == n && o.attributes.size() == (n ? 11u : 0u));
    CHECK(o.diagnostic == "profiles=1280;missing=" + std::to_string(width - n) + ";rejected=0;saturated=0");
    CHECK(c.packet_quality_flags == (empty_mask | (unknown ? l1::attribution_unresolved : 0u)));
    const auto &config = f.request.extractor;
    CHECK(o.emitter_dictionary == std::vector{config.emitter});
    CHECK(o.line_dictionary ==
          (std::vector{LineIdentity{config.emitter, config.pattern, config.pattern_revision, config.line}}));
    for (size_t row = 0; row < n; ++row) {
        CHECK(l1_fixture::value<float>(o, laser::source_pixel, row) == float(first + row));
        CHECK(l1_fixture::value<float>(o, laser::source_pixel, row, 1) == packed::vertex);
        CHECK(l1_fixture::value<uint8_t>(o, l1::refinement_valid, row) == 1);
        CHECK(l1_fixture::value<uint32_t>(o, l1::curvature, row) == 68);
        CHECK(l1_fixture::value<uint32_t>(o, l1::peak, row) == 723 + (first + row + 3) % 4);
        CHECK(l1_fixture::value<uint32_t>(o, l1::width, row) == 5);
        CHECK(l1_fixture::value<uint32_t>(o, l1::reason, row) == 0);
        CHECK(l1_fixture::value<uint32_t>(o, laser::quality_flags, row) ==
              (unknown ? laser::emitter_unknown | laser::line_unknown : 0));
        for (const auto column : {laser::emitter_valid, laser::line_valid})
            CHECK(l1_fixture::value<uint8_t>(o, column, row) == (unknown ? 0 : 1));
        for (const auto column : {laser::emitter_index, laser::line_index})
            CHECK(l1_fixture::value<uint32_t>(o, column, row) == 0);
    }
}

Json manifest(const Packet &packet) {
    return Json::parse(a3::detail::document_bytes(packet, "org.mantis.alg03.manifest")).at("payload");
}
template <typename Publication> uint64_t payload_bytes(const Publication &p) {
    uint64_t bytes{};
    for (const auto &column : p.attributes)
        bytes += column.buffer.size();
    return bytes;
}
struct Checked {
    LaserObservation observation;
    Published packet;
    Json manifest;
};
Checked read_result(artifact::Store &s, const Graph &g, const Fixture &f, const a3::RecordedResult &refs,
                    size_t i, bool print) {
    const auto p = patterns.at(i);
    const bool unknown = p == Pattern::unresolved;
    const auto n = has_line(p) ? rows : 0u;
    const auto od = packed::check_finalized(s, refs.observation, schema::laser_observation, 1);
    const auto rd = s.get(refs.result.id);
    CHECK(rd.state == artifact::ArtifactState::finalized && rd.type.name == a3::result_type.name &&
          rd.type.schema_version == 1 && rd.hash == refs.result.hash);
    CHECK(od.chunks == 1 && rd.chunks == 1 && od.bytes <= observation_file_cap && rd.bytes <= result_cap);
    CHECK((od.provenance.inputs == std::vector<Id>{f.raw.id, g.left.descriptor.id, g.rig.descriptor.id}));
    CHECK((rd.provenance.inputs == std::vector<Id>{od.id, g.left.descriptor.id, g.rig.descriptor.id,
                                                   f.request.plane.id, f.request.policy.id}));
    packed::check_logical(rd.provenance.calibration, f.request.rig.calibration);
    auto observation = s.laser_observation(od.id);
    check_observation(observation, f, i);
    auto packet =
        get(a3::load_result(s, rd.id)); // Extra consumer/readback, outside process_recorded measurement.
    CHECK(packet->type == schema::tensor && packet->attributes.size() == (n ? 17u : 1u));
    const auto &result_backing = packet->attributes.front().buffer;
    CHECK(result_backing.backing_size() == rd.bytes &&
          result_backing.domain() == memory::MemoryDomain::shared_memory);
    for (const auto &a : packet->attributes)
        CHECK(a.buffer.identity() == result_backing.identity());
    if (n) {
        const auto &observation_backing = observation.attributes.front().buffer;
        CHECK(observation_backing.backing_size() == od.bytes &&
              observation_backing.domain() == memory::MemoryDomain::shared_memory);
        for (const auto &a : observation.attributes)
            CHECK(a.buffer.identity() == observation_backing.identity());
    }
    const auto cloud = point_cloud(*packet);
    CHECK(!cloud && cloud.error().code == Status::incompatible && cloud.error().component == "data" &&
          cloud.error().message == "Expected PointCloud");
    CHECK(a3::detail::same(packet->header.frame, g.rig.value.solution.config.rig_frame));
    packed::check_logical(packet->header.calibration, f.request.rig.calibration);
    auto m = manifest(*packet);
    CHECK(m.at("layout_version") == 1 && m.at("source_disposition") == "success");
    CHECK(m.at("sample_count") == n && m.at("accepted_count") == (unknown ? 0 : n) &&
          m.at("rejected_count") == (unknown ? n : 0));
    CHECK(m.at("outcome") == (unknown ? "all_rejected" : n ? "all_accepted" : "empty_input"));
    CHECK(m.at("origin") == "synthetic" && m.at("validity_scope") == "declared_geometry_numeric_only");
    CHECK(m.at("source_key") == a3::detail::observation_key(identity(i).observation()));
    CHECK(m.at("source_frame") == a3::detail::frame_key(identity(i).frame()));
    CHECK(m.at("image_width") == width && m.at("image_height") == height && m.at("camera_role") == "left");
    const auto &bindings = m.at("inputs");
    CHECK(bindings.at("observation") == a3::detail::content(refs.observation));
    CHECK(bindings.at("plane") == a3::detail::content(f.request.plane));
    CHECK(bindings.at("policy") == a3::detail::content(f.request.policy));
    CHECK(bindings.at("camera") == a3::detail::exact(f.request.camera) &&
          bindings.at("rig") == a3::detail::exact(f.request.rig));
    CHECK(bindings.at("calibration_selection") == "recorded_effective");
    CHECK(m.at("recorded_calibration").at("original").at("presence") == "unavailable");
    CHECK(m.at("recorded_calibration").at("camera").at("value") == a3::detail::exact(f.request.camera));
    CHECK(m.at("recorded_calibration").at("rig").at("value") == a3::detail::exact(f.request.rig));
    long double worst_mm{};
    for (size_t row = 0; row < n; ++row) {
        const auto u = float(first + row);
        CHECK(value<uint32_t>(*packet, "source_row", row) == row);
        CHECK(value<float>(*packet, "source_pixel", row) == u);
        CHECK(value<float>(*packet, "source_pixel", row, 1) == packed::vertex);
        CHECK(value<uint32_t>(*packet, "source_quality_flags", row) ==
              (unknown ? laser::emitter_unknown | laser::line_unknown : 0u));
        CHECK(value<uint8_t>(*packet, "accepted", row) == (unknown ? 0 : 1));
        CHECK(value<uint32_t>(*packet, "reason_bits", row) == (unknown ? a3::unresolved_attribution : 0u));
        const auto pos = [&](size_t axis) { return value<double>(*packet, "position", row, axis); };
        if (unknown) {
            CHECK(value<uint32_t>(*packet, "diagnostic_valid", row) == 0);
            for (size_t j = 0; j < 3; ++j)
                CHECK(pos(j) == 0.0 && !std::signbit(pos(j)));
        } else {
            const auto expected = oracle(g, u, packed::vertex);
            const auto err = std::hypot(pos(0) - expected[0], pos(1) - expected[1], pos(2) - expected[2]);
            CHECK(err < 2e-8L);
            worst_mm = std::max(worst_mm, err);
        }
    }
    if (print)
        std::cout << "record=" << i + 1 << " producer=" << observation.key.producer_stream.id.value
                  << " producer_generation=" << observation.key.producer_generation.id.value
                  << " obs_sequence=" << observation.key.sequence.value << " obs=" << od.id.value
                  << " obs_hash=" << od.hash.hex << " obs_chunks=" << od.chunks
                  << " obs_chunk_bytes=" << od.bytes
                  << " obs_column_span_bytes=" << payload_bytes(observation) << " result=" << rd.id.value
                  << " result_hash=" << rd.hash.hex << " result_chunks=" << rd.chunks
                  << " result_chunk_bytes=" << rd.bytes << " result_payload_bytes=" << payload_bytes(*packet)
                  << " counts=" << n << '/' << m.at("accepted_count") << '/' << m.at("rejected_count")
                  << " outcome=" << m.at("outcome") << " diagnostic=" << observation.diagnostic
                  << " quality_flags=" << observation.context.packet_quality_flags
                  << " max_accepted_L2_error_mm=" << worst_mm << " PointCloud=incompatible\n";
    if (print && i == 0)
        std::cout << "l1_build=" << observation.context.producer.build
                  << " l1_config_hash=" << observation.context.parameters.get()->hash.get()->hex
                  << " alg03_build=" << m.at("algorithm").at("build") << '\n';
    return {std::move(observation), std::move(packet), std::move(m)};
}

void same_outputs(const Checked &control, const Checked &checked) {
    CHECK(l1_fixture::encode(checked.observation) == l1_fixture::encode(control.observation));
    auto expected = control.manifest, actual = checked.manifest;
    auto &expected_ref = expected.at("inputs").at("observation");
    auto &actual_ref = actual.at("inputs").at("observation");
    CHECK(expected_ref.at("id") != actual_ref.at("id"));
    expected_ref.erase("id"); // The only volatile binding in this comparison.
    actual_ref.erase("id");
    CHECK(expected == actual && checked.packet->attributes.size() == control.packet->attributes.size());
    for (size_t col = 1; col < checked.packet->attributes.size(); ++col)
        CHECK(std::ranges::equal(get(checked.packet->attributes[col].buffer.map_read()),
                                 get(control.packet->attributes[col].buffer.map_read())));
}

struct Usage {
    int64_t user_us{}, sys_us{}, peak_kib{};
};
Usage usage() {
    rusage u{};
    CHECK(getrusage(RUSAGE_SELF, &u) == 0 && u.ru_maxrss > 0);
    auto us = [](timeval v) { return int64_t(v.tv_sec) * 1'000'000 + v.tv_usec; };
    return {us(u.ru_utime), us(u.ru_stime), u.ru_maxrss};
}
void print_phase(std::string_view phase, const Usage &u) {
    std::cout << "phase=" << phase << " lifetime_user_us=" << u.user_us << " lifetime_sys_us=" << u.sys_us
              << " process_peak_rss_KiB=" << u.peak_kib << '\n';
}
struct Totals {
    uint64_t cpu_us{}, extract_us{}, triangulate_us{}, wall_us{};
};
Checked measure(const std::shared_ptr<artifact::Store> &s, const Graph &g, const Fixture &f, size_t i) {
    auto req = f.request;
    req.record = i + 1;
    constexpr size_t repeats = 3;
    std::array<Totals, repeats> samples{};
    std::optional<Checked> last;
    auto stage = [](std::string_view label, const a3::detail::RecordedStageMetrics &m, uint64_t bytes) {
        CHECK(m.nodes == 2 && m.process_cpu_ns && m.wall_ns >= m.node_wall_ns);
        CHECK(m.retained_payload_limit_bytes == 256ull * 1024 * 1024);
        if (m.retained_payload_bytes != bytes)
            throw std::runtime_error(std::string(label) + " retained payload: expected " +
                                     std::to_string(bytes) + ", observed " +
                                     std::to_string(m.retained_payload_bytes));
        return Json{{"cpu_ns", *m.process_cpu_ns},
                    {"wall_ns", m.wall_ns},
                    {"node_wall_ns", m.node_wall_ns},
                    {"backing_high_water_bytes", m.retained_payload_bytes},
                    {"admission_limit_bytes", m.retained_payload_limit_bytes}};
    };
    for (size_t j = 0; j <= samples.size(); ++j) { // One checked warm-up, three measured calls.
        const auto before = s->list().size();
        a3::detail::RecordedMetrics metrics;
        const auto u0 = usage();
        const auto t0 = std::chrono::steady_clock::now();
        auto r = a3::detail::process_recorded(s, req, {}, &metrics);
        const auto t1 = std::chrono::steady_clock::now();
        const auto u1 = usage();
        const auto refs = get(std::move(r));
        CHECK(s->list().size() == before + 2 && metrics.extraction && metrics.triangulation);
        const auto user = u1.user_us - u0.user_us, sys = u1.sys_us - u0.sys_us;
        const auto wall = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        CHECK(user >= 0 && sys >= 0 && user + sys > 0 && wall > 0 && u1.peak_kib >= u0.peak_kib);
        // Oracle and extra typed consumer are deliberately outside all reported windows.
        auto checked = read_result(*s, g, f, refs, i, j == samples.size());
        const auto row_backing = has_line(patterns[i]) ? width * 36u : 0u;
        const auto payload = payload_bytes(*checked.packet);
        const auto extraction = stage("extraction", *metrics.extraction, f.segment_bytes + row_backing);
        // ALG03 couples the retained Prepared observation mapping to one output backing.
        const auto pinned = checked.observation.attributes.empty()
                                ? 0
                                : checked.observation.attributes[0].buffer.backing_size();
        const auto triangulation =
            stage("triangulation", *metrics.triangulation, row_backing + payload + pinned);
        const auto extra = usage();
        std::cout << "ALG04_SAMPLE "
                  << Json{{"pattern", name(patterns[i])},
                          {"sample", j},
                          {"warmup", j == 0},
                          {"process_recorded_user_us", user},
                          {"process_recorded_sys_us", sys},
                          {"wall_us", wall},
                          {"peak_rss_KiB_before", u0.peak_kib},
                          {"peak_rss_KiB_after_recorded", u1.peak_kib},
                          {"peak_rss_KiB_after_extra_consumer", extra.peak_kib},
                          {"result_payload_bytes", payload},
                          {"extraction_execute", extraction},
                          {"triangulation_execute", triangulation}}
                         .dump()
                  << '\n';
        if (j)
            samples[j - 1] = {uint64_t(user + sys), *metrics.extraction->process_cpu_ns / 1000,
                              *metrics.triangulation->process_cpu_ns / 1000, uint64_t(wall)};
        if (j == repeats)
            last = std::move(checked);
    }
    auto dispersion = [&](auto field) {
        std::array<uint64_t, repeats> v{};
        std::ranges::transform(samples, v.begin(), field);
        std::ranges::sort(v);
        return Json{{"min", v[0]}, {"median", v[1]}, {"max", v[2]}, {"range", v[2] - v[0]}};
    };
    std::cout << "ALG04_DISPERSION "
              << Json{{"pattern", name(patterns[i])},
                      {"unit", "us"},
                      {"n", samples.size()},
                      {"recorded_cpu", dispersion(&Totals::cpu_us)},
                      {"extraction_cpu", dispersion(&Totals::extract_us)},
                      {"triangulation_cpu", dispersion(&Totals::triangulate_us)},
                      {"recorded_wall", dispersion(&Totals::wall_us)}}
                     .dump()
              << '\n';
    return std::move(*last);
}

alg04_test::FileIdentity source_file(const artifact::Store &s, const Fixture &f) {
    struct stat raw {};
    CHECK(stat(s.object_path(f.raw.id).c_str(), &raw) == 0);
    return {raw.st_dev, raw.st_ino};
}
void probe_liveness(const std::shared_ptr<artifact::Store> &s, const Fixture &f) {
    std::array<std::byte, alg04_test::bulk_min> target{};
    alg04_test::Probe probe(source_file(*s, f));
    artifact::BundleCaptureReader reader(s, f.raw.id);
    CHECK(reader.next());
    const auto record = reader.next();
    CHECK(record && probe.io.source_maps == 2 && !probe.io.source_tracking_incomplete);
    const auto bytes = image_bytes(*record).first(target.size());
    const auto before = probe.io;
    alg04_test::copy_control(target.data(), bytes.data(), target.size());
    CHECK(probe.io.copies == before.copies + 1 && probe.io.copy_bytes == before.copy_bytes + bytes.size());
    CHECK(probe.io.source_copies == before.source_copies + 1 &&
          probe.io.source_copy_bytes == before.source_copy_bytes + bytes.size());
    CHECK(std::ranges::equal(bytes, target));
    std::cout << "bulk_probe_positive mapped_source_calls=1 mapped_source_bytes=" << bytes.size() << '\n';
}
void probe_recorded(const std::shared_ptr<artifact::Store> &s, const Graph &g, const Fixture &f, size_t i,
                    const Checked &control) {
    const auto file = source_file(*s, f);
    auto req = f.request;
    req.record = i + 1;
    const auto [refs, io] = [&] {
        alg04_test::Probe probe(file);
        auto result = get(a3::process_recorded(s, req)); // Ordinary entry; no diagnostic receipt active.
        CHECK(!probe.io.source_tracking_incomplete &&
              std::ranges::none_of(probe.live_source_mappings, [](auto r) { return r.bytes != 0; }));
        return std::pair{std::move(result), probe.io};
    }();
    std::cout << "bulk_probe pattern=" << name(patterns[i]) << " calls_at_least_" << alg04_test::bulk_min
              << "B=" << io.copies << " copy_bytes=" << io.copy_bytes << " largest_copy=" << io.largest_copy
              << " file_maps=" << io.mappings << " file_map_bytes=" << io.mapping_bytes
              << " raw_segment_maps=" << io.source_maps << " raw_segment_map_bytes=" << io.source_map_bytes
              << " raw_segment_bulk_source_calls=" << io.source_copies
              << " raw_segment_bulk_source_bytes=" << io.source_copy_bytes << '\n';
    CHECK(io.source_maps == 2 &&
          io.source_map_bytes == 2 * f.segment_bytes); // Hash verification + reader scan.
    CHECK(io.mappings > io.source_maps && io.mapping_bytes > io.source_map_bytes);
    CHECK(io.mappings <= 128 && io.mapping_bytes <= (32u << 20)); // Cumulative, not concurrent mapped length.
    CHECK(io.source_copies == 0 && io.source_copy_bytes == 0);
    CHECK(io.copies <= 512 && io.copy_bytes <= (16u << 20) && io.largest_copy < pixel_bytes);
    same_outputs(control, read_result(*s, g, f, refs, i, false));
}

void refuse_samples(const std::shared_ptr<artifact::Store> &s, const Fixture &f,
                    const LaserObservation &control) {
    auto bad = f.request;
    bad.policy = f.tight;
    std::set<Id> before;
    for (const auto &d : s->list())
        before.insert(d.id);
    a3::detail::RecordedMetrics metrics;
    metrics.triangulation.emplace(); // A failed invocation must not leak a stale second-stage receipt.
    const auto r = a3::detail::process_recorded(s, bad, {}, &metrics);
    CHECK(!r && r.error().code == Status::busy && r.error().component == "alg03");
    CHECK(metrics.extraction && !metrics.triangulation);
    CHECK(metrics.extraction->retained_payload_bytes == f.segment_bytes + width * 36u);
    size_t added{};
    for (const auto &d : s->list()) {
        if (before.erase(d.id))
            continue;
        ++added;
        CHECK(d.state == artifact::ArtifactState::finalized && d.type.name == schema::laser_observation.name);
        CHECK(r.error().message == "Recorded observation; ALG03 artifact " + d.id.value +
                                       "; observed state FINALIZED; ALG03 sample admission");
        CHECK(l1_fixture::encode(s->laser_observation(d.id)) == l1_fixture::encode(control));
    }
    CHECK(before.empty() && added == 1); // No metric reservation in ANY state.
    CHECK(s->get(f.raw.id).hash == f.raw.hash &&
          std::filesystem::file_size(s->object_path(f.raw.id)) == f.segment_bytes);
    std::cout << "max_samples=" << rows - 1 << " status=busy " << r.error().message
              << " metric_reservations=0\n";
}

void legacy_cloud(const std::filesystem::path &dir, const std::filesystem::path &host,
                  const std::filesystem::path &project) {
    // Same product example-points and virtual-scanner positive used by core.
    // Stage just their real manifests and libraries; discovery otherwise probes
    // unrelated plugins (and their binaries need not exist in a focused build).
    const auto plugin_dir = project / "legacy-plugins";
    std::filesystem::create_directory(plugin_dir);
    for (const auto *file :
         {"virtual-scanner.json", "virtual-scanner.so", "example-points.json", "example-points.so"})
        CHECK(std::filesystem::copy_file(dir / file, plugin_dir / file));
    plugins::Registry registry(host, project / "legacy-host", {});
    registry.discover(plugin_dir, {"org.mantis.virtual-scanner", "org.mantis.example-points"});
    auto devices = registry.devices();
    auto device = std::find_if(devices.begin(), devices.end(),
                               [](const auto &d) { return d->descriptor().id.value == "virtual-scanner"; });
    CHECK(device != devices.end());
    get((*device)->start());
    auto captured = (*device)->next();
    auto stopped = (*device)->stop();
    auto image = get(std::move(captured));
    get(std::move(stopped));
    pipeline::NodeDescriptor source;
    source.id = "legacy-input";
    source.outputs = {{"out", schema::image}};
    pipeline::PipelineRecipe recipe;
    recipe.graph.nodes = {{source, {}}, registry.node("org.mantis.example-points")};
    recipe.graph.connections = {{0, 0, 1, 0, 1, pipeline::QueuePolicy::lossless}};
    const auto packet = get(pipeline::execute(get(pipeline::compile(recipe)), image, {})).output;
    const auto cloud = get(point_cloud(*packet));
    const auto &positions = cloud.attributes.at("org.mantis.position");
    CHECK((positions.descriptor.shape == std::vector<uint64_t>{3072, 3}));
    CHECK(positions.descriptor.scalar == schema::ScalarType::f32 && positions.descriptor.unit == "mm");
    std::array<float, 3> xyz{};
    const auto bytes = get(positions.buffer.map_read());
    CHECK(bytes.size() == 3072 * 3 * sizeof(float));
    std::memcpy(xyz.data(), bytes.data(), sizeof(xyz));
    CHECK((xyz == std::array{-32.0f, -24.0f, 0.0f}));
    std::cout
        << "legacy virtual-scanner -> example-points -> PointCloud accepted_rows=3072 (separate control)\n";
}

pipeline::SemanticExecutionPlan admit_stream(const artifact::Store &s, const Fixture &f) {
    pipeline::NodeDescriptor source;
    source.id = "bundle-input";
    source.outputs = {{"out", schema::acquisition_bundle}};
    pipeline::SemanticGraph graph;
    graph.nodes = {{source, {}},
                   pipeline::l1_candidate_extractor(f.request.extractor),
                   pipeline::observation_consumer()};
    graph.connections = {{0, 0, 1, 0, in_cap, pipeline::QueuePolicy::lossless},
                         {1, 0, 2, 0, out_cap, pipeline::QueuePolicy::lossless}};
    CHECK(graph.retained_payload_limit_bytes == 256ull * 1024 * 1024);
    // Sentinel wraps the real factory without changing its node, backend, or value.
    // compile must not instantiate any production node, even on refusal.
    auto factories = std::make_shared<unsigned>();
    for (auto &node : graph.nodes)
        if (node.factory)
            node.factory = [factory = node.factory, factories] {
                ++*factories;
                return factory();
            };
    auto remote = graph; // Only the required location changes between plans.
    remote.nodes.back().descriptor.resources.location = pipeline::Location::remote;
    const auto before = s.list().size();
    const auto r = pipeline::compile(remote);
    CHECK(*factories == 0 && s.list().size() == before && s.get(f.raw.id).hash == f.raw.hash);
    CHECK(!r && r.error().code == Status::unsupported && r.error().component == "compute" &&
          r.error().message == "Requested execution location unavailable in v0.1");
    auto local = get(pipeline::compile(graph));
    CHECK(*factories == 0);
    CHECK((local.backends == std::vector<std::string>{"cpu", "cpu", "cpu"}));
    std::cout
        << "remote downstream status=unsupported factories=0 cpu_fallback=0 reservations=0; local=cpu\n";
    return local;
}

void refuse_backing(const std::shared_ptr<artifact::Store> &store, const Fixture &f,
                    const pipeline::SemanticExecutionPlan &compiled) {
    artifact::BundleCaptureReader reader(store, f.raw.id);
    CHECK(reader.next());
    auto stripe = reader.next();
    CHECK(stripe);
    check_identity(*stripe, 0);
    // Input alone fits. The actual extraction candidate must take the charge
    // over the limit before it can be published to the observation consumer.
    auto tight = compiled.graph;
    tight.retained_payload_limit_bytes = f.segment_bytes + width * 36u - 1;
    const auto rejected =
        pipeline::execute(get(pipeline::compile(tight)), publish(SemanticPacket{std::move(*stripe)}));
    CHECK(!rejected && rejected.error().code == Status::busy &&
          rejected.error().component == "semantic-pipeline" &&
          rejected.error().message == "Semantic retained payload budget exhausted");
    std::cout << "semantic backing admission status=busy cap_bytes=" << tight.retained_payload_limit_bytes
              << '\n';
}

void stream(const std::shared_ptr<artifact::Store> &store, const Fixture &f,
            const std::array<std::string, patterns.size()> &sequential) {
    const auto compiled = admit_stream(*store, f);
    const auto before = store->list().size();
    refuse_backing(store, f, compiled);
    artifact::BundleCaptureReader reader(store, f.raw.id);
    CHECK(reader.next());
    std::optional<memory::BufferView> source_backing;
    auto next = [&](size_t i) {
        auto b = reader.next();
        CHECK(b);
        check_identity(*b, i);
        const auto &buffer = b->frameset->frames.at(0)->attributes.at(0).buffer;
        if (!source_backing)
            source_backing = buffer; // Before starting producer. The rest of the views share this backing.
        CHECK(buffer.identity() == source_backing->identity() && buffer.backing_size() == f.segment_bytes);
        return publish(SemanticPacket{std::move(*b)});
    };
    pipeline::BoundedQueue<SemanticPublished> in(in_cap, pipeline::QueuePolicy::lossless);
    pipeline::BoundedQueue<pipeline::SemanticExecutionResult> out(out_cap, pipeline::QueuePolicy::lossless);
    const auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < in_cap; ++i)
        CHECK(in.push(next(i)));
    std::stop_source cancellation;
    test::ObservedWait blocked_sink, blocked_source;
    auto unblock = [&] {
        cancellation.request_stop();
        in.close();
        out.close();
        blocked_sink.wake();
        blocked_source.wake();
    };
    test::Async processing(unblock, [&] {
        test::ObservedWait::Arm arm(blocked_sink);
        return pipeline::execute_stream(compiled, in, out, cancellation.get_token());
    });
    // No consumer drains until both independently observed waits have entered.
    CHECK(blocked_sink.blocked(wait_bound) && "consumer gate must observe blocked output publication");
    const auto sink_entered = std::chrono::steady_clock::now();
    CHECK(in.metrics().popped == 2 && out.metrics().pushed == 1 && out.metrics().occupancy == out_cap);
    test::Async producer(unblock, [&] {
        test::OnExit close{[&] { in.close(); }};
        for (size_t i = in_cap; i < patterns.size(); ++i) {
            auto value = next(i);
            test::ObservedWait::Arm arm(blocked_source); // Scope observation to the queue wait.
            CHECK(in.push(std::move(value), cancellation.get_token()));
        }
        CHECK(!reader.next());
    });
    CHECK(blocked_source.blocked(wait_bound) && "consumer gate must observe blocked input publication");
    const auto source_entered = std::chrono::steady_clock::now();
    const auto gated_in = in.metrics(), gated_out = out.metrics();
    CHECK(gated_in.occupancy == in_cap && gated_in.pushed == 4 && gated_in.popped == 2);
    CHECK(gated_out.occupancy == out_cap && gated_out.pushed == 1 && gated_out.popped == 0);
    CHECK(gated_in.full_ns > 0 && gated_out.full_ns > 0);
    const auto release = std::chrono::steady_clock::now();
    std::cout << "stream gate: both publications observed blocked; source pushed=4 popped=2, sink pushed=1 "
                 "popped=0\n";
    size_t received{};
    uint64_t high_water{};
    while (auto value = out.pop()) {
        const auto i = received++;
        CHECK(i < patterns.size() && value->output && value->outputs.size() == 1);
        const auto &o = std::get<LaserObservation>(*value->output);
        CHECK(o.key.sequence == identity(i).observation().sequence && "stream output FIFO identity");
        CHECK(l1_fixture::encode(o) ==
              sequential.at(i)); // Exact normal recorded output, not a fixed Prepared.
        const uint64_t expected = f.segment_bytes + (has_line(patterns[i]) ? width * 36u : 0u);
        CHECK(value->retained_payload_high_water == expected);
        high_water = std::max(high_water, value->retained_payload_high_water);
    }
    get(processing.future.get());
    producer.future.get();
    CHECK(received == patterns.size() && store->list().size() == before);
    CHECK(!in.push({}) && !out.push({})); // Both closed after clean drain.
    const auto end_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
            .count();
    auto verify_queue = [&](std::string_view queue, const pipeline::QueueMetrics &m, size_t cap,
                            uint64_t gated_ns, std::chrono::steady_clock::time_point entered) {
        CHECK(m.pushed == patterns.size() && m.popped == patterns.size() && m.dropped == 0);
        CHECK(m.high_water == cap && m.occupancy == 0);
        CHECK(m.full_ns >= gated_ns && m.full_ns <= uint64_t(end_ns));
        const auto blocked_before_drain_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(release - entered).count();
        CHECK(blocked_before_drain_ns > 0 && uint64_t(blocked_before_drain_ns) <= m.full_ns);
        std::cout << "queue=" << queue << " pushed=" << m.pushed << " popped=" << m.popped
                  << " dropped=" << m.dropped << " high_water/cap=" << m.high_water << '/' << cap
                  << " full_ns=" << m.full_ns << " observed_gated_full_ns=" << gated_ns
                  << " observed_block_to_release_ns=" << blocked_before_drain_ns << '\n';
    };
    verify_queue("source", in.metrics(), in_cap, gated_in.full_ns, source_entered);
    verify_queue("sink", out.metrics(), out_cap, gated_out.full_ns, sink_entered);
    std::cout << "stream wall_ns=" << end_ns << " received=" << received
              << " per_frame_retained_high_water=" << high_water
              << " raw_backings=1 max_shared_backing_bytes=" << f.segment_bytes + (out_cap + 2) * width * 36u
              << " drained_and_retired=1\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        CHECK(argc == 5);
        const std::string_view mode = argv[3];
        CHECK(mode == "baseline" || mode == "fixture" || mode == "campaign");
        print_phase("process_entry", usage());
        if (mode == "baseline")
            return 0;
        Temp temp;
        const auto store = std::make_shared<artifact::Store>(temp.path);
        struct statfs filesystem {};
        CHECK(statfs(temp.path.c_str(), &filesystem) == 0);
        std::cout << "filesystem_type=0x" << std::hex << filesystem.f_type << std::dec << '\n';
        const auto g = fixed_graph(*store, argv[4]);
        const auto f = record(*store, g);
        print_phase("fixture_finalized", usage());
        const auto digest = check_capture(store, f, mode == "campaign");
        print_phase("fixture_verified", usage());
        if (mode == "fixture")
            return 0;
        const auto numeric = get(a3::load_policy(*store, f.request.policy));
        std::cout << "ALG04_INPUT "
                  << Json{{"format", "Y10P"},
                          {"stride", stride},
                          {"roi", {0, 0, width, height}},
                          {"profiles", width},
                          {"background_dn", f.request.extractor.background_level},
                          {"threshold_dn", f.request.extractor.minimum_signal},
                          {"saturation_dn", f.request.extractor.saturation_level},
                          {"min_neighbor_dn", f.request.extractor.subpixel->minimum_neighbor_signal},
                          {"min_curvature_dn", f.request.extractor.subpixel->minimum_curvature},
                          {"camera", a3::detail::exact(f.request.camera)},
                          {"rig", a3::detail::exact(f.request.rig)},
                          {"plane", a3::detail::content(f.request.plane)},
                          {"policy_ref", a3::detail::content(f.request.policy)},
                          {"policy", a3::detail::policy(numeric)},
                          {"frozen_fit_opencv", g.left.value.implementation.opencv_version},
                          {"brown5", a3::detail::enc(g.left.value.solution.final_model)},
                          {"rig_from_optical", g.rig.value.solution.rig.T_rig_from_left.matrix},
                          {"plane_equation", "Y=800mm"},
                          {"calibration_document_bytes",
                           {g.target.descriptor.bytes, g.dataset.descriptor.bytes, g.left.descriptor.bytes,
                            g.right.descriptor.bytes, g.rig.descriptor.bytes}}}
                         .dump()
                  << '\n';
        std::array<std::string, patterns.size()> sequential;
        probe_liveness(store, f);
        for (size_t i = 0; i < 2; ++i) {
            const auto checked = measure(store, g, f, i);
            sequential[i] = l1_fixture::encode(checked.observation);
            probe_recorded(store, g, f, i, checked);
            if (i == 0) {
                refuse_samples(store, f, checked.observation);
                // Explicit deterministic recovery, not an automatic retry or policy fallback.
                const auto retried = get(a3::process_recorded(store, f.request));
                same_outputs(checked, read_result(*store, g, f, retried, i, false));
                std::cout << "explicit normal-policy recovery=all_accepted\n";
            }
        }
        for (size_t i = 2; i < patterns.size(); ++i) {
            auto req = f.request;
            req.record = i + 1;
            const auto before = store->list().size();
            const auto refs = get(a3::process_recorded(store, req));
            CHECK(store->list().size() == before + 2);
            auto checked = read_result(*store, g, f, refs, i, true);
            sequential[i] = l1_fixture::encode(checked.observation);
        }

        legacy_cloud(argv[1], argv[2], temp.path);
        stream(store, f, sequential);
        CHECK(check_capture(store, f, false) == digest);
        print_phase("campaign_verified", usage());
        return 0;
    } catch (const Failure &e) {
        std::cerr << "ALG04 " << e.error.component << ' ' << static_cast<int>(e.error.code) << ": "
                  << e.what() << '\n';
        return 1;
    } catch (const std::exception &e) {
        std::cerr << "ALG04 " << e.what() << '\n';
        return 1;
    }
}
