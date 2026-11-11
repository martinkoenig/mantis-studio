#pragma once
#include "../unit/projected_storage_values.hpp"
#include <cstring>
#include <functional>
#include <mantis/l1_candidates.hpp>
#include <mantis/laser_observation_io.hpp>

namespace l1_fixture {
using namespace storage_fixture;
inline const spatial::CoordinateFrame optical{{"camera-optical"}, "Camera optical"};
using Signal = std::function<uint16_t(uint32_t, uint32_t)>;
inline AcquisitionBundle image_bundle(bool packed, uint32_t width, uint32_t height, Signal signal,
                                      uint32_t padding = 3, bool dark = false) {
    auto b = bundle(1);
    auto &f = b.evidence.frames[0];
    f.width = width;
    f.height = height;
    f.sync = SyncEvidence{{{"sync"}, 2}, time::SyncQuality::software, Unavailable{}};
    if (dark) {
        b.key.sequence.value = 2;
        b.evidence.key.ordinal.value = 8;
        f.frame.native_sequence = 8;
        auto set = *b.evidence.frameset.get();
        set.sequence = 8;
        b.evidence.frameset = set;
        auto &e = b.evidence.emitters[0];
        e.commanded = EmitterCommand{{{"off-request"}}, emitter, EmitterState::off, host(0)};
        e.acknowledged = Unavailable{};
        e.observed = Unavailable{};
        e.exposure_effective = {
            {f.frame, ExposureEffectiveState{f.frame,
                                             EmitterState::off,
                                             EvidenceScope::optical_emission,
                                             proof(EvidenceMethod::optical_sensor, emitter),
                                             {sensor(0), sensor(10)}}}};
    }
    Packet image;
    image.type = schema::image;
    image.header.sequence.value = f.frame.native_sequence;
    image.header.timestamp = {0, {{"sensor"}, "device"}};
    image.header.received = {9};
    image.header.sync = {{"sync"}, 2};
    image.header.sync_quality = time::SyncQuality::software;
    image.header.calibration = f.rig_calibration.get()->calibration;
    image.header.frame = optical;
    image.header.metadata = {{"role", "left"},
                             {"component_id", "cam"},
                             {"stream_id", "stream"},
                             {"fourcc", packed ? "Y10P" : "GREY"}};
    const uint32_t row_bytes = packed ? width / 4 * 5 : width;
    const uint32_t stride = row_bytes + padding;
    memory::BufferBuilder storage(size_t(stride) * height);
    auto bytes = storage.writable();
    std::fill(bytes.begin(), bytes.end(), std::byte{0xe3});
    for (uint32_t y = 0; y < height; ++y) {
        if (packed) {
            for (uint32_t group = 0; group < width / 4; ++group) {
                uint8_t tails = 0;
                for (uint32_t i = 0; i < 4; ++i) {
                    const auto sample = signal(group * 4 + i, y);
                    bytes[size_t(y) * stride + group * 5 + i] = std::byte(sample >> 2);
                    tails |= static_cast<uint8_t>((sample & 3) << (2 * i));
                }
                bytes[size_t(y) * stride + group * 5 + 4] = std::byte(tails);
            }
        } else {
            for (uint32_t x = 0; x < width; ++x)
                bytes[size_t(y) * stride + x] = std::byte(signal(x, y));
        }
    }
    if (packed) {
        image.header.metadata.insert({{"org.mantis.image.layout", "mipi-raw10-v1"},
                                      {"org.mantis.image.bits_per_sample", "10"},
                                      {"org.mantis.image.width", std::to_string(width)},
                                      {"org.mantis.image.height", std::to_string(height)},
                                      {"org.mantis.image.row_stride_bytes", std::to_string(stride)}});
        image.attributes = {
            {{"org.mantis.image.packed_bytes", schema::ScalarType::u8, {bytes.size()}, {1}, "byte"},
             std::move(storage).publish()}};
    } else
        image.attributes = {
            {{"org.mantis.pixels", schema::ScalarType::u8, {height, width}, {stride, 1}, "DN"},
             std::move(storage).publish()}};
    Packet set;
    set.type = schema::frameset;
    set.header = image.header;
    set.header.sequence.value = b.evidence.frameset.get()->sequence;
    set.frames = {publish(std::move(image))};
    b.frameset = publish(std::move(set));
    return b;
}
inline pipeline::L1CandidateConfig config(bool packed = false) {
    pipeline::L1CandidateConfig c;
    c.run = run;
    c.camera = camera;
    c.emitter = emitter;
    c.stream = frame().stream;
    c.camera_role = "left";
    c.pattern = {{"l1-pattern"}};
    c.pattern_revision = 7;
    c.line = {{"single"}};
    c.producer_stream = {{"l1-producer"}};
    c.producer_generation = {{"l1-generation"}};
    c.raw_input = {{"raw-fixture"}, {"org.mantis.RawCapture", 3}, Hash{"fnv1a64", "aabb"}, 0};
    c.optical_frame = optical;
    c.origin = ObservationOrigin::synthetic;
    c.minimum_signal = packed ? 400 : 100;
    c.saturation_level = packed ? 1023 : 255;
    c.minimum_width = 2;
    c.maximum_width = 3;
    return c;
}
inline pipeline::L1CandidateConfig subpixel_config(bool packed = false) {
    auto c = config(packed);
    c.subpixel = pipeline::L1SubpixelConfig{};
    c.minimum_signal = packed ? 64 : 16;
    c.background_level = packed ? 48 : 12;
    c.minimum_width = 1;
    c.maximum_width = 16;
    c.producer_generation = {{"l1-subpixel-generation"}};
    return c;
}
// Independently specified exact quadratic: vertex (x, 4.25), curvature 32 DN
// per pixel squared. Integer samples [left,center,right]=[155,179,171].
inline AcquisitionBundle quadratic(bool packed = false, uint32_t width = 8, uint32_t height = 12) {
    return image_bundle(packed, width, height, [](uint32_t, uint32_t y) -> uint16_t {
        switch (y) {
        case 2:
            return 99;
        case 3:
            return 155;
        case 4:
            return 179;
        case 5:
            return 171;
        case 6:
            return 131;
        default:
            return 12;
        }
    });
}
inline AcquisitionBundle line(bool packed = false, uint32_t w = 8, uint32_t h = 8) {
    return image_bundle(packed, w, h, [=](uint32_t, uint32_t y) {
        return static_cast<uint16_t>(y == 2 ? (packed ? 701 : 181) : y == 3 ? (packed ? 700 : 180) : 10);
    });
}
template <class F> inline void edit_image(AcquisitionBundle &b, F edit) {
    auto set = *b.frameset;
    auto image = *set.frames[0];
    edit(image);
    // Negative fixtures deliberately bypass publication validation.
    set.frames[0] = std::make_shared<const Packet>(std::move(image));
    b.frameset = std::make_shared<const Packet>(std::move(set));
}
inline std::string encode(const LaserObservation &o) {
    std::ostringstream out;
    write_laser_observation(out, o);
    return out.str();
}
template <class T>
inline T value(const LaserObservation &o, std::string_view name, size_t row, size_t component = 0) {
    for (const auto &a : o.attributes)
        if (a.descriptor.name == name) {
            auto bytes = a.buffer.map_read();
            if (!bytes)
                throw Failure(bytes.error());
            T v;
            std::memcpy(&v, bytes->data() + row * a.descriptor.stride[0] + component * sizeof(T), sizeof(T));
            return v;
        }
    throw std::runtime_error("Missing test column: " + std::string(name));
}
} // namespace l1_fixture
