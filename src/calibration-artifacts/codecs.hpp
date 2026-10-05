// Private v1 document codecs. Required fields are explicit; unknown fields are rejected.
#pragma once
#include <mantis/calibration_artifacts.hpp>
#include <nlohmann/json.hpp>
#include <type_traits>
namespace mantis::calibration::artifacts::codec {
using Json = nlohmann::json;
inline void require(bool condition, std::string message) {
    if (!condition)
        fail(Status::corrupt, std::move(message), "calibration-artifacts");
}
inline void fields(const Json &j, std::initializer_list<std::string_view> names) {
    require(j.is_object() && j.size() == names.size(),
            "Calibration document object fields do not match schema 1");
    for (auto name : names)
        require(j.contains(name), "Missing calibration document field: " + std::string(name));
}
template <class T> struct Codec {
    static Json encode(const T &v) {
        if constexpr (std::is_floating_point_v<T>)
            require(std::isfinite(v), "Nonfinite calibration document number");
        return Json(v);
    }
    static T decode(const Json &j) {
        if constexpr (std::is_same_v<T, bool>)
            require(j.is_boolean(), "Expected JSON boolean");
        else if constexpr (std::is_integral_v<T>) {
            require(j.is_number_integer(), "Expected JSON integer");
            if constexpr (std::is_unsigned_v<T>) {
                require(j.is_number_unsigned() || j.get<int64_t>() >= 0,
                        "Negative unsigned calibration value");
                require(j.get<uint64_t>() <= std::numeric_limits<T>::max(),
                        "Calibration integer out of range");
            } else {
                require(!j.is_number_unsigned() ||
                            j.get<uint64_t>() <= uint64_t(std::numeric_limits<T>::max()),
                        "Calibration integer out of range");
                require(j.get<int64_t>() >= std::numeric_limits<T>::min() &&
                            j.get<int64_t>() <= std::numeric_limits<T>::max(),
                        "Calibration integer out of range");
            }
        } else if constexpr (std::is_floating_point_v<T>) {
            require(j.is_number() && std::isfinite(j.get<T>()), "Expected finite calibration number");
        } else if constexpr (std::is_same_v<T, std::string>)
            require(j.is_string(), "Expected JSON string");
        return j.get<T>();
    }
};
template <class T> Json encode(const T &v) {
    return Codec<T>::encode(v);
}
template <class T> T decode(const Json &j) {
    return Codec<T>::decode(j);
}
template <class T> struct Codec<std::optional<T>> {
    static Json encode(const std::optional<T> &v) {
        return v ? codec::encode(*v) : Json(nullptr);
    }
    static std::optional<T> decode(const Json &j) {
        if (j.is_null())
            return {};
        return codec::decode<T>(j);
    }
};
template <class T> struct Codec<std::vector<T>> {
    static Json encode(const std::vector<T> &v) {
        Json j = Json::array();
        for (const auto &x : v)
            j.push_back(codec::encode(x));
        return j;
    }
    static std::vector<T> decode(const Json &j) {
        require(j.is_array(), "Expected calibration array");
        std::vector<T> v;
        v.reserve(j.size());
        for (const auto &x : j)
            v.push_back(codec::decode<T>(x));
        return v;
    }
};
template <class T, size_t N> struct Codec<std::array<T, N>> {
    static Json encode(const std::array<T, N> &v) {
        Json j = Json::array();
        for (const auto &x : v)
            j.push_back(codec::encode(x));
        return j;
    }
    static std::array<T, N> decode(const Json &j) {
        require(j.is_array() && j.size() == N, "Wrong calibration array length");
        std::array<T, N> v{};
        for (size_t i = 0; i < N; ++i)
            v[i] = codec::decode<T>(j[i]);
        return v;
    }
};
template <> struct Codec<Id> {
    static Json encode(const Id &v) {
        return Json{{"value", codec::encode(v.value)}};
    }
    static Id decode(const Json &j) {
        fields(j, {"value"});
        Id v;
        v.value = codec::decode<decltype(v.value)>(j.at("value"));
        return v;
    }
};
template <> struct Codec<SemanticVersion> {
    static Json encode(const SemanticVersion &v) {
        return Json{{"major", codec::encode(v.major)},
                    {"minor", codec::encode(v.minor)},
                    {"patch", codec::encode(v.patch)}};
    }
    static SemanticVersion decode(const Json &j) {
        fields(j, {"major", "minor", "patch"});
        SemanticVersion v;
        v.major = codec::decode<decltype(v.major)>(j.at("major"));
        v.minor = codec::decode<decltype(v.minor)>(j.at("minor"));
        v.patch = codec::decode<decltype(v.patch)>(j.at("patch"));
        return v;
    }
};
template <> struct Codec<Hash> {
    static Json encode(const Hash &v) {
        return Json{{"algorithm", codec::encode(v.algorithm)}, {"hex", codec::encode(v.hex)}};
    }
    static Hash decode(const Json &j) {
        fields(j, {"algorithm", "hex"});
        Hash v;
        v.algorithm = codec::decode<decltype(v.algorithm)>(j.at("algorithm"));
        v.hex = codec::decode<decltype(v.hex)>(j.at("hex"));
        return v;
    }
};
template <> struct Codec<artifact::ArtifactReference> {
    static Json encode(const artifact::ArtifactReference &v) {
        return Json{{"id", codec::encode(v.id)}, {"hash", codec::encode(v.hash)}};
    }
    static artifact::ArtifactReference decode(const Json &j) {
        fields(j, {"id", "hash"});
        artifact::ArtifactReference v;
        v.id = codec::decode<decltype(v.id)>(j.at("id"));
        v.hash = codec::decode<decltype(v.hash)>(j.at("hash"));
        return v;
    }
};
template <> struct Codec<Reference> {
    static Json encode(const Reference &v) {
        return Json{{"id", codec::encode(v.id)},
                    {"schema_version", codec::encode(v.schema_version)},
                    {"revision", codec::encode(v.revision)}};
    }
    static Reference decode(const Json &j) {
        fields(j, {"id", "schema_version", "revision"});
        Reference v;
        v.id = codec::decode<decltype(v.id)>(j.at("id"));
        v.schema_version = codec::decode<decltype(v.schema_version)>(j.at("schema_version"));
        v.revision = codec::decode<decltype(v.revision)>(j.at("revision"));
        return v;
    }
};
template <> struct Codec<spatial::CoordinateFrame> {
    static Json encode(const spatial::CoordinateFrame &v) {
        return Json{{"id", codec::encode(v.id)}, {"name", codec::encode(v.name)}};
    }
    static spatial::CoordinateFrame decode(const Json &j) {
        fields(j, {"id", "name"});
        spatial::CoordinateFrame v;
        v.id = codec::decode<decltype(v.id)>(j.at("id"));
        v.name = codec::decode<decltype(v.name)>(j.at("name"));
        return v;
    }
};
template <> struct Codec<spatial::Transform> {
    static Json encode(const spatial::Transform &v) {
        require(!v.covariance, "M4 calibration transforms do not carry covariance");
        return Json{{"source", codec::encode(v.source)},
                    {"target", codec::encode(v.target)},
                    {"matrix", codec::encode(v.matrix)}};
    }
    static spatial::Transform decode(const Json &j) {
        fields(j, {"source", "target", "matrix"});
        spatial::Transform v;
        v.source = codec::decode<spatial::CoordinateFrame>(j.at("source"));
        v.target = codec::decode<spatial::CoordinateFrame>(j.at("target"));
        v.matrix = codec::decode<std::array<double, 16>>(j.at("matrix"));
        return v;
    }
};
template <> struct Codec<CharucoPatternLayout> {
    static Json encode(CharucoPatternLayout v) {
        switch (v) {
        case CharucoPatternLayout::black_square_at_origin:
            return "black_square_at_origin";
        case CharucoPatternLayout::white_square_at_origin_even_rows:
            return "white_square_at_origin_even_rows";
        }
        fail(Status::invalid_argument, "Unknown CharucoPatternLayout", "calibration-artifacts");
    }
    static CharucoPatternLayout decode(const Json &j) {
        auto v = codec::decode<std::string>(j);
        if (v == "black_square_at_origin")
            return CharucoPatternLayout::black_square_at_origin;
        if (v == "white_square_at_origin_even_rows")
            return CharucoPatternLayout::white_square_at_origin_even_rows;
        fail(Status::corrupt, "Invalid CharucoPatternLayout string", "calibration-artifacts");
    }
};
template <> struct Codec<TargetType> {
    static Json encode(TargetType v) {
        switch (v) {
        case TargetType::checkerboard:
            return "checkerboard";
        case TargetType::charuco:
            return "charuco";
        }
        fail(Status::invalid_argument, "Unknown TargetType", "calibration-artifacts");
    }
    static TargetType decode(const Json &j) {
        auto v = codec::decode<std::string>(j);
        if (v == "checkerboard")
            return TargetType::checkerboard;
        if (v == "charuco")
            return TargetType::charuco;
        fail(Status::corrupt, "Invalid TargetType string", "calibration-artifacts");
    }
};
template <> struct Codec<CameraModel> {
    static Json encode(CameraModel v) {
        switch (v) {
        case CameraModel::pinhole_brown5:
            return "pinhole-brown5";
        }
        fail(Status::invalid_argument, "Unknown CameraModel", "calibration-artifacts");
    }
    static CameraModel decode(const Json &j) {
        auto v = codec::decode<std::string>(j);
        if (v == "pinhole-brown5")
            return CameraModel::pinhole_brown5;
        fail(Status::corrupt, "Invalid CameraModel string", "calibration-artifacts");
    }
};
template <> struct Codec<DetectionOutcome> {
    static Json encode(DetectionOutcome v) {
        switch (v) {
        case DetectionOutcome::detected:
            return "detected";
        case DetectionOutcome::no_target:
            return "no_target";
        }
        fail(Status::invalid_argument, "Unknown DetectionOutcome", "calibration-artifacts");
    }
    static DetectionOutcome decode(const Json &j) {
        auto v = codec::decode<std::string>(j);
        if (v == "detected")
            return DetectionOutcome::detected;
        if (v == "no_target")
            return DetectionOutcome::no_target;
        fail(Status::corrupt, "Invalid DetectionOutcome string", "calibration-artifacts");
    }
};
template <> struct Codec<SolveSampleDisposition> {
    static Json encode(SolveSampleDisposition v) {
        switch (v) {
        case SolveSampleDisposition::training:
            return "training";
        case SolveSampleDisposition::held_out:
            return "held_out";
        case SolveSampleDisposition::ineligible_insufficient_points:
            return "ineligible_insufficient_points";
        case SolveSampleDisposition::ineligible_degenerate_geometry:
            return "ineligible_degenerate_geometry";
        case SolveSampleDisposition::ineligible_missing_detection:
            return "ineligible_missing_detection";
        }
        fail(Status::invalid_argument, "Unknown SolveSampleDisposition", "calibration-artifacts");
    }
    static SolveSampleDisposition decode(const Json &j) {
        auto v = codec::decode<std::string>(j);
        if (v == "training")
            return SolveSampleDisposition::training;
        if (v == "held_out")
            return SolveSampleDisposition::held_out;
        if (v == "ineligible_insufficient_points")
            return SolveSampleDisposition::ineligible_insufficient_points;
        if (v == "ineligible_degenerate_geometry")
            return SolveSampleDisposition::ineligible_degenerate_geometry;
        if (v == "ineligible_missing_detection")
            return SolveSampleDisposition::ineligible_missing_detection;
        fail(Status::corrupt, "Invalid SolveSampleDisposition string", "calibration-artifacts");
    }
};
template <> struct Codec<TargetIdentity> {
    static Json encode(const TargetIdentity &v) {
        return Json{{"id", codec::encode(v.id)}, {"revision", codec::encode(v.revision)}};
    }
    static TargetIdentity decode(const Json &j) {
        fields(j, {"id", "revision"});
        TargetIdentity v;
        v.id = codec::decode<decltype(v.id)>(j.at("id"));
        v.revision = codec::decode<decltype(v.revision)>(j.at("revision"));
        return v;
    }
};
template <> struct Codec<TargetGrid> {
    static Json encode(const TargetGrid &v) {
        return Json{{"squares_x", codec::encode(v.squares_x)},
                    {"squares_y", codec::encode(v.squares_y)},
                    {"nominal_square_size_mm", codec::encode(v.nominal_square_size_mm)}};
    }
    static TargetGrid decode(const Json &j) {
        fields(j, {"squares_x", "squares_y", "nominal_square_size_mm"});
        TargetGrid v;
        v.squares_x = codec::decode<decltype(v.squares_x)>(j.at("squares_x"));
        v.squares_y = codec::decode<decltype(v.squares_y)>(j.at("squares_y"));
        v.nominal_square_size_mm =
            codec::decode<decltype(v.nominal_square_size_mm)>(j.at("nominal_square_size_mm"));
        return v;
    }
};
template <> struct Codec<CharucoDefinition> {
    static Json encode(const CharucoDefinition &v) {
        return Json{{"dictionary", codec::encode(v.dictionary)},
                    {"nominal_marker_size_mm", codec::encode(v.nominal_marker_size_mm)},
                    {"pattern_layout", codec::encode(v.pattern_layout)}};
    }
    static CharucoDefinition decode(const Json &j) {
        fields(j, {"dictionary", "nominal_marker_size_mm", "pattern_layout"});
        CharucoDefinition v;
        v.dictionary = codec::decode<decltype(v.dictionary)>(j.at("dictionary"));
        v.nominal_marker_size_mm =
            codec::decode<decltype(v.nominal_marker_size_mm)>(j.at("nominal_marker_size_mm"));
        v.pattern_layout = codec::decode<decltype(v.pattern_layout)>(j.at("pattern_layout"));
        return v;
    }
};
template <> struct Codec<TargetPattern> {
    static Json encode(const TargetPattern &v) {
        if (auto c = std::get_if<CharucoDefinition>(&v))
            return {{"type", "charuco"}, {"definition", codec::encode(*c)}};
        return {{"type", "checkerboard"}, {"definition", Json::object()}};
    }
    static TargetPattern decode(const Json &j) {
        fields(j, {"type", "definition"});
        auto type = codec::decode<TargetType>(j.at("type"));
        if (type == TargetType::charuco)
            return codec::decode<CharucoDefinition>(j.at("definition"));
        fields(j.at("definition"), {});
        return CheckerboardDefinition{};
    }
};
template <> struct Codec<MeasurementProvenance> {
    static Json encode(const MeasurementProvenance &v) {
        return Json{{"width_uncertainty_mm", codec::encode(v.width_uncertainty_mm)},
                    {"height_uncertainty_mm", codec::encode(v.height_uncertainty_mm)},
                    {"instrument", codec::encode(v.instrument)},
                    {"note", codec::encode(v.note)}};
    }
    static MeasurementProvenance decode(const Json &j) {
        fields(j, {"width_uncertainty_mm", "height_uncertainty_mm", "instrument", "note"});
        MeasurementProvenance v;
        v.width_uncertainty_mm =
            codec::decode<decltype(v.width_uncertainty_mm)>(j.at("width_uncertainty_mm"));
        v.height_uncertainty_mm =
            codec::decode<decltype(v.height_uncertainty_mm)>(j.at("height_uncertainty_mm"));
        v.instrument = codec::decode<decltype(v.instrument)>(j.at("instrument"));
        v.note = codec::decode<decltype(v.note)>(j.at("note"));
        return v;
    }
};
template <> struct Codec<PhysicalMeasurement> {
    static Json encode(const PhysicalMeasurement &v) {
        return Json{{"active_width_mm", codec::encode(v.active_width_mm)},
                    {"active_height_mm", codec::encode(v.active_height_mm)},
                    {"provenance", codec::encode(v.provenance)}};
    }
    static PhysicalMeasurement decode(const Json &j) {
        fields(j, {"active_width_mm", "active_height_mm", "provenance"});
        PhysicalMeasurement v;
        v.active_width_mm = codec::decode<decltype(v.active_width_mm)>(j.at("active_width_mm"));
        v.active_height_mm = codec::decode<decltype(v.active_height_mm)>(j.at("active_height_mm"));
        v.provenance = codec::decode<decltype(v.provenance)>(j.at("provenance"));
        return v;
    }
};
template <> struct Codec<CalibrationTarget> {
    static Json encode(const CalibrationTarget &v) {
        return Json{{"identity", codec::encode(v.identity)},
                    {"grid", codec::encode(v.grid)},
                    {"pattern", codec::encode(v.pattern)},
                    {"measurement", codec::encode(v.measurement)}};
    }
    static CalibrationTarget decode(const Json &j) {
        fields(j, {"identity", "grid", "pattern", "measurement"});
        CalibrationTarget v;
        v.identity = codec::decode<decltype(v.identity)>(j.at("identity"));
        v.grid = codec::decode<decltype(v.grid)>(j.at("grid"));
        v.pattern = codec::decode<decltype(v.pattern)>(j.at("pattern"));
        v.measurement = codec::decode<decltype(v.measurement)>(j.at("measurement"));
        return v;
    }
};
template <> struct Codec<TargetPoint> {
    static Json encode(const TargetPoint &v) {
        return Json{{"x_mm", codec::encode(v.x_mm)},
                    {"y_mm", codec::encode(v.y_mm)},
                    {"z_mm", codec::encode(v.z_mm)}};
    }
    static TargetPoint decode(const Json &j) {
        fields(j, {"x_mm", "y_mm", "z_mm"});
        TargetPoint v;
        v.x_mm = codec::decode<decltype(v.x_mm)>(j.at("x_mm"));
        v.y_mm = codec::decode<decltype(v.y_mm)>(j.at("y_mm"));
        v.z_mm = codec::decode<decltype(v.z_mm)>(j.at("z_mm"));
        return v;
    }
};
template <> struct Codec<ImagePoint> {
    static Json encode(const ImagePoint &v) {
        return Json{{"x_px", codec::encode(v.x_px)}, {"y_px", codec::encode(v.y_px)}};
    }
    static ImagePoint decode(const Json &j) {
        fields(j, {"x_px", "y_px"});
        ImagePoint v;
        v.x_px = codec::decode<decltype(v.x_px)>(j.at("x_px"));
        v.y_px = codec::decode<decltype(v.y_px)>(j.at("y_px"));
        return v;
    }
};
template <> struct Codec<ObservationSource> {
    static Json encode(const ObservationSource &v) {
        return Json{{"raw_capture_id", codec::encode(v.raw_capture_id)},
                    {"frameset_sequence", codec::encode(v.frameset_sequence)},
                    {"camera_id", codec::encode(v.camera_id)},
                    {"camera_role", codec::encode(v.camera_role)}};
    }
    static ObservationSource decode(const Json &j) {
        fields(j, {"raw_capture_id", "frameset_sequence", "camera_id", "camera_role"});
        ObservationSource v;
        v.raw_capture_id = codec::decode<decltype(v.raw_capture_id)>(j.at("raw_capture_id"));
        v.frameset_sequence = codec::decode<decltype(v.frameset_sequence)>(j.at("frameset_sequence"));
        v.camera_id = codec::decode<decltype(v.camera_id)>(j.at("camera_id"));
        v.camera_role = codec::decode<decltype(v.camera_role)>(j.at("camera_role"));
        return v;
    }
};
template <> struct Codec<DetectionEvidence> {
    static Json encode(const DetectionEvidence &v) {
        return Json{{"detected_points", codec::encode(v.detected_points)},
                    {"detected_markers", codec::encode(v.detected_markers)},
                    {"partial", codec::encode(v.partial)}};
    }
    static DetectionEvidence decode(const Json &j) {
        fields(j, {"detected_points", "detected_markers", "partial"});
        DetectionEvidence v;
        v.detected_points = codec::decode<decltype(v.detected_points)>(j.at("detected_points"));
        v.detected_markers = codec::decode<decltype(v.detected_markers)>(j.at("detected_markers"));
        v.partial = codec::decode<decltype(v.partial)>(j.at("partial"));
        return v;
    }
};
template <> struct Codec<TargetObservation> {
    static Json encode(const TargetObservation &v) {
        return Json{{"source", codec::encode(v.source)},
                    {"target", codec::encode(v.target)},
                    {"target_type", codec::encode(v.target_type)},
                    {"image_width", codec::encode(v.image_width)},
                    {"image_height", codec::encode(v.image_height)},
                    {"point_ids", codec::encode(v.point_ids)},
                    {"image_points_px", codec::encode(v.image_points_px)},
                    {"object_points_mm", codec::encode(v.object_points_mm)},
                    {"evidence", codec::encode(v.evidence)}};
    }
    static TargetObservation decode(const Json &j) {
        fields(j, {"source", "target", "target_type", "image_width", "image_height", "point_ids",
                   "image_points_px", "object_points_mm", "evidence"});
        TargetObservation v;
        v.source = codec::decode<decltype(v.source)>(j.at("source"));
        v.target = codec::decode<decltype(v.target)>(j.at("target"));
        v.target_type = codec::decode<decltype(v.target_type)>(j.at("target_type"));
        v.image_width = codec::decode<decltype(v.image_width)>(j.at("image_width"));
        v.image_height = codec::decode<decltype(v.image_height)>(j.at("image_height"));
        v.point_ids = codec::decode<decltype(v.point_ids)>(j.at("point_ids"));
        v.image_points_px = codec::decode<decltype(v.image_points_px)>(j.at("image_points_px"));
        v.object_points_mm = codec::decode<decltype(v.object_points_mm)>(j.at("object_points_mm"));
        v.evidence = codec::decode<decltype(v.evidence)>(j.at("evidence"));
        return v;
    }
};
template <> struct Codec<FrameSetKey> {
    static Json encode(const FrameSetKey &v) {
        return Json{{"raw_capture_id", codec::encode(v.raw_capture_id)},
                    {"frameset_sequence", codec::encode(v.frameset_sequence)}};
    }
    static FrameSetKey decode(const Json &j) {
        fields(j, {"raw_capture_id", "frameset_sequence"});
        FrameSetKey v;
        v.raw_capture_id = codec::decode<decltype(v.raw_capture_id)>(j.at("raw_capture_id"));
        v.frameset_sequence = codec::decode<decltype(v.frameset_sequence)>(j.at("frameset_sequence"));
        return v;
    }
};
template <> struct Codec<ObservationKey> {
    static Json encode(const ObservationKey &v) {
        return Json{{"frame", codec::encode(v.frame)},
                    {"camera_role", codec::encode(v.camera_role)},
                    {"camera_id", codec::encode(v.camera_id)}};
    }
    static ObservationKey decode(const Json &j) {
        fields(j, {"frame", "camera_role", "camera_id"});
        ObservationKey v;
        v.frame = codec::decode<decltype(v.frame)>(j.at("frame"));
        v.camera_role = codec::decode<decltype(v.camera_role)>(j.at("camera_role"));
        v.camera_id = codec::decode<decltype(v.camera_id)>(j.at("camera_id"));
        return v;
    }
};
template <> struct Codec<DatasetCamera> {
    static Json encode(const DatasetCamera &v) {
        return Json{{"role", codec::encode(v.role)},
                    {"camera_id", codec::encode(v.camera_id)},
                    {"image_width", codec::encode(v.image_width)},
                    {"image_height", codec::encode(v.image_height)},
                    {"optical_frame", codec::encode(v.optical_frame)}};
    }
    static DatasetCamera decode(const Json &j) {
        fields(j, {"role", "camera_id", "image_width", "image_height", "optical_frame"});
        DatasetCamera v;
        v.role = codec::decode<decltype(v.role)>(j.at("role"));
        v.camera_id = codec::decode<decltype(v.camera_id)>(j.at("camera_id"));
        v.image_width = codec::decode<decltype(v.image_width)>(j.at("image_width"));
        v.image_height = codec::decode<decltype(v.image_height)>(j.at("image_height"));
        v.optical_frame = codec::decode<decltype(v.optical_frame)>(j.at("optical_frame"));
        return v;
    }
};
template <> struct Codec<DatasetAnalysisConfig> {
    static Json encode(const DatasetAnalysisConfig &v) {
        return Json{{"schema_version", codec::encode(v.schema_version)},
                    {"camera_roles", codec::encode(v.camera_roles)},
                    {"selection_policy_version", codec::encode(v.selection_policy_version)},
                    {"max_selected_per_camera", codec::encode(v.max_selected_per_camera)}};
    }
    static DatasetAnalysisConfig decode(const Json &j) {
        fields(j, {"schema_version", "camera_roles", "selection_policy_version", "max_selected_per_camera"});
        DatasetAnalysisConfig v;
        v.schema_version = codec::decode<decltype(v.schema_version)>(j.at("schema_version"));
        v.camera_roles = codec::decode<decltype(v.camera_roles)>(j.at("camera_roles"));
        v.selection_policy_version =
            codec::decode<decltype(v.selection_policy_version)>(j.at("selection_policy_version"));
        v.max_selected_per_camera =
            codec::decode<decltype(v.max_selected_per_camera)>(j.at("max_selected_per_camera"));
        return v;
    }
};
template <> struct Codec<DiversityDescriptor> {
    static Json encode(const DiversityDescriptor &v) {
        return Json{{"centroid_x", codec::encode(v.centroid_x)},
                    {"centroid_y", codec::encode(v.centroid_y)},
                    {"extent_x", codec::encode(v.extent_x)},
                    {"extent_y", codec::encode(v.extent_y)},
                    {"variance_x", codec::encode(v.variance_x)},
                    {"variance_y", codec::encode(v.variance_y)},
                    {"covariance_xy", codec::encode(v.covariance_xy)},
                    {"visible_fraction", codec::encode(v.visible_fraction)}};
    }
    static DiversityDescriptor decode(const Json &j) {
        fields(j, {"centroid_x", "centroid_y", "extent_x", "extent_y", "variance_x", "variance_y",
                   "covariance_xy", "visible_fraction"});
        DiversityDescriptor v;
        v.centroid_x = codec::decode<decltype(v.centroid_x)>(j.at("centroid_x"));
        v.centroid_y = codec::decode<decltype(v.centroid_y)>(j.at("centroid_y"));
        v.extent_x = codec::decode<decltype(v.extent_x)>(j.at("extent_x"));
        v.extent_y = codec::decode<decltype(v.extent_y)>(j.at("extent_y"));
        v.variance_x = codec::decode<decltype(v.variance_x)>(j.at("variance_x"));
        v.variance_y = codec::decode<decltype(v.variance_y)>(j.at("variance_y"));
        v.covariance_xy = codec::decode<decltype(v.covariance_xy)>(j.at("covariance_xy"));
        v.visible_fraction = codec::decode<decltype(v.visible_fraction)>(j.at("visible_fraction"));
        return v;
    }
};
template <> struct Codec<DatasetObservationRecord> {
    static Json encode(const DatasetObservationRecord &v) {
        return Json{{"key", codec::encode(v.key)},
                    {"outcome", codec::encode(v.outcome)},
                    {"observation", codec::encode(v.observation)},
                    {"diversity", codec::encode(v.diversity)},
                    {"selection_rank", codec::encode(v.selection_rank)}};
    }
    static DatasetObservationRecord decode(const Json &j) {
        fields(j, {"key", "outcome", "observation", "diversity", "selection_rank"});
        DatasetObservationRecord v;
        v.key = codec::decode<decltype(v.key)>(j.at("key"));
        v.outcome = codec::decode<decltype(v.outcome)>(j.at("outcome"));
        v.observation = codec::decode<decltype(v.observation)>(j.at("observation"));
        v.diversity = codec::decode<decltype(v.diversity)>(j.at("diversity"));
        v.selection_rank = codec::decode<decltype(v.selection_rank)>(j.at("selection_rank"));
        return v;
    }
};
template <> struct Codec<CalibrationDataset> {
    static Json encode(const CalibrationDataset &v) {
        return Json{{"target", codec::encode(v.target)},
                    {"config", codec::encode(v.config)},
                    {"raw_capture_ids", codec::encode(v.raw_capture_ids)},
                    {"cameras", codec::encode(v.cameras)},
                    {"records", codec::encode(v.records)}};
    }
    static CalibrationDataset decode(const Json &j) {
        fields(j, {"target", "config", "raw_capture_ids", "cameras", "records"});
        CalibrationDataset v;
        v.target = codec::decode<decltype(v.target)>(j.at("target"));
        v.config = codec::decode<decltype(v.config)>(j.at("config"));
        v.raw_capture_ids = codec::decode<decltype(v.raw_capture_ids)>(j.at("raw_capture_ids"));
        v.cameras = codec::decode<decltype(v.cameras)>(j.at("cameras"));
        v.records = codec::decode<decltype(v.records)>(j.at("records"));
        return v;
    }
};
template <> struct Codec<PinholeBrown5> {
    static Json encode(const PinholeBrown5 &v) {
        return Json{{"model", codec::encode(v.model)}, {"fx", codec::encode(v.fx)},
                    {"fy", codec::encode(v.fy)},       {"cx", codec::encode(v.cx)},
                    {"cy", codec::encode(v.cy)},       {"k1", codec::encode(v.k1)},
                    {"k2", codec::encode(v.k2)},       {"p1", codec::encode(v.p1)},
                    {"p2", codec::encode(v.p2)},       {"k3", codec::encode(v.k3)}};
    }
    static PinholeBrown5 decode(const Json &j) {
        fields(j, {"model", "fx", "fy", "cx", "cy", "k1", "k2", "p1", "p2", "k3"});
        PinholeBrown5 v;
        v.model = codec::decode<decltype(v.model)>(j.at("model"));
        v.fx = codec::decode<decltype(v.fx)>(j.at("fx"));
        v.fy = codec::decode<decltype(v.fy)>(j.at("fy"));
        v.cx = codec::decode<decltype(v.cx)>(j.at("cx"));
        v.cy = codec::decode<decltype(v.cy)>(j.at("cy"));
        v.k1 = codec::decode<decltype(v.k1)>(j.at("k1"));
        v.k2 = codec::decode<decltype(v.k2)>(j.at("k2"));
        v.p1 = codec::decode<decltype(v.p1)>(j.at("p1"));
        v.p2 = codec::decode<decltype(v.p2)>(j.at("p2"));
        v.k3 = codec::decode<decltype(v.k3)>(j.at("k3"));
        return v;
    }
};
template <> struct Codec<MonoSolveConfig> {
    static Json encode(const MonoSolveConfig &v) {
        return Json{{"schema_version", codec::encode(v.schema_version)},
                    {"solver_policy_version", codec::encode(v.solver_policy_version)},
                    {"split_policy_version", codec::encode(v.split_policy_version)},
                    {"heldout_per_camera", codec::encode(v.heldout_per_camera)}};
    }
    static MonoSolveConfig decode(const Json &j) {
        fields(j, {"schema_version", "solver_policy_version", "split_policy_version", "heldout_per_camera"});
        MonoSolveConfig v;
        v.schema_version = codec::decode<decltype(v.schema_version)>(j.at("schema_version"));
        v.solver_policy_version =
            codec::decode<decltype(v.solver_policy_version)>(j.at("solver_policy_version"));
        v.split_policy_version =
            codec::decode<decltype(v.split_policy_version)>(j.at("split_policy_version"));
        v.heldout_per_camera = codec::decode<decltype(v.heldout_per_camera)>(j.at("heldout_per_camera"));
        return v;
    }
};
template <> struct Codec<StereoSolveConfig> {
    static Json encode(const StereoSolveConfig &v) {
        return Json{{"schema_version", codec::encode(v.schema_version)},
                    {"solver_policy_version", codec::encode(v.solver_policy_version)},
                    {"split_policy_version", codec::encode(v.split_policy_version)},
                    {"left_role", codec::encode(v.left_role)},
                    {"right_role", codec::encode(v.right_role)},
                    {"heldout_pairs", codec::encode(v.heldout_pairs)},
                    {"rig_frame", codec::encode(v.rig_frame)}};
    }
    static StereoSolveConfig decode(const Json &j) {
        fields(j, {"schema_version", "solver_policy_version", "split_policy_version", "left_role",
                   "right_role", "heldout_pairs", "rig_frame"});
        StereoSolveConfig v;
        v.schema_version = codec::decode<decltype(v.schema_version)>(j.at("schema_version"));
        v.solver_policy_version =
            codec::decode<decltype(v.solver_policy_version)>(j.at("solver_policy_version"));
        v.split_policy_version =
            codec::decode<decltype(v.split_policy_version)>(j.at("split_policy_version"));
        v.left_role = codec::decode<decltype(v.left_role)>(j.at("left_role"));
        v.right_role = codec::decode<decltype(v.right_role)>(j.at("right_role"));
        v.heldout_pairs = codec::decode<decltype(v.heldout_pairs)>(j.at("heldout_pairs"));
        v.rig_frame = codec::decode<decltype(v.rig_frame)>(j.at("rig_frame"));
        return v;
    }
};
template <> struct Codec<MonoSample> {
    static Json encode(const MonoSample &v) {
        return Json{{"key", codec::encode(v.key)},
                    {"selection_rank", codec::encode(v.selection_rank)},
                    {"point_count", codec::encode(v.point_count)},
                    {"disposition", codec::encode(v.disposition)}};
    }
    static MonoSample decode(const Json &j) {
        fields(j, {"key", "selection_rank", "point_count", "disposition"});
        MonoSample v;
        v.key = codec::decode<decltype(v.key)>(j.at("key"));
        v.selection_rank = codec::decode<decltype(v.selection_rank)>(j.at("selection_rank"));
        v.point_count = codec::decode<decltype(v.point_count)>(j.at("point_count"));
        v.disposition = codec::decode<decltype(v.disposition)>(j.at("disposition"));
        return v;
    }
};
template <> struct Codec<SolvePartition> {
    static Json encode(const SolvePartition &v) {
        return Json{{"samples", codec::encode(v.samples)}};
    }
    static SolvePartition decode(const Json &j) {
        fields(j, {"samples"});
        SolvePartition v;
        v.samples = codec::decode<decltype(v.samples)>(j.at("samples"));
        return v;
    }
};
template <> struct Codec<StereoSample> {
    static Json encode(const StereoSample &v) {
        return Json{{"key", codec::encode(v.key)},
                    {"left_selection_rank", codec::encode(v.left_selection_rank)},
                    {"right_selection_rank", codec::encode(v.right_selection_rank)},
                    {"common_point_ids", codec::encode(v.common_point_ids)},
                    {"disposition", codec::encode(v.disposition)}};
    }
    static StereoSample decode(const Json &j) {
        fields(j, {"key", "left_selection_rank", "right_selection_rank", "common_point_ids", "disposition"});
        StereoSample v;
        v.key = codec::decode<decltype(v.key)>(j.at("key"));
        v.left_selection_rank = codec::decode<decltype(v.left_selection_rank)>(j.at("left_selection_rank"));
        v.right_selection_rank =
            codec::decode<decltype(v.right_selection_rank)>(j.at("right_selection_rank"));
        v.common_point_ids = codec::decode<decltype(v.common_point_ids)>(j.at("common_point_ids"));
        v.disposition = codec::decode<decltype(v.disposition)>(j.at("disposition"));
        return v;
    }
};
template <> struct Codec<StereoPartition> {
    static Json encode(const StereoPartition &v) {
        return Json{{"samples", codec::encode(v.samples)}};
    }
    static StereoPartition decode(const Json &j) {
        fields(j, {"samples"});
        StereoPartition v;
        v.samples = codec::decode<decltype(v.samples)>(j.at("samples"));
        return v;
    }
};
template <> struct Codec<ResidualSummary> {
    static Json encode(const ResidualSummary &v) {
        return Json{{"point_count", codec::encode(v.point_count)}, {"rms_px", codec::encode(v.rms_px)},
                    {"mean_px", codec::encode(v.mean_px)},         {"median_px", codec::encode(v.median_px)},
                    {"p95_px", codec::encode(v.p95_px)},           {"max_px", codec::encode(v.max_px)}};
    }
    static ResidualSummary decode(const Json &j) {
        fields(j, {"point_count", "rms_px", "mean_px", "median_px", "p95_px", "max_px"});
        ResidualSummary v;
        v.point_count = codec::decode<decltype(v.point_count)>(j.at("point_count"));
        v.rms_px = codec::decode<decltype(v.rms_px)>(j.at("rms_px"));
        v.mean_px = codec::decode<decltype(v.mean_px)>(j.at("mean_px"));
        v.median_px = codec::decode<decltype(v.median_px)>(j.at("median_px"));
        v.p95_px = codec::decode<decltype(v.p95_px)>(j.at("p95_px"));
        v.max_px = codec::decode<decltype(v.max_px)>(j.at("max_px"));
        return v;
    }
};
template <> struct Codec<CoverageEvidence> {
    static Json encode(const CoverageEvidence &v) {
        return Json{{"min_x", codec::encode(v.min_x)},
                    {"min_y", codec::encode(v.min_y)},
                    {"max_x", codec::encode(v.max_x)},
                    {"max_y", codec::encode(v.max_y)},
                    {"bounding_box_area", codec::encode(v.bounding_box_area)}};
    }
    static CoverageEvidence decode(const Json &j) {
        fields(j, {"min_x", "min_y", "max_x", "max_y", "bounding_box_area"});
        CoverageEvidence v;
        v.min_x = codec::decode<decltype(v.min_x)>(j.at("min_x"));
        v.min_y = codec::decode<decltype(v.min_y)>(j.at("min_y"));
        v.max_x = codec::decode<decltype(v.max_x)>(j.at("max_x"));
        v.max_y = codec::decode<decltype(v.max_y)>(j.at("max_y"));
        v.bounding_box_area = codec::decode<decltype(v.bounding_box_area)>(j.at("bounding_box_area"));
        return v;
    }
};
template <> struct Codec<TargetViewPose> {
    static Json encode(const TargetViewPose &v) {
        return Json{{"rotation_vector_rad", codec::encode(v.rotation_vector_rad)},
                    {"translation_mm", codec::encode(v.translation_mm)},
                    {"ippe_solution_index", codec::encode(v.ippe_solution_index)},
                    {"ippe_solution_rms_px", codec::encode(v.ippe_solution_rms_px)}};
    }
    static TargetViewPose decode(const Json &j) {
        fields(j, {"rotation_vector_rad", "translation_mm", "ippe_solution_index", "ippe_solution_rms_px"});
        TargetViewPose v;
        v.rotation_vector_rad = codec::decode<decltype(v.rotation_vector_rad)>(j.at("rotation_vector_rad"));
        v.translation_mm = codec::decode<decltype(v.translation_mm)>(j.at("translation_mm"));
        v.ippe_solution_index = codec::decode<decltype(v.ippe_solution_index)>(j.at("ippe_solution_index"));
        v.ippe_solution_rms_px =
            codec::decode<decltype(v.ippe_solution_rms_px)>(j.at("ippe_solution_rms_px"));
        return v;
    }
};
template <> struct Codec<MonoViewEvidence> {
    static Json encode(const MonoViewEvidence &v) {
        return Json{{"key", codec::encode(v.key)},
                    {"pose", codec::encode(v.pose)},
                    {"residuals_px", codec::encode(v.residuals_px)},
                    {"residuals", codec::encode(v.residuals)},
                    {"coverage", codec::encode(v.coverage)}};
    }
    static MonoViewEvidence decode(const Json &j) {
        fields(j, {"key", "pose", "residuals_px", "residuals", "coverage"});
        MonoViewEvidence v;
        v.key = codec::decode<decltype(v.key)>(j.at("key"));
        v.pose = codec::decode<decltype(v.pose)>(j.at("pose"));
        v.residuals_px = codec::decode<decltype(v.residuals_px)>(j.at("residuals_px"));
        v.residuals = codec::decode<decltype(v.residuals)>(j.at("residuals"));
        v.coverage = codec::decode<decltype(v.coverage)>(j.at("coverage"));
        return v;
    }
};
template <> struct Codec<MonoStageEvidence> {
    static Json encode(const MonoStageEvidence &v) {
        return Json{{"views", codec::encode(v.views)},
                    {"residuals", codec::encode(v.residuals)},
                    {"coverage", codec::encode(v.coverage)},
                    {"opencv_solver_rms_px", codec::encode(v.opencv_solver_rms_px)}};
    }
    static MonoStageEvidence decode(const Json &j) {
        fields(j, {"views", "residuals", "coverage", "opencv_solver_rms_px"});
        MonoStageEvidence v;
        v.views = codec::decode<decltype(v.views)>(j.at("views"));
        v.residuals = codec::decode<decltype(v.residuals)>(j.at("residuals"));
        v.coverage = codec::decode<decltype(v.coverage)>(j.at("coverage"));
        v.opencv_solver_rms_px =
            codec::decode<decltype(v.opencv_solver_rms_px)>(j.at("opencv_solver_rms_px"));
        return v;
    }
};
template <> struct Codec<CameraCalibrationSolution> {
    static Json encode(const CameraCalibrationSolution &v) {
        return Json{{"target", codec::encode(v.target)},
                    {"camera", codec::encode(v.camera)},
                    {"config", codec::encode(v.config)},
                    {"partition", codec::encode(v.partition)},
                    {"training_model", codec::encode(v.training_model)},
                    {"training_fit", codec::encode(v.training_fit)},
                    {"heldout_validation", codec::encode(v.heldout_validation)},
                    {"final_model", codec::encode(v.final_model)},
                    {"final_fit", codec::encode(v.final_fit)}};
    }
    static CameraCalibrationSolution decode(const Json &j) {
        fields(j, {"target", "camera", "config", "partition", "training_model", "training_fit",
                   "heldout_validation", "final_model", "final_fit"});
        CameraCalibrationSolution v;
        v.target = codec::decode<decltype(v.target)>(j.at("target"));
        v.camera = codec::decode<decltype(v.camera)>(j.at("camera"));
        v.config = codec::decode<decltype(v.config)>(j.at("config"));
        v.partition = codec::decode<decltype(v.partition)>(j.at("partition"));
        v.training_model = codec::decode<decltype(v.training_model)>(j.at("training_model"));
        v.training_fit = codec::decode<decltype(v.training_fit)>(j.at("training_fit"));
        v.heldout_validation = codec::decode<decltype(v.heldout_validation)>(j.at("heldout_validation"));
        v.final_model = codec::decode<decltype(v.final_model)>(j.at("final_model"));
        v.final_fit = codec::decode<decltype(v.final_fit)>(j.at("final_fit"));
        return v;
    }
};
template <> struct Codec<StereoModel> {
    static Json encode(const StereoModel &v) {
        return Json{{"R_right_from_left", codec::encode(v.R_right_from_left)},
                    {"T_right_from_left", codec::encode(v.T_right_from_left)},
                    {"E", codec::encode(v.E)},
                    {"F", codec::encode(v.F)}};
    }
    static StereoModel decode(const Json &j) {
        fields(j, {"R_right_from_left", "T_right_from_left", "E", "F"});
        StereoModel v;
        v.R_right_from_left = codec::decode<decltype(v.R_right_from_left)>(j.at("R_right_from_left"));
        v.T_right_from_left = codec::decode<decltype(v.T_right_from_left)>(j.at("T_right_from_left"));
        v.E = codec::decode<decltype(v.E)>(j.at("E"));
        v.F = codec::decode<decltype(v.F)>(j.at("F"));
        return v;
    }
};
template <> struct Codec<StereoPairEvidence> {
    static Json encode(const StereoPairEvidence &v) {
        return Json{{"key", codec::encode(v.key)},
                    {"common_point_ids", codec::encode(v.common_point_ids)},
                    {"symmetric_epipolar_residuals_px", codec::encode(v.symmetric_epipolar_residuals_px)},
                    {"residuals", codec::encode(v.residuals)}};
    }
    static StereoPairEvidence decode(const Json &j) {
        fields(j, {"key", "common_point_ids", "symmetric_epipolar_residuals_px", "residuals"});
        StereoPairEvidence v;
        v.key = codec::decode<decltype(v.key)>(j.at("key"));
        v.common_point_ids = codec::decode<decltype(v.common_point_ids)>(j.at("common_point_ids"));
        v.symmetric_epipolar_residuals_px = codec::decode<decltype(v.symmetric_epipolar_residuals_px)>(
            j.at("symmetric_epipolar_residuals_px"));
        v.residuals = codec::decode<decltype(v.residuals)>(j.at("residuals"));
        return v;
    }
};
template <> struct Codec<StereoStageEvidence> {
    static Json encode(const StereoStageEvidence &v) {
        return Json{{"pairs", codec::encode(v.pairs)},
                    {"residuals", codec::encode(v.residuals)},
                    {"opencv_solver_rms_px", codec::encode(v.opencv_solver_rms_px)}};
    }
    static StereoStageEvidence decode(const Json &j) {
        fields(j, {"pairs", "residuals", "opencv_solver_rms_px"});
        StereoStageEvidence v;
        v.pairs = codec::decode<decltype(v.pairs)>(j.at("pairs"));
        v.residuals = codec::decode<decltype(v.residuals)>(j.at("residuals"));
        v.opencv_solver_rms_px =
            codec::decode<decltype(v.opencv_solver_rms_px)>(j.at("opencv_solver_rms_px"));
        return v;
    }
};
template <> struct Codec<RigGeometry> {
    static Json encode(const RigGeometry &v) {
        return Json{{"T_right_from_left", codec::encode(v.T_right_from_left)},
                    {"T_rig_from_left", codec::encode(v.T_rig_from_left)},
                    {"T_rig_from_right", codec::encode(v.T_rig_from_right)},
                    {"baseline_mm", codec::encode(v.baseline_mm)},
                    {"relative_rotation_angle_rad", codec::encode(v.relative_rotation_angle_rad)}};
    }
    static RigGeometry decode(const Json &j) {
        fields(j, {"T_right_from_left", "T_rig_from_left", "T_rig_from_right", "baseline_mm",
                   "relative_rotation_angle_rad"});
        RigGeometry v;
        v.T_right_from_left = codec::decode<decltype(v.T_right_from_left)>(j.at("T_right_from_left"));
        v.T_rig_from_left = codec::decode<decltype(v.T_rig_from_left)>(j.at("T_rig_from_left"));
        v.T_rig_from_right = codec::decode<decltype(v.T_rig_from_right)>(j.at("T_rig_from_right"));
        v.baseline_mm = codec::decode<decltype(v.baseline_mm)>(j.at("baseline_mm"));
        v.relative_rotation_angle_rad =
            codec::decode<decltype(v.relative_rotation_angle_rad)>(j.at("relative_rotation_angle_rad"));
        return v;
    }
};
template <> struct Codec<StereoCalibrationSolution> {
    static Json encode(const StereoCalibrationSolution &v) {
        return Json{{"target", codec::encode(v.target)},
                    {"left_camera", codec::encode(v.left_camera)},
                    {"right_camera", codec::encode(v.right_camera)},
                    {"config", codec::encode(v.config)},
                    {"left_final_intrinsics", codec::encode(v.left_final_intrinsics)},
                    {"right_final_intrinsics", codec::encode(v.right_final_intrinsics)},
                    {"partition", codec::encode(v.partition)},
                    {"training_model", codec::encode(v.training_model)},
                    {"training_fit", codec::encode(v.training_fit)},
                    {"heldout_validation", codec::encode(v.heldout_validation)},
                    {"final_model", codec::encode(v.final_model)},
                    {"final_fit", codec::encode(v.final_fit)},
                    {"rig", codec::encode(v.rig)}};
    }
    static StereoCalibrationSolution decode(const Json &j) {
        fields(j, {"target", "left_camera", "right_camera", "config", "left_final_intrinsics",
                   "right_final_intrinsics", "partition", "training_model", "training_fit",
                   "heldout_validation", "final_model", "final_fit", "rig"});
        StereoCalibrationSolution v;
        v.target = codec::decode<decltype(v.target)>(j.at("target"));
        v.left_camera = codec::decode<decltype(v.left_camera)>(j.at("left_camera"));
        v.right_camera = codec::decode<decltype(v.right_camera)>(j.at("right_camera"));
        v.config = codec::decode<decltype(v.config)>(j.at("config"));
        v.left_final_intrinsics =
            codec::decode<decltype(v.left_final_intrinsics)>(j.at("left_final_intrinsics"));
        v.right_final_intrinsics =
            codec::decode<decltype(v.right_final_intrinsics)>(j.at("right_final_intrinsics"));
        v.partition = codec::decode<decltype(v.partition)>(j.at("partition"));
        v.training_model = codec::decode<decltype(v.training_model)>(j.at("training_model"));
        v.training_fit = codec::decode<decltype(v.training_fit)>(j.at("training_fit"));
        v.heldout_validation = codec::decode<decltype(v.heldout_validation)>(j.at("heldout_validation"));
        v.final_model = codec::decode<decltype(v.final_model)>(j.at("final_model"));
        v.final_fit = codec::decode<decltype(v.final_fit)>(j.at("final_fit"));
        v.rig = codec::decode<decltype(v.rig)>(j.at("rig"));
        return v;
    }
};
template <> struct Codec<SolverImplementation> {
    static Json encode(const SolverImplementation &v) {
        return Json{{"opencv_version", codec::encode(v.opencv_version)},
                    {"mantis_version", codec::encode(v.mantis_version)},
                    {"mantis_build", codec::encode(v.mantis_build)}};
    }
    static SolverImplementation decode(const Json &j) {
        fields(j, {"opencv_version", "mantis_version", "mantis_build"});
        SolverImplementation v;
        v.opencv_version = codec::decode<decltype(v.opencv_version)>(j.at("opencv_version"));
        v.mantis_version = codec::decode<decltype(v.mantis_version)>(j.at("mantis_version"));
        v.mantis_build = codec::decode<decltype(v.mantis_build)>(j.at("mantis_build"));
        return v;
    }
};
template <> struct Codec<TargetArtifact> {
    static Json encode(const TargetArtifact &v) {
        return Json{{"revision", codec::encode(v.revision)}, {"target", codec::encode(v.target)}};
    }
    static TargetArtifact decode(const Json &j) {
        fields(j, {"revision", "target"});
        TargetArtifact v;
        v.revision = codec::decode<decltype(v.revision)>(j.at("revision"));
        v.target = codec::decode<decltype(v.target)>(j.at("target"));
        return v;
    }
};
template <> struct Codec<DatasetArtifact> {
    static Json encode(const DatasetArtifact &v) {
        return Json{{"revision", codec::encode(v.revision)},
                    {"target_reference", codec::encode(v.target_reference)},
                    {"raw_capture_references", codec::encode(v.raw_capture_references)},
                    {"dataset", codec::encode(v.dataset)}};
    }
    static DatasetArtifact decode(const Json &j) {
        fields(j, {"revision", "target_reference", "raw_capture_references", "dataset"});
        DatasetArtifact v;
        v.revision = codec::decode<decltype(v.revision)>(j.at("revision"));
        v.target_reference = codec::decode<decltype(v.target_reference)>(j.at("target_reference"));
        v.raw_capture_references =
            codec::decode<decltype(v.raw_capture_references)>(j.at("raw_capture_references"));
        v.dataset = codec::decode<decltype(v.dataset)>(j.at("dataset"));
        return v;
    }
};
template <> struct Codec<CameraArtifact> {
    static Json encode(const CameraArtifact &v) {
        return Json{{"revision", codec::encode(v.revision)},
                    {"dataset_reference", codec::encode(v.dataset_reference)},
                    {"target_reference", codec::encode(v.target_reference)},
                    {"implementation", codec::encode(v.implementation)},
                    {"solution", codec::encode(v.solution)}};
    }
    static CameraArtifact decode(const Json &j) {
        fields(j, {"revision", "dataset_reference", "target_reference", "implementation", "solution"});
        CameraArtifact v;
        v.revision = codec::decode<decltype(v.revision)>(j.at("revision"));
        v.dataset_reference = codec::decode<decltype(v.dataset_reference)>(j.at("dataset_reference"));
        v.target_reference = codec::decode<decltype(v.target_reference)>(j.at("target_reference"));
        v.implementation = codec::decode<decltype(v.implementation)>(j.at("implementation"));
        v.solution = codec::decode<decltype(v.solution)>(j.at("solution"));
        return v;
    }
};
template <> struct Codec<RigArtifact> {
    static Json encode(const RigArtifact &v) {
        return Json{{"revision", codec::encode(v.revision)},
                    {"dataset_reference", codec::encode(v.dataset_reference)},
                    {"target_reference", codec::encode(v.target_reference)},
                    {"left_camera_reference", codec::encode(v.left_camera_reference)},
                    {"right_camera_reference", codec::encode(v.right_camera_reference)},
                    {"implementation", codec::encode(v.implementation)},
                    {"solution", codec::encode(v.solution)}};
    }
    static RigArtifact decode(const Json &j) {
        fields(j, {"revision", "dataset_reference", "target_reference", "left_camera_reference",
                   "right_camera_reference", "implementation", "solution"});
        RigArtifact v;
        v.revision = codec::decode<decltype(v.revision)>(j.at("revision"));
        v.dataset_reference = codec::decode<decltype(v.dataset_reference)>(j.at("dataset_reference"));
        v.target_reference = codec::decode<decltype(v.target_reference)>(j.at("target_reference"));
        v.left_camera_reference =
            codec::decode<decltype(v.left_camera_reference)>(j.at("left_camera_reference"));
        v.right_camera_reference =
            codec::decode<decltype(v.right_camera_reference)>(j.at("right_camera_reference"));
        v.implementation = codec::decode<decltype(v.implementation)>(j.at("implementation"));
        v.solution = codec::decode<decltype(v.solution)>(j.at("solution"));
        return v;
    }
};
} // namespace mantis::calibration::artifacts::codec
