#include "../../plugins/first-party/devices/x1/backend.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mantis/plugin_runtime.hpp>
#include <mantis/image_layout.hpp>
#include <nlohmann/json.hpp>
using namespace mantis;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("Check failed: ") + #x); } while (false)
template<class F> void rejects(F f) { bool rejected = false; try { f(); } catch (const std::exception &) { rejected = true; } CHECK(rejected); }
int main(int argc, char **argv) {
    try {
        CHECK(argc == 3);
        auto profile = x1::load_profile(argv[2]);
        for (const auto *scenario : {"normal", "renumber"}) {
            auto backend = x1::fake_backend(scenario);
            auto cameras = x1::assign(profile, backend->discover());
            CHECK(cameras[0].sensor == profile.sensors[0]); CHECK(cameras[1].sensor == profile.sensors[1]);
        }
        for (const auto *scenario : {"missing", "ambiguous", "unsupported"})
            rejects([&] { auto backend = x1::fake_backend(scenario); (void)x1::assign(profile, backend->discover()); });
        auto dir = std::filesystem::temp_directory_path() / Id::random().value;
        std::filesystem::create_directory(dir);
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{dir};
        auto fixture = nlohmann::json::parse(profile.json);
        fixture["mode"] = {{"width", 64}, {"height", 48}, {"fourcc", "GREY"}, {"fps", 120}};
        fixture["stall_timeout_ms"] = 100;
        fixture["calibration_id"] = "test.calibration"; fixture["calibration_revision"] = 7;
        { std::ofstream out(dir / "profile.json"); out << fixture; }
        setenv("MANTIS_X1_PROFILE", (dir / "profile.json").c_str(), 1);
        for (const auto *scenario : {"normal", "y10p", "renumber", "eagain", "drop-left", "drop-right", "disconnect", "mismatch", "repeat", "timestamp-jump", "lag", "stall-left", "stall-right", "stop-right", "stream-mismatch"}) {
            fixture["mode"]["fourcc"] = std::string(scenario) == "y10p" ? "Y10P" : "GREY";
            { std::ofstream out(dir / "profile.json"); out << fixture; }
            setenv("MANTIS_X1_FAKE", scenario, 1);
            plugins::Registry registry(std::filesystem::path(argv[1]).parent_path() / "bin/mantis-plugin-host", dir / "hosts", {});
            registry.discover(argv[1], {"org.mantis.virtual-scanner", "org.mantis.x1", "org.mantis.example-points", "org.mantis.ply"});
            if (std::string(scenario) == "normal") {
                plugins::Loaded loaded(std::filesystem::path(argv[1]) / "mantis-x1.so");
                const auto *api = loaded.query<MantisAcquisitionV1>(MANTIS_ACQUISITION_V1);
                std::string id;
                unsigned descriptors{};
                sdk::enumerate(api, [&](const MantisDiscoveredDeviceV1 &d) { ++descriptors; if (!*d.parent_id) id = d.id; });
                CHECK(descriptors == 3);
                sdk::Acquisition sdk_stream(api, plugins::host_api(), id.c_str()); sdk_stream.start();
                auto emit = [](void *ctx, const MantisFrameSetV1 *set) { *static_cast<unsigned *>(ctx) = set->frame_count; return 0; };
                unsigned frames{}; CHECK(sdk_stream.next(100, emit, &frames) == 0 && frames == 2); sdk_stream.stop();
            }
            auto devices = registry.devices();
            device::ImageStream *stream{};
            for (auto &candidate : devices) if (candidate->descriptor().plugin_id == "org.mantis.x1") stream = candidate.get();
            CHECK(stream); CHECK(stream->components().size() == 2); CHECK(stream->start());
            bool failed = false;
            unsigned count = 0;
            data::Published retained;
            while (count < 8) {
                auto result = stream->next();
                if (!result) { failed = true; break; }
                if (!*result) continue;
                auto p = *result;
                CHECK(p->type == schema::frameset && p->frames.size() == 2);
                CHECK(p->header.sequence.value == count);
                for (size_t i = 0; i < 2; ++i) {
                    const auto &f = *p->frames[i];
                    CHECK(f.header.sequence.value == count);
                    CHECK(f.header.metadata.at("role") == (i ? "right" : "left"));
                    CHECK(f.header.timestamp.nanoseconds == int64_t(count) * (1000000000 / 120));
                    CHECK(f.header.calibration.id.value == "test.calibration" && f.header.calibration.revision == 7);
                    auto bytes = f.attributes[0].buffer.map_read(); CHECK(bytes);
                    if (std::string(scenario) == "y10p") {
                        CHECK(f.attributes[0].descriptor.name == data::packed_image_bytes);
                        CHECK(f.attributes[0].descriptor.shape == std::vector<uint64_t>{3840});
                        auto layout = data::image_layout(f);
                        CHECK(layout.packing == data::ImagePacking::mipi_raw10 && layout.width == 64 && layout.height == 48 && layout.row_stride == 80);
                    }
                    CHECK((*bytes)[0] == static_cast<std::byte>((count * 7u + (i ? 97u : 0u)) & 255u));
                }
                if (!retained) retained = p;
                ++count;
            }
            bool valid = std::string(scenario) == "normal" || std::string(scenario) == "y10p" || std::string(scenario) == "renumber" || std::string(scenario) == "eagain";
            CHECK(failed != valid);
            if (failed) CHECK(!stream->diagnostics().at("error").empty());
            CHECK(stream->stop());
            if (retained) CHECK((*retained->frames[0]->attributes[0].buffer.map_read())[0] == std::byte{0});
            std::cout << scenario << " passed\n";
        }
        unsetenv("MANTIS_X1_PROFILE"); unsetenv("MANTIS_X1_FAKE");
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
