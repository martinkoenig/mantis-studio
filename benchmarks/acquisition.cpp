#include <chrono>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/image_layout.hpp>
#include <mantis/pipeline_api.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#ifdef __linux__
#include <sys/resource.h>
#endif
using namespace mantis;
int main(int argc, char **argv) {
    try {
        const auto directory = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path();
        const uint64_t count = argc > 2 ? std::stoull(argv[2]) : 64;
        if (!count || count > 10000) throw std::runtime_error("Record count must be 1..10000");
        auto root = directory / ("mantis-benchmark-" + Id::random().value + ".mantis");
        struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); } } cleanup{root};
        const std::string fourcc = argc > 3 ? argv[3] : "GREY";
        if (fourcc != "GREY" && fourcc != "Y10P") throw std::runtime_error("Benchmark mode must be GREY or Y10P");
        const bool packed = fourcc == "Y10P";
        const size_t width = 1280, height = packed ? 720 : 800, stride = packed ? width / 4 * 5 : width, payload = stride * height * 2;
        std::vector<data::Published> images;
        auto copy_start = std::chrono::steady_clock::now();
        std::vector<std::byte> fixture(stride * height, std::byte{71});
        for (unsigned role = 0; role < 2; ++role) {
            data::Packet image; image.type = schema::image;
            image.header.metadata = {{"role", role ? "right" : "left"}, {"identity", role ? "fixture-b" : "fixture-a"}, {"fourcc", fourcc}};
            if (packed) {
                image.header.metadata.insert({{"org.mantis.image.layout", "mipi-raw10-v1"}, {"org.mantis.image.bits_per_sample", "10"},
                    {"org.mantis.image.width", std::to_string(width)}, {"org.mantis.image.height", std::to_string(height)},
                    {"org.mantis.image.row_stride_bytes", std::to_string(stride)}});
                image.attributes.push_back({{std::string(data::packed_image_bytes), schema::ScalarType::u8, {stride * height}, {1}, "byte"}, memory::copy(fixture)});
            } else image.attributes.push_back({{"org.mantis.pixels", schema::ScalarType::u8, {height, width}, {width, 1}, "intensity"}, memory::copy(fixture)});
            images.push_back(data::publish(std::move(image)));
        }
        auto seconds = [](auto begin) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count(); };
        auto copy_seconds = seconds(copy_start);
        auto build = [&](uint64_t sequence) {
            data::Packet packet; packet.type = schema::frameset; packet.header.sequence.value = sequence;
            packet.frames = images; return data::publish(std::move(packet));
        };
        constexpr uint64_t small_count = 10000;
        auto construction_start = std::chrono::steady_clock::now();
        data::Published last;
        for (uint64_t i = 0; i < small_count; ++i) last = build(i);
        auto construction_seconds = seconds(construction_start);
        pipeline::BoundedQueue<data::Published> queue(32, pipeline::QueuePolicy::lossless);
        auto queue_start = std::chrono::steady_clock::now();
        std::jthread producer([&] { for (uint64_t i = 0; i < small_count; ++i) queue.push(build(i)); queue.close(); });
        uint64_t received{};
        while (auto packet = queue.pop()) { if ((*packet)->header.sequence.value != received++) throw std::runtime_error("Queue order mismatch"); }
        producer.join(); auto queue_seconds = seconds(queue_start);
        artifact::Store store(root); auto capture = store.begin({"org.mantis.RawCapture", 2}, {});
        auto write_start = std::chrono::steady_clock::now();
        for (uint64_t i = 0; i < count; ++i) store.append(capture, *build(i));
        auto append_seconds = seconds(write_start);
        auto finalization_start = std::chrono::steady_clock::now(); auto artifact = store.finalize(capture);
        auto finalization_seconds = seconds(finalization_start);
        auto replay_start = std::chrono::steady_clock::now(); uint64_t replayed{};
        store.replay(capture, [&](data::Published packet) {
            if (packet->header.sequence.value != replayed++) throw std::runtime_error("Replay order mismatch");
        });
        auto replay_seconds = seconds(replay_start);
        auto index_start = std::chrono::steady_clock::now();
        auto indexed_count = store.record_count(capture); auto index_seconds = seconds(index_start);
        nlohmann::json result{{"fourcc", fourcc}, {"row_stride_bytes", stride}, {"width", width}, {"height", height}, {"framesets", count}, {"payload_bytes", count * payload},
            {"container_bytes", artifact.bytes}, {"segments", artifact.chunks}, {"sqlite_segment_commits", artifact.chunks},
            {"frameset_construction_ns", construction_seconds * 1e9 / small_count},
            {"queue_items_per_second", small_count / queue_seconds}, {"queue_high_water", queue.metrics().high_water},
            {"queue_capacity", 32}, {"queue_dropped", queue.metrics().dropped},
            {"fixture_initial_copy_seconds", copy_seconds}, {"fixture_acquisition_copies", 1},
            {"buffer_mode", "owned fixture buffers reused; no physical V4L2 measurement"},
            {"append_seconds", append_seconds}, {"append_payload_mb_s", double(count * payload) / append_seconds / 1e6},
            {"finalization_seconds", finalization_seconds},
            {"write_and_finalize_payload_mb_s", double(count * payload) / (append_seconds + finalization_seconds) / 1e6},
            {"replay_seconds", replay_seconds}, {"verified_replay_payload_mb_s", double(count * payload) / replay_seconds / 1e6},
            {"validated_index_scan_seconds", index_seconds}, {"indexed_framesets", indexed_count},
            {"theoretical_selected_dual_120fps_mb_s", double(payload) * 120 / 1e6}, {"theoretical_selected_dual_120fps_mib_s", double(payload) * 120 / 1048576}};
#ifdef __linux__
        rusage usage{}; if (getrusage(RUSAGE_SELF, &usage) == 0) result["process_peak_rss_bytes"] = uint64_t(usage.ru_maxrss) * 1024;
#endif
        std::cout << result.dump(2) << '\n';
        return replayed == count && indexed_count == count ? 0 : 1;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
