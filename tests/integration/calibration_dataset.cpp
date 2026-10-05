#include <mantis/calibration_dataset_builder.hpp>
#include <mantis/image_layout.hpp>
#include <opencv2/aruco/charuco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>

using namespace mantis;
using namespace mantis::calibration;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed at line " + std::to_string(__LINE__) + ": " #x); } while (false)
constexpr int width = 896, height = 704;
const DatasetAnalysisConfig config{1, {"right", "left"}, 1, 3}; // Deliberately noncanonical input roles.
struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / Id::random().value;
    std::shared_ptr<artifact::Store> store = std::make_shared<artifact::Store>(root);
    ~Fixture() { store.reset(); std::error_code error; std::filesystem::remove_all(root, error); }
};
CalibrationTarget target(bool charuco = false) {
    CalibrationTarget value;
    value.identity = {{"target.ingestion.fixture"}, 9}; value.grid = {8, 6, 40};
    if (charuco) value.pattern = CharucoDefinition{"DICT_6X6_250", 25};
    value.measurement = {8 * 41.3, 6 * 40.6, MeasurementProvenance{0.1, 0.2, "fixture.instrument", "active extents"}};
    return value;
}
cv::Mat checkerboard(int center, int pitch = 40, bool perspective = false) {
    cv::Mat image(height, width, CV_8UC1, cv::Scalar(255));
    const int x0 = center - 4 * pitch, y0 = height / 2 - 3 * pitch;
    for (int row = 0; row < 6; ++row) for (int col = 0; col < 8; ++col) if ((row + col) % 2 == 0)
        cv::rectangle(image, cv::Rect(x0 + col * pitch, y0 + row * pitch, pitch, pitch), cv::Scalar(0), cv::FILLED);
    if (perspective) {
        const float w = float(width - 1), h = float(height - 1);
        const std::vector<cv::Point2f> from{{0, 0}, {w, 0}, {w, h}, {0, h}};
        const std::vector<cv::Point2f> to{{8, 12}, {w - 18, 4}, {w - 4, h - 20}, {15, h - 8}};
        cv::Mat warped;
        cv::warpPerspective(image, warped, cv::getPerspectiveTransform(from, to), image.size(), cv::INTER_LINEAR,
                            cv::BORDER_CONSTANT, cv::Scalar(255));
        return warped;
    }
    return image;
}
cv::Mat charuco_image() {
    cv::Mat image;
#if CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR < 7
    const auto board = cv::aruco::CharucoBoard::create(8, 6, 40, 25, cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250));
    board->draw({640, 512}, image, 64, 1);
#else
    cv::aruco::CharucoBoard board({8, 6}, 40, 25, cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250));
    board.generateImage({640, 512}, image, 64, 1);
