#include "../fixtures/calibration_artifacts.hpp"
#include <fstream>
#include <iostream>
#include <mantis/calibration_artifacts.hpp>
#include <mantis/data_io.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
using namespace mantis;
using namespace mantis::calibration;
namespace ca = mantis::calibration::artifacts;
using Json = nlohmann::json;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #x);                          \
    } while (false)
template <class T> T get(Result<T> r) {
    if (!r)
        throw Failure(r.error());
    return std::move(*r);
}
void get(Result<void> r) {
    if (!r)
        throw Failure(r.error());
}
template <class T> void rejects(const Result<T> &r, Status s = Status::corrupt) {
    CHECK(!r);
    CHECK(r.error().code == s);
}
struct Temp {
    std::filesystem::path path = std::filesystem::temp_directory_path() / Id::random().value;
    ~Temp() {
        std::filesystem::remove_all(path);
    }
};
std::string payload(const artifact::Store &s, const Id &id) {
    auto packet = s.packet(id);
    auto m = get(packet->attributes.at(0).buffer.map_read());
    return {reinterpret_cast<const char *>(m.data()), m.size()};
}
data::Published document_packet(std::string bytes) {
    data::Packet p;
    p.type = ca::document_type;
    p.attributes = {
        {{std::string(ca::document_attribute), schema::ScalarType::u8, {bytes.size()}, {1}, "utf8-json"},
         memory::copy(std::as_bytes(std::span(bytes.data(), bytes.size())))}};
    return data::publish(std::move(p));
}
void sql(artifact::Store &s, const std::string &command) {
    sqlite3 *db{};
    CHECK(sqlite3_open((s.root() / "project.sqlite").c_str(), &db) == SQLITE_OK);
    char *error{};
    auto rc = sqlite3_exec(db, command.c_str(), nullptr, nullptr, &error);
    std::string msg = error ? error : "";
    sqlite3_free(error);
    sqlite3_close(db);
    if (rc != SQLITE_OK)
        throw std::runtime_error(msg);
}
CalibrationTarget physical_target(bool checker = false) {
    CalibrationTarget t;
    t.grid = {8, 6, 40};
    if (!checker)
        t.pattern = CharucoDefinition{"DICT_6X6_250", 25};
    t.measurement = {8 * 41.3, 6 * 40.6,
                     MeasurementProvenance{.01, .02, "digital_caliper", "measured active extent"}};
    return t;
}
Id raw(artifact::Store &s) {
    auto id = s.begin({"org.mantis.RawCapture", 2}, {});
    for (uint64_t i = 100; i < 114; ++i) {
        data::Packet fs;
        fs.type = schema::frameset;
        fs.header.sequence.value = i;
        for (const auto *role : {"left", "right"}) {
            data::Packet p;
            p.type = schema::image;
            p.header.metadata = {{"role", role}, {"identity", "camera." + std::string(role)}};
            p.attributes = {{{"org.mantis.pixels", schema::ScalarType::u8, {8, 8}, {8, 1}, "intensity"},
                             memory::copy(std::vector<std::byte>(64, std::byte{42}))}};
            fs.frames.push_back(data::publish(std::move(p)));
        }
        s.append(id, fs);
    }
    s.finalize(id);
    return id;
}
void targets() {
    Temp tmp;
    artifact::Store s(tmp.path);
    for (bool checker : {false, true})
        for (bool provenance : {false, true})
            for (bool empty : {false, true})
                for (bool historical : {false, true}) {
                    auto t = physical_target(checker);
                    if (!provenance)
                        t.measurement.provenance.reset();
                    else if (empty)
                        t.measurement.provenance = MeasurementProvenance{};
                    if (!checker && historical)
                        std::get<CharucoDefinition>(t.pattern).pattern_layout =
                            CharucoPatternLayout::white_square_at_origin_even_rows;
                    auto stored = get(ca::create_calibration_target(s, t));
                    auto loaded = get(ca::load_calibration_target(s, stored.descriptor.id));
                    CHECK(solve_detail::same_target(loaded.target, stored.value.target));
                    CHECK(loaded.revision.id == stored.value.revision.id);
                    CHECK(get(ca::encode_document(loaded)) == payload(s, stored.descriptor.id));
                }
    for (bool checker : {false, true}) {
        auto nominal = physical_target(checker);
        nominal.measurement = {};
        auto persisted = get(ca::create_calibration_target(s, nominal));
        CHECK(solve_detail::same_target(get(ca::load_calibration_target(s, persisted.descriptor.id)).target,
                                        persisted.value.target));
    }
    auto golden_target = physical_target();
    golden_target.identity = {{"target.canonical"}, 1};
    ca::TargetArtifact golden{{{"target.canonical"}, 1, 1}, golden_target};
    auto golden_bytes = get(ca::encode_document(golden));
    auto golden_id = s.begin(ca::target_type, {});
    s.append(golden_id, *document_packet(golden_bytes));
    auto golden_descriptor = s.finalize(golden_id);
    CHECK(content_hash(std::as_bytes(std::span(golden_bytes.data(), golden_bytes.size()))).hex ==
          "599fe7bd77973964");
    CHECK(golden_descriptor.hash.hex == "9d6bdf95ad8d8ade");
    auto t = physical_target();
    t.measurement = {};
    auto first = get(ca::create_calibration_target(s, t));
    auto bytes = get(ca::encode_document(first.value));
    CHECK(bytes == get(ca::encode_document(first.value)));
    auto duplicate = s.begin(ca::target_type, {});
    s.append(duplicate, *document_packet(bytes));
    auto duplicate_desc = s.finalize(duplicate);
    CHECK(first.descriptor.hash == duplicate_desc.hash);
    CHECK(first.descriptor.id != duplicate_desc.id);
    CHECK(payload(s, duplicate) == bytes);
    auto second = get(ca::create_calibration_target(s, t, first.value.revision.id));
    CHECK(second.value.revision.revision == 2);
    CHECK(second.descriptor.id != first.descriptor.id);
    CHECK(s.get(first.descriptor.id).hash == first.descriptor.hash);
    CHECK(get(ca::encode_document(get(ca::load_calibration_target(s, first.descriptor.id)))) == bytes);
    rejects(ca::create_calibration_target(s, first.value.target), Status::invalid_argument);
    auto malformed_utf8 = physical_target();
    malformed_utf8.measurement.provenance->note = std::string(1, char(0xff));
    CHECK(!ca::create_calibration_target(s, malformed_utf8, first.value.revision.id));
    CHECK(s.get(s.calibration_revision(first.value.revision.id, 3).artifact_id).state ==
          artifact::ArtifactState::recoverable);
    auto fourth = get(ca::create_calibration_target(s, physical_target(), first.value.revision.id));
    CHECK(fourth.value.revision.revision == 4);

    // Store filesystem failures must remain structured errors at the typed boundary.
    std::filesystem::rename(tmp.path / "objects", tmp.path / "objects.saved");
    std::ofstream(tmp.path / "objects") << "not a directory";
    rejects(ca::create_calibration_target(s, physical_target()), Status::io);
    std::filesystem::remove(tmp.path / "objects");
    std::filesystem::rename(tmp.path / "objects.saved", tmp.path / "objects");
    CHECK(s.get(first.descriptor.id).hash == first.descriptor.hash);

    auto bad = first.value;
    bad.target.grid.nominal_square_size_mm = std::numeric_limits<double>::infinity();
    CHECK(!ca::encode_document(bad));
    // Ensure exact round-trip of negative zero as well as nontrivial IEEE doubles.
    t = physical_target();
    t.measurement.provenance->width_uncertainty_mm = -0.0;
    auto zero = get(ca::create_calibration_target(s, t));
    CHECK(std::signbit(*get(ca::load_calibration_target(s, zero.descriptor.id))
                            .target.measurement.provenance->width_uncertainty_mm));
}
void corruption() {
    Temp tmp;
    artifact::Store s(tmp.path);
    auto source = get(ca::create_calibration_target(s, physical_target()));
    const auto original = Json::parse(payload(s, source.descriptor.id));
    auto corrupt = [&](auto mutate, Status expected = Status::corrupt) {
        auto reserved = s.begin_calibration(ca::target_type, {});
        auto j = original;
        j["payload"]["revision"]["id"]["value"] = reserved.reference.id.value;
        j["payload"]["revision"]["revision"] = reserved.reference.revision;
        j["payload"]["target"]["identity"]["id"]["value"] = reserved.reference.id.value;
        j["payload"]["target"]["identity"]["revision"] = reserved.reference.revision;
        mutate(j);
        s.append(reserved.artifact_id, *document_packet(j.dump()));
        s.finalize(reserved.artifact_id);
        rejects(ca::load_calibration_target(s, reserved.artifact_id), expected);
    };
    corrupt([](Json &j) { j["kind"] = "wrong"; });
    corrupt([](Json &j) { j["schema_version"] = 2; }, Status::incompatible);
    corrupt([](Json &j) { j["payload"]["target"].erase("grid"); });
    corrupt([](Json &j) { j["payload"]["target"]["pattern"]["type"] = "unknown"; });
    corrupt([](Json &j) { j["payload"]["target"]["pattern"]["definition"]["pattern_layout"] = "legacy"; });
    corrupt([](Json &j) { j["payload"]["target"]["grid"]["squares_x"] = -1; });
    corrupt([](Json &j) { j["payload"]["target"]["grid"]["nominal_square_size_mm"] = 40; });
    corrupt([](Json &j) { j["payload"]["target"]["grid"]["nominal_square_size_mm"] = nullptr; });
    corrupt([](Json &j) { j["payload"]["target"]["identity"]["revision"] = 22; });
    corrupt([](Json &j) {
        j["payload"]["revision"]["id"]["value"] = "different";
        j["payload"]["target"]["identity"]["id"]["value"] = "different";
    });
    corrupt([](Json &j) { j["unknown"] = true; });
    corrupt([](Json &j) { j["conventions"]["target_pose"] = "T_target_from_camera"; });
    auto packet_error = [&](auto mutate) {
        auto r = s.begin_calibration(ca::target_type, {});
        auto p = *document_packet(original.dump());
        mutate(p);
        s.append(r.artifact_id, p);
        s.finalize(r.artifact_id);
        rejects(ca::load_calibration_target(s, r.artifact_id));
    };
    packet_error([](data::Packet &p) { p.type.name = "wrong"; });
    packet_error([](data::Packet &p) { p.type.version = 2; });
    packet_error([](data::Packet &p) { p.attributes.clear(); });
    packet_error([](data::Packet &p) { p.attributes.push_back(p.attributes[0]); });
    packet_error([](data::Packet &p) { p.attributes[0].descriptor.name = "wrong.attribute"; });
    packet_error([](data::Packet &p) { p.attributes[0].descriptor.unit = "bytes"; });
    packet_error([](data::Packet &p) { p.header.received.nanoseconds = 42; });
    packet_error([](data::Packet &p) {
        auto &d = p.attributes[0].descriptor;
        d.shape = {1, d.shape[0]};
        d.stride = {d.shape[1], 1};
    });
    for (auto bytes : {original.dump() + " ", std::string("{\"kind\":\"org.mantis.CalibrationTarget\",") +
                                                  original.dump().substr(1)}) {
        auto r = s.begin_calibration(ca::target_type, {});
        s.append(r.artifact_id, *document_packet(bytes));
        s.finalize(r.artifact_id);
        rejects(ca::load_calibration_target(s, r.artifact_id));
    }
    for (const std::string &bytes : std::vector<std::string>{"{", "{\"x\":NaN}", "{\"x\":1e999}",
                                                             std::string("{\"x\":\"") + char(0xff) + "\"}",
                                                             "{\"kind\":\"x\",\"kind\":\"x\"}"}) {
        auto r = s.begin_calibration(ca::target_type, {});
        s.append(r.artifact_id, *document_packet(bytes));
        s.finalize(r.artifact_id);
        rejects(ca::load_calibration_target(s, r.artifact_id));
    }
    auto open = s.begin_calibration(ca::target_type, {});
    rejects(ca::load_calibration_target(s, open.artifact_id), Status::incompatible);
    s.append(open.artifact_id, *document_packet(original.dump()));
    s.append(open.artifact_id, *document_packet(original.dump()));
    s.finalize(open.artifact_id);
    rejects(ca::load_calibration_target(s, open.artifact_id));
    auto wrong = s.begin({"wrong", 1}, {});
    s.append(wrong, *document_packet(original.dump()));
    s.finalize(wrong);
    rejects(ca::load_calibration_target(s, wrong), Status::incompatible);
    sql(s, "UPDATE artifacts SET version=2 WHERE id='" + source.descriptor.id.value + "'");
    rejects(ca::load_calibration_target(s, source.descriptor.id), Status::incompatible);
    sql(s, "UPDATE artifacts SET version=1,chunks=0 WHERE id='" + source.descriptor.id.value + "'");
    rejects(ca::load_calibration_target(s, source.descriptor.id));
    sql(s, "UPDATE artifacts SET "
           "chunks=1,provenance=json_set(provenance,'$.inputs',json('[\"unexpected\"]')) WHERE id='" +
               source.descriptor.id.value + "'");
    rejects(ca::load_calibration_target(s, source.descriptor.id));
}
struct Graph {
    ca::Stored<ca::TargetArtifact> target;
    ca::Stored<ca::DatasetArtifact> dataset;
    ca::Stored<ca::CameraArtifact> left, right;
    ca::Stored<ca::RigArtifact> rig;
};
Graph graph(artifact::Store &s) {
    auto t = get(ca::create_calibration_target(s, physical_target()));
    std::vector<Id> captures{raw(s), raw(s)};
    std::sort(captures.begin(), captures.end());
    auto d = m5fixture::fixture(t.value.target, captures);
    auto stored = get(ca::create_calibration_dataset(s, d, t.reference()));
    MonoSolveConfig mono;
    mono.heldout_per_camera = 3;
    auto left = get(solve_camera_intrinsics(d, "left", mono)),
         right = get(solve_camera_intrinsics(d, "right", mono));
    ca::SolverImplementation implementation{std::string(solver_opencv_version())};
    auto l = get(ca::create_camera_calibration(s, left, stored.reference(), t.reference(), implementation));
    auto r = get(ca::create_camera_calibration(s, right, stored.reference(), t.reference(), implementation));
    StereoSolveConfig stereo;
    stereo.left_role = "left";
    stereo.right_role = "right";
    stereo.heldout_pairs = 3;
    stereo.rig_frame = {{"rig.fixture"}, "+X left to right, +Y forward, +Z cross; millimeters"};
    auto rig = get(solve_stereo_rig(d, left, right, stereo));
    auto persisted = get(ca::create_rig_calibration(s, rig, stored.reference(), t.reference(), l.reference(),
                                                    r.reference(), implementation));
    return {std::move(t), std::move(stored), std::move(l), std::move(r), std::move(persisted)};
}
void roundtrip() {
    Temp tmp;
    auto owner = std::make_unique<artifact::Store>(tmp.path);
    auto &s = *owner;
    auto g = graph(s);
    auto d = get(ca::load_calibration_dataset(s, g.dataset.descriptor.id));
    auto l = get(ca::load_camera_calibration(s, g.left.descriptor.id));
    auto r = get(ca::load_rig_calibration(s, g.rig.descriptor.id));
    CHECK(get(ca::encode_document(d)) == payload(s, g.dataset.descriptor.id));
    CHECK(get(ca::encode_document(l)) == payload(s, g.left.descriptor.id));
    CHECK(get(ca::encode_document(r)) == payload(s, g.rig.descriptor.id));
    CHECK(l.solution.final_model == g.left.value.solution.final_model);
    CHECK(r.solution.final_model.R_right_from_left == g.rig.value.solution.final_model.R_right_from_left);
    CHECK(r.solution.rig.T_rig_from_left.matrix == g.rig.value.solution.rig.T_rig_from_left.matrix);
    CHECK(d.raw_capture_references.size() == 2);
    CHECK(d.dataset.records.size() == 52);
    CHECK(d.dataset.records.front().key.frame.frameset_sequence == 100);
    CHECK(d.dataset.records.back().key.frame.frameset_sequence == 113);
    CHECK(get(quantize_diversity_descriptor(*d.dataset.records[2].diversity)) ==
          get(quantize_diversity_descriptor(*g.dataset.value.dataset.records[2].diversity)));
    // A graph may not substitute a revision merely because physical dimensions match.
    auto other = get(ca::create_calibration_target(s, physical_target(), g.target.value.revision.id));
    CHECK(!ca::create_calibration_dataset(s, d.dataset, other.reference()));
    auto wrong_hash = g.target.reference();
    wrong_hash.hash.hex = "deadbeef";
    rejects(ca::create_calibration_dataset(s, d.dataset, wrong_hash));
    CHECK(!ca::create_camera_calibration(s, l.solution, g.dataset.reference(), other.reference(),
                                         l.implementation));
    CHECK(!ca::create_rig_calibration(s, r.solution, g.dataset.reference(), g.target.reference(),
                                      g.right.reference(), g.left.reference(), r.implementation));
    // Corrupt a stored direct reference while keeping packet/hash/registry consistent.
    auto corrupt_ref = [&](bool camera_doc) {
        auto type = camera_doc ? ca::camera_type : ca::dataset_type;
        auto reserve = s.begin_calibration(type, camera_doc ? s.get(g.left.descriptor.id).provenance
                                                            : s.get(g.dataset.descriptor.id).provenance);
        auto j = Json::parse(payload(s, camera_doc ? g.left.descriptor.id : g.dataset.descriptor.id));
        j["payload"]["revision"]["id"]["value"] = reserve.reference.id.value;
        j["payload"]["revision"]["revision"] = reserve.reference.revision;
        j["payload"]["target_reference"]["hash"]["hex"] = "deadbeef";
        s.append(reserve.artifact_id, *document_packet(j.dump()));
        s.finalize(reserve.artifact_id);
        if (camera_doc)
            rejects(ca::load_camera_calibration(s, reserve.artifact_id));
        else
            rejects(ca::load_calibration_dataset(s, reserve.artifact_id));
    };
    corrupt_ref(false);
    corrupt_ref(true);
    auto graph_corruption = [&](const auto &original, const artifact::ArtifactType &type, auto mutate,
                                auto load) {
        auto provenance = original.descriptor.provenance;
        auto j = Json::parse(payload(s, original.descriptor.id));
        mutate(j, provenance);
        auto reserved = s.begin_calibration(type, provenance);
        j["payload"]["revision"]["id"]["value"] = reserved.reference.id.value;
        j["payload"]["revision"]["revision"] = reserved.reference.revision;
        s.append(reserved.artifact_id, *document_packet(j.dump()));
        s.finalize(reserved.artifact_id);
        rejects(load(s, reserved.artifact_id));
    };
    graph_corruption(
        g.dataset, ca::dataset_type,
        [&](Json &j, artifact::Provenance &provenance) {
            j["payload"]["target_reference"]["id"]["value"] = other.descriptor.id.value;
            j["payload"]["target_reference"]["hash"]["hex"] = other.descriptor.hash.hex;
            provenance.inputs[0] = other.descriptor.id;
        },
        ca::load_calibration_dataset);
    graph_corruption(
        g.left, ca::camera_type,
        [](Json &j, artifact::Provenance &) {
            j["payload"]["solution"]["target"]["identity"]["revision"] = 2;
        },
        ca::load_camera_calibration);
    graph_corruption(
        g.rig, ca::rig_type,
        [](Json &j, artifact::Provenance &provenance) {
            std::swap(j["payload"]["left_camera_reference"], j["payload"]["right_camera_reference"]);
            std::swap(provenance.inputs[2], provenance.inputs[3]);
        },
        ca::load_rig_calibration);
    graph_corruption(
        g.rig, ca::rig_type,
        [](Json &j, artifact::Provenance &) {
            j["payload"]["left_camera_reference"]["hash"]["hex"] = "bad-hash";
        },
        ca::load_rig_calibration);
    // Every byte of evidence survives close/reopen; finalized files are immutable.
    auto old_hash = g.rig.descriptor.hash;
    auto old_bytes = payload(s, g.rig.descriptor.id);
    auto revision2 = get(ca::create_rig_calibration(
        s, r.solution, g.dataset.reference(), g.target.reference(), g.left.reference(), g.right.reference(),
        r.implementation, g.rig.value.revision.id));
    CHECK(revision2.value.revision.revision == 2);
    CHECK(payload(s, g.rig.descriptor.id) == old_bytes && s.get(g.rig.descriptor.id).hash == old_hash);
    auto dataset_bytes = payload(s, g.dataset.descriptor.id), camera_bytes = payload(s, g.left.descriptor.id);
    owner.reset();
    artifact::Store reopened(tmp.path);
    CHECK(get(ca::encode_document(get(ca::load_calibration_dataset(reopened, g.dataset.descriptor.id)))) ==
          dataset_bytes);
    CHECK(get(ca::encode_document(get(ca::load_camera_calibration(reopened, g.left.descriptor.id)))) ==
          camera_bytes);
    CHECK(get(ca::encode_document(get(ca::load_rig_calibration(reopened, g.rig.descriptor.id)))) ==
          old_bytes);
    std::cout << "OpenCV " << solver_opencv_version()
              << "; full target/dataset/camera/rig graph round-trip passed\n";
}
void binding() {
    Temp tmp;
    artifact::Store s(tmp.path);
    auto g = graph(s);
    Id device{"org.mantis.x1:camera.left:camera.right"};
    CHECK(!s.active_calibration(device));
    get(ca::activate_rig_calibration(s, device, g.rig.descriptor.id));
    CHECK(s.active_calibration(device)->reference.revision == 1);
    auto v2 = get(ca::create_rig_calibration(s, g.rig.value.solution, g.dataset.reference(),
                                             g.target.reference(), g.left.reference(), g.right.reference(),
                                             g.rig.value.implementation, g.rig.value.revision.id));
    CHECK(s.active_calibration(device)->artifact.id == g.rig.descriptor.id);
    get(ca::activate_rig_calibration(s, device, v2.descriptor.id));
    CHECK(s.active_calibration(device)->reference.id == g.rig.value.revision.id);
    CHECK(s.active_calibration(device)->reference.revision == 2);
    CHECK(s.active_calibration(device)->artifact.hash == v2.descriptor.hash);
    CHECK(s.get(g.rig.descriptor.id).hash == g.rig.descriptor.hash);
    rejects(ca::activate_rig_calibration(s, {"org.mantis.x1:other:camera.right"}, v2.descriptor.id),
            Status::incompatible);
    std::vector<ca::CameraComponent> matching{{"left", {"camera.left"}, 1280, 960},
                                              {"right", {"camera.right"}, 1280, 960}};
    get(ca::validate_rig_device(v2.value, device, matching));
    auto components = matching;
    components[0].camera_id = {"wrong"};
    rejects(ca::activate_rig_calibration(s, device, v2.descriptor.id, components), Status::incompatible);
    components = matching;
    components[0].image_width = 1024;
    auto width_mismatch = ca::validate_rig_device(v2.value, device, components);
    rejects(width_mismatch, Status::incompatible);
    CHECK(width_mismatch.error().message.find("left component: calibrated 1280x960, discovered 1024x960") !=
          std::string::npos);
    rejects(ca::activate_rig_calibration(s, device, v2.descriptor.id, components), Status::incompatible);
    components = matching;
    components[1].image_height = 800;
    auto height_mismatch = ca::validate_rig_device(v2.value, device, components);
    rejects(height_mismatch, Status::incompatible);
    CHECK(height_mismatch.error().message.find("right component: calibrated 1280x960, discovered 1280x800") !=
          std::string::npos);
    components = matching;
    components[0].role = "unknown";
    rejects(ca::validate_rig_device(v2.value, device, components), Status::incompatible);
    CHECK(s.active_calibration(device)->artifact.id == v2.descriptor.id);
    auto reversed_config = g.rig.value.solution.config;
    reversed_config.left_role = "right";
    reversed_config.right_role = "left";
    reversed_config.rig_frame = {{"rig.reversed"}, "explicitly reversed solve roles; millimeters"};
    auto reversed_solution = get(solve_stereo_rig(g.dataset.value.dataset, g.right.value.solution,
                                                  g.left.value.solution, reversed_config));
    auto reversed =
        get(ca::create_rig_calibration(s, reversed_solution, g.dataset.reference(), g.target.reference(),
                                       g.right.reference(), g.left.reference(), g.rig.value.implementation));
    get(ca::activate_rig_calibration(s, device, reversed.descriptor.id, matching));
    CHECK(s.active_calibration(device)->artifact.id == reversed.descriptor.id);
    s.clear_active_calibration(device);
    CHECK(!s.active_calibration(device));
    CHECK(s.get(g.rig.descriptor.id).hash == g.rig.descriptor.hash);
}
int main(int argc, char **argv) {
    try {
        std::string mode = argc > 1 ? argv[1] : "codec";
        if (mode == "binding")
            binding();
        else {
            targets();
            corruption();
            roundtrip();
        }
        std::cout << mode << " passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
