#include <mantis/calibration_opencv.hpp>
#include <opencv2/aruco/charuco.hpp>
#include <opencv2/calib3d.hpp>
#include <algorithm>
#include <limits>
#include <numeric>

namespace mantis::calibration::opencv {
namespace {
#if CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR < 7
using DictionaryName = cv::aruco::PREDEFINED_DICTIONARY_NAME;
#else
using DictionaryName = cv::aruco::PredefinedDictionaryType;
#endif
struct DictionaryEntry { std::string_view name; DictionaryName value; };
constexpr DictionaryEntry dictionaries[] = {
    {"DICT_4X4_50", cv::aruco::DICT_4X4_50}, {"DICT_4X4_100", cv::aruco::DICT_4X4_100},
    {"DICT_4X4_250", cv::aruco::DICT_4X4_250}, {"DICT_4X4_1000", cv::aruco::DICT_4X4_1000},
    {"DICT_5X5_50", cv::aruco::DICT_5X5_50}, {"DICT_5X5_100", cv::aruco::DICT_5X5_100},
    {"DICT_5X5_250", cv::aruco::DICT_5X5_250}, {"DICT_5X5_1000", cv::aruco::DICT_5X5_1000},
    {"DICT_6X6_50", cv::aruco::DICT_6X6_50}, {"DICT_6X6_100", cv::aruco::DICT_6X6_100},
    {"DICT_6X6_250", cv::aruco::DICT_6X6_250}, {"DICT_6X6_1000", cv::aruco::DICT_6X6_1000},
    {"DICT_7X7_50", cv::aruco::DICT_7X7_50}, {"DICT_7X7_100", cv::aruco::DICT_7X7_100},
    {"DICT_7X7_250", cv::aruco::DICT_7X7_250}, {"DICT_7X7_1000", cv::aruco::DICT_7X7_1000},
    {"DICT_ARUCO_ORIGINAL", cv::aruco::DICT_ARUCO_ORIGINAL},
    {"DICT_APRILTAG_16h5", cv::aruco::DICT_APRILTAG_16h5},
    {"DICT_APRILTAG_25h9", cv::aruco::DICT_APRILTAG_25h9},
    {"DICT_APRILTAG_36h10", cv::aruco::DICT_APRILTAG_36h10},
    {"DICT_APRILTAG_36h11", cv::aruco::DICT_APRILTAG_36h11}
};
Result<cv::Ptr<cv::aruco::Dictionary>> dictionary_for(std::string_view name) {
    for (const auto &entry : dictionaries) if (name == entry.name) {
#if CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR < 7
        return cv::aruco::getPredefinedDictionary(entry.value);
#else
        return cv::makePtr<cv::aruco::Dictionary>(cv::aruco::getPredefinedDictionary(entry.value));
#endif
    }
    return detail::target_error("Unsupported ChArUco dictionary: " + std::string(name));
}
std::unexpected<Error> detector_error(std::string message) {
    return std::unexpected(Error{Status::corrupt, std::move(message), "calibration"});
}
template<class Board>
Result<void> configure_pattern_layout(Board &board, CharucoPatternLayout layout) {
    const bool white_origin = layout == CharucoPatternLayout::white_square_at_origin_even_rows;
    // Detect the actual API, including vendor backports. Upstream first exposes
    // setLegacyPattern in 4.8.0; the Tier-1 4.6 headers do not provide it.
    if constexpr (requires { board.setLegacyPattern(true); }) {
        board.setLegacyPattern(white_origin);
    } else if (white_origin) {
        return std::unexpected(Error{Status::incompatible,
            "ChArUco white_square_at_origin_even_rows requires OpenCV legacy-pattern support; linked OpenCV "
            CV_VERSION " cannot represent this physical board layout", "calibration"});
    }
    return {};
}
TargetObservation observation_for(const CalibrationTarget &target, GrayImageView image, const ObservationSource &source) {
    TargetObservation observation;
    observation.source = source;
    observation.target = target.identity;
    observation.target_type = target.type();
    observation.image_width = image.width;
    observation.image_height = image.height;
    return observation;
}
Result<void> append_point(TargetObservation &observation, const CalibrationTarget &target,
                          uint32_t id, cv::Point2f pixel) {
    const auto columns = target.grid.squares_x - 1;
    // The executable OpenCV contract verifies this row-major mapping. Reconstruct
    // nominal coordinates in double; OpenCV's float board is not physical truth.
    TargetPoint nominal{double(id % columns + 1) * target.grid.nominal_square_size_mm,
                        double(id / columns + 1) * target.grid.nominal_square_size_mm, 0};
    auto physical = scale_target_point(target, nominal);
    if (!physical) return std::unexpected(physical.error());
    observation.point_ids.push_back(id);
    observation.image_points_px.push_back({double(pixel.x), double(pixel.y)});
    observation.object_points_mm.push_back(*physical);
    return {};
}
Result<std::optional<TargetObservation>> checkerboard(
    const CalibrationTarget &target, const cv::Mat &image, TargetObservation observation, uint64_t corner_count) {
    const auto columns = target.grid.squares_x - 1, rows = target.grid.squares_y - 1;
    if (columns < 3 || rows < 3)
        return detail::target_error("OpenCV SB requires at least three inner corners per axis");
    // SB internally doubles the signed-int pattern area for its search budget.
    if (corner_count > uint64_t(std::numeric_limits<int>::max() / 2))
        return detail::target_error("Checkerboard inner grid exceeds OpenCV signed arithmetic limits");
    std::vector<cv::Point2f> corners;
    constexpr int flags = cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_ACCURACY | cv::CALIB_CB_EXHAUSTIVE;
    if (!cv::findChessboardCornersSB(image, {static_cast<int>(columns), static_cast<int>(rows)}, corners, flags))
        return std::optional<TargetObservation>{};
    if (corners.size() != corner_count) return detector_error("OpenCV SB returned an incomplete inner corner grid");
    for (size_t i = 0; i < corners.size(); ++i) {
        auto appended = append_point(observation, target, static_cast<uint32_t>(i), corners[i]);
        if (!appended) return std::unexpected(appended.error());
    }
    observation.evidence = {static_cast<uint32_t>(corners.size()), 0, false};
    return std::optional<TargetObservation>{std::move(observation)};
}
Result<std::optional<TargetObservation>> charuco(
    const CalibrationTarget &target, const cv::Mat &image, TargetObservation observation, uint64_t corner_count) {
    const auto &definition = std::get<CharucoDefinition>(target.pattern);
    auto dictionary = dictionary_for(definition.dictionary);
    if (!dictionary) return std::unexpected(dictionary.error());
    const auto &grid = target.grid;
    const auto marker_count = uint64_t(grid.squares_x) * grid.squares_y / 2;
    if (marker_count > static_cast<uint64_t>((*dictionary)->bytesList.rows))
        return detail::target_error("ChArUco board needs more markers than the selected dictionary contains");
    if (grid.nominal_square_size_mm > std::numeric_limits<float>::max() ||
        definition.nominal_marker_size_mm > std::numeric_limits<float>::max())
        return detail::target_error("Nominal ChArUco geometry exceeds OpenCV float board representation");
    const float square = static_cast<float>(grid.nominal_square_size_mm);
    const float marker = static_cast<float>(definition.nominal_marker_size_mm);
    if (!detail::positive_finite(square) || !detail::positive_finite(marker) || marker >= square ||
        !std::isfinite(float(grid.squares_x) * square) || !std::isfinite(float(grid.squares_y) * square))
        return detail::target_error("Nominal ChArUco geometry is not representable by OpenCV float board geometry");
#if CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR < 7
    auto board = cv::aruco::CharucoBoard::create(static_cast<int>(grid.squares_x), static_cast<int>(grid.squares_y),
                                               square, marker, *dictionary);
    auto parameters = cv::aruco::DetectorParameters::create();
#else
    auto board = cv::makePtr<cv::aruco::CharucoBoard>(
        cv::Size(static_cast<int>(grid.squares_x), static_cast<int>(grid.squares_y)), square, marker, **dictionary);
    auto parameters = cv::makePtr<cv::aruco::DetectorParameters>();
#endif
    auto configured = configure_pattern_layout(*board, definition.pattern_layout);
    if (!configured) return std::unexpected(configured.error());
#if CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR < 7
    const auto &board_points = board->chessboardCorners;
#else
    const auto board_points = board->getChessboardCorners();
#endif
    if (board_points.size() != corner_count) return detector_error("OpenCV ChArUco board corner count differs from target grid");
    parameters->cornerRefinementMethod = cv::aruco::CORNER_REFINE_NONE;
    std::vector<int> marker_ids, ids;
    std::vector<std::vector<cv::Point2f>> marker_corners;
    std::vector<cv::Point2f> corners;
    cv::aruco::detectMarkers(image, *dictionary, marker_corners, marker_ids, parameters);
    if (marker_ids.empty()) return std::optional<TargetObservation>{};
    const int count = cv::aruco::interpolateCornersCharuco(
        marker_corners, marker_ids, image, board, corners, ids, cv::noArray(), cv::noArray(), 2);
    if (count < 0 || static_cast<size_t>(count) != ids.size() || corners.size() != ids.size())
        return detector_error("OpenCV ChArUco returned inconsistent corner arrays");
    if (!count) return std::optional<TargetObservation>{};
    if (marker_ids.size() > std::numeric_limits<uint32_t>::max())
        return detector_error("OpenCV marker count exceeds observation representation");
    std::vector<size_t> order(ids.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ids[a] < ids[b]; });
    for (auto i : order) {
        if (ids[i] < 0 || static_cast<uint64_t>(ids[i]) >= corner_count)
            return detector_error("OpenCV ChArUco corner ID is outside the board");
        const auto id = static_cast<uint32_t>(ids[i]);
        if (!observation.point_ids.empty() && observation.point_ids.back() == id)
            return detector_error("OpenCV ChArUco returned duplicate corner IDs");
        const auto &native = board_points[id];
        const auto columns = grid.squares_x - 1;
        // Verify only the chessboard-corner ID/index and planar X/Y contract.
        // append_point converts this index into Mantis-owned double coordinates;
        // this asserts no general equivalence of OpenCV coordinate frames.
        if (native.x != float(id % columns + 1) * square || native.y != float(id / columns + 1) * square || native.z != 0)
            return detector_error("OpenCV ChArUco corner indexing differs from the target-local grid contract");
        auto appended = append_point(observation, target, id, corners[i]);
        if (!appended) return std::unexpected(appended.error());
    }
    observation.evidence = {static_cast<uint32_t>(count), static_cast<uint32_t>(marker_ids.size()),
                            ids.size() < corner_count};
    return std::optional<TargetObservation>{std::move(observation)};
}
} // namespace
Result<void> validate_gray_image_view(GrayImageView image) {
    if (!image.width || !image.height) return detail::target_error("Grayscale image dimensions must be nonzero");
    if (image.row_stride < image.width) return detail::target_error("Grayscale row stride must be at least image width");
    const auto max = std::numeric_limits<size_t>::max();
    if (size_t(image.height - 1) > (max - image.width) / image.row_stride)
        return detail::target_error("Grayscale byte extent overflows size_t");
    const auto required = size_t(image.height - 1) * image.row_stride + image.width;
    if (required > image.bytes.size()) return detail::target_error("Grayscale image buffer is too small");
    if (image.width > uint32_t(std::numeric_limits<int>::max()) || image.height > uint32_t(std::numeric_limits<int>::max()) ||
        image.row_stride > static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max()) / image.height)
        return detail::target_error("Grayscale layout exceeds OpenCV representation limits");
    return {};
}
std::span<const std::string_view> supported_charuco_dictionaries() {
    static constexpr auto names = [] {
        std::array<std::string_view, std::size(dictionaries)> values{};
        for (size_t i = 0; i < values.size(); ++i) values[i] = dictionaries[i].name;
        return values;
    }();
    return names;
}
Result<std::optional<TargetObservation>> detect_target(
    const CalibrationTarget &target, GrayImageView image, const ObservationSource &source) {
    auto valid_target = validate_target(target);
    if (!valid_target) return std::unexpected(valid_target.error());
    auto valid_image = validate_gray_image_view(image);
    if (!valid_image) return std::unexpected(valid_image.error());
    auto valid_source = validate_observation_source(source);
    if (!valid_source) return std::unexpected(valid_source.error());
    if (target.identity.id.value.empty()) return detail::target_error("Detection requires target identity");
    const auto &grid = target.grid;
    const auto corner_count = uint64_t(grid.squares_x - 1) * (grid.squares_y - 1);
    if (grid.squares_x > uint32_t(std::numeric_limits<int>::max()) || grid.squares_y > uint32_t(std::numeric_limits<int>::max()) ||
        corner_count > uint64_t(std::numeric_limits<int>::max()))
        return detail::target_error("Target grid exceeds OpenCV integer representation");
    try {
        // Input-only calls; cv::Mat's header API requires a mutable pointer. No
        // pixels are copied or mutated, and the header never escapes this call.
        const cv::Mat view(static_cast<int>(image.height), static_cast<int>(image.width), CV_8UC1,
                           const_cast<uint8_t *>(image.bytes.data()), image.row_stride);
        auto observation = observation_for(target, image, source);
        auto result = target.type() == TargetType::checkerboard
            ? checkerboard(target, view, std::move(observation), corner_count)
            : charuco(target, view, std::move(observation), corner_count);
        if (result && *result) {
            auto valid = validate_target_observation(**result);
            if (!valid) return detector_error("Invalid OpenCV observation: " + valid.error().message);
        }
        return result;
    } catch (const cv::Exception &error) {
        return std::unexpected(Error{Status::incompatible, "OpenCV detection failed: " + std::string(error.what()), "calibration"});
    }
}
} // namespace mantis::calibration::opencv
