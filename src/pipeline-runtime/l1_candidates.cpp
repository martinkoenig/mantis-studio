#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mantis/image_layout.hpp>
#include <mantis/l1_candidates.hpp>
#ifndef MANTIS_L1_SOURCE_HASH
#error "Build L1 through its registered CMake target to retain source provenance"
#endif

namespace mantis::pipeline {
namespace {
using namespace data;
void require(bool condition, std::string_view message) {
    if (!condition)
        fail(Status::invalid_argument, std::string(message), "l1-candidates");
}
void check_id(std::string_view value) {
    require(!value.empty() && value.size() <= max_semantic_id, "Missing/oversized L1 identity");
}
void raw_reference(const ContentReference &r) {
    check_id(r.id.value);
    require(r.type == schema::DataTypeId{"org.mantis.RawCapture", 3} && r.hash.get() &&
                !r.hash.get()->algorithm.empty() && !r.hash.get()->hex.empty() &&
                r.hash.get()->algorithm.size() <= max_semantic_string &&
                r.hash.get()->hex.size() <= max_semantic_string,
            "L1 requires an exact immutable RawCapture-3 reference");
}
uint64_t bounded_backing(const AcquisitionBundle &b) {
    uint64_t bytes = 0;
    if (!b.frameset)
        return bytes;
    auto metadata_bounds = [](const Packet &p) {
        require(p.header.metadata.size() <= 256 &&
                    p.header.timestamp.domain.id.value.size() <= max_semantic_id &&
                    p.header.timestamp.domain.name.size() <= max_semantic_string &&
                    p.header.sync.id.value.size() <= max_semantic_id &&
                    p.header.calibration.id.value.size() <= max_semantic_id &&
                    p.header.frame.id.value.size() <= max_semantic_id &&
                    p.header.frame.name.size() <= max_semantic_string,
                "L1 image header exceeds finite bounds");
        for (const auto &[key, value] : p.header.metadata)
            require(key.size() <= max_semantic_id && value.size() <= max_semantic_string,
                    "Oversized L1 image metadata entry");
    };
    require(b.frameset->frames.size() <= 16 && b.frameset->attributes.empty(), "Invalid L1 FrameSet bounds");
    metadata_bounds(*b.frameset);
    for (const auto &image : b.frameset->frames) {
        require(image && image->frames.empty() && image->attributes.size() <= max_semantic_attributes &&
                    image->header.metadata.size() <= 256,
                "L1 image structure exceeds finite bounds");
        metadata_bounds(*image);
        for (const auto &a : image->attributes) {
            require(a.descriptor.name.size() <= max_semantic_string &&
                        a.descriptor.unit.size() <= max_semantic_string,
                    "Oversized L1 image attribute descriptor");
            // Conservative charge of aliased attributes avoids a hot-path identity set.
            require(a.buffer.backing_size() <= max_semantic_payload - bytes,
                    "L1 input backing budget exceeded");
            bytes += a.buffer.backing_size();
        }
    }
    return bytes;
}
void configure(const L1CandidateConfig &c) {
    for (const auto &s :
         {c.run.id.value, c.camera.id.value, c.emitter.id.value, c.stream.id.id.value,
          c.stream.generation.id.value, c.camera_role, c.pattern.id.value, c.line.id.value,
          c.producer_stream.id.value, c.producer_generation.id.value, c.optical_frame.id.value})
        check_id(s);
    require(c.camera != c.emitter, "L1 camera and emitter identities must differ");
    require(c.optical_frame.name.size() <= max_semantic_string, "Oversized optical frame name");
    require(c.origin >= ObservationOrigin::real && c.origin <= ObservationOrigin::imported,
            "Invalid L1 origin");
    raw_reference(c.raw_input);
    require(c.minimum_signal > 0 && c.minimum_signal <= 1023 && c.background_level <= 1023 &&
                c.saturation_level > 0 && c.saturation_level <= 1023 && c.ambiguity_margin <= 1023,
            "Invalid L1 intensity thresholds");
    require(c.minimum_width > 0 && c.minimum_width <= c.maximum_width && c.maximum_width <= 8192 &&
                c.maximum_dimension > 0 && c.maximum_dimension <= 8192 && c.maximum_pixels > 0 &&
                c.maximum_pixels <= 32ull * 1024 * 1024,
            "Invalid L1 resource/profile bounds");
    if (c.roi)
        require(c.roi->width && c.roi->height, "Empty L1 ROI");
    if (c.subpixel)
        require(c.subpixel->minimum_neighbor_signal > 0 &&
                    c.subpixel->minimum_neighbor_signal <= c.minimum_signal &&
                    c.subpixel->minimum_curvature > 0 && c.subpixel->minimum_curvature <= 2046,
                "Invalid L1 subpixel conditioning thresholds");
    if (c.dark) {
        require(bool(c.dark->bundle), "Missing dark bundle");
        raw_reference(c.dark->raw_input);
        require(c.dark->bundle->key.run_id == c.run, "Dark reference belongs to another run");
        (void)bounded_backing(*c.dark->bundle);
        auto valid = data::validate(*c.dark->bundle);
        if (!valid)
            throw Failure(valid.error());
    }
}
// Stable little-endian, length-prefixed configuration identity. No native layout,
// locale, addresses, mutable environment or wall clock enters algorithm provenance.
ContentReference parameters(const L1CandidateConfig &c) {
    std::string bytes;
    auto number = [&](uint64_t n) {
        for (unsigned i = 0; i < 8; ++i) {
            bytes.push_back(static_cast<char>(n & 255));
            n >>= 8;
        }
    };
    auto text = [&](std::string_view s) {
        number(s.size());
        bytes.append(s);
    };
    auto reference = [&](const ContentReference &r) {
        text(r.id.value);
        text(r.type.name);
        number(r.type.version);
        number(r.revision);
        text(r.hash.get()->algorithm);
        text(r.hash.get()->hex);
    };
    text(c.subpixel ? "org.mantis.l1-subpixel/1.0.0" : "org.mantis.l1-candidates/1.0.0");
    for (const auto &s : {c.run.id.value, c.camera.id.value, c.emitter.id.value, c.stream.id.id.value,
                          c.stream.generation.id.value, c.camera_role, c.pattern.id.value, c.line.id.value,
                          c.producer_stream.id.value, c.producer_generation.id.value,
                          c.optical_frame.id.value, c.optical_frame.name})
        text(s);
    number(c.pattern_revision);
    number(static_cast<uint64_t>(*c.origin));
    reference(c.raw_input);
    number(c.roi.has_value());
    if (c.roi) {
        number(c.roi->x);
        number(c.roi->y);
        number(c.roi->width);
        number(c.roi->height);
    }
    for (auto n : {c.minimum_signal, c.background_level, c.ambiguity_margin, c.saturation_level})
        number(n);
    number(c.minimum_width);
    number(c.maximum_width);
    number(c.maximum_dimension);
    number(c.maximum_pixels);
    number(c.dark.has_value());
    if (c.dark) {
        reference(c.dark->raw_input);
        text(c.dark->bundle->key.run_id.id.value);
        number(c.dark->bundle->key.sequence.value);
    }
    // Do not append even an absence marker to the frozen ALG-01 parameter bytes.
    if (c.subpixel) {
        number(c.subpixel->minimum_neighbor_signal);
        number(c.subpixel->minimum_curvature);
    }
    auto hash = content_hash(std::as_bytes(std::span(bytes)));
    // Opaque algorithm configuration reference, not a new canonical packet schema.
    return {{"l1-parameters-" + hash.hex}, {"org.mantis.L1CandidateParameters", 1}, hash, 1};
}
struct Selected {
    const CameraFrameEvidence &source;
    const Packet &image;
    ImageLayout layout;
};
Selected select(const AcquisitionBundle &b, const L1CandidateConfig &c, const CancellationToken &token) {
    token.check();
    (void)bounded_backing(b);
    auto valid = validate(b);
    if (!valid)
        throw Failure(valid.error());
    require(b.key.run_id == c.run && b.evidence.disposition == AcquisitionDisposition::captured && b.frameset,
            "L1 requires captured images from the configured run");
    size_t index = b.evidence.frames.size();
    for (size_t i = 0; i < b.evidence.frames.size(); ++i) {
        const auto &f = b.evidence.frames[i];
        if (f.frame.camera == c.camera && f.frame.stream == c.stream) {
            require(index == b.evidence.frames.size(), "Multiple matching L1 source frames");
            index = i;
        }
    }
    require(index < b.evidence.frames.size(), "Configured L1 camera/stream/generation is absent");
    const auto &f = b.evidence.frames[index];
    const auto &p = *b.frameset->frames[index];
    require(f.camera_role == c.camera_role, "L1 camera role mismatch");
    auto metadata = [&](std::string_view key, std::string_view expected, bool required = false) {
        auto i = p.header.metadata.find(std::string(key));
        require(i == p.header.metadata.end() ? !required : i->second == expected,
                "L1 image metadata contradicts typed source identity");
    };
    metadata("role", c.camera_role, true);
    metadata("component_id", c.camera.id.value);
    metadata("stream_id", c.stream.id.id.value);
    const auto *stamp = f.source_timestamp.get();
    require(!stamp || (stamp->nanoseconds == p.header.timestamp.nanoseconds &&
                       stamp->clock.domain.id == p.header.timestamp.domain.id &&
                       stamp->clock.domain.name == p.header.timestamp.domain.name),
            "L1 source timestamp mismatch");
    const auto *received = f.host_received.get();
    require(!received || received->time.nanoseconds == p.header.received.nanoseconds,
            "L1 host receive timestamp mismatch");
    if (auto sync = f.sync.get())
        require(sync->group.id == p.header.sync.id && sync->group.trigger == p.header.sync.trigger &&
                    sync->quality == p.header.sync_quality,
                "L1 sync evidence mismatch");
    const auto *cal = f.rig_calibration.get();
    if (!cal)
        cal = f.camera_calibration.get();
    if (cal)
        require(cal->calibration.id == p.header.calibration.id &&
                    cal->calibration.schema_version == p.header.calibration.schema_version &&
                    cal->calibration.revision == p.header.calibration.revision,
                "L1 calibration mismatch");
    require(f.width <= c.maximum_dimension && f.height <= c.maximum_dimension &&
                uint64_t(f.width) * f.height <= c.maximum_pixels,
            "L1 source exceeds finite image budget");
    auto layout = image_layout(p, token);
    require(layout.width == f.width && layout.height == f.height, "L1 pixel/evidence dimensions mismatch");
    const auto maximum = layout.packing == ImagePacking::raw8 ? 255 : 1023;
    require(c.minimum_signal <= maximum && c.background_level <= maximum && c.saturation_level <= maximum &&
                c.ambiguity_margin <= maximum,
            "L1 thresholds exceed source sample range");
    if (layout.packing == ImagePacking::raw8) {
        auto fourcc = p.header.metadata.find("fourcc");
        require(fourcc == p.header.metadata.end() || fourcc->second == "GREY" || fourcc->second == "RAW8",
                "L1 refuses noncanonical RAW8 packing");
    }
    // Canonical layouts describe the full source. Cropped input requires a future
    // explicit preprocessing contract; ROI here is only a scan window.
    require(p.header.frame.id == c.optical_frame.id && p.header.frame.name == c.optical_frame.name,
            "L1 optical coordinate frame mismatch");
    return {f, p, layout};
}
const ExposureEffectiveState *effective(const EmitterEvidence &e, const SourceFrameKey &frame) {
    for (const auto &f : e.exposure_effective)
        if (f.frame == frame)
            return f.state.get();
    return nullptr;
}
bool attribution(const AcquisitionBundle &b, const SourceFrameKey &frame, const ComponentId &emitter) {
    bool found = false, established = true;
    for (const auto &e : b.evidence.emitters) {
        const auto *s = effective(e, frame);
        const bool optical = s && s->scope == EvidenceScope::optical_emission;
        if (e.emitter == emitter) {
            found = true;
            require(!optical || s->state == EmitterState::on, "L1 target is optically OFF for this exposure");
            established &= optical;
        } else {
            require(!optical || s->state == EmitterState::off, "Another emitter is optically ON in L1 input");
            established &= optical;
        }
    }
    require(found, "Declared L1 emitter is absent from acquisition evidence");
    return established;
}
struct Pixels {
    ImageLayout layout;
    std::span<const std::byte> bytes;
    std::optional<Y10PView> packed;
    Pixels(const Selected &s, const CancellationToken &token) : layout(s.layout) {
        auto name = layout.packing == ImagePacking::raw8 ? "org.mantis.pixels" : packed_image_bytes;
        auto a = std::find_if(s.image.attributes.begin(), s.image.attributes.end(),
                              [&](const auto &v) { return v.descriptor.name == name; });
        auto mapped = a->buffer.map_read(token);
        if (!mapped)
            throw Failure(mapped.error());
        bytes = *mapped;
        if (layout.packing == ImagePacking::mipi_raw10)
            packed.emplace(bytes, layout.width, layout.height, layout.row_stride);
    }
    uint16_t sample(uint32_t x, uint32_t y) const {
        return packed ? packed->sample(x, y)
                      : std::to_integer<uint8_t>(bytes[size_t(y) * layout.row_stride + x]);
    }
};
bool same_calibration(const Evidence<ExactCalibrationReference> &a,
                      const Evidence<ExactCalibrationReference> &b) {
    if (a.presence() != b.presence())
        return false;
    if (!a.get())
        return true;
    const auto &x = *a.get();
    const auto &y = *b.get();
    if (x.calibration.id != y.calibration.id ||
        x.calibration.schema_version != y.calibration.schema_version ||
        x.calibration.revision != y.calibration.revision || x.content.presence() != y.content.presence())
        return false;
    if (!x.content.get())
        return true;
    const auto &u = *x.content.get();
    const auto &v = *y.content.get();
    return u.id == v.id && u.type == v.type && u.revision == v.revision && u.hash == v.hash;
}
struct Row {
    float x{}, y{};
    uint32_t flags{}, reason{}, peak{}, width{}, index{};
    uint8_t valid{};
    std::array<uint8_t, 3> reserved{}; // No uninitialized record padding in immutable column slices.
};
static_assert(sizeof(Row) == 32 && offsetof(Row, reserved) == 29);
// ALG-02 keeps the ALG-01 record extent and default bytes unchanged. In refined
// mode reserved[0] carries refinement validity; curvature has its own strided
// bulk buffer below, so no unaligned integer is stored in byte padding.
class Extractor final : public SemanticNodeInstance {
    const L1CandidateConfig config;
    const ContentReference parameter_reference;

