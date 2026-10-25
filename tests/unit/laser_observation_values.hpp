#pragma once
#include "projected_storage_values.hpp"
#include <cstring>
#include <mantis/laser_observation_io.hpp>
namespace observation_fixture {
using namespace storage_fixture;
template <class T>
inline data::Attribute column(std::string name, std::vector<T> values, schema::ScalarType scalar,
                              std::string unit = "") {
    auto n = values.size();
    auto bytes = std::as_bytes(std::span(values));
    return {{std::move(name), scalar, {n}, {sizeof(T)}, std::move(unit)}, memory::copy(bytes)};
}
inline data::LaserObservation observation(unsigned mode = 0, uint64_t samples = 2) {
    data::LaserObservation o;
    o.key = {run, {{"producer"}}, {{"producer-gen"}}, {1}};
    auto &c = o.context;
    c.source = camera_frame();
    c.frameset = frameset();
    c.bundle = bundle(1).key;
    c.raw_input = ContentReference{{"raw-art"}, {"org.mantis.RawCapture", 3}, Hash{"fnv1a64", "abcd"}, 0};
    c.optical_frame = {{"optical"}, "Optical"};
    PreprocessingTransform transform;
    transform.original_from_processed[2] = 0.25;
    transform.reference = ref("preprocess");
    c.preprocessing = transform;
    c.requested_emitters = {emitter};
    c.emitter_evidence = bundle(1).evidence.emitters;
    c.emitter_patterns = {{emitter, {{"pattern"}}, 7}};
    c.correlation = ProgramCorrelation{program_ref(), {run, 0, 9}};
    c.acquisition_evidence = bundle(1).evidence.key;
    c.triggers = {trigger(2)};
    c.clock_mappings = {mapping()};
    c.producer = {{"org.example.synthetic-observation"}, {1, 0, 0}, "fixture-build", ref("producer-config")};
    c.parameters = ref("parameters");
    c.exact_inputs = {ref("fixture-input")};
    c.origin = ObservationOrigin::synthetic;
    c.producer_completed = host(21);
    c.packet_quality_flags = 0x80000000u;
    o.disposition = mode == 3   ? ObservationDisposition::extractor_failed
                    : mode == 5 ? ObservationDisposition::extractor_unavailable
                                : ObservationDisposition::success;
    o.sample_count = mode == 2 || mode == 3 || mode == 5 ? 0 : samples;
    o.emitter_dictionary = {emitter};
    o.line_dictionary = {{emitter, {{"pattern"}}, 7, {{"stripe"}}}};
    o.confidence_interpretation = mode == 1 ? Evidence<ImplementationIdentity>{Unavailable{}}
                                            : Evidence<ImplementationIdentity>{ImplementationIdentity{
                                                  {"org.example.confidence"}, {1, 2, 3}, "test", ref()}};
    o.diagnostic = "synthetic fixture";
    if (!o.sample_count)
        return o;
    std::vector<float> pixels(static_cast<size_t>(o.sample_count) * 2);
    for (size_t i = 0; i < o.sample_count; ++i) {
        pixels[i * 2] = 0.25F;
        pixels[i * 2 + 1] = 0.5F;
    }
    o.attributes.push_back(
        {laser::source_pixel_descriptor(o.sample_count), memory::copy(std::as_bytes(std::span(pixels)))});
    std::vector<uint32_t> flags(static_cast<size_t>(o.sample_count),
                                mode == 1 ? laser::emitter_unknown | laser::line_unknown | 0x40000000u
                                          : 0x40000000u);
    o.attributes.push_back(column(std::string(laser::quality_flags), flags, schema::ScalarType::u32));
    for (auto name : {laser::emitter_index, laser::line_index})
        o.attributes.push_back(column(std::string(name),
                                      std::vector<uint32_t>(static_cast<size_t>(o.sample_count), 0),
                                      schema::ScalarType::u32));
    for (auto name : {laser::emitter_valid, laser::line_valid})
        o.attributes.push_back(column(
            std::string(name), std::vector<uint8_t>(static_cast<size_t>(o.sample_count), mode == 1 ? 0 : 1),
            schema::ScalarType::u8));
    if (mode != 1) {
        o.attributes.push_back(column(std::string(laser::confidence),
                                      std::vector<float>(static_cast<size_t>(o.sample_count), 0.75F),
                                      schema::ScalarType::f32));
        o.attributes.push_back(column(std::string(laser::confidence_valid),
                                      std::vector<uint8_t>(static_cast<size_t>(o.sample_count), 1),
                                      schema::ScalarType::u8));
    }
    if (mode == 4)
        o.attributes.push_back(column("org.example.future.width",
                                      std::vector<double>(static_cast<size_t>(o.sample_count), -0.0),
                                      schema::ScalarType::f64, "pixel"));
    return o;
}
inline std::string encode(const LaserObservation &o) {
    std::ostringstream s;
    write_laser_observation(s, o);
    return s.str();
}
inline memory::BufferView view(std::string_view s) {
    return memory::copy({reinterpret_cast<const std::byte *>(s.data()), s.size()});
}
inline std::string semantic_bytes(const data::SemanticPacket &v) {
    std::ostringstream s;
    write_semantic_packet(s, v);
    return s.str();
}
} // namespace observation_fixture
