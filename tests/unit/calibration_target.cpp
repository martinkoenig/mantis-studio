#include <mantis/calibration_target.hpp> // The focused public header is self-contained.
#include <mantis/calibration.hpp> // Existing umbrella exposes the same foundation.
#include <algorithm>
#include <iostream>
#include <limits>

using namespace mantis;
using namespace mantis::calibration;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed: " #x); } while (false)
bool near(double a, double b) { return std::abs(a - b) <= 1e-12 * std::max({1.0, std::abs(a), std::abs(b)}); }
CalibrationTarget target(bool charuco = false) {
    CalibrationTarget value;
    value.identity = {{"test.calibration.target"}, 7};
    value.grid = {8, 6, 40}; // Test fixture only; no preferred board dimensions.
    if (charuco) value.pattern = CharucoDefinition{"dictionary-validated-by-M2", 25};
    return value;
}
void rejects(const CalibrationTarget &value) {
    auto geometry = derive_target_geometry(value);
    auto valid = validate_target(value);
    CHECK(!geometry && !valid);
    CHECK(geometry.error().code == Status::invalid_argument);
    CHECK(geometry.error().component == "calibration" && !geometry.error().message.empty());
    CHECK(valid.error().message == geometry.error().message); // One deterministic validation contract.
    auto point = scale_target_point(value, {80, 120, 0});
    CHECK(!point && point.error().message == geometry.error().message);
}
void geometry_examples() {
    for (bool charuco : {false, true}) {
        auto value = target(charuco);
        CHECK(value.type() == (charuco ? TargetType::charuco : TargetType::checkerboard));
        auto nominal = derive_target_geometry(value);
        CHECK(nominal && validate_target(value));
        CHECK(nominal->source == GeometrySource::nominal);
        CHECK(nominal->nominal_active_width_mm == 320 && nominal->nominal_active_height_mm == 240);
        CHECK(nominal->scale_x == 1 && nominal->scale_y == 1);
        CHECK(nominal->effective_square_pitch_x_mm == 40 && nominal->effective_square_pitch_y_mm == 40);
        CHECK(nominal->effective_marker.has_value() == charuco);
        if (charuco) CHECK(nominal->effective_marker->width_mm == 25 && nominal->effective_marker->height_mm == 25);
        auto point = scale_target_point(value, {80, 120, 0});
        CHECK(point && point->x_mm == 80 && point->y_mm == 120 && point->z_mm == 0);

        value.measurement.active_width_mm = 8 * 41.3;
        value.measurement.active_height_mm = 6 * 41.3;
        auto uniform = derive_target_geometry(value);
        CHECK(uniform && uniform->source == GeometrySource::measured);
        CHECK(near(uniform->scale_x, 1.0325) && near(uniform->scale_y, 1.0325));
        CHECK(near(uniform->effective_square_pitch_x_mm, 41.3) && near(uniform->effective_square_pitch_y_mm, 41.3));
        if (charuco) CHECK(near(uniform->effective_marker->width_mm, 25.8125) && near(uniform->effective_marker->height_mm, 25.8125));

        value.measurement.active_height_mm = 6 * 40.6;
        value.measurement.provenance = MeasurementProvenance{0.1, 0.2, "digital_caliper", "measured between outer active-grid edges"};
        auto anisotropic = derive_target_geometry(value);
        CHECK(anisotropic && anisotropic->source == GeometrySource::measured);
        CHECK(near(anisotropic->scale_x, 1.0325) && near(anisotropic->scale_y, 1.015));
        CHECK(near(anisotropic->effective_square_pitch_x_mm, 41.3));
        CHECK(near(anisotropic->effective_square_pitch_y_mm, 40.6));
        CHECK(anisotropic->effective_square_pitch_x_mm != anisotropic->effective_square_pitch_y_mm);
        if (charuco) {
            CHECK(near(anisotropic->effective_marker->width_mm, 25.8125));
            CHECK(near(anisotropic->effective_marker->height_mm, 25.375));
            CHECK(anisotropic->effective_marker->width_mm != anisotropic->effective_marker->height_mm);
        }
        point = scale_target_point(value, {80, 120, 0});
        CHECK(point && near(point->x_mm, 82.6) && near(point->y_mm, 121.8) && point->z_mm == 0);
        // The primitive preserves Z; it does not certify planarity or correct warp.
        point = scale_target_point(value, {-80, 120, 7});
        CHECK(point && near(point->x_mm, -82.6) && near(point->y_mm, 121.8) && point->z_mm == 7);
        auto again = derive_target_geometry(value);
        CHECK(again && again->scale_x == anisotropic->scale_x && again->scale_y == anisotropic->scale_y);
        CHECK(again->effective_square_pitch_x_mm == anisotropic->effective_square_pitch_x_mm);
        CHECK(again->effective_square_pitch_y_mm == anisotropic->effective_square_pitch_y_mm);
        CHECK(value.identity.id.value == "test.calibration.target" && value.identity.revision == 7);

        // No invented limits on positive physical size, scale error or anisotropy.
        value.measurement.active_width_mm = 320 * 1e-6;
        value.measurement.active_height_mm = 240 * 2e6;
        CHECK(validate_target(value));
        value = target(charuco);
        value.grid.nominal_square_size_mm = 1e-12;
        if (charuco) std::get<CharucoDefinition>(value.pattern).nominal_marker_size_mm = 5e-13;
        CHECK(validate_target(value));
    }
}
void charuco_pattern_layout() {
    const CharucoDefinition aggregate{"dictionary-validated-by-M2", 25};
    CHECK(aggregate.pattern_layout == CharucoPatternLayout::black_square_at_origin);
    for (bool measured : {false, true}) {
        auto black = target(true);
        std::get<CharucoDefinition>(black.pattern).pattern_layout = CharucoPatternLayout::black_square_at_origin;
        if (measured) {
            black.measurement.active_width_mm = 8 * 41.3;
            black.measurement.active_height_mm = 6 * 40.6;
        }
        auto white = black;
        std::get<CharucoDefinition>(white.pattern).pattern_layout = CharucoPatternLayout::white_square_at_origin_even_rows;
        CHECK(validate_target(black) && validate_target(white));
        const auto a = derive_target_geometry(black), b = derive_target_geometry(white);
        CHECK(a && b && a->source == b->source);
        CHECK(a->nominal_active_width_mm == b->nominal_active_width_mm && a->nominal_active_height_mm == b->nominal_active_height_mm);
        CHECK(a->scale_x == b->scale_x && a->scale_y == b->scale_y);
        CHECK(a->effective_square_pitch_x_mm == b->effective_square_pitch_x_mm && a->effective_square_pitch_y_mm == b->effective_square_pitch_y_mm);
        CHECK(a->effective_marker->width_mm == b->effective_marker->width_mm && a->effective_marker->height_mm == b->effective_marker->height_mm);
        const auto p = scale_target_point(black, {80, 120, 0}), q = scale_target_point(white, {80, 120, 0});
        CHECK(p && q && p->x_mm == q->x_mm && p->y_mm == q->y_mm && p->z_mm == q->z_mm);
        white.grid.squares_y = 5;
        rejects(white);
        CHECK(derive_target_geometry(white).error().message == "ChArUco white-origin even-row layout requires even squares_y");
        black.grid.squares_y = 5;
        CHECK(validate_target(black)); // Black-origin has no even-row restriction.
    }
    auto unknown = target(true);
    std::get<CharucoDefinition>(unknown.pattern).pattern_layout = static_cast<CharucoPatternLayout>(99);
    rejects(unknown);
    CHECK(derive_target_geometry(unknown).error().message == "Unknown ChArUco physical pattern layout");
}
void measurement_provenance() {
    for (bool charuco : {false, true}) {
        auto value = target(charuco);
        auto nominal = derive_target_geometry(value);
        CHECK(nominal && validate_target(value)); // No extents and no provenance.
        CHECK(nominal->source == GeometrySource::nominal && nominal->scale_x == 1 && nominal->scale_y == 1);

        value.measurement.active_width_mm = 8 * 41.3;
        value.measurement.active_height_mm = 6 * 40.6;
        auto measured = derive_target_geometry(value);
        CHECK(measured && validate_target(value)); // Measured extents do not require provenance.
        CHECK(measured->source == GeometrySource::measured);
        CHECK(near(measured->scale_x, 1.0325) && near(measured->scale_y, 1.015));

        for (const auto &provenance : {
                 MeasurementProvenance{0.1, 0.2, {}, {}},
                 MeasurementProvenance{{}, {}, "digital_caliper", {}},
                 MeasurementProvenance{{}, {}, {}, "measured between outer active-grid edges"},
                 MeasurementProvenance{}}) {
            value.measurement.provenance = provenance;
            auto with_provenance = derive_target_geometry(value);
            CHECK(with_provenance && validate_target(value));
            CHECK(with_provenance->source == GeometrySource::measured);
            CHECK(with_provenance->scale_x == measured->scale_x && with_provenance->scale_y == measured->scale_y);

            auto orphan = target(charuco);
            orphan.measurement.provenance = provenance;
            rejects(orphan); // Presence requires extents, even if all provenance fields are empty.
            CHECK(derive_target_geometry(orphan).error().message == "Measurement provenance requires measured active extents");
        }
    }
}
void invalid_structure() {
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    for (bool charuco : {false, true}) {
        for (uint32_t count : {0u, 1u}) for (bool x : {false, true}) {
            auto value = target(charuco);
            (x ? value.grid.squares_x : value.grid.squares_y) = count;
            rejects(value);
        }
        for (double invalid : {0.0, -40.0, nan, inf, -inf}) {
            auto value = target(charuco);
            value.grid.nominal_square_size_mm = invalid;
            rejects(value);
            for (bool width : {false, true}) {
                value = target(charuco);
                value.measurement.active_width_mm = 320;
                value.measurement.active_height_mm = 240;
                (width ? value.measurement.active_width_mm : value.measurement.active_height_mm) = invalid;
                rejects(value);
            }
        }
        for (bool width : {false, true}) {
            auto value = target(charuco);
            (width ? value.measurement.active_width_mm : value.measurement.active_height_mm) = 320;
            rejects(value); // One extent only is never nominal or measured geometry.
        }
        for (double uncertainty : {-0.1, nan, inf, -inf}) for (bool width : {false, true}) {
            auto value = target(charuco);
            value.measurement.active_width_mm = 320;
            value.measurement.active_height_mm = 240;
            value.measurement.provenance.emplace();
            auto &provenance = *value.measurement.provenance;
            (width ? provenance.width_uncertainty_mm : provenance.height_uncertainty_mm) = uncertainty;
            rejects(value);
        }
        auto value = target(charuco);
        value.measurement.active_width_mm = 320;
        value.measurement.active_height_mm = 240;
        value.measurement.provenance = MeasurementProvenance{0.0, {}, {}, {}};
        CHECK(validate_target(value)); // Zero uncertainty is valid with measured extents.
        value.measurement.provenance->height_uncertainty_mm = 0;
        CHECK(validate_target(value));
    }
    auto value = target(true);
    std::get<CharucoDefinition>(value.pattern).dictionary.clear();
    rejects(value);
    for (double invalid : {0.0, -25.0, 40.0, 41.0, nan, inf, -inf}) {
        value = target(true);
        std::get<CharucoDefinition>(value.pattern).nominal_marker_size_mm = invalid;
        rejects(value);
    }
}
void arithmetic_limits() {
    auto value = target();
    value.grid.nominal_square_size_mm = std::numeric_limits<double>::max();
    rejects(value); // Active nominal extents overflow.
    value = target();
    value.grid.nominal_square_size_mm = 1e-300;
    value.measurement.active_width_mm = 1e100;
    value.measurement.active_height_mm = 1e100;
    rejects(value); // Scale overflow, not a print-quality limit.
    value = target();
    value.grid.nominal_square_size_mm = 1e100;
    value.measurement.active_width_mm = std::numeric_limits<double>::denorm_min();
    value.measurement.active_height_mm = 1e100;
    rejects(value); // Scale underflow cannot represent positive physical geometry.
    value = target(true);
    std::get<CharucoDefinition>(value.pattern).nominal_marker_size_mm = std::numeric_limits<double>::denorm_min();
    value.measurement.active_width_mm = 160;
    value.measurement.active_height_mm = 120;
    rejects(value); // Physical marker underflow.
    value = target();
    value.grid = {std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(), 1};
    CHECK(validate_target(value)); // No arbitrary maximum grid size.

    value = target();
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
        for (auto member : {&TargetPoint::x_mm, &TargetPoint::y_mm, &TargetPoint::z_mm}) {
            TargetPoint nominal{80, 120, 0}; nominal.*member = invalid;
            auto result = scale_target_point(value, nominal);
            CHECK(!result && result.error().code == Status::invalid_argument && result.error().component == "calibration");
        }
    }
    value.measurement.active_width_mm = 640;
    value.measurement.active_height_mm = 480;
    for (TargetPoint nominal : {TargetPoint{std::numeric_limits<double>::max(), 0, 0},
                               TargetPoint{0, std::numeric_limits<double>::max(), 0}}) {
        auto result = scale_target_point(value, nominal);
        CHECK(!result && result.error().code == Status::invalid_argument);
    }
}
int main() {
    try {
        geometry_examples(); charuco_pattern_layout(); measurement_provenance(); invalid_structure(); arithmetic_limits();
        std::cout << "Calibration targets: nominal/measured scale, anisotropy, marker dimensions, points and structural validation passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
