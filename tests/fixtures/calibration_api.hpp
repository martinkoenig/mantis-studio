#pragma once
#include "calibration_artifacts.hpp"
#include <mantis/calibration_artifacts.hpp>
namespace m6fixture {
using namespace mantis;
namespace ca = calibration::artifacts;
using m5fixture::checked;
calibration::CalibrationTarget physical_target(bool checker = false) {
    calibration::CalibrationTarget t;
    t.grid = {8, 6, 40};
    if (!checker) t.pattern = calibration::CharucoDefinition{"DICT_6X6_250", 25};
    t.measurement = {8 * 41.3, 6 * 40.6, calibration::MeasurementProvenance{}};
    return t;
}
Id raw(artifact::Store &s, bool board) {
    auto id = s.begin({"org.mantis.RawCapture", 2}, {});
    data::Packet fs; fs.type = schema::frameset; fs.header.sequence.value = 100;
    for (const auto *role : {"left", "right"}) {
        data::Packet image; image.type = schema::image;
        image.header.frame = {{"optical." + std::string(role)}, "optical +X right +Y down +Z forward"};
        image.header.metadata = {{"role", role}, {"identity", "camera." + std::string(role)}, {"fourcc", "GREY"}};
        std::vector<std::byte> pixels(480 * 360, std::byte{255});
        if (board) for (size_t y = 0; y < 240; ++y) for (size_t x = 0; x < 320; ++x)
            if ((x / 40 + y / 40) % 2 == 0) pixels[(y + 60) * 480 + x + 80] = std::byte{0};
        image.attributes = {{{"org.mantis.pixels", schema::ScalarType::u8, {360, 480}, {480, 1}, "intensity"}, memory::copy(pixels)}};
        fs.frames.push_back(data::publish(std::move(image)));
    }
    s.append(id, fs); s.finalize(id); return id;
}
struct Seed {
    ca::Stored<ca::TargetArtifact> charuco, checker;
    ca::Stored<ca::DatasetArtifact> dataset, checker_dataset;
    std::vector<Id> raw_ids;
};
Seed seed(artifact::Store &store) {
    auto charuco = checked(ca::create_calibration_target(store, physical_target()));
    auto checker = checked(ca::create_calibration_target(store, physical_target(true)));
    std::vector<Id> raw_ids{raw(store, true), raw(store, false)};
    std::sort(raw_ids.begin(), raw_ids.end());
    auto dataset = checked(ca::create_calibration_dataset(store,
        m5fixture::fixture(charuco.value.target, raw_ids), charuco.reference()));
    auto checker_dataset = checked(ca::create_calibration_dataset(store,
        m5fixture::fixture(checker.value.target, raw_ids), checker.reference()));
    return {std::move(charuco), std::move(checker), std::move(dataset), std::move(checker_dataset), std::move(raw_ids)};
}
} // namespace m6fixture
