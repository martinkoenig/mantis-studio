#include <fstream>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/data_io.hpp>
#include <mantis/platform.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include "../../src/artifact-store/segments.hpp"
using namespace mantis;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("Check failed: ") + #x); } while (false)
template<class F> void rejects(F f) { bool ok = false; try { f(); } catch (const std::exception &) { ok = true; } CHECK(ok); }
data::Published frame_set(uint64_t n) {
    data::Packet parent; parent.type = schema::frameset;
    parent.header.sequence.value = n; parent.header.calibration = {{"calibration.test"}, 1, 9};
    parent.header.sync = {{"sync.test"}, n}; parent.header.sync_quality = time::SyncQuality::software;
    for (unsigned role = 0; role < 2; ++role) {
        data::Packet image; image.type = schema::image; image.header = parent.header;
        image.header.sequence.value = n + 100;
        image.header.timestamp = {int64_t(n * 8333333), {{"clock.test"}, "fixture"}};
        image.header.received.nanoseconds = int64_t(n * 8333333 + role * 101);
        image.header.metadata = {{"role", role ? "right" : "left"}, {"identity", role ? "sensor-b" : "sensor-a"}, {"fourcc", "GREY"}};
        memory::BufferBuilder buffer(64);
        std::fill(buffer.writable().begin(), buffer.writable().end(), static_cast<std::byte>(n + role));
        image.attributes.push_back({{"org.mantis.pixels", schema::ScalarType::u8, {8, 8}, {8, 1}, "intensity"}, std::move(buffer).publish()});
        parent.frames.push_back(data::publish(std::move(image)));
    }
    return data::publish(std::move(parent));
}
void equal(const data::Packet &a, const data::Packet &b) {
    CHECK(a.type == b.type); CHECK(a.header.sequence.value == b.header.sequence.value);
    CHECK(a.header.timestamp.nanoseconds == b.header.timestamp.nanoseconds);
    CHECK(a.header.timestamp.domain.id == b.header.timestamp.domain.id);
    CHECK(a.header.received.nanoseconds == b.header.received.nanoseconds);
    CHECK(a.header.sync.id == b.header.sync.id && a.header.sync.trigger == b.header.sync.trigger);
    CHECK(a.header.sync_quality == b.header.sync_quality);
    CHECK(a.header.calibration.id == b.header.calibration.id && a.header.calibration.revision == b.header.calibration.revision);
    CHECK(a.header.frame.id == b.header.frame.id && a.header.frame.name == b.header.frame.name);
    CHECK(a.header.timestamp.domain.name == b.header.timestamp.domain.name);
    CHECK(a.header.calibration.schema_version == b.header.calibration.schema_version);
    CHECK(a.header.metadata == b.header.metadata); CHECK(a.frames.size() == b.frames.size());
    CHECK(a.attributes.size() == b.attributes.size());
    for (size_t i = 0; i < a.attributes.size(); ++i) {
        const auto &x = a.attributes[i].descriptor, &y = b.attributes[i].descriptor;
        CHECK(x.name == y.name && x.scalar == y.scalar && x.shape == y.shape && x.stride == y.stride && x.unit == y.unit);
        CHECK(std::ranges::equal(*a.attributes[i].buffer.map_read(), *b.attributes[i].buffer.map_read()));
    }
    for (size_t i = 0; i < a.frames.size(); ++i) equal(*a.frames[i], *b.frames[i]);
}
int main() {
    try {
        auto root = std::filesystem::temp_directory_path() / Id::random().value;
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{root};
        Id finalized, interrupted;
        {
            artifact::Store store(root);
            artifact::Provenance provenance; provenance.parameters = {{"segment_max_bytes", "8192"}, {"profile", "fixture"}};
            finalized = store.begin({"org.mantis.RawCapture", 2}, provenance);
            for (uint64_t i = 0; i < 20; ++i) store.append(finalized, *frame_set(i));
            auto a = store.finalize(finalized);
            CHECK(a.chunks < 20 && a.chunks > 1); CHECK(a.provenance.parameters.at("profile") == "fixture");
            CHECK(a.provenance.calibration.id == Id{"calibration.test"});
            CHECK(a.provenance.calibration.revision == 9);
            auto observations = nlohmann::json::parse(a.provenance.parameters.at("initial_observations"));
            CHECK(observations.size() == 2);
            CHECK(observations[0]["clock_domain"]["id"] == "clock.test");
            CHECK(observations[1]["metadata"]["identity"] == "sensor-b");
            CHECK(store.record_count(finalized) == 20);
            for (unsigned pass = 0; pass < 2; ++pass) {
                uint64_t sequence{};
                store.replay(finalized, [&](data::Published p) { equal(*p, *frame_set(sequence++)); });
                CHECK(sequence == 20);
            }
            equal(*store.packet(finalized, 7), *frame_set(7));
            rejects([&] { store.append(finalized, *frame_set(20)); });
            interrupted = store.begin({"org.mantis.RawCapture", 2}, {});
            for (uint64_t i = 0; i < 4; ++i) store.append(interrupted, *frame_set(i));
            store.abandon(interrupted);
        }
        auto tail = root / "objects" / interrupted.value / "0.segment.part";
        { std::ofstream out(tail, std::ios::binary | std::ios::app); out.write("MRAWREC2", 8); }
        auto scan = artifact::segments::scan(tail);
        CHECK(scan.incomplete && scan.corruption.empty() && scan.records.size() == 4);
        {
            artifact::Store store(root);
            CHECK(store.get(interrupted).state == artifact::ArtifactState::recoverable);
            CHECK(store.get(finalized).state == artifact::ArtifactState::finalized);
            auto recovered = store.recover(interrupted);
            CHECK(recovered.state == artifact::ArtifactState::finalized);
            CHECK(store.record_count(interrupted) == 4);
            equal(*store.packet(interrupted, 3), *frame_set(3));
            CHECK(std::filesystem::file_size(root / "objects" / interrupted.value / "0.segment") == scan.complete_bytes);
        }
        // Payload and length corruption cannot be silently reclassified as incomplete.
        auto corrupt = root / "corrupt.segment";
        { std::ofstream out(corrupt, std::ios::binary); artifact::segments::append(out, *frame_set(0)); artifact::segments::append(out, *frame_set(1)); }
        auto valid = artifact::segments::scan(corrupt); CHECK(valid.records.size() == 2);
        {
            std::fstream out(corrupt, std::ios::binary | std::ios::in | std::ios::out);
            out.seekp(static_cast<std::streamoff>(valid.records[1].offset + valid.records[1].bytes - 1)); out.put(99);
        }
        auto invalid = artifact::segments::scan(corrupt);
        CHECK(!invalid.corruption.empty() && invalid.records.size() == 1);
        equal(*artifact::segments::packet(invalid, 0), *frame_set(0));
        auto truncated = root / "truncated.segment.part";
        { std::ofstream out(truncated, std::ios::binary); artifact::segments::append(out, *frame_set(0)); artifact::segments::append(out, *frame_set(1)); }
        std::filesystem::resize_file(truncated, std::filesystem::file_size(truncated) - 30);
        auto partial = artifact::segments::scan(truncated); CHECK(partial.incomplete && partial.records.size() == 1);
        // An unindexed closed segment is discoverable after restart/recovery.
        Id unindexed;
        { artifact::Store store(root); unindexed = store.begin({"org.mantis.RawCapture", 2}, {}); store.append(unindexed, *frame_set(0)); store.abandon(unindexed); }
        auto part = root / "objects" / unindexed.value / "0.segment.part";
        auto closed = part; closed.replace_extension(); std::filesystem::rename(part, closed);
        { artifact::Store store(root); CHECK(store.recover(unindexed).chunks == 1); equal(*store.packet(unindexed), *frame_set(0)); }
        // A complete corrupt record is refused by the Store, preserving earlier bytes.
        Id broken;
        { artifact::Store store(root); broken = store.begin({"org.mantis.RawCapture", 2}, {}); store.append(broken, *frame_set(0)); store.append(broken, *frame_set(1)); store.abandon(broken); }
        auto broken_tail = root / "objects" / broken.value / "0.segment.part";
        auto broken_scan = artifact::segments::scan(broken_tail);
        {
            std::fstream out(broken_tail, std::ios::binary | std::ios::in | std::ios::out);
            out.seekp(static_cast<std::streamoff>(broken_scan.records[1].offset + broken_scan.records[1].bytes - 1)); out.put(99);
        }
        auto broken_size = std::filesystem::file_size(broken_tail);
        {
            artifact::Store store(root);
            rejects([&] { store.recover(broken); });
            CHECK(store.get(broken).state == artifact::ArtifactState::recoverable);
            CHECK(std::filesystem::file_size(broken_tail) == broken_size);
            equal(*artifact::segments::packet(artifact::segments::scan(broken_tail), 0), *frame_set(0));
        }
        // An index checksum mismatch is never silently repaired or accepted.
        Id mismatch;
        {
            artifact::Store store(root);
            artifact::Provenance p; p.parameters = {{"segment_max_bytes", "4096"}};
            mismatch = store.begin({"org.mantis.RawCapture", 2}, p);
            for (uint64_t i = 0; i < 6; ++i) store.append(mismatch, *frame_set(i));
            store.abandon(mismatch); CHECK(store.get(mismatch).chunks > 0);
        }
        sqlite3 *db{};
        CHECK(sqlite3_open((root / "project.sqlite").string().c_str(), &db) == SQLITE_OK);
        auto sql = "UPDATE chunks SET hash='0000000000000000' WHERE artifact='" + mismatch.value + "' AND idx=0";
        int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        sqlite3_close(db); CHECK(rc == SQLITE_OK);
        { artifact::Store store(root); rejects([&] { store.recover(mismatch); }); CHECK(store.get(mismatch).state == artifact::ArtifactState::recoverable); }
        // Cancellation leaves a retryable capture and does not fabricate records.
        Id cancelled;
        { artifact::Store store(root); cancelled = store.begin({"org.mantis.RawCapture", 2}, {}); store.append(cancelled, *frame_set(0)); store.abandon(cancelled); }
        {
            artifact::Store store(root); CancellationToken token; token.cancel();
            rejects([&] { store.recover(cancelled, token); });
            CHECK(store.get(cancelled).state == artifact::ArtifactState::recoverable);
            CHECK(store.recover(cancelled).state == artifact::ArtifactState::finalized);
            CHECK(store.record_count(cancelled) == 1);
        }
        // The original schema-v1 project manifest and packet path remain readable.
        { artifact::Store store(root); auto old = store.begin({"org.mantis.RawCapture", 1}, {}); store.append(old, *frame_set(0)->frames[0]); store.finalize(old); CHECK(store.packet(old)->type == schema::image); }
        { std::ifstream in(root / "manifest.json"); CHECK(nlohmann::json::parse(in).at("version") == 1); }
        std::cout << "Segment storage, prefix recovery, legacy compatibility and two byte-identical replays passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
