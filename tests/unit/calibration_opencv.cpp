#include <mantis/calibration_opencv.hpp>
#include <opencv2/aruco/charuco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <iostream>
#include <limits>
#include <numeric>

using namespace mantis;
using namespace mantis::calibration;
using namespace mantis::calibration::opencv;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed at line " + std::to_string(__LINE__) + ": " #x); } while (false)
constexpr int pitch_px = 64, border_px = 64;
const ObservationSource source{{"capture.synthetic"}, 23, {"camera.left"}, "LEFT"};
CalibrationTarget target(bool charuco = false) {
    CalibrationTarget value;
    value.identity = {{"target.synthetic"}, 5};
    value.grid = {8, 6, 40};
    if (charuco) value.pattern = CharucoDefinition{"DICT_6X6_250", 25};
    return value;
}
GrayImageView view(const cv::Mat &image) {
    CHECK(image.type() == CV_8UC1);
    return {{image.ptr<uint8_t>(), size_t(image.rows - 1) * image.step + size_t(image.cols)},
            static_cast<uint32_t>(image.cols), static_cast<uint32_t>(image.rows), image.step};
}
TargetObservation detection(const CalibrationTarget &value, const cv::Mat &image) {
    auto result = detect_target(value, view(image), source);
    if (!result) throw std::runtime_error(result.error().message);
    CHECK(result->has_value());
    CHECK(validate_target_observation(**result));
    return std::move(**result);
}
void error(const CalibrationTarget &value, GrayImageView image, const ObservationSource &identity = source) {
    auto result = detect_target(value, image, identity);
    CHECK(!result && result.error().code == Status::invalid_argument && result.error().component == "calibration");
    CHECK(!result.error().message.empty());
    auto again = detect_target(value, image, identity);
    CHECK(!again && again.error().message == result.error().message);
}
void same_detection(const TargetObservation &a, const TargetObservation &b) {
    CHECK(a.point_ids == b.point_ids);
    CHECK(a.evidence.detected_points == b.evidence.detected_points);
    CHECK(a.evidence.detected_markers == b.evidence.detected_markers && a.evidence.partial == b.evidence.partial);
    for (size_t i = 0; i < a.point_ids.size(); ++i) {
        CHECK(a.image_points_px[i].x_px == b.image_points_px[i].x_px);
        CHECK(a.image_points_px[i].y_px == b.image_points_px[i].y_px);
        CHECK(a.object_points_mm[i].x_mm == b.object_points_mm[i].x_mm);
        CHECK(a.object_points_mm[i].y_mm == b.object_points_mm[i].y_mm && a.object_points_mm[i].z_mm == b.object_points_mm[i].z_mm);
    }
}
void geometry(const TargetObservation &observation, double pitch_x = 40, double pitch_y = 40) {
    CHECK(observation.source.raw_capture_id == source.raw_capture_id && observation.source.frameset_sequence == 23);
    CHECK(observation.source.camera_id == source.camera_id && observation.source.camera_role == "LEFT");
    CHECK(observation.target.id.value == "target.synthetic" && observation.target.revision == 5);
    for (size_t i = 0; i < observation.point_ids.size(); ++i) {
        const auto id = observation.point_ids[i];
        const auto &point = observation.object_points_mm[i];
        CHECK(std::abs(point.x_mm - double(id % 7 + 1) * pitch_x) < 1e-10);
        CHECK(std::abs(point.y_mm - double(id / 7 + 1) * pitch_y) < 1e-10);
        CHECK(point.z_mm == 0);
    }
}
void measured_scale(const CalibrationTarget &nominal, const cv::Mat &image, const TargetObservation &unmeasured) {
    auto measured = nominal;
    measured.measurement.active_width_mm = 8 * 41.3;
    measured.measurement.active_height_mm = 6 * 40.6;
    const auto scaled = detection(measured, image);
    CHECK(scaled.point_ids == unmeasured.point_ids);
    for (size_t i = 0; i < scaled.point_ids.size(); ++i) {
        CHECK(scaled.image_points_px[i].x_px == unmeasured.image_points_px[i].x_px);
        CHECK(scaled.image_points_px[i].y_px == unmeasured.image_points_px[i].y_px);
    }
    geometry(scaled, 41.3, 40.6);
}
cv::Mat checkerboard_image() {
    cv::Mat image(6 * pitch_px + 2 * border_px, 8 * pitch_px + 2 * border_px, CV_8UC1, cv::Scalar(255));
    for (int row = 0; row < 6; ++row) for (int col = 0; col < 8; ++col)
        if ((row + col) % 2 == 0)
            cv::rectangle(image, cv::Rect(border_px + col * pitch_px, border_px + row * pitch_px, pitch_px, pitch_px),
                          cv::Scalar(0), cv::FILLED);
    return image;
}
cv::Mat perspective(const cv::Mat &image, cv::Mat &transform) {
    const float w = float(image.cols - 1), h = float(image.rows - 1);
    const std::vector<cv::Point2f> from{{0, 0}, {w, 0}, {w, h}, {0, h}};
    const std::vector<cv::Point2f> to{{30, 20}, {w - 40, 45}, {w - 15, h - 30}, {55, h - 10}};
    transform = cv::getPerspectiveTransform(from, to);
    cv::Mat warped;
    cv::warpPerspective(image, warped, transform, image.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(255));
    return warped;
}
cv::Mat rotation_transform(int rotation, const cv::Mat &original) {
    const double w = original.cols - 1, h = original.rows - 1;
    if (rotation == cv::ROTATE_180) return (cv::Mat_<double>(3, 3) << -1, 0, w, 0, -1, h, 0, 0, 1);
    if (rotation == cv::ROTATE_90_CLOCKWISE) return (cv::Mat_<double>(3, 3) << 0, -1, h, 1, 0, 0, 0, 0, 1);
    return (cv::Mat_<double>(3, 3) << 0, 1, 0, -1, 0, w, 0, 0, 1);
}
std::vector<cv::Point2f> ideal_pixels() {
    std::vector<cv::Point2f> pixels;
    for (int row = 0; row < 5; ++row) for (int col = 0; col < 7; ++col)
        pixels.emplace_back(float(border_px + (col + 1) * pitch_px) - 0.5f,
                            float(border_px + (row + 1) * pitch_px) - 0.5f);
    return pixels;
}
std::vector<uint32_t> physical_corner_ids(const TargetObservation &observation, const cv::Mat &transform) {
    const auto original = ideal_pixels();
    std::vector<cv::Point2f> projected;
    cv::perspectiveTransform(original, projected, transform);
    std::vector<uint32_t> mapping;
    for (const auto &pixel : observation.image_points_px) {
        double best = std::numeric_limits<double>::infinity();
        uint32_t physical_id{};
        for (size_t i = 0; i < projected.size(); ++i) {
            const auto dx = double(projected[i].x) - pixel.x_px, dy = double(projected[i].y) - pixel.y_px;
            const double distance = std::hypot(dx, dy);
            if (distance < best) { best = distance; physical_id = static_cast<uint32_t>(i); }
        }
        CHECK(best < 1.5); // Synthetic pixel contract, not a calibration-quality gate.
        mapping.push_back(physical_id);
    }
    return mapping;
}
void checkerboard_contract() {
    const auto value = target();
    const auto image = checkerboard_image();
    const auto original_bytes = image.clone();
    const auto detected = detection(value, image);
    geometry(detected);
    CHECK(detected.target_type == TargetType::checkerboard);
    CHECK(detected.point_id_semantics() == PointIdSemantics::detector_grid);
    CHECK(detected.image_width == uint32_t(image.cols) && detected.image_height == uint32_t(image.rows));
    CHECK(detected.evidence.detected_points == 35 && detected.evidence.detected_markers == 0 && !detected.evidence.partial);
    std::vector<uint32_t> forward(35); std::iota(forward.begin(), forward.end(), 0u);
    CHECK(detected.point_ids == forward);
    std::vector<cv::Point2f> native;
    constexpr int flags = cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_ACCURACY | cv::CALIB_CB_EXHAUSTIVE;
    CHECK(cv::findChessboardCornersSB(image, {7, 5}, native, flags) && native.size() == 35);
    for (size_t i = 0; i < native.size(); ++i) {
        CHECK(std::abs(detected.image_points_px[i].x_px - native[i].x) < 1e-5);
        CHECK(std::abs(detected.image_points_px[i].y_px - native[i].y) < 1e-5);
    }
    CHECK(!cv::findChessboardCornersSB(image, {8, 6}, native, flags)); // Squares are not inner corners.
    same_detection(detected, detection(value, image));
    measured_scale(value, image, detected);
    CHECK(cv::norm(image, original_bytes, cv::NORM_INF) == 0); // Borrowed pixels were not mutated.

    // Also exercise a non-contiguous buffer with no final-row padding.
    const auto stride = size_t(image.cols) + 17;
    std::vector<uint8_t> padded(size_t(image.rows - 1) * stride + size_t(image.cols), 77);
    for (int row = 0; row < image.rows; ++row) std::copy_n(image.ptr<uint8_t>(row), image.cols, padded.data() + size_t(row) * stride);
    const auto before = padded;
    auto result = detect_target(value, {padded, uint32_t(image.cols), uint32_t(image.rows), stride}, source);
    CHECK(result && *result);
    same_detection(detected, **result);
    CHECK(padded == before);

    const cv::Mat identity = cv::Mat::eye(3, 3, CV_64F);
    const auto normal_mapping = physical_corner_ids(detected, identity);
    auto reverse = forward; std::reverse(reverse.begin(), reverse.end());
    CHECK(normal_mapping == forward || normal_mapping == reverse);
    cv::Mat homography;
    const auto warped = perspective(image, homography);
    const auto perspective_detection = detection(value, warped);
    const auto perspective_mapping = physical_corner_ids(perspective_detection, homography);
    CHECK(perspective_mapping == forward || perspective_mapping == reverse);
    same_detection(perspective_detection, detection(value, warped));

    std::vector<std::vector<uint32_t>> rotation_mappings;
    for (int rotation : {cv::ROTATE_180, cv::ROTATE_90_CLOCKWISE, cv::ROTATE_90_COUNTERCLOCKWISE}) {
        cv::Mat rotated; cv::rotate(image, rotated, rotation);
        const auto rotated_detection = detection(value, rotated);
        CHECK(rotated_detection.point_ids == forward);
        const auto physical = physical_corner_ids(rotated_detection, rotation_transform(rotation, image));
        CHECK(physical == forward || physical == reverse);
        same_detection(rotated_detection, detection(value, rotated));
        if (rotation == cv::ROTATE_180) {
            CHECK(cv::norm(image, rotated, cv::NORM_INF) == 0); // The board image encodes no 180-degree origin.
            same_detection(detected, rotated_detection);
            for (size_t i = 0; i < physical.size(); ++i) CHECK(physical[i] == 34 - normal_mapping[i]);
        }
        std::cout << "Checkerboard rotation " << (rotation == cv::ROTATE_180 ? 180 : rotation == cv::ROTATE_90_CLOCKWISE ? 90 : 270)
                  << ": detector ID 0 -> physical fixture corner " << physical.front() << '\n';
        rotation_mappings.push_back(physical);
    }
    for (size_t i = 0; i < 35; ++i) CHECK(rotation_mappings[1][i] == 34 - rotation_mappings[2][i]);
    std::cout << "Checkerboard normal/perspective: detector ID 0 -> physical fixture corner "
              << normal_mapping.front() << '/' << perspective_mapping.front()
              << "; plain symmetric board cannot supply absolute physical IDs across 180 degrees\n";
}

