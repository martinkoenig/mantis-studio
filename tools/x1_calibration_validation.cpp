// INTERNAL M8 validation executable. Never linked into production clients.
#include <mantis/calibration_artifacts.hpp>
#include <mantis/calibration_solver_opencv.hpp>
#include <mantis/calibration_opencv.hpp>
#include <mantis/image_layout.hpp>
#include <opencv2/aruco/charuco.hpp>
#include <opencv2/imgcodecs.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <locale>
using namespace mantis;
using namespace mantis::calibration;
namespace ca = mantis::calibration::artifacts;
using Json = nlohmann::json;
template<class T> T get(Result<T> r) { if (!r) throw Failure(r.error()); return std::move(*r); }
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
Json document(auto const &value) { return Json::parse(get(ca::encode_document(value))); }
Json descriptor(const artifact::Store &store, const Id &id) {
    auto d = store.get(id);
    return {{"id", id.value}, {"hash", d.hash.algorithm + ":" + d.hash.hex}, {"type", d.type.name},
            {"schema_version", d.type.schema_version}, {"state", artifact::state_name(d.state)},
            {"provenance_parameters", d.provenance.parameters}};
}
CalibrationTarget target_from_spec(const Json &s) {
    CalibrationTarget t;
    t.identity = {{"validation.target"}, 1};
    t.grid = {s.at("squares_x").get<uint32_t>(), s.at("squares_y").get<uint32_t>(), s.at("nominal_square_size_mm")};
    require(s.at("layout") == "black_square_at_origin", "Generator supports only black_square_at_origin");
    if (s.at("target_type") == "charuco") {
        require(s.at("dictionary") == "DICT_6X6_250", "Generator supports DICT_6X6_250 only");
        t.pattern = CharucoDefinition{"DICT_6X6_250", s.at("nominal_marker_size_mm")};
    } else require(s.at("target_type") == "checkerboard", "Unknown target type");
    auto valid = validate_target(t); if (!valid) throw Failure(valid.error());
    return t;
}
Json markers(const Json &s) {
    auto t = target_from_spec(s);
    require(uint64_t(t.grid.squares_x) * t.grid.squares_y <= 500, "Board exceeds dictionary/generator budget");
    auto dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250);
    Json bits = Json::array();
    for (uint32_t id = 0; id < t.grid.squares_x * t.grid.squares_y / 2; ++id) {
        cv::Mat image;
#if CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR < 7
        cv::aruco::drawMarker(dict, int(id), 8, image, 1);
#else
        cv::aruco::generateImageMarker(dict, int(id), 8, image, 1);
#endif
        Json rows = Json::array();
        for (int y = 0; y < 8; ++y) { std::string row; for (int x = 0; x < 8; ++x) row += image.at<uint8_t>(y,x) ? '1' : '0'; rows.push_back(row); }
        bits.push_back(rows);
    }
    return {{"markers", bits}, {"opencv_version", solver_opencv_version()}};
}
Json detect(const Json &s) {
    auto t = target_from_spec(s.at("specification"));
    auto image = cv::imread(s.at("image").get<std::string>(), cv::IMREAD_GRAYSCALE);
    require(!image.empty() && image.isContinuous(), "Invalid rendered image");
    ObservationSource source{{"validation.raw"}, 1, {"validation.camera"}, "left"};
    auto result = get(opencv::detect_target(t, {{image.ptr<uint8_t>(), image.total()}, uint32_t(image.cols), uint32_t(image.rows), size_t(image.cols)}, source));
    require(result.has_value(), "Generated target not detected by M2");
    Json points = Json::array();
    for (size_t i=0; i<result->point_ids.size(); ++i) points.push_back({{"id", result->point_ids[i]}, {"x_mm", result->object_points_mm[i].x_mm}, {"y_mm", result->object_points_mm[i].y_mm}, {"x_px", result->image_points_px[i].x_px}, {"y_px", result->image_points_px[i].y_px}});
    return {{"points", points}};
}
struct Session {
    std::shared_ptr<artifact::Store> store;
    ca::TargetArtifact target;
    ca::DatasetArtifact dataset;
    ca::CameraArtifact left, right;
    std::optional<ca::RigArtifact> rig;
    explicit Session(const Json &s, std::shared_ptr<artifact::Store> shared = {}): store(shared ? shared : std::make_shared<artifact::Store>(s.at("project").get<std::string>())),
        target(get(ca::load_calibration_target(*store, {s.at("target")}))),
        dataset(get(ca::load_calibration_dataset(*store, {s.at("dataset")}))),
        left(get(ca::load_camera_calibration(*store, {s.at("left")}))),
        right(get(ca::load_camera_calibration(*store, {s.at("right")}))) {
        require(dataset.target_reference.id.value == s.at("target").get<std::string>() && left.dataset_reference.id.value == s.at("dataset").get<std::string>() && right.dataset_reference.id.value == s.at("dataset").get<std::string>(), "Session artifact lineage mismatch");
        require(left.solution.camera.role == "left" && right.solution.camera.role == "right", "Wrong calibration camera roles");
        if (s.contains("rig") && !s.at("rig").is_null()) {
            rig = get(ca::load_rig_calibration(*store, {s.at("rig")}));
            require(rig->dataset_reference.id.value == s.at("dataset").get<std::string>() && rig->left_camera_reference.id.value == s.at("left").get<std::string>() && rig->right_camera_reference.id.value == s.at("right").get<std::string>(), "Rig lineage mismatch");
        }
    }
};
Json export_session(const Json &s) {
    Session v(s);
    Json out{{"target", document(v.target)}, {"dataset", document(v.dataset)},
             {"left", document(v.left)}, {"right", document(v.right)},
             {"rig", v.rig ? document(*v.rig) : Json(nullptr)}, {"artifacts", Json::object()}};
    for (auto field : {"target", "dataset", "left", "right", "rig"}) if (s.contains(field) && !s.at(field).is_null()) out["artifacts"][field] = descriptor(*v.store, {s.at(field)});
    out["raw_captures"] = Json::array();
    for (auto &id : v.dataset.dataset.raw_capture_ids) out["raw_captures"].push_back(descriptor(*v.store,id));
    return out;
}
// Uses the SAME fixed-model evidence helpers as M4; only target poses are estimated.
Json evaluate(const Json &s) {
    Session a(s.at("calibration_session"));
    const auto destination = std::filesystem::weakly_canonical(s.at("dataset_session").at("project").get<std::string>());
    Session b(s.at("dataset_session"), destination == std::filesystem::weakly_canonical(a.store->root()) ? a.store : nullptr);
    require(a.store->root() != b.store->root() || a.dataset.dataset.raw_capture_ids != b.dataset.dataset.raw_capture_ids, "Independent dataset required");
    for (auto &id : a.dataset.dataset.raw_capture_ids) require(std::find(b.dataset.dataset.raw_capture_ids.begin(), b.dataset.dataset.raw_capture_ids.end(), id) == b.dataset.dataset.raw_capture_ids.end() || a.store->root() != b.store->root(), "Shared source capture is not independent");
    require(solve_detail::same_target(a.target.target, b.target.target), "Fixed calibration requires exact physical target revision/geometry");
    require(a.rig && b.rig, "ChArUco rig sessions required");
    Json out{{"schema_version",1},{"calibration_session",s.at("calibration_session").at("name")},{"dataset_session",s.at("dataset_session").at("name")},{"parameters_fixed",true}};
    for (auto camera : {&a.left.solution, &a.right.solution}) {
        auto found = std::find_if(b.dataset.dataset.cameras.begin(), b.dataset.dataset.cameras.end(), [&](auto &c){return solve_detail::same_camera(c,camera->camera);});
        require(found != b.dataset.dataset.cameras.end(), "Independent camera identity/geometry mismatch");
        std::vector<double> residuals; uint64_t count{}, skipped{};
        for (auto &record : b.dataset.dataset.records) if (record.selection_rank && record.key.camera_role == camera->camera.role) {
            require(record.observation.has_value(), "Selected observation missing");
            if (!solve_detail::eligible(solver_eligibility(record.observation->object_points_mm))) {++skipped; continue;}
            auto evidence = get(evaluate_heldout_target(*record.observation, camera->final_model));
            residuals.insert(residuals.end(), evidence.residuals_px.begin(), evidence.residuals_px.end()); ++count;
        }
        auto summary = get(summarize_residuals(residuals));
        out[camera->camera.role] = {{"usable_observations",count},{"skipped_ineligible",skipped},{"residuals",{{"point_count",summary.point_count},{"rms_px",summary.rms_px},{"mean_px",summary.mean_px},{"median_px",summary.median_px},{"p95_px",summary.p95_px},{"max_px",summary.max_px}}}};
    }
    auto partition = get(partition_stereo_samples(b.dataset.dataset, b.rig->solution.config));
    std::vector<double> residuals; uint64_t count{}, skipped{};
    for (auto &sample : partition.samples) {
        if (!solve_detail::eligible(sample.disposition)) {++skipped; continue;}
        auto l = solve_detail::record(b.dataset.dataset, sample.key, a.left.solution.camera);
        auto r = solve_detail::record(b.dataset.dataset, sample.key, a.right.solution.camera);
        require(l && r && l->observation && r->observation, "Missing stereo observation");
        auto common = get(intersect_observations(*l->observation,*r->observation));
        auto evidence = get(evaluate_epipolar_pair(sample.key,common,a.rig->solution.final_model,a.left.solution.final_model,a.right.solution.final_model));
        residuals.insert(residuals.end(), evidence.symmetric_epipolar_residuals_px.begin(), evidence.symmetric_epipolar_residuals_px.end()); ++count;
    }
    auto summary = get(summarize_residuals(residuals));
    out["rig"]={{"usable_pairs",count},{"skipped_ineligible",skipped},{"residuals",{{"point_count",summary.point_count},{"rms_px",summary.rms_px},{"mean_px",summary.mean_px},{"median_px",summary.median_px},{"p95_px",summary.p95_px},{"max_px",summary.max_px}}}};
    return out;
}
Json binding(const Json &s) {
    artifact::Store store(s.at("project").get<std::string>());
    auto rig = get(ca::load_rig_calibration(store, {s.at("rig")}));
    Id raw{s.at("raw")}; auto d = store.get(raw);
    require(d.type.name == "org.mantis.RawCapture" && d.type.schema_version == 2 && d.state == artifact::ArtifactState::finalized, "Expected finalized schema-2 RawCapture");
    auto same = [&](const Reference &r) {return r.id == rig.revision.id && r.schema_version == rig.revision.schema_version && r.revision == rig.revision.revision;};
    require(same(d.provenance.calibration), "Raw provenance binding mismatch");
    auto params = d.provenance.parameters;
    require(d.provenance.producer == "org.mantis.x1" && !params.at("producer_plugin_version").empty(), "Source plugin provenance missing");
    require(params.at("logical_device_id") == s.at("logical_device_id").get<std::string>(), "Source logical identity mismatch");
    auto compatible=ca::validate_rig_device(rig,{params.at("logical_device_id")});
    if(!compatible) throw Failure(compatible.error());
    auto rig_descriptor=store.get({s.at("rig")});
    require(std::find(d.provenance.inputs.begin(),d.provenance.inputs.end(),rig_descriptor.id)!=d.provenance.inputs.end() &&
            params.at("active_rig_artifact_id")==rig_descriptor.id.value &&
            params.at("active_rig_artifact_hash")==rig_descriptor.hash.hex &&
            params.at("active_rig_artifact_hash_algorithm")==rig_descriptor.hash.algorithm, "Exact active artifact provenance mismatch");
    require(params.at("active_calibration_id")==rig.revision.id.value &&
            params.at("active_calibration_schema_version")==std::to_string(rig.revision.schema_version) &&
            params.at("active_calibration_revision")==std::to_string(rig.revision.revision), "Exact active reference provenance mismatch");
    require(params.at("source_device_calibration_id") == s.at("source_id").get<std::string>() && params.at("source_device_calibration_schema_version") == "1" && params.at("source_device_calibration_revision") == s.at("source_revision").get<std::string>(), "Source device provenance mismatch");
    uint64_t count{};
    store.replay(raw,[&](data::Published p) {
        require(p->type == schema::frameset && p->frames.size()==2 && same(p->header.calibration), "Parent binding mismatch");
        std::set<std::string> roles;
        for (auto &f : p->frames) {
            require(same(f->header.calibration), "Child binding mismatch");
            auto role=f->header.metadata.at("role"); require(roles.insert(role).second, "Duplicate role");
            auto &camera= role == "left" ? rig.solution.left_camera : rig.solution.right_camera;
            require(role == camera.role && f->header.metadata.at("identity") == camera.camera_id.value, "Recorded physical identity mismatch");
            auto layout=data::image_layout(*f);
            require(layout.height==camera.image_height && layout.width==camera.image_width, "Recorded geometry mismatch");
        }
        ++count;
    });
    require(count>0,"No bound frames");
    return {{"schema_version",1},{"status","PASS"},{"framesets",count},{"raw",descriptor(store,raw)},{"binding",document(rig).at("payload").at("revision")}};
}
int main(int argc, char **argv) {
    try {
        require(argc==2,"Usage: mantis-x1-calibration-validation markers|detect|export|evaluate|binding < input.json");
        cv::setNumThreads(1); std::locale::global(std::locale::classic());
        Json s; std::cin >> s; Json out;
        std::string mode=argv[1];
        if(mode=="markers") out=markers(s);
        else if(mode=="detect") out=detect(s);
        else if(mode=="export") out=export_session(s);
        else if(mode=="evaluate") out=evaluate(s);
        else if(mode=="binding") out=binding(s);
        else throw std::runtime_error("Unknown validation command");
        std::cout << out.dump(2) << '\n'; return 0;
    } catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