#endif
    return image;
}
data::Published image_packet(const cv::Mat &image, std::string role, uint64_t child_sequence, bool packed) {
    data::Packet packet; packet.type = schema::image;
    packet.header.sequence.value = child_sequence;
    packet.header.frame = {{"optical." + role}, "optical +X right +Y down +Z forward"};
    packet.header.metadata = {{"role", role}, {"identity", "sensor." + role}, {"fourcc", packed ? "Y10P" : "GREY"}};
    CHECK(image.type() == CV_8UC1 && (!packed || image.cols % 4 == 0));
    const size_t stride = size_t(image.cols) * (packed ? 5u : 4u) / 4 + 16;
    const size_t bytes = stride * size_t(image.rows);
    memory::BufferBuilder buffer(bytes);
    auto out = buffer.writable(); std::fill(out.begin(), out.end(), std::byte{0xa5});
    for (int y = 0; y < image.rows; ++y) for (int x = 0; x < image.cols; ++x) {
        const size_t offset = size_t(y) * stride + (packed ? size_t(x / 4) * 5 + size_t(x % 4) : size_t(x));
        out[offset] = static_cast<std::byte>(image.at<uint8_t>(y, x));
        if (packed && x % 4 == 0) out[size_t(y) * stride + size_t(x / 4) * 5 + 4] = std::byte{0xe4}; // Samples have tails 0,1,2,3.
    }
    if (packed) {
        packet.header.metadata.insert({{"org.mantis.image.layout", "mipi-raw10-v1"}, {"org.mantis.image.bits_per_sample", "10"},
            {"org.mantis.image.width", std::to_string(image.cols)}, {"org.mantis.image.height", std::to_string(image.rows)},
            {"org.mantis.image.row_stride_bytes", std::to_string(stride)}});
        packet.attributes.push_back({{std::string(data::packed_image_bytes), schema::ScalarType::u8, {bytes}, {1}, "byte"}, std::move(buffer).publish()});
    } else {
        packet.attributes.push_back({{"org.mantis.pixels", schema::ScalarType::u8, {uint64_t(image.rows), uint64_t(image.cols)}, {stride, 1}, "intensity"}, std::move(buffer).publish()});
    }
    return data::publish(std::move(packet));
}
data::Published frameset(uint64_t sequence, const cv::Mat &left, const cv::Mat &right, bool packed = false) {
    data::Packet packet; packet.type = schema::frameset; packet.header.sequence.value = sequence;
    packet.frames = {image_packet(right, "right", sequence + 10001, packed), image_packet(left, "left", sequence + 10000, packed)};
    // A third unrequested camera is valid. Its missing physical identity is irrelevant to this analysis.
    auto auxiliary = *image_packet(cv::Mat(4, 4, CV_8UC1, cv::Scalar(0)), "aux", sequence + 10002, false);
    auxiliary.header.metadata.erase("identity");
    packet.frames.push_back(data::publish(std::move(auxiliary)));
    return data::publish(std::move(packet));
}
Id capture(Fixture &fixture, const std::vector<data::Published> &packets, artifact::ArtifactType type = {"org.mantis.RawCapture", 2}) {
    const auto id = fixture.store->begin(std::move(type), {});
    for (const auto &packet : packets) fixture.store->append(id, *packet);
    fixture.store->finalize(id);
    return id;
}
CalibrationDataset build(Fixture &fixture, std::vector<Id> ids, CalibrationTarget value = target(), DatasetAnalysisConfig analysis = config) {
    auto result = build_calibration_dataset(fixture.store, std::move(ids), std::move(value), std::move(analysis));
    if (!result) throw std::runtime_error(result.error().message);
    CHECK(validate_calibration_dataset(*result));
    return std::move(*result);
}
void builder_error(Fixture &fixture, const std::vector<Id> &ids, Status expected,
                   CalibrationTarget value = target(), DatasetAnalysisConfig analysis = config) {
    const auto a = build_calibration_dataset(fixture.store, ids, value, analysis);
    const auto b = build_calibration_dataset(fixture.store, ids, value, analysis);
    CHECK(!a && !b && a.error().code == expected && a.error().code == b.error().code);
    CHECK(!a.error().message.empty() && a.error().message == b.error().message);
}
void same_target(const CalibrationTarget &a, const CalibrationTarget &b) {
    CHECK(a.identity.id == b.identity.id && a.identity.revision == b.identity.revision && a.type() == b.type());
    CHECK(a.grid.squares_x == b.grid.squares_x && a.grid.squares_y == b.grid.squares_y && a.grid.nominal_square_size_mm == b.grid.nominal_square_size_mm);
    CHECK(a.measurement.active_width_mm == b.measurement.active_width_mm && a.measurement.active_height_mm == b.measurement.active_height_mm);
    CHECK(a.measurement.provenance.has_value() == b.measurement.provenance.has_value());
    if (a.measurement.provenance) {
        const auto &x = *a.measurement.provenance, &y = *b.measurement.provenance;
        CHECK(x.width_uncertainty_mm == y.width_uncertainty_mm && x.height_uncertainty_mm == y.height_uncertainty_mm && x.instrument == y.instrument && x.note == y.note);
    }
    if (a.type() == TargetType::charuco) {
        const auto &x = std::get<CharucoDefinition>(a.pattern), &y = std::get<CharucoDefinition>(b.pattern);
        CHECK(x.dictionary == y.dictionary && x.nominal_marker_size_mm == y.nominal_marker_size_mm && x.pattern_layout == y.pattern_layout);
    }
}
void same_observation(const TargetObservation &a, const TargetObservation &b) {
    CHECK(a.point_ids == b.point_ids && a.image_width == b.image_width && a.image_height == b.image_height);
    CHECK(a.evidence.detected_points == b.evidence.detected_points && a.evidence.detected_markers == b.evidence.detected_markers && a.evidence.partial == b.evidence.partial);
    for (size_t i = 0; i < a.point_ids.size(); ++i) {
        CHECK(std::abs(a.image_points_px[i].x_px - b.image_points_px[i].x_px) < 1e-6);
        CHECK(std::abs(a.image_points_px[i].y_px - b.image_points_px[i].y_px) < 1e-6);
        CHECK(a.object_points_mm[i].x_mm == b.object_points_mm[i].x_mm && a.object_points_mm[i].y_mm == b.object_points_mm[i].y_mm && a.object_points_mm[i].z_mm == b.object_points_mm[i].z_mm);
    }
}
void same_dataset(const CalibrationDataset &a, const CalibrationDataset &b) {
    same_target(a.target, b.target);
    CHECK(a.raw_capture_ids == b.raw_capture_ids && a.config.camera_roles == b.config.camera_roles);
    CHECK(a.config.schema_version == b.config.schema_version && a.config.selection_policy_version == b.config.selection_policy_version && a.config.max_selected_per_camera == b.config.max_selected_per_camera);
    CHECK(a.cameras.size() == b.cameras.size() && a.records.size() == b.records.size());
    for (size_t i = 0; i < a.cameras.size(); ++i) {
        const auto &x = a.cameras[i], &y = b.cameras[i];
        CHECK(x.role == y.role && x.camera_id == y.camera_id && x.image_width == y.image_width && x.image_height == y.image_height);
        CHECK(x.optical_frame.id == y.optical_frame.id && x.optical_frame.name == y.optical_frame.name);
    }
    for (size_t i = 0; i < a.records.size(); ++i) {
        const auto &x = a.records[i], &y = b.records[i];
        CHECK(x.key == y.key && x.outcome == y.outcome && x.selection_rank == y.selection_rank && x.diversity == y.diversity);
        CHECK(x.observation.has_value() == y.observation.has_value());
        if (x.observation) same_observation(*x.observation, *y.observation);
        if (x.diversity) CHECK(*quantize_diversity_descriptor(*x.diversity) == *quantize_diversity_descriptor(*y.diversity));
    }
}
const DatasetObservationRecord &record(const CalibrationDataset &dataset, const Id &id, uint64_t sequence, std::string_view role) {
    const auto found = std::find_if(dataset.records.begin(), dataset.records.end(), [&](const auto &r) {
        return r.key.frame == FrameSetKey{id, sequence} && r.key.camera_role == role;
    });
    CHECK(found != dataset.records.end()); return *found;
}
std::vector<ObservationKey> selected_keys(const CalibrationDataset &dataset, std::string_view role) {
    std::vector<const DatasetObservationRecord *> records;
    for (const auto &r : dataset.records) if (r.key.camera_role == role && r.selection_rank) records.push_back(&r);
    std::sort(records.begin(), records.end(), [](auto a, auto b) { return *a->selection_rank < *b->selection_rank; });
    std::vector<ObservationKey> keys; for (auto r : records) keys.push_back(r->key);
    return keys;
}
ObservationKey expected_key(const Id &id, uint64_t sequence, std::string role) { return {{id, sequence}, role, {"sensor." + role}}; }
std::vector<Hash> source_hashes(Fixture &fixture, const Id &id) {
    artifact::CaptureReader reader(fixture.store, id); std::vector<Hash> hashes;
    while (auto packet = reader.next()) for (const auto &image : packet->frames) for (const auto &a : image->attributes)
        hashes.push_back(content_hash(*a.buffer.map_read()));
    return hashes;
}
void checkerboard_multi_capture() {
    Fixture fixture;
    std::vector<Id> ids{fixture.store->begin({"org.mantis.RawCapture", 2}, {}), fixture.store->begin({"org.mantis.RawCapture", 2}, {})};
    std::sort(ids.begin(), ids.end()); const auto &a = ids[0], &b = ids[1];
    const cv::Mat blank(height, width, CV_8UC1, cv::Scalar(255));
    fixture.store->append(a, *frameset(17, checkerboard(220), checkerboard(596)));
    fixture.store->append(a, *frameset(18, checkerboard(300, 40, true), checkerboard(676, 40, true)));
    fixture.store->append(a, *frameset(19, blank, blank)); fixture.store->finalize(a);
    fixture.store->append(b, *frameset(17, checkerboard(676), checkerboard(220), true));
    fixture.store->append(b, *frameset(18, checkerboard(448, 48), checkerboard(448, 48), true));
    fixture.store->append(b, *frameset(19, blank, blank, true)); fixture.store->finalize(b);
    const auto before = source_hashes(fixture, b);
    const auto artifact_hash = fixture.store->get(b).hash;
    const auto dataset = build(fixture, {b, a}); same_target(dataset.target, target());
    same_dataset(dataset, build(fixture, {a, b}, target(), {1, {"left", "right"}, 1, 3}));
    same_dataset(dataset, build(fixture, {b, a}));
    CHECK(dataset.raw_capture_ids == ids && dataset.records.size() == 12);
    CHECK(record(dataset, a, 17, "left").key.frame != record(dataset, b, 17, "left").key.frame);
    const std::vector<ObservationKey> left{expected_key(a, 17, "left"), expected_key(b, 17, "left"), expected_key(b, 18, "left")};
    const std::vector<ObservationKey> right{expected_key(b, 17, "right"), expected_key(a, 18, "right"), expected_key(b, 18, "right")};
    CHECK(selected_keys(dataset, "left") == left && selected_keys(dataset, "right") == right);
    CHECK(!record(dataset, a, 18, "left").selection_rank && record(dataset, a, 18, "left").observation);
    CHECK(record(dataset, a, 19, "left").outcome == DetectionOutcome::no_target && !record(dataset, a, 19, "left").diversity);
    for (const auto &r : dataset.records) {
        CHECK(r.key.camera_role == "left" || r.key.camera_role == "right");
        CHECK(r.key.camera_id.value == "sensor." + r.key.camera_role);
        if (!r.observation) continue;
        CHECK(r.observation->source.frameset_sequence == r.key.frame.frameset_sequence && r.observation->point_ids.size() == 35);
        CHECK(r.observation->point_id_semantics() == PointIdSemantics::detector_grid && r.diversity->visible_fraction == 1);
    }
    const auto summary = summarize_dataset(dataset);
    CHECK(summary && summary->framesets_per_capture.at(a) == 3 && summary->framesets_per_capture.at(b) == 3);
    CHECK(summary->cameras.at("left").detected == 4 && summary->cameras.at("left").no_target == 2 && summary->cameras.at("left").selected == 3);
    CHECK(source_hashes(fixture, b) == before && fixture.store->get(b).hash == artifact_hash);
    auto single_role = config; single_role.camera_roles = {"left"};
    CHECK(build(fixture, ids, target(), single_role).records.size() == 6);
    const auto equivalent = checkerboard(448, 48);
    const auto raw = capture(fixture, {frameset(71, equivalent, equivalent)});
    const auto packed = capture(fixture, {frameset(71, equivalent, equivalent, true)});
    const auto raw_dataset = build(fixture, {raw}), packed_dataset = build(fixture, {packed});
    for (const auto &role : {"left", "right"}) {
        same_observation(*record(raw_dataset, raw, 71, role).observation, *record(packed_dataset, packed, 71, role).observation);
        CHECK(record(raw_dataset, raw, 71, role).diversity == record(packed_dataset, packed, 71, role).diversity);
    }
    std::cout << "Multi-capture RAW8/Y10P exact selection: left canonical A/17 B/17 B/18; right B/17 A/18 B/18\n";
}
void charuco_partial() {
    Fixture fixture;
    const auto full = charuco_image();
    auto right_visible = full.clone(), left_visible = full.clone();
    cv::rectangle(right_visible, cv::Rect(0, 0, 64 + 3 * 64, full.rows), cv::Scalar(255), cv::FILLED);
    cv::rectangle(left_visible, cv::Rect(64 + 5 * 64, 0, full.cols - (64 + 5 * 64), full.rows), cv::Scalar(255), cv::FILLED);
    const cv::Mat blank(full.size(), CV_8UC1, cv::Scalar(255));
    const auto id = capture(fixture, {frameset(31, full, full), frameset(32, right_visible, right_visible, true),
                                    frameset(33, blank, blank), frameset(34, left_visible, left_visible)});
    auto analysis = config; analysis.max_selected_per_camera = 2;
    const auto dataset = build(fixture, {id}, target(true), analysis);
    same_dataset(dataset, build(fixture, {id}, target(true), analysis)); same_target(dataset.target, target(true));
    for (const auto &role : {"left", "right"}) {
        const auto &f = record(dataset, id, 31, role), &p = record(dataset, id, 32, role);
        CHECK(f.observation && !f.observation->evidence.partial && f.observation->point_ids.size() == 35 && f.diversity->visible_fraction == 1);
        CHECK(p.observation && p.observation->evidence.partial && p.observation->point_id_semantics() == PointIdSemantics::physical_board);
        std::vector<uint32_t> expected;
        for (uint32_t row = 0; row < 5; ++row) for (uint32_t col = 3; col < 7; ++col) expected.push_back(row * 7 + col);
        CHECK(p.observation->point_ids == expected && p.diversity->visible_fraction == 20.0 / 35.0);
        CHECK((*quantize_diversity_descriptor(*p.diversity))[7] == 571429);
        CHECK(selected_keys(dataset, role) == std::vector<ObservationKey>({expected_key(id, 34, role), expected_key(id, 31, role)}));
        CHECK(!p.selection_rank && record(dataset, id, 33, role).outcome == DetectionOutcome::no_target);
    }
    const auto summary = summarize_dataset(dataset);
    CHECK(summary && summary->cameras.at("left").partial_charuco == 2 && summary->cameras.at("left").full_charuco == 1);
    CHECK(summary->cameras.at("left").minimum_points == 20 && summary->cameras.at("left").maximum_points == 35);
}
template<class Change>
void changed_image(data::Packet &packet, size_t index, Change change) {
    auto image = *packet.frames[index]; change(image); packet.frames[index] = data::publish(std::move(image));
}
void source_and_camera_errors() {
    Fixture fixture; const cv::Mat blank(height, width, CV_8UC1, cv::Scalar(255));
    const auto base = frameset(17, blank, blank); const auto good = capture(fixture, {base});
    for (const auto &type : {artifact::ArtifactType{"org.mantis.fixture", 1}, artifact::ArtifactType{"org.mantis.RawCapture", 1}, artifact::ArtifactType{"org.mantis.RawCapture", 3}})
        builder_error(fixture, {capture(fixture, {base}, type)}, Status::incompatible);
    const auto open = fixture.store->begin({"org.mantis.RawCapture", 2}, {}); fixture.store->append(open, *base);
    builder_error(fixture, {open}, Status::incompatible);
    fixture.store->prepare_finalize(open); builder_error(fixture, {open}, Status::incompatible);
    fixture.store->abandon(open); builder_error(fixture, {open}, Status::incompatible);
    builder_error(fixture, {good, good}, Status::invalid_argument); builder_error(fixture, {}, Status::invalid_argument);
    builder_error(fixture, {{"not.a.capture"}}, Status::not_found);
    auto analysis = config; analysis.camera_roles = {"LEFT"}; builder_error(fixture, {good}, Status::corrupt, target(), analysis);
    analysis.camera_roles = {"left", "left"}; builder_error(fixture, {good}, Status::invalid_argument, target(), analysis);
    analysis.camera_roles = {}; builder_error(fixture, {good}, Status::invalid_argument, target(), analysis);
    analysis = config; analysis.max_selected_per_camera = 0; builder_error(fixture, {good}, Status::invalid_argument, target(), analysis);
    auto invalid_target = target(); invalid_target.grid.squares_x = 0; builder_error(fixture, {good}, Status::invalid_argument, invalid_target);
    CHECK(!build_calibration_dataset({}, {good}, target(), config));
    CancellationToken cancelled; cancelled.cancel();
    CHECK(build_calibration_dataset(fixture.store, {good}, target(), config, cancelled).error().code == Status::cancelled);
    for (unsigned change = 0; change < 4; ++change) {
        auto packet = *base;
        changed_image(packet, 1, [&](auto &image) {
            if (change == 0) image.header.metadata["identity"] = "different.sensor";
            if (change == 1) image = *image_packet(cv::Mat(height, width + 4, CV_8UC1, cv::Scalar(255)), "left", 999, false);
            if (change == 2) image.header.frame.id = {"different.optical"};
            if (change == 3) image.header.frame.name += " changed convention";
        });
        const auto bad = capture(fixture, {data::publish(packet)}); builder_error(fixture, {good, bad}, Status::incompatible);
        packet.header.sequence.value = 18;
        builder_error(fixture, {capture(fixture, {base, data::publish(packet)})}, Status::incompatible);
    }
    auto missing = *base; missing.frames.erase(missing.frames.begin());
    builder_error(fixture, {capture(fixture, {data::publish(missing)})}, Status::corrupt);
    auto duplicate = *base; duplicate.frames.push_back(duplicate.frames[0]);
    builder_error(fixture, {capture(fixture, {data::publish(duplicate)})}, Status::corrupt);
    for (const auto &field : {"role", "identity"}) {
        auto packet = *base; changed_image(packet, 1, [&](auto &image) { image.header.metadata.erase(field); });
        builder_error(fixture, {capture(fixture, {data::publish(packet)})}, Status::corrupt);
    }
    auto frame = *base; changed_image(frame, 1, [](auto &image) { image.header.frame.name.clear(); });
    builder_error(fixture, {capture(fixture, {data::publish(frame)})}, Status::corrupt);
    // The real Store prevents constructing non-FrameSet records or duplicate
    // parent sequences. M3 also guards both after CaptureReader for corrupt sources.
    const auto protected_capture = fixture.store->begin({"org.mantis.RawCapture", 2}, {});
    bool non_frame{}, duplicate_sequence{};
    try { fixture.store->append(protected_capture, *base->frames[0]); } catch (const Failure &f) { non_frame = f.error.code == Status::incompatible; }
    fixture.store->append(protected_capture, *base);
    try { fixture.store->append(protected_capture, *base); } catch (const Failure &f) { duplicate_sequence = f.error.code == Status::corrupt; }
    CHECK(non_frame && duplicate_sequence); fixture.store->abandon(protected_capture);
    const auto zero = build(fixture, {good});
    CHECK(summarize_dataset(zero)->cameras.at("left").detected == 0 && selected_keys(zero, "left").empty());
}
template<class Board> constexpr bool has_legacy_pattern = requires(Board &board) { board.setLegacyPattern(true); };
void image_and_backend_errors() {
    Fixture fixture; const cv::Mat blank(height, width, CV_8UC1, cv::Scalar(255));
    for (unsigned change = 0; change < 5; ++change) {
        auto packet = *frameset(17, blank, blank, change >= 3);
        changed_image(packet, 1, [&](auto &image) {
            if (change == 0) image.header.metadata["fourcc"] = "Y10P";
            if (change == 1) image.attributes[0].descriptor.name = "org.mantis.unknown";
            if (change == 2) image.attributes[0].descriptor.stride[0] = width - 1;
            if (change == 3) image.header.metadata["org.mantis.image.bits_per_sample"] = "12";
            if (change == 4) image.header.metadata["org.mantis.image.width"] = "not.a.number";
        });
        builder_error(fixture, {capture(fixture, {data::publish(packet)})}, change == 1 || change == 3 ? Status::unsupported : Status::corrupt);
    }
    const auto id = capture(fixture, {frameset(17, blank, blank)});
    auto unsupported = target(true); std::get<CharucoDefinition>(unsupported.pattern).dictionary = "UNKNOWN";
    builder_error(fixture, {id}, Status::invalid_argument, unsupported);
    if (!has_legacy_pattern<cv::aruco::CharucoBoard>) {
        auto white = target(true);
        std::get<CharucoDefinition>(white.pattern).pattern_layout = CharucoPatternLayout::white_square_at_origin_even_rows;
        CHECK(validate_target(white)); builder_error(fixture, {id}, Status::incompatible, white);
        std::cout << "OpenCV " << CV_VERSION << ": unsupported white-origin physical layout propagated as incompatible\n";
    }
}
void metadata_lifetime() {
    CalibrationDataset dataset; std::weak_ptr<artifact::Store> weak;
    {
        Fixture fixture; weak = fixture.store;
        const cv::Mat blank(height, width, CV_8UC1, cv::Scalar(255));
        dataset = build(fixture, {capture(fixture, {frameset(17, blank, blank)})});
    }
    CHECK(weak.expired() && validate_calibration_dataset(dataset));
}
int main() {
    try {
        cv::setNumThreads(1);
        checkerboard_multi_capture(); charuco_partial(); source_and_camera_errors(); image_and_backend_errors(); metadata_lifetime();
        std::cout << "RawCapture dataset ingestion: canonical identities, exact roles, packing, partial visibility, selection, source errors and metadata-only lifetime passed\n";
        return 0;
    } catch (const std::exception &failure) { std::cerr << failure.what() << '\n'; return 1; }
}
