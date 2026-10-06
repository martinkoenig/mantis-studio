#include "../fixtures/calibration_api.hpp"
#include <mantis/service_adapter.hpp>
#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>
using namespace mantis;
namespace ca = calibration::artifacts;
#define CHECK(...) do { if (!(__VA_ARGS__)) throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__); } while (false)
template <class F> void rejects(F call, Status status) {
    try { call(); } catch (const Failure &f) { CHECK(f.error.code == status); return; }
    throw std::runtime_error("Expected failure");
}
jobs::Snapshot wait(services::Runtime &runtime, const Id &id) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (std::chrono::steady_clock::now() < end) {
        for (const auto &j : runtime.jobs()) if (j.id == id && j.state != jobs::State::queued && j.state != jobs::State::running) return j;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Job timeout");
}
Id completed(services::Runtime &runtime, const Id &job) {
    auto j = wait(runtime, job); CHECK(j.state == jobs::State::completed); CHECK(j.result); return j.result->id;
}
wire::v1::Response dispatch(services::Runtime &r, wire::v1::Request q, std::optional<Status> error = {}) {
    q.set_protocol_version(1); q.set_request_id("m6.test");
    // Exercise actual generated serialization and parsing on both sides of dispatch.
    wire::v1::Request parsed; CHECK(parsed.ParseFromString(q.SerializeAsString()));
    auto result = protocol::dispatch(r, parsed); wire::v1::Response out;
    CHECK(out.ParseFromString(result.SerializeAsString())); CHECK(out.request_id() == "m6.test");
    if (error) { CHECK(out.has_error()); CHECK(out.error().code() == static_cast<uint32_t>(*error)); }
    else if (out.has_error()) throw std::runtime_error(out.error().message());
    CHECK(out.ByteSizeLong() < protocol::max_control_bytes); return out;
}
void exercise(services::Runtime &r) {
    auto store = r.project_store();
    auto spec = m6fixture::physical_target(true);
    services::TargetCreate request{{spec.grid, spec.pattern, spec.measurement}, {}};
    auto rev1 = r.create_calibration_target(request); CHECK(rev1.entry.reference.revision == 1);
    auto old_hash = rev1.entry.artifact.hash;
    CHECK(std::get<services::TargetInfo>(rev1.detail).target.measurement.provenance.has_value());
    request.series_id = rev1.entry.reference.id.value; request.target.grid.nominal_square_size_mm = 41;
    auto rev2 = r.create_calibration_target(request); CHECK(rev2.entry.reference.revision == 2);
    CHECK(r.calibration_info(rev1.entry.artifact.id).entry.artifact.hash == old_hash);
    auto old = m6fixture::checked(ca::load_calibration_target(*store, rev1.entry.artifact.id));
    CHECK(old.target.grid.nominal_square_size_mm == 40);
    auto geometry = m6fixture::checked(calibration::derive_target_geometry(old.target));
    CHECK(std::abs(geometry.effective_square_pitch_x_mm - 41.3) < 1e-12); CHECK(std::abs(geometry.effective_square_pitch_y_mm - 40.6) < 1e-12);
    request.target.grid.squares_x = 0;
    auto count = r.calibrations().size(); rejects([&] { r.create_calibration_target(request); }, Status::invalid_argument);
    CHECK(r.calibrations().size() == count); request.target.grid.squares_x = 8;
    auto seeded = m6fixture::seed(*store);
    request.series_id = seeded.dataset.value.revision.id.value;
    rejects([&] { r.create_calibration_target(request); }, Status::incompatible);
    // Incomplete registry entries are listed without decoding a nonexistent document.
    auto open = store->begin_calibration(ca::target_type, {});
    auto entries = r.calibrations();
    CHECK(std::any_of(entries.begin(), entries.end(), [&](const auto &e) { return e.artifact.id == open.artifact_id && e.reference.revision == 1; }));
    rejects([&] { r.calibration_info(open.artifact_id); }, Status::incompatible);
    services::DatasetBuild build{seeded.checker.descriptor.id, seeded.raw_ids, {"right", "left"}, 3, {}};
    auto dataset = completed(r, r.build_calibration_dataset(build));
    CHECK(store->get(dataset).type.name == ca::dataset_type.name);
    auto built = m6fixture::checked(ca::load_calibration_dataset(*store, dataset));
    CHECK(built.target_reference.id == seeded.checker.descriptor.id);
    auto second = completed(r, r.build_calibration_dataset(build));
    auto rebuilt = m6fixture::checked(ca::load_calibration_dataset(*store, second));
    CHECK(built.dataset.records.size() == rebuilt.dataset.records.size());
    for (size_t i=0; i < built.dataset.records.size(); ++i) {
        CHECK(built.dataset.records[i].key == rebuilt.dataset.records[i].key);
        CHECK(built.dataset.records[i].selection_rank == rebuilt.dataset.records[i].selection_rank);
    }
    auto cancelled = r.build_calibration_dataset(build); r.cancel_job(cancelled);
    auto cj = wait(r, cancelled); CHECK(cj.state == jobs::State::cancelled); CHECK(!cj.result);
    CHECK(r.calibrations().size() == entries.size() + 2);
    auto invalid = build; invalid.raw_capture_artifact_ids = {seeded.checker.descriptor.id};
    rejects([&] { r.build_calibration_dataset(invalid); }, Status::incompatible);
    auto nonfinal = store->begin({"org.mantis.RawCapture", 2}, {}); invalid.raw_capture_artifact_ids = {nonfinal};
    rejects([&] { r.build_calibration_dataset(invalid); }, Status::incompatible);
    invalid = build; invalid.raw_capture_artifact_ids.push_back(build.raw_capture_artifact_ids[0]);
    rejects([&] { r.build_calibration_dataset(invalid); }, Status::invalid_argument);
    invalid = build; invalid.camera_roles = {"left", "left"};
    rejects([&] { r.build_calibration_dataset(invalid); }, Status::invalid_argument);
    invalid = build; invalid.max_selected_per_camera = 0;
    rejects([&] { r.build_calibration_dataset(invalid); }, Status::invalid_argument);
    invalid = build; invalid.series_id = seeded.charuco.value.revision.id.value;
    rejects([&] { r.build_calibration_dataset(invalid); }, Status::incompatible);
    invalid = build; invalid.raw_capture_artifact_ids.resize(1025, seeded.raw_ids[0]);
    rejects([&] { r.build_calibration_dataset(invalid); }, Status::invalid_argument);
    rejects([&] { r.solve_camera_calibration({seeded.dataset.descriptor.id, "missing", 3, {}}); }, Status::invalid_argument);
    rejects([&] { r.solve_camera_calibration({seeded.dataset.descriptor.id, "left", 0, {}}); }, Status::invalid_argument);
    rejects([&] { r.solve_camera_calibration({{}, "left", 3, {}}); }, Status::invalid_argument);
    auto left = completed(r, r.solve_camera_calibration({seeded.dataset.descriptor.id, "left", 3, {}}));
    auto right = completed(r, r.solve_camera_calibration({seeded.dataset.descriptor.id, "right", 3, {}}));
    auto lc = m6fixture::checked(ca::load_camera_calibration(*store, left));
    CHECK(lc.solution.camera.role == "left"); CHECK(lc.dataset_reference.id == seeded.dataset.descriptor.id);
    CHECK(lc.implementation.opencv_version == calibration::solver_opencv_version());
    CHECK(lc.implementation.mantis_version == application_version); CHECK(lc.implementation.mantis_build == build_version);
    auto l2 = completed(r, r.solve_camera_calibration({seeded.dataset.descriptor.id, "left", 3, lc.revision.id.value}));
    CHECK(m6fixture::checked(ca::load_camera_calibration(*store, l2)).revision.revision == 2);
    services::RigSolve rig{seeded.dataset.descriptor.id, left, right, 3, {{"fixture.rig"}, "Fixture rig"}, {}};
    auto solved = completed(r, r.solve_rig_calibration(rig));
    auto rs = m6fixture::checked(ca::load_rig_calibration(*store, solved));
    CHECK(rs.left_camera_reference.id == left && rs.right_camera_reference.id == right);
    CHECK(rs.dataset_reference.id == seeded.dataset.descriptor.id && rs.target_reference.id == seeded.charuco.descriptor.id);
    auto reversed = rig; std::swap(reversed.left_camera_artifact_id, reversed.right_camera_artifact_id);
    auto reversed_id = completed(r, r.solve_rig_calibration(reversed));
    auto rr = m6fixture::checked(ca::load_rig_calibration(*store, reversed_id));
    CHECK(rr.solution.config.left_role == "right" && rr.solution.config.right_role == "left");
    CHECK(rr.left_camera_reference.id == right); CHECK(std::abs(rr.solution.rig.baseline_mm - rs.solution.rig.baseline_mm) < .001);
    auto badrig = rig; badrig.right_camera_artifact_id = left;
    rejects([&] { r.solve_rig_calibration(badrig); }, Status::invalid_argument);
    badrig = rig; badrig.rig_frame.name.clear(); rejects([&] { r.solve_rig_calibration(badrig); }, Status::invalid_argument);
    auto checker_left = completed(r, r.solve_camera_calibration({seeded.checker_dataset.descriptor.id, "left", 3, {}}));
    auto checker_right = completed(r, r.solve_camera_calibration({seeded.checker_dataset.descriptor.id, "right", 3, {}}));
    badrig = rig; badrig.left_camera_artifact_id = checker_left;
    rejects([&] { r.solve_rig_calibration(badrig); }, Status::incompatible);
    badrig.dataset_artifact_id = seeded.checker_dataset.descriptor.id; badrig.right_camera_artifact_id = checker_right;
    rejects([&] { r.solve_rig_calibration(badrig); }, Status::incompatible);
    // Offline is an exact project-local binding, without fabricated current geometry.
    CHECK(!r.active_calibration({"offline.device"})); r.activate_calibration({"offline.device"}, solved);
    auto active = r.active_calibration({"offline.device"}); CHECK(active && active->artifact.id == solved);
    CHECK(active->artifact.hash == store->get(solved).hash); CHECK(active->reference.id == rs.revision.id);
    auto before = r.calibrations().size(); r.clear_calibration({"offline.device"});
    CHECK(!r.active_calibration({"offline.device"})); CHECK(r.calibrations().size() == before);
    // All commands through generated wire + adapter, including structured failures.
    wire::v1::Request q; auto *t = q.mutable_calibration_target_create()->mutable_target();
    t->set_squares_x(8); t->set_squares_y(6); t->set_nominal_square_size_mm(40); t->mutable_checkerboard();
    auto wire_target = dispatch(r,q); CHECK(wire_target.calibration_info().has_target_info());
    CHECK(!wire_target.calibration_info().target_info().target().measurement().has_provenance());
    t->mutable_measurement()->set_active_width_mm(330.4); t->mutable_measurement()->set_active_height_mm(243.6);
    t->mutable_measurement()->mutable_provenance();
    auto present = dispatch(r,q); CHECK(present.calibration_info().target_info().target().measurement().has_provenance());
    q.mutable_calibration_target_create()->set_series_id(present.calibration_info().entry().reference().id());
    CHECK(dispatch(r,q).calibration_info().entry().reference().revision() == 2);
    t->mutable_charuco()->set_dictionary("DICT_6X6_250"); t->mutable_charuco()->set_nominal_marker_size_mm(25);
    t->mutable_charuco()->set_pattern_layout("bad"); dispatch(r,q,Status::invalid_argument);
    t->mutable_charuco()->set_pattern_layout("black_square_at_origin"); CHECK(dispatch(r,q).calibration_info().has_target_info());
    q.Clear(); auto *d = q.mutable_calibration_dataset_build(); d->set_target_artifact_id(seeded.checker.descriptor.id.value);
    for (auto id : seeded.raw_ids) d->add_raw_capture_artifact_ids(id.value);
    d->add_camera_roles("left"); d->set_max_selected_per_camera(3);
    auto dj = dispatch(r,q); CHECK(!dj.result_id().empty()); completed(r,{dj.result_id()});
    q.Clear(); auto *c = q.mutable_calibration_camera_solve(); c->set_dataset_artifact_id(seeded.dataset.descriptor.id.value);
    c->set_camera_role("left"); c->set_heldout_per_camera(3);
    auto cp = completed(r,{dispatch(r,q).result_id()}); CHECK(store->get(cp).type.name == ca::camera_type.name);
    q.Clear(); auto *g = q.mutable_calibration_rig_solve(); g->set_dataset_artifact_id(seeded.dataset.descriptor.id.value);
    g->set_left_camera_artifact_id(left.value); g->set_right_camera_artifact_id(right.value); g->set_heldout_pairs(3);
    g->set_rig_frame_id("wire.rig"); g->set_rig_frame_name("Wire rig"); completed(r,{dispatch(r,q).result_id()});
    q.Clear(); q.mutable_calibrations_list(); auto listed = dispatch(r,q); CHECK(listed.calibrations_size() == static_cast<int>(r.calibrations().size()));
    for (int i=1; i<listed.calibrations_size(); ++i) {
        const auto &a=listed.calibrations(i-1), &b=listed.calibrations(i);
        CHECK(std::tuple{a.artifact().type(),a.reference().id(),a.reference().revision(),a.artifact().id()} <
              std::tuple{b.artifact().type(),b.reference().id(),b.reference().revision(),b.artifact().id()});
    }
    for (auto id : {seeded.charuco.descriptor.id, seeded.dataset.descriptor.id, left, solved}) {
        q.Clear(); q.mutable_calibration_info()->set_id(id.value); auto info = dispatch(r,q);
        CHECK(info.calibration_info().detail_case() != wire::v1::CalibrationInfo::DETAIL_NOT_SET);
        CHECK(info.ByteSizeLong() < 16384); // Full persisted dataset/camera evidence is much larger.
        if (info.calibration_info().has_rig_info()) CHECK(info.calibration_info().rig_info().t_rig_from_left().matrix_size() == 16);
    }
    q.Clear(); q.mutable_calibration_info()->set_id("nonexistent"); dispatch(r,q,Status::not_found);
    q.mutable_calibration_info()->set_id(std::string(4097,'a')); dispatch(r,q,Status::invalid_argument);
    q.mutable_calibration_info()->set_id(nonfinal.value); dispatch(r,q,Status::incompatible);
    q.Clear(); q.mutable_calibration_active()->set_id("wire.device"); CHECK(!dispatch(r,q).active_calibration().has_binding());
    q.Clear(); q.mutable_calibration_activate()->set_logical_device_id("wire.device"); q.mutable_calibration_activate()->set_rig_artifact_id(solved.value); dispatch(r,q);
    q.Clear(); q.mutable_calibration_active()->set_id("wire.device"); CHECK(dispatch(r,q).active_calibration().binding().artifact().id() == solved.value);
    q.Clear(); q.mutable_calibration_clear()->set_id("wire.device"); dispatch(r,q);
    q.Clear(); q.mutable_calibration_active()->set_id("wire.device"); CHECK(!dispatch(r,q).active_calibration().has_binding());
    q.Clear(); q.mutable_artifacts_list(); CHECK(dispatch(r,q).artifacts_size() > 0);
    // Oversized registry lists return a bounded structured error rather than sending >4 MiB.
    artifact::Provenance large_descriptor; large_descriptor.producer = std::string(4096, 'p');
    for (unsigned i = 0; i < 1100; ++i) store->begin_calibration(ca::target_type, large_descriptor);
    q.Clear(); q.mutable_calibrations_list(); auto oversized = dispatch(r, q, Status::busy);
    CHECK(oversized.calibrations_size() == 0 && oversized.ByteSizeLong() < 1024);
    auto event_log = r.events(0);
    CHECK(std::any_of(event_log.begin(), event_log.end(), [](const auto &e) { return e.kind == "calibration.target.created"; }));
}
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "seed") {
            artifact::Store store(argv[2]); auto f=m6fixture::seed(store);
            std::cout << nlohmann::json{{"target",f.charuco.descriptor.id.value},{"checker",f.checker.descriptor.id.value},
                {"dataset",f.dataset.descriptor.id.value},{"checker_dataset",f.checker_dataset.descriptor.id.value},
                {"raw",{f.raw_ids[0].value,f.raw_ids[1].value}}}.dump() << '\n'; return 0;
        }
        auto root = std::filesystem::temp_directory_path() / Id::random().value;
        std::filesystem::create_directories(root / "plugins");
        try { services::Runtime runtime({root / "plugins", {}, root / "project", {}, {}}); exercise(runtime); }
        catch (...) { std::filesystem::remove_all(root); throw; }
        std::filesystem::remove_all(root); std::cout << "CalibrationService and generated protocol dispatch passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