#if CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR < 7
using DictionaryName = cv::aruco::PREDEFINED_DICTIONARY_NAME;
#else
using DictionaryName = cv::aruco::PredefinedDictionaryType;
#endif
cv::Ptr<cv::aruco::Dictionary> native_dictionary(DictionaryName name) {
#if CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR < 7
    return cv::aruco::getPredefinedDictionary(name);
#else
    return cv::makePtr<cv::aruco::Dictionary>(cv::aruco::getPredefinedDictionary(name));
#endif
}
cv::Ptr<cv::aruco::CharucoBoard> native_board(DictionaryName name, float square = 40, float marker = 25) {
    const auto dictionary = native_dictionary(name);
#if CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR < 7
    return cv::aruco::CharucoBoard::create(8, 6, square, marker, dictionary);
#else
    return cv::makePtr<cv::aruco::CharucoBoard>(cv::Size(8, 6), square, marker, *dictionary);
#endif
}
std::vector<cv::Point3f> native_board_points(const cv::Ptr<cv::aruco::CharucoBoard> &board) {
#if CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR < 7
    return board->chessboardCorners;
#else
    return board->getChessboardCorners();
#endif
}
cv::Mat charuco_image(const cv::Ptr<cv::aruco::CharucoBoard> &board) {
    cv::Mat image;
    const cv::Size size(8 * pitch_px + 2 * border_px, 6 * pitch_px + 2 * border_px);
#if CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR < 7
    board->draw(size, image, border_px, 1);
#else
    board->generateImage(size, image, border_px, 1);
#endif
    return image;
}
void charuco_native_contract(const TargetObservation &observation, const cv::Mat &image,
                             const cv::Ptr<cv::aruco::CharucoBoard> &board, DictionaryName name) {
    std::vector<int> ids, marker_ids;
    std::vector<cv::Point2f> corners;
    std::vector<std::vector<cv::Point2f>> markers;
#if CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR < 7
    auto parameters = cv::aruco::DetectorParameters::create();
#else
    auto parameters = cv::makePtr<cv::aruco::DetectorParameters>();
#endif
    parameters->cornerRefinementMethod = cv::aruco::CORNER_REFINE_NONE;
    cv::aruco::detectMarkers(image, native_dictionary(name), markers, marker_ids, parameters);
    CHECK(!marker_ids.empty());
    CHECK(cv::aruco::interpolateCornersCharuco(markers, marker_ids, image, board, corners, ids,
                                              cv::noArray(), cv::noArray(), 2) == int(observation.point_ids.size()));
    auto sorted = ids; std::sort(sorted.begin(), sorted.end());
    CHECK(std::vector<uint32_t>(sorted.begin(), sorted.end()) == observation.point_ids);
    CHECK(observation.evidence.detected_markers == marker_ids.size());
    const auto points = native_board_points(board);
    CHECK(points.size() == 35);
    for (size_t i = 0; i < observation.point_ids.size(); ++i) {
        const auto id = observation.point_ids[i];
        const auto found = std::find(ids.begin(), ids.end(), int(id));
        CHECK(found != ids.end());
        const auto index = static_cast<size_t>(found - ids.begin());
        CHECK(std::abs(observation.image_points_px[i].x_px - corners[index].x) < 1e-5);
        CHECK(std::abs(observation.image_points_px[i].y_px - corners[index].y) < 1e-5);
        CHECK(points[id].x == float(id % 7 + 1) * 40 && points[id].y == float(id / 7 + 1) * 40 && points[id].z == 0);
        const auto physical = scale_target_point(target(true), {double(points[id].x), double(points[id].y), double(points[id].z)});
        CHECK(physical && physical->x_mm == observation.object_points_mm[i].x_mm && physical->y_mm == observation.object_points_mm[i].y_mm);
    }
}
void charuco_contract() {
    const auto value = target(true);
    const auto board = native_board(cv::aruco::DICT_6X6_250);
    const auto image = charuco_image(board);
    const auto original = image.clone();
    const auto full = detection(value, image);
    CHECK(full.target_type == TargetType::charuco && full.point_id_semantics() == PointIdSemantics::physical_board);
    CHECK(full.evidence.detected_points == 35 && full.evidence.detected_markers == 24 && !full.evidence.partial);
    std::vector<uint32_t> all(35); std::iota(all.begin(), all.end(), 0u);
    CHECK(full.point_ids == all);
    geometry(full);
    charuco_native_contract(full, image, board, cv::aruco::DICT_6X6_250);
    same_detection(full, detection(value, image));
    measured_scale(value, image, full);
    const auto mapping = physical_corner_ids(full, cv::Mat::eye(3, 3, CV_64F));
    CHECK(mapping == all); // Pixels verify that the native origin/axes match Mantis.
    for (int rotation : {cv::ROTATE_180, cv::ROTATE_90_CLOCKWISE, cv::ROTATE_90_COUNTERCLOCKWISE}) {
        cv::Mat rotated; cv::rotate(image, rotated, rotation);
        auto rotated_detection = detection(value, rotated);
        CHECK(rotated_detection.point_ids == all);
        CHECK(physical_corner_ids(rotated_detection, rotation_transform(rotation, image)) == all);
        geometry(rotated_detection);
    }
    cv::Mat homography;
    const auto warped = perspective(image, homography);
    const auto perspective_detection = detection(value, warped);
    CHECK(perspective_detection.point_ids == all);
    CHECK(physical_corner_ids(perspective_detection, homography) == all);

    const auto left_image = image(cv::Rect(0, 0, border_px + 5 * pitch_px, image.rows));
    const auto right_offset = border_px + 3 * pitch_px;
    const auto right_image = image(cv::Rect(right_offset, 0, image.cols - right_offset, image.rows));
    const auto left = detection(value, left_image), right = detection(value, right_image);
    CHECK(left.evidence.partial && right.evidence.partial);
    std::vector<uint32_t> left_ids, right_ids;
    for (uint32_t row = 0; row < 5; ++row) {
        for (uint32_t col = 0; col < 4; ++col) left_ids.push_back(row * 7 + col);
        for (uint32_t col = 3; col < 7; ++col) right_ids.push_back(row * 7 + col);
    }
    CHECK(left.point_ids == left_ids && right.point_ids == right_ids);
    CHECK(right.point_ids.front() == 3 && right.point_ids.back() == 34); // No 0..N-1 renumbering.
    charuco_native_contract(left, left_image, board, cv::aruco::DICT_6X6_250);
    charuco_native_contract(right, right_image, board, cv::aruco::DICT_6X6_250);
    geometry(left); geometry(right);
    same_detection(left, detection(value, left_image));
    same_detection(right, detection(value, right_image));
    measured_scale(value, right_image, right);
    const auto common = intersect_observations(left, right);
    CHECK(common && common->size() == 5);
    for (uint32_t row = 0; row < 5; ++row) {
        const auto &point = (*common)[row];
        CHECK(point.point_id == row * 7 + 3);
        CHECK(std::abs(point.left_px.x_px - point.right_px.x_px - right_offset) < 0.05);
        CHECK(std::abs(point.left_px.y_px - point.right_px.y_px) < 0.05);
        CHECK(point.object_mm.x_mm == 160 && point.object_mm.y_mm == double(row + 1) * 40);
    }
    CHECK(cv::norm(image, original, cv::NORM_INF) == 0);
    auto fractional = value;
    fractional.grid.nominal_square_size_mm = 40.123456789;
    std::get<CharucoDefinition>(fractional.pattern).nominal_marker_size_mm = 25.012345678;
    const auto fractional_image = charuco_image(native_board(cv::aruco::DICT_6X6_250,
        float(fractional.grid.nominal_square_size_mm), float(std::get<CharucoDefinition>(fractional.pattern).nominal_marker_size_mm)));
    const auto precise = detection(fractional, fractional_image);
    for (size_t i = 0; i < precise.point_ids.size(); ++i) {
        CHECK(precise.object_points_mm[i].x_mm == double(precise.point_ids[i] % 7 + 1) * fractional.grid.nominal_square_size_mm);
        CHECK(precise.object_points_mm[i].y_mm == double(precise.point_ids[i] / 7 + 1) * fractional.grid.nominal_square_size_mm);
    }
    fractional.measurement.active_width_mm = 8 * 41.3;
    fractional.measurement.active_height_mm = 6 * 40.6;
    geometry(detection(fractional, fractional_image), 41.3, 40.6); // No float geometry round trip.
    std::cout << "ChArUco contract: 35 full corners, 20/20 partial corners, common IDs 3 10 17 24 31; native board indexing and physical scaling verified\n";
}
void dictionary_contract() {
    const std::pair<std::string_view, DictionaryName> expected[] = {
        {"DICT_4X4_50", cv::aruco::DICT_4X4_50}, {"DICT_4X4_100", cv::aruco::DICT_4X4_100},
        {"DICT_4X4_250", cv::aruco::DICT_4X4_250}, {"DICT_4X4_1000", cv::aruco::DICT_4X4_1000},
        {"DICT_5X5_50", cv::aruco::DICT_5X5_50}, {"DICT_5X5_100", cv::aruco::DICT_5X5_100},
        {"DICT_5X5_250", cv::aruco::DICT_5X5_250}, {"DICT_5X5_1000", cv::aruco::DICT_5X5_1000},
        {"DICT_6X6_50", cv::aruco::DICT_6X6_50}, {"DICT_6X6_100", cv::aruco::DICT_6X6_100},
        {"DICT_6X6_250", cv::aruco::DICT_6X6_250}, {"DICT_6X6_1000", cv::aruco::DICT_6X6_1000},
        {"DICT_7X7_50", cv::aruco::DICT_7X7_50}, {"DICT_7X7_100", cv::aruco::DICT_7X7_100},
        {"DICT_7X7_250", cv::aruco::DICT_7X7_250}, {"DICT_7X7_1000", cv::aruco::DICT_7X7_1000},
        {"DICT_ARUCO_ORIGINAL", cv::aruco::DICT_ARUCO_ORIGINAL},
        {"DICT_APRILTAG_16h5", cv::aruco::DICT_APRILTAG_16h5}, {"DICT_APRILTAG_25h9", cv::aruco::DICT_APRILTAG_25h9},
        {"DICT_APRILTAG_36h10", cv::aruco::DICT_APRILTAG_36h10}, {"DICT_APRILTAG_36h11", cv::aruco::DICT_APRILTAG_36h11}
    };
    CHECK(supported_charuco_dictionaries().size() == std::size(expected));
    for (size_t i = 0; i < std::size(expected); ++i) {
        const auto &[name, code] = expected[i];
        CHECK(supported_charuco_dictionaries()[i] == name);
        auto value = target(true);
        std::get<CharucoDefinition>(value.pattern).dictionary = name;
        const auto board = native_board(code);
        const auto image = charuco_image(board);
        auto observed = detection(value, image);
        CHECK(observed.evidence.detected_points == 35);
        charuco_native_contract(observed, image, board, code);
        // First IDs are shared by some dictionaries in one family. Check exact
        // dictionary capacity as well, so a wrong family-size mapping cannot hide.
        const auto capacity = static_cast<uint32_t>(native_dictionary(code)->bytesList.rows);
        cv::Mat blank(64, 64, CV_8UC1, cv::Scalar(255));
        value.grid = {2, capacity, 40};
        auto within_capacity = detect_target(value, view(blank), source);
        CHECK(within_capacity && !*within_capacity);
        ++value.grid.squares_y;
        error(value, view(blank));
    }
}
void invalid_inputs_and_no_target() {
    cv::Mat blank(512, 640, CV_8UC1, cv::Scalar(255));
    for (bool charuco : {false, true}) {
        auto value = target(charuco);
        auto result = detect_target(value, view(blank), source);
        CHECK(result && !*result);
        auto repeated = detect_target(value, view(blank), source);
        CHECK(repeated && !*repeated);
        for (cv::Size dimensions : {cv::Size(1, 1), cv::Size(2, 8), cv::Size(8, 2), cv::Size(8, 8)}) {
            cv::Mat tiny(dimensions, CV_8UC1, cv::Scalar(255));
            auto absent = detect_target(value, view(tiny), source);
            CHECK(absent && !*absent);
        }
        for (int invalid = 0; invalid < 5; ++invalid) {
            auto image = view(blank);
            if (invalid == 0) image.width = 0;
            if (invalid == 1) image.height = 0;
            if (invalid == 2) image.row_stride = image.width - 1;
            if (invalid == 3) image.bytes = image.bytes.first(image.bytes.size() - 1);
            if (invalid == 4) image.bytes = {};
            error(value, image);
        }
        auto overflow = view(blank); overflow.row_stride = std::numeric_limits<size_t>::max(); error(value, overflow);
        value.grid.squares_x = 1; error(value, view(blank));
        value = target(charuco); value.grid.nominal_square_size_mm = 0; error(value, view(blank));
        value = target(charuco); value.measurement.active_width_mm = 320; error(value, view(blank));
        value = target(charuco); value.measurement.provenance.emplace(); error(value, view(blank));
        value = target(charuco); value.identity.id.value.clear(); error(value, view(blank));
        value = target(charuco); value.grid.squares_x = std::numeric_limits<uint32_t>::max(); error(value, view(blank));
        value = target(charuco); auto identity = source; identity.camera_id.value.clear(); error(value, view(blank), identity);
    }
    for (std::string name : {"", "dict_6x6_250", "DICT_6X6_UNKNOWN", "10", " DICT_6X6_250", "DICT_6X6_250 ", "DICT_APRILTAG_16H5"}) {
        auto value = target(true); std::get<CharucoDefinition>(value.pattern).dictionary = name;
        error(value, view(blank));
    }
    auto value = target(true); value.grid.squares_x = 16; value.grid.squares_y = 16;
    std::get<CharucoDefinition>(value.pattern).dictionary = "DICT_4X4_50"; error(value, view(blank));
    for (double size : {1e-300, 1e100}) {
        value = target(true); value.grid.nominal_square_size_mm = size;
        std::get<CharucoDefinition>(value.pattern).nominal_marker_size_mm = size / 2;
        CHECK(validate_target(value)); error(value, view(blank));
    }
    value = target(true); std::get<CharucoDefinition>(value.pattern).nominal_marker_size_mm = std::nextafter(40.0, 0.0);
    CHECK(validate_target(value)); error(value, view(blank));
    for (uint32_t squares : {2u, 3u}) {
        value = target(); value.grid.squares_x = squares;
        CHECK(validate_target(value)); error(value, view(blank)); // SB API constraint, not an M1 quality threshold.
    }
}
int main() {
    try {
        std::cout << "OpenCV " << CV_VERSION << ": 4.6-generation SB/ArUco/ChArUco contracts\n";
        // Keep synthetic tests economical on CI; the adapter does not change global OpenCV settings.
        cv::setNumThreads(1);
        checkerboard_contract(); charuco_contract(); dictionary_contract(); invalid_inputs_and_no_target();
        std::cout << "Calibration OpenCV: synthetic detection, identity, partial visibility, scale, dictionaries and input validation passed\n";
        return 0;
    } catch (const std::exception &failure) { std::cerr << failure.what() << '\n'; return 1; }
}
