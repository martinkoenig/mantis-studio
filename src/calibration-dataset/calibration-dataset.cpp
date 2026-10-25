#include <mantis/calibration_dataset_builder.hpp>
#include <mantis/calibration_opencv.hpp>
#include <mantis/image_layout.hpp>

namespace mantis::calibration {
namespace {
std::unexpected<Error> source_error(Status status, std::string message) {
    return std::unexpected(Error{status, std::move(message), "calibration"});
}
Result<std::string_view> metadata(const data::Packet &image, const std::string &field) {
    const auto found = image.header.metadata.find(field);
    if (found == image.header.metadata.end() || found->second.empty())
        return source_error(Status::corrupt, "Calibration image requires nonempty metadata: " + field);
    return found->second;
}
Result<std::optional<TargetObservation>> detect_image(
    const CalibrationTarget &target, const data::Packet &image, const data::ImageLayout &layout,
    const ObservationSource &source, std::vector<std::byte> &scratch, const CancellationToken &token) {
    // image_layout owns format/shape validation. Select its validated byte attribute;
    // the converter maps it once for all rows, without a uint16 intermediate.
    const auto name = layout.packing == data::ImagePacking::raw8 ? "org.mantis.pixels" : data::packed_image_bytes;
    const auto attribute = std::find_if(image.attributes.begin(), image.attributes.end(),
        [&](const auto &value) { return value.descriptor.name == name; });
    if (attribute == image.attributes.end()) return source_error(Status::corrupt, "Validated image byte attribute is missing");
    auto mapped = attribute->buffer.map_read(token);
    if (!mapped) return std::unexpected(mapped.error());
    if (layout.packing == data::ImagePacking::raw8) {
        return opencv::detect_target(target,
            {{reinterpret_cast<const uint8_t *>(mapped->data()), mapped->size()}, layout.width, layout.height, layout.row_stride}, source);
    }
    if (size_t(layout.height) > std::numeric_limits<size_t>::max() / layout.width)
        return source_error(Status::corrupt, "Grayscale scratch byte extent overflows size_t");
    scratch.resize(size_t(layout.width) * layout.height);
    const data::Y10PView packed(*mapped, layout.width, layout.height, layout.row_stride);
    for (uint32_t y = 0; y < layout.height; ++y)
        packed.display_row(y, std::span(scratch).subspan(size_t(y) * layout.width, layout.width));
    return opencv::detect_target(target,
        {{reinterpret_cast<const uint8_t *>(scratch.data()), scratch.size()}, layout.width, layout.height, layout.width}, source);
}
} // namespace
Result<CalibrationDataset> build_calibration_dataset(
    std::shared_ptr<const artifact::Store> store, std::vector<Id> raw_capture_ids,
    CalibrationTarget target, DatasetAnalysisConfig config, const CancellationToken &token) {
    if (!store) return detail::target_error("Calibration dataset builder requires a store");
    auto valid_target = validate_target(target);
    if (!valid_target) return std::unexpected(valid_target.error());
    if (target.identity.id.value.empty()) return detail::target_error("Dataset requires target identity");
    auto canonical_config = canonical_analysis_config(std::move(config));
    if (!canonical_config) return std::unexpected(canonical_config.error());
    auto sources = canonical_raw_capture_ids(std::move(raw_capture_ids));
    if (!sources) return std::unexpected(sources.error());
    CalibrationDataset dataset{std::move(target), std::move(*canonical_config), std::move(*sources), {}, {}};
    try {
        std::map<std::string, DatasetCamera> cameras;
        std::vector<std::byte> scratch; // One reusable tight u8 frame, shared across sequential images/captures.
        for (const auto &capture_id : dataset.raw_capture_ids) {
            token.check();
            const auto descriptor = store->get(capture_id);
            if (descriptor.type.name != "org.mantis.RawCapture" || descriptor.type.schema_version != 2 ||
                descriptor.state != artifact::ArtifactState::finalized)
                return source_error(Status::incompatible, "Calibration ingestion requires FINALIZED org.mantis.RawCapture schema 2: " + capture_id.value);
            artifact::CaptureReader reader(store, capture_id);
            std::set<uint64_t> sequences;
            while (auto packet = reader.next()) {
                token.check();
                if (packet->type != schema::frameset || packet->frames.empty() || !packet->attributes.empty())
                    return source_error(Status::corrupt, "Calibration RawCapture record must be a FrameSet");
                const uint64_t sequence = packet->header.sequence.value; // Parent identity, never child sequence or dataset position.
                if (!sequences.insert(sequence).second)
                    return source_error(Status::corrupt, "Duplicate parent FrameSet sequence in RawCapture: " + capture_id.value);
                std::map<std::string, const data::Packet *> requested;
                for (const auto &image : packet->frames) {
                    if (!image || image->type != schema::image || !image->frames.empty())
                        return source_error(Status::corrupt, "Calibration FrameSet child must be an ImageFrame");
                    auto role = metadata(*image, "role");
                    if (!role) return std::unexpected(role.error());
                    if (!std::binary_search(dataset.config.camera_roles.begin(), dataset.config.camera_roles.end(), *role)) continue;
                    if (!requested.emplace(std::string(*role), image.get()).second)
                        return source_error(Status::corrupt, "Duplicate requested camera role in FrameSet: " + std::string(*role));
                }
                for (const auto &role : dataset.config.camera_roles) {
                    token.check();
                    const auto found = requested.find(role);
                    if (found == requested.end()) return source_error(Status::corrupt, "Missing requested camera role in FrameSet: " + role);
                    const auto &image = *found->second;
                    auto identity = metadata(image, "identity");
                    if (!identity) return std::unexpected(identity.error());
                    const auto layout = data::image_layout(image);
                    DatasetCamera camera{role, {std::string(*identity)}, layout.width, layout.height, image.header.frame};
                    if (camera.optical_frame.id.value.empty() || camera.optical_frame.name.empty())
                        return source_error(Status::corrupt, "Calibration image requires optical frame identity and convention representation");
                    const auto [existing, inserted] = cameras.emplace(role, camera);
                    if (!inserted) {
                        const auto &previous = existing->second;
                        if (previous.camera_id != camera.camera_id || previous.image_width != camera.image_width ||
                            previous.image_height != camera.image_height || previous.optical_frame.id != camera.optical_frame.id ||
                            previous.optical_frame.name != camera.optical_frame.name)
                            return source_error(Status::incompatible, "Calibration camera identity, image geometry or optical frame changed for role: " + role);
                    }
                    DatasetObservationRecord record;
                    record.key = {{capture_id, sequence}, role, camera.camera_id};
                    auto detected = detect_image(dataset.target, image, layout,
                        {capture_id, sequence, camera.camera_id, role}, scratch, token);
                    if (!detected) return std::unexpected(detected.error());
                    if (*detected) {
                        record.outcome = DetectionOutcome::detected;
                        record.observation = std::move(**detected);
                        auto diversity = describe_diversity(dataset.target, *record.observation);
                        if (!diversity) return std::unexpected(diversity.error());
                        record.diversity = *diversity;
                    }
                    dataset.records.push_back(std::move(record));
                }
            }
        }
        if (cameras.size() != dataset.config.camera_roles.size())
            return source_error(Status::corrupt, "RawCapture sources contain no images for requested cameras");
        for (auto &[role, camera] : cameras) { (void)role; dataset.cameras.push_back(std::move(camera)); }
        std::sort(dataset.records.begin(), dataset.records.end(), [](const auto &a, const auto &b) { return a.key < b.key; });
        auto selected = select_dataset_samples(dataset);
        if (!selected) return std::unexpected(selected.error());
        return dataset;
    } catch (const Failure &failure) {
        auto error = failure.error;
        if (error.component.empty()) error.component = "calibration";
        return std::unexpected(std::move(error));
    } catch (const std::filesystem::filesystem_error &failure) {
        return source_error(Status::io, "Calibration source IO failed: " + std::string(failure.what()));
    } catch (const std::bad_alloc &) {
        return source_error(Status::io, "Calibration dataset memory allocation failed");
    }
}
} // namespace mantis::calibration
