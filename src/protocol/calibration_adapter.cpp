#include "calibration_adapter.hpp"
namespace mantis::protocol {
namespace {
namespace w = wire::v1;
namespace s = services;
namespace c = calibration;
s::TargetSpecification target(const w::CalibrationTargetSpecification &t) {
    s::TargetSpecification out;
    out.grid = {t.squares_x(), t.squares_y(), t.nominal_square_size_mm()};
    switch (t.pattern_case()) {
    case w::CalibrationTargetSpecification::kCheckerboard: out.pattern = c::CheckerboardDefinition{}; break;
    case w::CalibrationTargetSpecification::kCharuco: {
        const auto &p = t.charuco();
        c::CharucoPatternLayout layout;
        if (p.pattern_layout() == "black_square_at_origin") layout = c::CharucoPatternLayout::black_square_at_origin;
        else if (p.pattern_layout() == "white_square_at_origin_even_rows") layout = c::CharucoPatternLayout::white_square_at_origin_even_rows;
        else fail(Status::invalid_argument, "Unknown ChArUco physical pattern_layout", "calibration");
        out.pattern = c::CharucoDefinition{p.dictionary(), p.nominal_marker_size_mm(), layout};
        break;
    }
    default: fail(Status::invalid_argument, "Target requires explicit checkerboard or charuco pattern", "calibration");
    }
    const auto &m = t.measurement();
    if (m.has_active_width_mm()) out.measurement.active_width_mm = m.active_width_mm();
    if (m.has_active_height_mm()) out.measurement.active_height_mm = m.active_height_mm();
    if (m.has_provenance()) {
        c::MeasurementProvenance p;
        const auto &v = m.provenance();
        if (v.has_width_uncertainty_mm()) p.width_uncertainty_mm = v.width_uncertainty_mm();
        if (v.has_height_uncertainty_mm()) p.height_uncertainty_mm = v.height_uncertainty_mm();
        if (v.has_instrument()) p.instrument = v.instrument();
        if (v.has_note()) p.note = v.note();
        out.measurement.provenance = std::move(p);
    }
    return out;
}
void write(w::CalibrationTargetSpecification *o, const s::TargetSpecification &t) {
    o->set_squares_x(t.grid.squares_x); o->set_squares_y(t.grid.squares_y);
    o->set_nominal_square_size_mm(t.grid.nominal_square_size_mm);
    if (const auto *c = std::get_if<c::CharucoDefinition>(&t.pattern)) {
        auto *p = o->mutable_charuco(); p->set_dictionary(c->dictionary);
        p->set_nominal_marker_size_mm(c->nominal_marker_size_mm);
        p->set_pattern_layout(c->pattern_layout == c::CharucoPatternLayout::black_square_at_origin ?
                             "black_square_at_origin" : "white_square_at_origin_even_rows");
    } else o->mutable_checkerboard();
    auto *m = o->mutable_measurement();
    if (t.measurement.active_width_mm) m->set_active_width_mm(*t.measurement.active_width_mm);
    if (t.measurement.active_height_mm) m->set_active_height_mm(*t.measurement.active_height_mm);
    if (t.measurement.provenance) {
        auto *p = m->mutable_provenance(); const auto &v = *t.measurement.provenance;
        if (v.width_uncertainty_mm) p->set_width_uncertainty_mm(*v.width_uncertainty_mm);
        if (v.height_uncertainty_mm) p->set_height_uncertainty_mm(*v.height_uncertainty_mm);
        if (v.instrument) p->set_instrument(*v.instrument);
        if (v.note) p->set_note(*v.note);
    }
}
void write(w::CalibrationReference *o, const c::Reference &v) {
    o->set_id(v.id.value); o->set_schema_version(v.schema_version); o->set_revision(v.revision);
}
void write(w::CalibrationArtifactReference *o, const artifact::ArtifactReference &v) {
    o->set_id(v.id.value); o->set_hash_algorithm(v.hash.algorithm); o->set_hash(v.hash.hex);
}
void write(w::CalibrationEntry *o, const s::CalibrationEntry &v) {
    write_artifact(o->mutable_artifact(), v.artifact); write(o->mutable_reference(), v.reference);
}
void write(w::CoordinateFrame *o, const spatial::CoordinateFrame &v) {
    o->set_id(v.id.value); o->set_name(v.name);
}
void write(w::CalibrationCamera *o, const c::DatasetCamera &v) {
    o->set_role(v.role); o->set_physical_camera_id(v.camera_id.value);
    o->set_image_width(v.image_width); o->set_image_height(v.image_height);
    write(o->mutable_optical_frame(), v.optical_frame);
}
void write(w::PinholeBrown5 *o, const c::PinholeBrown5 &v) {
    o->set_model("pinhole-brown5");
    o->set_fx(v.fx); o->set_fy(v.fy); o->set_cx(v.cx); o->set_cy(v.cy);
    o->set_k1(v.k1); o->set_k2(v.k2); o->set_p1(v.p1); o->set_p2(v.p2); o->set_k3(v.k3);
}
void write(w::ResidualSummary *o, const c::ResidualSummary &v) {
    o->set_point_count(v.point_count); o->set_rms_px(v.rms_px); o->set_mean_px(v.mean_px);
    o->set_median_px(v.median_px); o->set_p95_px(v.p95_px); o->set_max_px(v.max_px);
}
void write(w::CoverageEvidence *o, const c::CoverageEvidence &v) {
    o->set_min_x(v.min_x); o->set_min_y(v.min_y); o->set_max_x(v.max_x); o->set_max_y(v.max_y);
    o->set_bounding_box_area(v.bounding_box_area);
}
void write(w::MonoStageInfo *o, const s::MonoStageInfo &v) {
    o->set_sample_count(v.sample_count); write(o->mutable_residuals(), v.residuals);
    write(o->mutable_coverage(), v.coverage);
    if (v.opencv_solver_rms_px) o->set_opencv_solver_rms_px(*v.opencv_solver_rms_px);
}
void write(w::StereoStageInfo *o, const s::StereoStageInfo &v) {
    o->set_pair_count(v.pair_count); write(o->mutable_residuals(), v.residuals);
    if (v.opencv_solver_rms_px) o->set_opencv_solver_rms_px(*v.opencv_solver_rms_px);
}
void write(w::CalibrationSolverImplementation *o, const s::SolverInfo &v) {
    o->set_opencv_version(v.opencv_version); o->set_mantis_version(std::to_string(v.mantis_version.major) + "." + std::to_string(v.mantis_version.minor) + "." + std::to_string(v.mantis_version.patch));
    o->set_mantis_build(v.mantis_build);
}
void write(w::Transform *o, const spatial::Transform &v) {
    write(o->mutable_source(), v.source); write(o->mutable_target(), v.target);
    for (double value : v.matrix) o->add_matrix(value);
}
void write(w::StereoModel *o, const c::StereoModel &v) {
    for (double value : v.R_right_from_left) o->add_r_right_from_left(value);
    for (double value : v.T_right_from_left) o->add_t_right_from_left(value);
    for (double value : v.E) o->add_e(value);
    for (double value : v.F) o->add_f(value);
}
void write(w::CalibrationInfo *o, const s::CalibrationInfo &v) {
    write(o->mutable_entry(), v.entry);
    std::visit([&](const auto &detail) {
        using T = std::decay_t<decltype(detail)>;
        if constexpr (std::is_same_v<T, s::TargetInfo>) write(o->mutable_target_info()->mutable_target(), detail.target);
        else if constexpr (std::is_same_v<T, s::DatasetInfo>) {
            auto *d = o->mutable_dataset_info(); write(d->mutable_target(), detail.target);
            for (const auto &id : detail.raw_capture_ids) d->add_raw_capture_ids(id.value);
            d->set_raw_capture_count(detail.raw_capture_ids.size());
            for (const auto &role : detail.config.camera_roles) d->add_requested_roles(role);
            d->set_max_selected_per_camera(detail.config.max_selected_per_camera);
            d->set_selection_policy_version(detail.config.selection_policy_version);
            d->set_analysis_schema_version(detail.config.schema_version); d->set_total_records(detail.total_records);
            for (const auto &camera : detail.cameras) {
                auto *a = d->add_cameras(); write(a->mutable_camera(), camera.camera);
                a->set_analyzed(camera.analyzed); a->set_detected(camera.detected);
                a->set_no_target(camera.no_target); a->set_selected(camera.selected);
            }
        } else if constexpr (std::is_same_v<T, s::CameraInfo>) {
            auto *a = o->mutable_camera_info(); write(a->mutable_dataset(), detail.dataset);
            write(a->mutable_target(), detail.target); write(a->mutable_camera(), detail.camera);
            write(a->mutable_training_model(), detail.training_model); write(a->mutable_final_model(), detail.final_model);
            write(a->mutable_training(), detail.training); write(a->mutable_heldout(), detail.heldout); write(a->mutable_final(), detail.final);
            a->set_heldout_per_camera(detail.config.heldout_per_camera); a->set_schema_version(detail.config.schema_version);
            a->set_solver_policy_version(detail.config.solver_policy_version); a->set_split_policy_version(detail.config.split_policy_version);
            write(a->mutable_implementation(), detail.implementation);
        } else {
            auto *a = o->mutable_rig_info(); write(a->mutable_dataset(), detail.dataset); write(a->mutable_target(), detail.target);
            write(a->mutable_left_camera(), detail.left_camera); write(a->mutable_right_camera(), detail.right_camera);
            write(a->mutable_left(), detail.left); write(a->mutable_right(), detail.right);
            write(a->mutable_left_final_model(), detail.left_final_model); write(a->mutable_right_final_model(), detail.right_final_model);
            write(a->mutable_final_model(), detail.final_model);
            write(a->mutable_t_right_from_left(), detail.geometry.T_right_from_left);
            write(a->mutable_t_rig_from_left(), detail.geometry.T_rig_from_left);
            write(a->mutable_t_rig_from_right(), detail.geometry.T_rig_from_right);
            a->set_baseline_mm(detail.geometry.baseline_mm); a->set_relative_rotation_angle_rad(detail.geometry.relative_rotation_angle_rad);
            write(a->mutable_training(), detail.training); write(a->mutable_heldout(), detail.heldout); write(a->mutable_final(), detail.final);
            write(a->mutable_rig_frame(), detail.config.rig_frame); a->set_heldout_pairs(detail.config.heldout_pairs);
            a->set_schema_version(detail.config.schema_version); a->set_solver_policy_version(detail.config.solver_policy_version);
            a->set_split_policy_version(detail.config.split_policy_version); write(a->mutable_implementation(), detail.implementation);
        }
    }, v.detail);
}
} // namespace
bool dispatch_calibration(services::CalibrationService &runtime, const wire::v1::Request &request, wire::v1::Response &out) {
    using R = wire::v1::Request;
    switch (request.command_case()) {
    case R::kCalibrationTargetCreate: {
        const auto &r = request.calibration_target_create();
        auto created = runtime.create_calibration_target({target(r.target()), r.series_id()});
        write(out.mutable_calibration_info(), created); out.set_result_id(created.entry.artifact.id.value); break;
    }
    case R::kCalibrationDatasetBuild: {
        const auto &r = request.calibration_dataset_build();
        if (r.raw_capture_artifact_ids_size() > static_cast<int>(services::calibration_source_limit) ||
            r.camera_roles_size() > static_cast<int>(services::calibration_role_limit))
            fail(Status::invalid_argument, "Calibration request exceeds source/role bounds", "calibration");
        services::DatasetBuild dto; dto.target_artifact_id = {r.target_artifact_id()};
        for (const auto &id : r.raw_capture_artifact_ids()) dto.raw_capture_artifact_ids.push_back({id});
        dto.camera_roles = {r.camera_roles().begin(), r.camera_roles().end()};
        dto.max_selected_per_camera = r.max_selected_per_camera(); dto.series_id = r.series_id();
        out.set_result_id(runtime.build_calibration_dataset(dto).value); break;
    }
    case R::kCalibrationCameraSolve: {
        const auto &r = request.calibration_camera_solve();
        out.set_result_id(runtime.solve_camera_calibration({{r.dataset_artifact_id()}, r.camera_role(), r.heldout_per_camera(), r.series_id()}).value); break;
    }
    case R::kCalibrationRigSolve: {
        const auto &r = request.calibration_rig_solve();
        out.set_result_id(runtime.solve_rig_calibration({{r.dataset_artifact_id()}, {r.left_camera_artifact_id()},
            {r.right_camera_artifact_id()}, r.heldout_pairs(), {{r.rig_frame_id()}, r.rig_frame_name()}, r.series_id()}).value); break;
    }
    case R::kCalibrationsList:
        for (const auto &entry : runtime.calibrations()) write(out.add_calibrations(), entry);
        break;
    case R::kCalibrationInfo: write(out.mutable_calibration_info(), runtime.calibration_info({request.calibration_info().id()})); break;
    case R::kCalibrationActive: {
        auto active = runtime.active_calibration({request.calibration_active().id()});
        auto *a = out.mutable_active_calibration();
        if (active) {
            auto *b = a->mutable_binding(); b->set_logical_device_id(active->logical_device_id.value);
            write(b->mutable_reference(), active->reference); write(b->mutable_artifact(), active->artifact);
        }
        break;
    }
    case R::kCalibrationActivate:
        runtime.activate_calibration({request.calibration_activate().logical_device_id()}, {request.calibration_activate().rig_artifact_id()}); break;
    case R::kCalibrationClear: runtime.clear_calibration({request.calibration_clear().id()}); break;
    default: return false;
    }
    return true;
}
} // namespace mantis::protocol
