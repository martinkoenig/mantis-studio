#include "laser_observation_values.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
using namespace observation_fixture;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__));                 \
    } while (false)
template <class F> void rejects(F f) {
    bool bad = false;
    try {
        f();
    } catch (const std::exception &) {
        bad = true;
    }
    CHECK(bad);
}
std::string file(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
void number(std::string &b, size_t at, uint64_t n) {
    CHECK(at + 8 <= b.size());
    for (unsigned i = 0; i < 8; ++i)
        b[at + i] = static_cast<char>(n >> (8 * i));
}
void checksum(std::string &b) {
    auto h = content_hash({reinterpret_cast<const std::byte *>(b.data() + 32), b.size() - 48});
    number(b, 24, std::stoull(h.hex, nullptr, 16));
}
void tests(const std::filesystem::path &root) {
    unsigned mode = 0;
    for (const char *name : {"known", "unknown", "empty", "failed", "extension", "unavailable"}) {
        auto expected = file(root / (std::string(name) + ".bin"));
        auto value = observation(mode++);
        CHECK(data::validate(value));
        CHECK(encode(value) == expected);
        auto storage = view(expected);
        auto decoded = read_laser_observation(storage);
        CHECK(encode(decoded) == expected);
        CHECK(decoded.key.sequence.value == value.key.sequence.value);
        CHECK(decoded.key.run_id == value.key.run_id);
        CHECK(decoded.context.source.frame == frame());
        CHECK(decoded.context.source.rig_calibration.get()->content.get()->id == Id{"rig-art"});
        CHECK(decoded.context.source.original_calibration.presence() == Presence::unavailable);
        CHECK(decoded.context.packet_quality_flags == 0x80000000u);
        for (const auto &a : decoded.attributes)
            CHECK(a.buffer.identity() == storage.identity());
        auto transport = semantic_bytes(SemanticPacket{value});
        CHECK(semantic_bytes(read_semantic_packet(view(transport))) == transport);
        if (value.sample_count == 0)
            CHECK(decoded.attributes.empty());
    }
    auto base = encode(observation());
    // Every truncated prefix, every single-byte corruption, and trailing data are refused.
    for (size_t n = 0; n < base.size(); ++n)
        rejects([&] { (void)read_laser_observation(view(std::string_view(base).substr(0, n))); });
    for (size_t n = 0; n < base.size(); ++n) {
        auto bad = base;
        bad[n] ^= 1;
        rejects([&] { (void)read_laser_observation(view(bad)); });
    }
    rejects([&] { (void)read_laser_observation(view(base + "x")); });
    auto edit = [&](size_t at, uint64_t value, bool fix = true) {
        auto b = base;
        number(b, at, value);
        if (fix)
            checksum(b);
        rejects([&] { (void)read_laser_observation(view(b)); });
    };
    edit(8, 2, false);
    edit(16, UINT64_MAX, false);
    edit(base.size() - 8, 0, false);
    const auto pixel = base.find(laser::source_pixel);
    CHECK(pixel != std::string::npos);
    {
        auto b = base;
        b[pixel + laser::source_pixel.size()] = char(99);
        checksum(b);
        rejects([&] { (void)read_laser_observation(view(b)); });
    }
    edit(pixel + laser::source_pixel.size() + 1, UINT64_MAX);
    edit(pixel + laser::source_pixel.size() + 9,
         UINT64_MAX); // dimension overflow/excessive sample buffer extent
    // First context run Presence immediately follows type and schema.
    const auto run_presence = 32 + 8 + schema::laser_observation.name.size() + 8;
    {
        auto b = base;
        b[run_presence] = char(9);
        checksum(b);
        rejects([&] { (void)read_laser_observation(view(b)); });
    }
    {
        auto b = base;
        number(b, 32 + 8 + schema::laser_observation.name.size(), 2);
        checksum(b);
        rejects([&] { (void)read_laser_observation(view(b)); });
    }
    auto encoded_storage = view(base);
    auto mapped = read_laser_observation(encoded_storage);
    auto raw = encoded_storage.map_read();
    CHECK(raw);
    auto mutate_column = [&](size_t index, auto scalar) {
        auto b = base;
        auto bytes = mapped.attributes[index].buffer.map_read();
        CHECK(bytes);
        auto at = static_cast<size_t>(bytes->data() - raw->data());
        std::memcpy(b.data() + at, &scalar, sizeof(scalar));
        checksum(b);
        rejects([&] { (void)read_laser_observation(view(b)); });
    };
    mutate_column(0, std::numeric_limits<float>::infinity());
    mutate_column(1, uint32_t{laser::emitter_unknown});
    mutate_column(2, uint32_t{999});
    mutate_column(3, uint32_t{999});
    mutate_column(4, uint8_t{2});
    mutate_column(5, uint8_t{2});
    mutate_column(6, float{2});
    mutate_column(7, uint8_t{0});
    edit(pixel - 16, UINT64_MAX); // excessive attribute table count
    auto negative = [&](const auto &edit_value) {
        auto o = observation();
        edit_value(o);
        CHECK(!data::validate(o));
        rejects([&] { (void)encode(o); });
    };
    negative([](auto &o) { o.sample_count = 1; });
    negative([](auto &o) { o.sample_count = max_observation_samples + 1; });
    negative([](auto &o) { o.disposition.reset(); });
    negative([](auto &o) { o.attributes.erase(o.attributes.begin()); });
    negative([](auto &o) { o.attributes.erase(o.attributes.begin() + 1); });
    negative([](auto &o) { o.attributes[0].descriptor.stride[0] = 0; });
    negative([](auto &o) { o.attributes[0].descriptor.shape[0] = UINT64_MAX; });
    negative([](auto &o) { o.attributes[0].buffer = o.attributes[0].buffer.slice(0, 1); });
    negative([](auto &o) {
        o.attributes[0] =
            column(std::string(laser::source_pixel), std::vector<float>{NAN, NAN}, schema::ScalarType::f32);
    });
    negative([](auto &o) {
        o.attributes[4] =
            column(std::string(laser::emitter_valid), std::vector<uint8_t>{2, 1}, schema::ScalarType::u8);
    });
    negative([](auto &o) {
        o.attributes[2] =
            column(std::string(laser::emitter_index), std::vector<uint32_t>{1, 0}, schema::ScalarType::u32);
    });
    negative([](auto &o) {
        o.attributes[3] =
            column(std::string(laser::line_index), std::vector<uint32_t>{1, 0}, schema::ScalarType::u32);
    });
    negative([](auto &o) { o.line_dictionary[0].emitter = {{"other"}}; });
    negative([](auto &o) {
        o.attributes[6] =
            column(std::string(laser::confidence), std::vector<float>{2, 0}, schema::ScalarType::f32);
    });
    negative([](auto &o) {
        o.attributes[7] =
            column(std::string(laser::confidence_valid), std::vector<uint8_t>{0, 1}, schema::ScalarType::u8);
    });
    negative([](auto &o) { o.confidence_interpretation = Unknown{}; });
    negative([](auto &o) { o.context.bundle = BundleKey{RunId{{"other"}}, {1}}; });
    negative([](auto &o) {
        auto e = *o.context.source.exposure.get();
        e.integration_duration = Duration{-1};
        o.context.source.exposure = e;
    });
    negative([](auto &o) {
        o.context.preprocessing = PreprocessingTransform{{0, 0, 0, 0, 0, 0, 0, 0, 0}, Unknown{}};
    });
    negative([](auto &o) { o.context.producer.build = std::string(1025, 'x'); });
    negative([](auto &o) { o.line_dictionary.resize(4097); });
    auto unknown = observation(1);
    CHECK(data::validate(unknown));
    CHECK(!unknown.confidence_interpretation.get());
    auto known_unknownline = observation();
    known_unknownline.attributes[5] =
        column(std::string(laser::line_valid), std::vector<uint8_t>{0, 0}, schema::ScalarType::u8);
    known_unknownline.attributes[1] =
        column(std::string(laser::quality_flags),
               std::vector<uint32_t>{laser::line_unknown, laser::line_unknown}, schema::ScalarType::u32);
    CHECK(data::validate(known_unknownline));
    auto unresolved = observation(1);
    unresolved.context.source.camera_calibration = Unknown{};
    unresolved.context.source.rig_calibration = Unavailable{};
    unresolved.context.preprocessing = Unknown{};
    unresolved.context.parameters = Unknown{};
    unresolved.context.raw_input = Unavailable{};
    for (auto &e : unresolved.context.emitter_evidence) {
        e.commanded = Unknown{};
        e.acknowledged = Unavailable{};
        e.observed = Unknown{};
        for (auto &f : e.exposure_effective)
            f.state = Unknown{};
    }
    CHECK(data::validate(unresolved));
    auto exact = read_laser_observation(view(encode(unresolved)));
    CHECK(encode(exact) == encode(unresolved));
    CHECK(exact.context.source.camera_calibration.presence() == Presence::unknown);
    CHECK(exact.context.source.rig_calibration.presence() == Presence::unavailable);
    auto imported = observation(2);
    imported.key.run_id = Unavailable{};
    imported.context.origin = ObservationOrigin::imported;
    imported.context.frameset = Unknown{};
    imported.context.bundle = Unavailable{};
    imported.context.correlation = Unknown{};
    imported.context.acquisition_evidence = Unknown{};
    imported.context.triggers.clear();
    imported.context.source.sync = Unavailable{};
    CHECK(data::validate(imported));
    CHECK(!read_laser_observation(view(encode(imported))).key.run_id.get());
    auto next = observation();
    next.key.sequence = {2};
    CHECK(validate_successor(observation(), next));
    CHECK(!validate_successor(next, observation()));
    CHECK(!validate_successor(next, next));
    next.key.producer_generation = {{"other"}};
    CHECK(!validate_successor(observation(), next));
    for (SemanticPacket packet : {SemanticPacket{images()}, SemanticPacket{bundle(0).evidence},
                                  SemanticPacket{bundle(2).triggers[0]}, SemanticPacket{bundle(1)}}) {
        auto b = semantic_bytes(packet);
        CHECK(semantic_bytes(read_semantic_packet(view(b))) == b);
        auto bad = b;
        number(bad, 32, 99);
        checksum(bad);
        rejects([&] { (void)read_semantic_packet(view(bad)); });
    }
    auto flat = *images()->frames[0];
    flat.type = schema::laser_observation;
    rejects([&] { (void)data::publish(SemanticPacket{data::publish(flat)}); });
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        tests(argv[1]);
        std::cout << "Observation golden/context/adversarial/semantic transport checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
