#include "../support/l1_candidate_values.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <mantis/pipeline_runtime.hpp>

using namespace l1_fixture;
using namespace mantis::pipeline;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__));                 \
    } while (false)

SemanticPublished invoke(SemanticNodeInstance &node, const AcquisitionBundle &b,
                         const CancellationToken &token = {}) {
    std::array<SemanticPublished, 1> inputs{std::make_shared<const SemanticPacket>(b)};
    auto result = node.process(inputs, token);
    if (!result)
        throw Failure(result.error());
    return *result;
}
LaserObservation extract(const AcquisitionBundle &b, L1CandidateConfig c) {
    auto node = l1_candidate_extractor(std::move(c)).factory();
    return std::get<LaserObservation>(*invoke(*node, b));
}
void accepted(const LaserObservation &o, double y, double tolerance = 0) {
    CHECK(o.sample_count > 0);
    CHECK(o.confidence_interpretation.presence() == Presence::unavailable);
    for (uint32_t i = 0; i < o.sample_count; ++i) {
        CHECK(value<uint8_t>(o, l1::refinement_valid, i) == 1);
        CHECK(value<uint32_t>(o, l1::reason, i) == 0);
        CHECK((value<uint32_t>(o, laser::quality_flags, i) & laser::producer_rejected) == 0);
        CHECK(std::abs(double(value<float>(o, laser::source_pixel, i, 1)) - y) <= tolerance);
    }
}
void exact_and_lineage() {
    for (bool packed : {false, true}) {
        auto b = quadratic(packed);
        auto c = subpixel_config(packed);
        c.roi = L1CandidateConfig::Roi{2, 1, 4, 10};
        const auto source_hash = content_hash(*b.frameset->frames[0]->attributes[0].buffer.map_read());
        auto o = extract(b, c);
        CHECK(o.sample_count == 4);
        accepted(o, 4.25);
        CHECK(o.context.producer.implementation.value == "org.mantis.l1-subpixel");
        CHECK((o.context.producer.version == SemanticVersion{1, 0, 0}));
        CHECK(o.context.packet_quality_flags == 0);
        CHECK(encode(o) == encode(extract(b, c)));
        for (uint32_t i = 0; i < o.sample_count; ++i) {
            CHECK(value<float>(o, laser::source_pixel, i) == 2 + i);
            CHECK(value<uint32_t>(o, l1::curvature, i) == 32);
        }
        for (const auto &a : o.attributes) {
            CHECK(a.descriptor.name != laser::confidence && a.descriptor.name != laser::confidence_valid);
            if (a.descriptor.name == l1::curvature)
                CHECK(a.descriptor.unit == "DN/pixel^2");
            else
                CHECK(a.buffer.identity() == o.attributes[0].buffer.identity());
        }
        // Compare the entire nested typed context against the integer producer.
        auto coarse_config = c;
        coarse_config.subpixel.reset();
        const auto coarse = extract(b, coarse_config);
        auto expected = o;
        expected.context = coarse.context;
        expected.context.producer = o.context.producer;
        expected.context.parameters = o.context.parameters;
        CHECK(encode(o) == encode(expected));
        CHECK(*o.context.parameters.get()->hash.get() != *coarse.context.parameters.get()->hash.get());
        auto changed = c;
        ++changed.subpixel->minimum_curvature;
        CHECK(*extract(b, changed).context.parameters.get()->hash.get() !=
              *o.context.parameters.get()->hash.get());
        changed = c;
        ++changed.subpixel->minimum_neighbor_signal;
        CHECK(*extract(b, changed).context.parameters.get()->hash.get() !=
              *o.context.parameters.get()->hash.get());

        NodeDescriptor source;
        source.id = "recorded-input";
        source.outputs = {{"bundle", schema::acquisition_bundle}};
        SemanticGraph graph;
        graph.nodes = {{source, {}}, l1_candidate_extractor(c), observation_consumer()};
        graph.connections = {{0, 0, 1, 0, 1, QueuePolicy::lossless}, {1, 0, 2, 0, 1, QueuePolicy::lossless}};
        auto plan = compile(graph);
        CHECK(plan);
        auto result = execute(*plan, data::publish(SemanticPacket{b}));
        CHECK(result);
        CHECK(encode(std::get<LaserObservation>(*result->output)) == encode(o));
        CHECK(content_hash(*b.frameset->frames[0]->attributes[0].buffer.map_read()) == source_hash);

        for (Presence presence : {Presence::unknown, Presence::unavailable}) {
            auto unresolved = b;
            unresolved.evidence.emitters[0].exposure_effective[0].state =
                presence == Presence::unknown ? Evidence<ExposureEffectiveState>{Unknown{}}
                                              : Evidence<ExposureEffectiveState>{Unavailable{}};
            const auto u = extract(unresolved, c);
            accepted(u, 4.25);
            CHECK(value<uint8_t>(u, laser::emitter_valid, 0) == 0);
            CHECK(value<uint8_t>(u, laser::line_valid, 0) == 0);
            CHECK(u.context.emitter_evidence[0].exposure_effective[0].state.presence() == presence);
            CHECK(value<uint32_t>(u, laser::quality_flags, 0) ==
                  (laser::emitter_unknown | laser::line_unknown));
        }
    }
    // Original-image coordinates near the maximum supported dimension.
    auto tall = image_bundle(false, 4, 8192, [](auto, auto y) -> uint16_t {
        if (y >= 8186 && y <= 8190) {
            constexpr uint16_t samples[]{99, 155, 179, 171, 131};
            return samples[y - 8186];
        }
        return 12;
    });
    auto c = subpixel_config();
    c.roi = L1CandidateConfig::Roi{1, 8185, 2, 7};
    accepted(extract(tall, c), 8188.25);
}
void rejected_profiles() {
    for (bool packed : {false, true}) {
        auto c = subpixel_config(packed);
        c.background_level = 0;
        c.minimum_signal = 20;
        auto node = l1_candidate_extractor(c).factory();
        const auto good = quadratic(packed);
        auto rejection = [&](Signal profile, uint32_t reason, float integer_y, uint32_t height = 12) {
            const auto b = image_bundle(packed, 8, height, std::move(profile), 11);
            const auto o = std::get<LaserObservation>(*invoke(*node, b));
            CHECK(o.sample_count == 8);
            for (uint32_t i = 0; i < 8; ++i) {
                CHECK((value<uint32_t>(o, l1::reason, i) & reason) == reason);
                CHECK(value<uint8_t>(o, l1::refinement_valid, i) == 0);
                CHECK(value<float>(o, laser::source_pixel, i, 1) == integer_y);
                CHECK(value<uint32_t>(o, laser::quality_flags, i) & laser::producer_rejected);
            }
            CHECK(o.context.packet_quality_flags & l1::rejected_profiles);
            accepted(std::get<LaserObservation>(*invoke(*node, good)), 4.25);
            return o;
        };
        rejection([](auto, auto) { return uint16_t(180); }, l1::insufficient_neighbors | l1::edge_profile, 0,
                  1);
        rejection([](auto, auto y) { return uint16_t(y == 0 ? 180 : 140); },
                  l1::insufficient_neighbors | l1::edge_profile, 0, 2);
        rejection([](auto, auto y) { return uint16_t(y == 0   ? 180
                                                     : y == 1 ? 140
                                                              : 0); },
                  l1::insufficient_neighbors | l1::edge_profile, 0);
        rejection([](auto, auto y) { return uint16_t(y == 11   ? 180
                                                     : y == 10 ? 140
                                                               : 0); },
                  l1::insufficient_neighbors | l1::edge_profile, 11);
        rejection([](auto, auto y) { return uint16_t(y == 3 || y == 4 ? 180
                                                     : y == 5         ? 100
                                                                      : 0); },
                  l1::flat_peak, 3);
        const auto maximum = c.saturation_level;
        rejection([=](auto, auto y) { return uint16_t(y == 4             ? maximum
                                                      : y == 3 || y == 5 ? 100
                                                                         : 0); },
                  l1::saturated, 4);
        rejection([](auto, auto y) { return uint16_t(y == 4   ? 180
                                                     : y == 5 ? 140
                                                              : 0); }, l1::weak_neighbors,
                  4);
        auto multiple = rejection(
            [](auto, auto y) {
                return uint16_t(y == 3 ? 180 : y == 5 ? 170 : y >= 2 && y <= 6 ? 80 : 0);
            },
            l1::multiple_local_peaks, 3);
        CHECK(value<uint32_t>(multiple, laser::quality_flags, 0) & laser::ambiguous);
        auto competing = rejection([](auto, auto y) { return uint16_t(y == 3 || y == 7 ? 180 : 0); },
                                   l1::competing_peaks, 3);
        CHECK(value<uint32_t>(competing, laser::quality_flags, 0) & laser::ambiguous);

        auto low = image_bundle(packed, 8, 12, [](auto x, auto y) { return uint16_t(10 + (x + y) % 3); });
        auto empty = std::get<LaserObservation>(*invoke(*node, low));
        CHECK(empty.sample_count == 0 && empty.attributes.empty());
        CHECK(empty.disposition == ObservationDisposition::success);
        CHECK(empty.context.packet_quality_flags == l1::missing_profiles);
        accepted(std::get<LaserObservation>(*invoke(*node, good)), 4.25);

        // Missing columns compact all columns identically, including curvature.
        auto occluded = image_bundle(packed, 8, 12, [=](auto x, auto y) -> uint16_t {
            if (x == 1)
                return 0;
            if (x == 2)
                return y == 4 ? maximum : y == 3 || y == 5 ? 100 : 0;
            constexpr uint16_t samples[]{99, 155, 179, 171, 131};
            return y >= 2 && y <= 6 ? samples[y - 2] : 0;
        });
        const auto compact = std::get<LaserObservation>(*invoke(*node, occluded));
        CHECK(compact.sample_count == 7);
        CHECK(compact.context.packet_quality_flags ==
              (l1::missing_profiles | l1::rejected_profiles | l1::saturation_observed));
        for (uint32_t i = 0; i < 7; ++i) {
            CHECK(value<float>(compact, laser::source_pixel, i) == (i == 0 ? 0 : i + 1));
            CHECK(value<uint8_t>(compact, l1::refinement_valid, i) == (i == 1 ? 0 : 1));
            if (i != 1) {
                CHECK(value<float>(compact, laser::source_pixel, i, 1) == 4.25f);
                CHECK(value<uint32_t>(compact, l1::curvature, i) == 32);
            }
        }

        c.subpixel->minimum_curvature = 3;
        auto weak = image_bundle(packed, 8, 12, [](auto, auto y) {
            return uint16_t(y == 4 ? 180 : y == 3 || y == 5 ? 179 : 0);
        });
        CHECK(value<uint32_t>(extract(weak, c), l1::reason, 0) & l1::weak_curvature);
        c.subpixel->minimum_curvature = 2;
        accepted(extract(weak, c), 4);
        auto flat =
            image_bundle(packed, 8, 12, [](auto, auto y) { return uint16_t(y >= 3 && y <= 5 ? 180 : 0); });
        auto o = extract(flat, c);
        CHECK(value<uint32_t>(o, l1::reason, 0) & l1::flat_peak);
        c.roi = L1CandidateConfig::Roi{2, 4, 3, 3};
        CHECK(value<uint32_t>(extract(good, c), l1::reason, 0) & l1::edge_profile);
    }
}
void invalid_and_dark() {
    auto c = subpixel_config();
    auto node = l1_candidate_extractor(c).factory();
    const auto good = quadratic();
    const auto bytes = encode(std::get<LaserObservation>(*invoke(*node, good)));
    for (unsigned mode = 0; mode < 7; ++mode) {
        auto bad = good;
        switch (mode) {
        case 0:
            edit_image(bad, [](auto &p) { p.attributes[0].descriptor.stride[0] = 1; });
            break;
        case 1:
            edit_image(bad, [](auto &p) { p.attributes[0].buffer = p.attributes[0].buffer.slice(0, 10); });
            break;
        case 2:
            edit_image(bad, [](auto &p) { p.attributes[0].descriptor.shape = {UINT64_MAX, UINT64_MAX}; });
            break;
        case 3:
            edit_image(bad, [](auto &p) { p.attributes[0].descriptor.scalar = schema::ScalarType::f32; });
            break;
        default: {
            auto exposure = *bad.evidence.frames[0].exposure.get();
            exposure.uncertainty_ns = mode == 4   ? std::numeric_limits<double>::quiet_NaN()
                                      : mode == 5 ? std::numeric_limits<double>::infinity()
                                                  : -std::numeric_limits<double>::infinity();
            bad.evidence.frames[0].exposure = exposure;
            break;
        }
        }
        std::array<SemanticPublished, 1> input{std::make_shared<const SemanticPacket>(bad)};
        CHECK(!node->process(input, {}));
        CHECK(encode(std::get<LaserObservation>(*invoke(*node, good))) == bytes);
    }
    CancellationToken cancelled;
    cancelled.cancel();
    std::array<SemanticPublished, 1> input{std::make_shared<const SemanticPacket>(good)};
    const auto result = node->process(input, cancelled);
    CHECK(!result && result.error().code == Status::cancelled);
    CHECK(encode(std::get<LaserObservation>(*invoke(*node, good))) == bytes);
    for (unsigned mode = 0; mode < 4; ++mode) {
        auto invalid = c;
        if (mode == 0)
            invalid.subpixel->minimum_neighbor_signal = 0;
        else if (mode == 1)
            invalid.subpixel->minimum_neighbor_signal = invalid.minimum_signal + 1;
        else
            invalid.subpixel->minimum_curvature = mode == 2 ? 0 : UINT16_MAX;
        bool refused = false;
        try {
            (void)l1_candidate_extractor(invalid);
        } catch (const Failure &e) {
            refused = e.error.code == Status::invalid_argument;
        }
        CHECK(refused);
    }
    auto dark = image_bundle(false, 8, 12, [](auto, auto) { return uint16_t(12); }, 3, true);
    c.background_level = 0;
    c.dark = L1DarkReference{std::make_shared<const AcquisitionBundle>(dark), c.raw_input};
    c.dark->raw_input.id.value = "exact-dark-input";
    auto o = extract(good, c);
    accepted(o, 4.25);
    CHECK(o.context.exact_inputs.size() == 2);
    // Dark clipping is invalid evidence for interpolation even when the source
    // selected run is not clipped. Its packet state must not become success.
    dark = image_bundle(false, 8, 12, [](auto, auto y) { return uint16_t(y == 3 ? 255 : 12); }, 3, true);
    c.dark->bundle = std::make_shared<const AcquisitionBundle>(dark);
    o = extract(good, c);
    CHECK(value<uint32_t>(o, l1::reason, 0) & l1::saturated);
    CHECK(value<uint8_t>(o, l1::refinement_valid, 0) == 0);
}
void gaussian_characterization() {
    // These sampled Gaussians and bounded deterministic integer perturbations
    // are independent of the production parabola and threshold-run estimator.
    for (bool packed : {false, true})
        for (double sigma : {0.75, 1.0, 1.5, 2.0})
            for (int amplitude : {96, 192})
                for (int noise : {0, 1}) {
                    auto c = subpixel_config(packed);
                    const int scale = packed ? 4 : 1;
                    double sum = 0, square = 0, coarse_square = 0, maximum = 0;
                    uint32_t count = 0;
                    for (double phase : {-0.375, -0.25, -0.125, 0.0, 0.125, 0.25, 0.375}) {
                        const double center = 12 + phase;
                        auto b = image_bundle(
                            packed, 8, 25,
                            [=](auto x, auto y) {
                                const double distance = (double(y) - center) / sigma;
                                const int mirrored_y = phase < 0 ? 24 - int(y) : int(y);
                                const int perturbation = noise * ((mirrored_y * 17 + int(x) * 13) % 3 - 1);
                                return uint16_t(
                                    12 * scale +
                                    std::max(0, int(std::lround(amplitude * scale *
                                                                std::exp(-0.5 * distance * distance))) +
                                                    perturbation));
                            },
                            7);
                        const auto o = extract(b, c);
                        CHECK(o.sample_count == 8);
                        accepted(o, center, 0.15);
                        auto coarse_config = c;
                        coarse_config.subpixel.reset();
                        const auto coarse = extract(b, coarse_config);
                        for (uint32_t i = 0; i < 8; ++i) {
                            const double error = value<float>(o, laser::source_pixel, i, 1) - center;
                            const double integer_error =
                                value<float>(coarse, laser::source_pixel, i, 1) - center;
                            sum += error;
                            square += error * error;
                            coarse_square += integer_error * integer_error;
                            maximum = std::max(maximum, std::abs(error));
                            ++count;
                        }
                    }
                    CHECK(std::abs(sum / count) <= 0.04);
                    CHECK(square < coarse_square);
                    std::cout << "synthetic " << (packed ? "Y10P" : "RAW8") << " sigma_pixel=" << sigma
                              << " amplitude_dn=" << amplitude * scale << " noise_dn=" << noise
                              << " n=" << count << " bias_pixel=" << sum / count
                              << " rms_pixel=" << std::sqrt(square / count) << " max_abs_pixel=" << maximum
                              << '\n';
                }
}
int main() {
    try {
        exact_and_lineage();
        rejected_profiles();
        invalid_and_dark();
        gaussian_characterization();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