  public:
    explicit Extractor(L1CandidateConfig c) : config(std::move(c)), parameter_reference(parameters(config)) {}
    Result<SemanticPublished> process(std::span<const SemanticPublished> inputs,
                                      const CancellationToken &token) override {
        try {
            token.check();
            require(inputs.size() == 1 && inputs[0], "L1 requires exactly one AcquisitionBundle");
            const auto *bundle = std::get_if<AcquisitionBundle>(inputs[0].get());
            require(bundle != nullptr, "L1 requires typed AcquisitionBundle input");
            const auto selected = select(*bundle, config, token);
            const bool known = attribution(*bundle, selected.source.frame, config.emitter);
            Pixels pixels(selected, token);
            auto roi = config.roi.value_or(
                L1CandidateConfig::Roi{0, 0, selected.layout.width, selected.layout.height});
            require(roi.x < selected.layout.width && roi.y < selected.layout.height &&
                        roi.width <= selected.layout.width - roi.x &&
                        roi.height <= selected.layout.height - roi.y,
                    "L1 ROI lies outside source image");
            std::optional<Pixels> dark;
            if (config.dark) {
                auto ds = select(*config.dark->bundle, config, token);
                require(ds.layout.width == selected.layout.width &&
                            ds.layout.height == selected.layout.height &&
                            ds.layout.packing == selected.layout.packing &&
                            ds.source.frame != selected.source.frame,
                        "Dark reference must be a distinct matching source image");
                require(config.dark->bundle->evidence.participants.emitters ==
                            bundle->evidence.participants.emitters,
                        "Dark reference emitter participants mismatch");
                for (const auto &e : config.dark->bundle->evidence.emitters) {
                    auto s = effective(e, ds.source.frame);
                    require(s && s->scope == EvidenceScope::optical_emission && s->state == EmitterState::off,
                            "Dark subtraction requires exposure-effective optical OFF evidence");
                }
                const auto *exposure = selected.source.exposure.get();
                const auto *de = ds.source.exposure.get();
                require(
                    exposure && de && exposure->integration_duration.get() &&
                        de->integration_duration.get() &&
                        *exposure->integration_duration.get() == *de->integration_duration.get() &&
                        selected.image.header.metadata == ds.image.header.metadata &&
                        same_calibration(selected.source.camera_calibration, ds.source.camera_calibration) &&
                        same_calibration(selected.source.rig_calibration, ds.source.rig_calibration) &&
                        same_calibration(selected.source.original_calibration,
                                         ds.source.original_calibration),
                    "Dark reference exposure/settings/calibration mismatch");
                dark.emplace(ds, token);
            }
            LaserObservation out;
            out.key = {
                config.run, config.producer_stream, config.producer_generation, {bundle->key.sequence.value}};
            auto &c = out.context;
            c.source = selected.source;
            c.frameset = bundle->evidence.frameset;
            c.bundle = bundle->key;
            c.raw_input = config.raw_input;
            c.optical_frame = config.optical_frame;
            c.preprocessing = Unavailable{};
            c.requested_emitters = bundle->evidence.participants.emitters;
            c.emitter_evidence = bundle->evidence.emitters;
            for (auto &e : c.emitter_evidence)
                std::erase_if(e.exposure_effective,
                              [&](const auto &f) { return f.frame != selected.source.frame; });
            c.emitter_patterns = {{config.emitter, config.pattern, config.pattern_revision}};
            if (auto step = bundle->evidence.step.get())
                c.correlation = ProgramCorrelation{bundle->evidence.program, *step};
            else if (bundle->evidence.step.presence() == Presence::unavailable)
                c.correlation = Unavailable{};
            else
                c.correlation = Unknown{};
            c.acquisition_evidence = bundle->evidence.key;
            c.triggers = bundle->evidence.triggers;
            c.clock_mappings = bundle->evidence.clock_mappings;
            c.producer = {{config.subpixel ? "org.mantis.l1-subpixel" : "org.mantis.l1-candidates"},
                          {1, 0, 0},
                          std::string(build_version) + ";l1-source-sha256:" + MANTIS_L1_SOURCE_HASH,
                          parameter_reference};
            c.parameters = parameter_reference;
            c.exact_inputs = {config.raw_input};
            if (config.dark) {
                if (config.dark->raw_input.id != config.raw_input.id)
                    c.exact_inputs.push_back(config.dark->raw_input);
                else
                    require(config.dark->raw_input.hash.get() &&
                                *config.dark->raw_input.hash.get() == *config.raw_input.hash.get() &&
                                config.dark->raw_input.revision == config.raw_input.revision,
                            "Conflicting exact input references");
            }
            c.origin = config.origin;
            c.producer_completed = Unavailable{}; // No invented deterministic completion timestamp.
            out.disposition = ObservationDisposition::success;
            out.confidence_interpretation = Unavailable{};
            out.emitter_dictionary = {config.emitter};
            out.line_dictionary = {{config.emitter, config.pattern, config.pattern_revision, config.line}};
            if (!known)
                c.packet_quality_flags |= l1::attribution_unresolved;
            // <=8192 rows; main columns share one allocation. Optional curvature
            // uses a second bounded bulk allocation, never one per sample.
            memory::BufferBuilder buffer(size_t(roi.width) * sizeof(Row));
            auto writable = buffer.writable();
            std::optional<memory::BufferBuilder> curvature_buffer;
            if (config.subpixel)
                curvature_buffer.emplace(size_t(roi.width) * sizeof(uint32_t));
            uint32_t missing = 0, rejected = 0, saturated_columns = 0;
            for (uint32_t x = roi.x; x < roi.x + roi.width; ++x) {
                token.check();
                struct Peak {
                    uint16_t signal{}, raw{};
                    uint32_t y{}, width{}, start{}, maxima{}, previous{};
                    bool rising{true};
                    bool saturated{};
                } best, second, run;
                bool clipped = false;
                auto finish = [&] {
                    if (!run.width)
                        return;
                    if (config.subpixel && run.rising)
                        ++run.maxima;
                    if (run.signal > best.signal) {
                        second = best;
                        best = run;
                    } else if (run.signal > second.signal)
                        second = run;
                    run = {};
                };
                for (uint32_t y = roi.y; y < roi.y + roi.height; ++y) {
                    if ((y - roi.y) % 64 == 0)
                        token.check();
                    auto value = pixels.sample(x, y);
                    const uint16_t dark_value = dark ? dark->sample(x, y) : 0;
                    clipped |= value >= config.saturation_level || dark_value >= config.saturation_level;
                    const uint32_t background = uint32_t(config.background_level) + dark_value;
                    const auto signal = static_cast<uint16_t>(value > background ? value - background : 0);
                    if (signal < config.minimum_signal) {
                        finish();
                        continue;
                    }
                    if (config.subpixel) {
                        if (!run.width)
                            run.start = y;
                        if (signal < run.previous && run.rising) {
                            ++run.maxima;
                            run.rising = false;
                        } else if (signal > run.previous)
                            run.rising = true;
                        run.previous = signal;
                    }
                    ++run.width;
                    run.saturated |= value >= config.saturation_level ||
                                     (config.subpixel && dark_value >= config.saturation_level);
                    if (signal > run.signal) {
                        run.signal = signal;
                        run.raw = value;
                        run.y = y;
                    }
                }
                finish();
                if (clipped)
                    ++saturated_columns;
                if (!best.width) {
                    ++missing;
                    continue;
                }
                Row row{};
                row.x = static_cast<float>(x);
                row.y = static_cast<float>(best.y);
                row.peak = best.raw;
                row.width = best.width;
                row.valid = known ? 1 : 0;
                if (!known)
                    row.flags |= laser::emitter_unknown | laser::line_unknown;
                if (best.saturated)
                    row.reason |= l1::saturated;
                if (second.width && best.signal - second.signal <= config.ambiguity_margin) {
                    row.reason |= l1::competing_peaks;
                    row.flags |= laser::ambiguous;
                }
                if (best.width < config.minimum_width || best.width > config.maximum_width)
                    row.reason |= l1::width_rejected;
                uint32_t curvature = 0;
                if (config.subpixel) {
                    if (best.start == roi.y || best.width == roi.y + roi.height - best.start)
                        row.reason |= l1::edge_profile;
                    if (best.maxima > 1) {
                        row.reason |= l1::multiple_local_peaks;
                        row.flags |= laser::ambiguous;
                    }
                    // Interpolation is confined to this ROI and original column.
                    if (best.y == roi.y || best.y == roi.y + roi.height - 1)
                        row.reason |= l1::insufficient_neighbors;
                    else {
                        auto corrected = [&](uint32_t y) {
                            const uint32_t value = pixels.sample(x, y);
                            const uint32_t d = dark ? dark->sample(x, y) : 0;
                            if (value >= config.saturation_level || d >= config.saturation_level)
                                row.reason |= l1::saturated;
                            const auto background = uint32_t(config.background_level) + d;
                            return int32_t(value > background ? value - background : 0);
                        };
                        const int32_t left = corrected(best.y - 1), center = best.signal,
                                      right = corrected(best.y + 1);
                        const int32_t denominator = 2 * center - left - right;
                        if (left == center || right == center)
                            row.reason |= l1::flat_peak;
                        if (left < config.subpixel->minimum_neighbor_signal ||
                            right < config.subpixel->minimum_neighbor_signal)
                            row.reason |= l1::weak_neighbors;
                        if (denominator < config.subpixel->minimum_curvature)
                            row.reason |= l1::weak_curvature;
                        if (denominator > 0)
                            curvature = static_cast<uint32_t>(denominator);
                        if (!row.reason) {
                            // Samples <=1023: signed arithmetic above cannot overflow.
                            // Convert only after conditioning, never divide by zero.
                            const double offset = double(right - left) / (2.0 * denominator);
                            const double y = double(best.y) + offset;
                            if (!std::isfinite(offset) || !std::isfinite(y) || left >= center ||
                                right >= center || std::abs(offset) > 0.5 || y < roi.y ||
                                y > roi.y + roi.height - 1)
                                row.reason |= l1::numeric_rejected;
                            else {
                                row.y = static_cast<float>(y);
                                row.reserved[0] = 1;
                            }
                        }
                    }
                    auto bytes = curvature_buffer->writable();
                    std::memcpy(bytes.data() + size_t(out.sample_count) * sizeof(uint32_t), &curvature,
                                sizeof(curvature));
                }
                if (row.reason) {
                    row.flags |= laser::producer_rejected;
                    ++rejected;
                }
                std::memcpy(writable.data() + size_t(out.sample_count++) * sizeof(Row), &row, sizeof(Row));
            }
            if (missing)
                c.packet_quality_flags |= l1::missing_profiles;
            if (rejected)
                c.packet_quality_flags |= l1::rejected_profiles;
            if (saturated_columns)
                c.packet_quality_flags |= l1::saturation_observed;
            out.diagnostic = "profiles=" + std::to_string(roi.width) + ";missing=" + std::to_string(missing) +
                             ";rejected=" + std::to_string(rejected) +
                             ";saturated=" + std::to_string(saturated_columns);
            if (out.sample_count) {
                out.attributes.reserve(config.subpixel ? 11 : 9);
                auto backing = std::move(buffer).publish();
                auto column = [&](std::string_view name, schema::ScalarType type, size_t offset,
                                  std::string unit = "") {
                    const auto extent =
                        size_t(out.sample_count - 1) * sizeof(Row) + schema::scalar_size(type);
                    out.attributes.push_back(
                        {{std::string(name), type, {out.sample_count}, {sizeof(Row)}, std::move(unit)},
                         backing.slice(offset, extent)});
                };
                out.attributes.push_back(
                    {{std::string(laser::source_pixel),
                      schema::ScalarType::f32,
                      {out.sample_count, 2},
                      {sizeof(Row), sizeof(float)},
                      "pixel"},
                     backing.slice(0, size_t(out.sample_count - 1) * sizeof(Row) + 2 * sizeof(float))});
                column(laser::quality_flags, schema::ScalarType::u32, offsetof(Row, flags));
                column(l1::reason, schema::ScalarType::u32, offsetof(Row, reason));
                column(l1::peak, schema::ScalarType::u32, offsetof(Row, peak), "DN");
                column(l1::width, schema::ScalarType::u32, offsetof(Row, width), "pixel");
                for (auto name : {laser::emitter_index, laser::line_index})
                    column(name, schema::ScalarType::u32, offsetof(Row, index));
                for (auto name : {laser::emitter_valid, laser::line_valid})
                    column(name, schema::ScalarType::u8, offsetof(Row, valid));
                if (config.subpixel) {
                    column(l1::refinement_valid, schema::ScalarType::u8, offsetof(Row, reserved));
                    out.attributes.push_back({{std::string(l1::curvature),
                                               schema::ScalarType::u32,
                                               {out.sample_count},
                                               {sizeof(uint32_t)},
                                               "DN/pixel^2"},
                                              std::move(*curvature_buffer)
                                                  .publish()
                                                  .slice(0, size_t(out.sample_count) * sizeof(uint32_t))});
                }
            }
            token.check();
            auto result = data::publish(SemanticPacket{std::move(out)});
            token.check();
            return result;
        } catch (const Failure &e) {
            return std::unexpected(e.error);
        } catch (const std::exception &e) {
            return std::unexpected(Error{Status::plugin_failed, e.what(), "l1-candidates"});
        }
    }
};
} // namespace
SemanticNode l1_candidate_extractor(L1CandidateConfig c) {
    configure(c);
    NodeDescriptor d;
    d.id = d.algorithm_id = c.subpixel ? "org.mantis.l1-subpixel" : "org.mantis.l1-candidates";
    d.version = {1, 0, 0};
    d.inputs = {{"acquisition", schema::acquisition_bundle}};
    d.outputs = {{"candidates", schema::laser_observation}};
    d.resources.backends = {"cpu"};
    d.resources.memory_bytes = uint64_t(c.maximum_dimension) * sizeof(Row);
    if (c.subpixel)
        d.resources.memory_bytes += uint64_t(c.maximum_dimension) * sizeof(uint32_t);
    if (c.dark)
        d.resources.memory_bytes += bounded_backing(*c.dark->bundle);
    d.resources.location = Location::local; // Host reference; ADR-031 deployment remains sensor-proximal.
    d.execution_class = ExecutionClass::offline;
    return {std::move(d), [c = std::move(c)] { return std::make_unique<Extractor>(c); }};
}
} // namespace mantis::pipeline
