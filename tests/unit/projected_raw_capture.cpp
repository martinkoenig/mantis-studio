#include "../../src/artifact-store/segments.hpp"
#include "projected_storage_values.hpp"
#include <fstream>
#include <future>
#include <iostream>
#include <mantis/projected_run.hpp>
#include <mantis/replay.hpp>
#include <sqlite3.h>
#include <thread>
#if defined(__unix__)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace storage_fixture;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__));                 \
    } while (false)
template <class F> void rejects(F f) {
    bool ok = false;
    try {
        f();
    } catch (const std::exception &) {
        ok = true;
    }
    CHECK(ok);
}
std::string file(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void write(const std::filesystem::path &p, const std::string &b, bool append = false) {
    std::ofstream out(p, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    out.write(b.data(), static_cast<std::streamsize>(b.size()));
    CHECK(out.good());
}
void u64(std::string &b, size_t p, uint64_t n) {
    CHECK(p + 8 <= b.size());
    for (unsigned i = 0; i < 8; ++i)
        b[p + i] = static_cast<char>(n >> (8 * i));
}
std::string record(const AcquisitionBundle &b) {
    std::ostringstream out;
    artifact::segments::append(out, b);
    return out.str();
}
std::string digest(const artifact::Store &s, const Id &id) {
    auto all = encode(s.capture_header(id));
    s.replay_bundles(id, [&](AcquisitionBundle b) { all += encode(b); });
    return content_hash({reinterpret_cast<const std::byte *>(all.data()), all.size()}).hex;
}
AcquisitionBundle evidence_only(uint64_t n, bool terminal = false) {
    auto b = bundle(0);
    b.key.sequence.value = n;
    b.evidence.key.ordinal.value = n * 4;
    b.published = host(static_cast<int64_t>(n) * 1000000);
    if (n)
        b.evidence.causal_predecessors = {{run, {(n - 1) * 4}}};
    if (terminal) {
        b.evidence.disposition = AcquisitionDisposition::cancelled;
        b.evidence.reason = AcquisitionReason::user_cancel;
    }
    return b;
}
int main() {
    try {
        auto root = std::filesystem::temp_directory_path() / Id::random().value;
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() { std::filesystem::remove_all(p); }
        } cleanup{root};
        std::filesystem::create_directories(root);
        Id finalized, empty;
        std::string before;
        {
            auto store = std::make_shared<artifact::Store>(root);
            device::ProjectedRunConfig config;
            auto converted = device::recorded_run_config(config);
            CHECK(converted.queue_capacity == config.queue_capacity &&
                  converted.publication_timeout_ms == config.publication_timeout_ms);
            artifact::Provenance provenance;
            provenance.parameters = {{"segment_max_bytes", "4096"}};
            finalized = store->begin_projected_capture(header(), provenance);
            CHECK(encode(store->capture_header(finalized)) == encode(header()));
            CHECK(store->get(finalized).bytes == encode(header()).size());
            CHECK(std::filesystem::exists(root / "objects" / finalized.value / "run.header"));
            CHECK(!std::filesystem::exists(root / "objects" / finalized.value / "run.header.part"));
            rejects([&] { store->append(finalized, *images()); });
            rejects([&] { store->begin({"org.mantis.RawCapture", 3}, {}); });
            rejects([&] { store->begin({"org.mantis.RawCapture", 4}, {}); });
            for (unsigned n = 0; n < 4; ++n)
                store->append_bundle(finalized, bundle(n));
            auto a = store->finalize(finalized);
            CHECK(a.state == artifact::ArtifactState::finalized && a.chunks > 1);
            uint64_t bytes = encode(header()).size();
            std::string hashes = content_hash({reinterpret_cast<const std::byte *>(encode(header()).data()),
                                               encode(header()).size()})
                                     .hex;
            for (uint64_t i = 0; i < a.chunks; ++i) {
                auto b = file(store->object_path(finalized, i));
                bytes += b.size();
                hashes += content_hash({reinterpret_cast<const std::byte *>(b.data()), b.size()}).hex;
            }
            CHECK(a.bytes == bytes);
            CHECK(a.hash ==
                  content_hash({reinterpret_cast<const std::byte *>(hashes.data()), hashes.size()}));
            auto summary = store->bundle_summary(finalized);
            CHECK(summary.records == 4 && summary.terminal == AcquisitionDisposition::failed &&
                  summary.reason == AcquisitionReason::device_failure);
            before = digest(*store, finalized);
            CHECK(before == digest(*store, finalized));
            // Activate, replace, then clear a project binding. None can alter recorded references.
            auto rev = store->begin_calibration({"org.mantis.RigCalibration", 1}, {});
            store->append(rev.artifact_id, *images()->frames[0]);
            store->finalize(rev.artifact_id);
            store->activate_calibration({"logical"}, rev.artifact_id);
            CHECK(store->active_calibration({"logical"}));
            auto newer = store->begin_calibration({"org.mantis.RigCalibration", 1}, {}, rev.reference.id);
            store->append(newer.artifact_id, *images()->frames[0]);
            store->finalize(newer.artifact_id);
            store->activate_calibration({"logical"}, newer.artifact_id);
            CHECK(digest(*store, finalized) == before);
            store->clear_active_calibration({"logical"});
            CHECK(digest(*store, finalized) == before);
            rejects([&] { store->packet(finalized); });
            rejects([&] { store->replay(finalized, [](Published) {}); });
            rejects([&] { artifact::CaptureReader r(store, finalized); });
            rejects([&] { device::recorded_source(store, finalized, false); });
            auto old = store->begin({"org.mantis.RawCapture", 2}, {});
            rejects([&] { store->append_bundle(old, bundle(0)); });
            store->abandon(old);
            empty = store->begin_projected_capture(
                header()); // exact header readable after reopening before ANY bundle
            store->abandon(empty);
            // Whole run without any image, failed/cancelled still FINALIZED.
            auto control = store->begin_projected_capture(header());
            for (unsigned n = 0; n < 4; ++n)
                store->append_bundle(control, evidence_only(n, n == 3));
            store->prepare_finalize(control);
            store->finalize(control);
            CHECK(store->bundle_summary(control).terminal == AcquisitionDisposition::cancelled);
            store->replay_bundles(
                control, [](AcquisitionBundle b) { CHECK(!b.frameset && b.evidence.frames.empty()); });
            // Appending invalid input never renumbers it or commits bytes.
            auto invalid = store->begin_projected_capture(header());
            store->append_bundle(invalid, bundle(0));
            auto reject_edit = [&](auto edit) {
                auto b = bundle(1);
                edit(b);
                auto size = store->get(invalid).bytes;
                rejects([&] { store->append_bundle(invalid, b); });
                CHECK(store->get(invalid).bytes == size);
            };
            reject_edit([](auto &b) { b.key.sequence.value = 2; });
            reject_edit([](auto &b) { b.key.sequence.value = 0; });
            reject_edit([](auto &b) {
                b.key.run_id = {{"different"}};
                b.evidence.key.run_id = b.key.run_id;
            });
            reject_edit([](auto &b) {
                b = evidence_only(1);
                b.key.run_id = {{"other"}};
                b.evidence.key.run_id = b.key.run_id;
                b.evidence.causal_predecessors.clear();
            });
            reject_edit([](auto &b) {
                b = evidence_only(1);
                b.evidence.participants.controllers.push_back({{"foreign"}});
            });
            reject_edit([](auto &b) { b.evidence.program.hash = Unavailable{}; });
            reject_edit([](auto &b) { b.evidence.program.content = Unknown{}; });
            reject_edit([](auto &b) { b.evidence.causal_predecessors = {{run, {2}}}; });
            reject_edit([](auto &b) {
                b.evidence.key.ordinal.value = 0;
                b.evidence.causal_predecessors.clear();
            });
            reject_edit([](auto &b) { b.published = host(-1); });
            store->append_bundle(invalid, bundle(1));
            store->append_bundle(invalid, bundle(2));
            store->append_bundle(invalid, bundle(3));
            rejects([&] { store->append_bundle(invalid, evidence_only(4)); });
            store->finalize(invalid);
            // Storage errors are separately reported; the verified prefix has no invented outcome.
            auto failure = store->begin_projected_capture(header());
            write(root / "objects" / failure.value / "0.segment.part", "");
            rejects([&] { store->append_bundle(failure, bundle(0)); });
            CHECK(store->get(failure).state == artifact::ArtifactState::recoverable);
            store->recover(failure);
            CHECK(!store->bundle_summary(failure).terminal);
        }
        {
            auto store = std::make_shared<artifact::Store>(root);
            CHECK(encode(store->capture_header(empty)) == encode(header()));
            CHECK(store->get(empty).chunks == 0);
            store->recover(empty);
            CHECK(!store->bundle_summary(empty).terminal);
            CHECK(digest(*store, finalized) == before);
            AcquisitionBundle retained;
            {
                artifact::BundleCaptureReader reader(store, finalized);
                CHECK(encode(reader.header()) == encode(header()));
                CHECK(reader.next());
                retained = *reader.next();
                while (reader.next()) {
                };
            }
            CHECK(retained.frameset);
            CHECK((*retained.frameset->frames[0]->attributes[0].buffer.map_read())[3] == std::byte{255});
            CHECK(encode(retained) == encode(bundle(1)));
            std::atomic<int64_t> ns{0};
            auto clock = [&] {
                return std::chrono::steady_clock::time_point{std::chrono::nanoseconds{ns.load()}};
            };
            device::BundleReplay asap(store, finalized, false), paced(store, finalized, true, clock);
            std::string a = encode(asap.header()), p = encode(paced.header());
            for (unsigned n = 0; n < 4; ++n) {
                auto x = asap.next();
                CHECK(x && *x);
                a += encode(**x);
                if (n) {
                    auto poll = paced.next();
                    CHECK(poll && !*poll);
                }
                ns = n * 1000000;
                paced.notify_clock_advanced();
                auto y = paced.next();
                CHECK(y && *y);
                p += encode(**y);
            }
            CHECK(a == p);
            CHECK(asap.next() && asap.finished());
            CHECK(paced.next() && paced.finished());
            std::promise<void> entered;
            std::atomic_bool report_wait{false};
            auto waiting_clock = [&] {
                if (report_wait.exchange(false))
                    entered.set_value();
                return clock();
            };
            device::BundleReplay waiting(store, finalized, true, waiting_clock);
            ns = 0;
            CHECK(waiting.next());
            report_wait = true;
            auto future = std::async(std::launch::async, [&] { return waiting.next(60000); });
            CHECK(entered.get_future().wait_for(1s) == std::future_status::ready);
            auto busy = waiting.next();
            CHECK(!busy && busy.error().code == Status::busy);
            waiting.stop();
            CHECK(future.wait_for(1s) == std::future_status::ready);
            CHECK(!future.get());
            device::BundleReplay advance(store, finalized, true, clock);
            CHECK(advance.next());
            auto delivery = std::async(std::launch::async, [&] { return advance.next(60000); });
            ns = 1000000;
            advance.notify_clock_advanced();
            CHECK(delivery.wait_for(1s) == std::future_status::ready);
            CHECK(delivery.get());
        }
        // Every incomplete trailing region truncates only to the verified prefix.
        for (auto tail : {std::string("MRAWREC3"), record(bundle(1)).substr(0, 100),
                          record(bundle(1)).substr(0, record(bundle(1)).size() - 3)}) {
            Id id;
            {
                artifact::Store store(root);
                id = store.begin_projected_capture(header());
                store.append_bundle(id, bundle(0));
                store.abandon(id);
            }
            auto path = root / "objects" / id.value / "0.segment.part";
            auto prefix = file(path);
            write(path, tail, true);
            {
                artifact::Store store(root);
                auto a = store.recover(id);
                CHECK(a.state == artifact::ArtifactState::finalized);
                CHECK(file(store.object_path(id)) == prefix);
                CHECK(store.bundle_summary(id).records == 1 && !store.bundle_summary(id).terminal);
            }
        }
        // Sealing failure reports storage failure, releases live writers and permits explicit retry.
        for (bool prepare : {false, true}) {
            artifact::Store store(root);
            auto id = store.begin_projected_capture(header());
            store.append_bundle(id, bundle(0));
            auto conflict = root / "objects" / id.value / "0.segment";
            std::filesystem::create_directory(conflict);
            rejects([&] {
                if (prepare)
                    store.prepare_finalize(id);
                else
                    store.finalize(id);
            });
            CHECK(store.get(id).state == artifact::ArtifactState::recoverable);
            std::filesystem::remove(conflict);
            store.recover(id);
            CHECK(!store.bundle_summary(id).terminal && store.bundle_summary(id).records == 1);
        }
        // Normal finalization requires terminal evidence; explicit recovery can preserve its absence.
        {
            artifact::Store store(root);
            auto id = store.begin_projected_capture(header());
            store.append_bundle(id, bundle(0));
            rejects([&] { store.finalize(id); });
            CHECK(store.get(id).state == artifact::ArtifactState::recoverable);
            store.recover(id);
            CHECK(!store.bundle_summary(id).terminal);
        }
        // Closed unindexed segment, retryable cancellation, terminal and no-terminal recovery.
        for (bool terminal : {false, true}) {
            Id id;
            {
                artifact::Store store(root);
                id = store.begin_projected_capture(header());
                for (unsigned n = 0; n < 4; ++n)
                    store.append_bundle(id, evidence_only(n, terminal && n == 3));
                store.abandon(id);
            }
            auto part = root / "objects" / id.value / "0.segment.part";
            auto closed = part;
            closed.replace_extension();
            std::filesystem::rename(part, closed);
            {
                artifact::Store store(root);
                CancellationToken token;
                token.cancel();
                rejects([&] { store.recover(id, token); });
                CHECK(store.get(id).state == artifact::ArtifactState::recoverable);
                store.recover(id);
                CHECK(store.bundle_summary(id).records == 4);
                CHECK(bool(store.bundle_summary(id).terminal) == terminal);
            }
        }
        // In-flight recovery cancellation leaves the same verified records retryable.
        {
            auto store = std::make_shared<artifact::Store>(root);
            auto h = header();
            h.program.bounds.max_events = 6000;
            h.config.max_correlation_entries = 6000;
            auto id = store->begin_projected_capture(h);
            for (uint64_t n = 0; n < 5000; ++n)
                store->append_bundle(id, evidence_only(n));
            store->abandon(id);
            CancellationToken token;
            auto recovering = std::async(std::launch::async, [&] {
                try {
                    store->recover(id, token);
                    return Status::ok;
                } catch (const Failure &e) {
                    return e.error.code;
                }
            });
            auto deadline = std::chrono::steady_clock::now() + 2s;
            while (store->get(id).state != artifact::ArtifactState::finalizing &&
                   std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            CHECK(store->get(id).state == artifact::ArtifactState::finalizing);
            token.cancel();
            CHECK(recovering.wait_for(2s) == std::future_status::ready &&
                  recovering.get() == Status::cancelled);
            CHECK(store->get(id).state == artifact::ArtifactState::recoverable);
            store->recover(id);
            CHECK(store->bundle_summary(id).records == 5000 && !store->bundle_summary(id).terminal);
        }
        // Corrupt complete records, semantic discontinuities and header corruption never get truncated.
        for (unsigned mode = 0; mode < 10; ++mode) {
            Id id;
            {
                artifact::Store store(root);
                id = store.begin_projected_capture(header());
                store.append_bundle(id, bundle(0));
                store.abandon(id);
            }
            auto path = root / "objects" / id.value / "0.segment.part";
            auto good = record(bundle(1));
            auto bad = good;
            if (mode == 0)
                bad.back() ^= 1; // complete bad footer
            if (mode == 1)
                bad[100] ^= 1; // checksum
            if (mode == 2)
                bad[24] ^= 1; // complement
            if (mode == 3) {
                auto b = bundle(1);
                b.key.sequence.value = 3;
                bad = record(b);
            }
            if (mode == 4) {
                auto b = bundle(1);
                b.evidence.causal_predecessors = {{run, {2}}};
                bad = record(b);
            }
            if (mode == 5) {
                auto b = bundle(1);
                b.evidence.program.hash = Unavailable{};
                bad = record(b);
            }
            if (mode == 6) {
                auto b = bundle(0);
                b.key.sequence.value = 1;
                b.evidence.key.ordinal.value = 4;
                bad = record(b);
                bad[0] = 'X';
            }
            if (mode == 7) {
                auto b = bundle(1);
                bad = record(b);
                auto pos = bad.find("org.mantis.AcquisitionBundle");
                CHECK(pos != std::string::npos);
                bad[pos] = 'X';
                auto payload = std::span<const std::byte>{
                    reinterpret_cast<const std::byte *>(bad.data() + 32), bad.size() - 40};
                auto h = content_hash(payload).hex;
                u64(bad, 16, std::stoull(h, nullptr, 16));
            } // valid checksum, invalid semantic type
            if (mode == 8) {
                auto hpath = root / "objects" / id.value / "run.header";
                auto h = file(hpath);
                h[40] ^= 1;
                write(hpath, h);
            }
            if (mode == 9) {
                auto b = evidence_only(1, true);
                write(path, record(b), true);
                bad = record(evidence_only(2));
            }
            if (mode != 8)
                write(path, bad, true);
            auto size = std::filesystem::file_size(path);
            {
                artifact::Store store(root);
                rejects([&] { store.recover(id); });
                CHECK(store.get(id).state == artifact::ArtifactState::recoverable);
                CHECK(std::filesystem::file_size(path) == size);
                auto scan = artifact::segments::scan(path, 3);
                CHECK(!scan.records.empty());
                CHECK(encode(artifact::segments::bundle(scan, 0)) == encode(bundle(0)));
            }
        }
        // Indexed segment hash mismatch and segment gaps are refused before changing bytes.
        for (bool gap : {false, true}) {
            Id id;
            {
                artifact::Store store(root);
                artifact::Provenance p;
                p.parameters = {{"segment_max_bytes", "4096"}};
                id = store.begin_projected_capture(header(), p);
                for (unsigned n = 0; n < 4; ++n)
                    store.append_bundle(id, bundle(n));
                store.abandon(id);
                CHECK(store.get(id).chunks > 0);
            }
            auto dir = root / "objects" / id.value;
            if (gap)
                std::filesystem::rename(dir / "0.segment", dir / "10.segment");
            else {
                auto b = file(dir / "0.segment");
                b[50] ^= 1;
                write(dir / "0.segment", b);
            }
            {
                artifact::Store store(root);
                rejects([&] { store.recover(id); });
                CHECK(store.get(id).state == artifact::ArtifactState::recoverable);
            }
        }
        // Pacing arithmetic/horizon failures preserve valid recorded semantics for ASAP replay.
        {
            auto store = std::make_shared<artifact::Store>(root);
            auto id = store->begin_projected_capture(header());
            auto first = evidence_only(0);
            first.published = host(INT64_MIN);
            auto last = evidence_only(1, true);
            last.published = host(INT64_MAX);
            store->append_bundle(id, first);
            store->append_bundle(id, last);
            store->finalize(id);
            device::BundleReplay paced(store, id, true);
            CHECK(paced.next());
            auto bad = paced.next();
            CHECK(!bad && bad.error().code == Status::corrupt);
            device::BundleReplay asap(store, id, false);
            CHECK(asap.next() && asap.next());
            auto near_max = std::chrono::steady_clock::time_point::max() - 1ns;
            device::BundleReplay overflow(store, finalized, true, [&] { return near_max; });
            CHECK(overflow.next());
            auto result = overflow.next();
            CHECK(!result && result.error().code == Status::corrupt);
        }
        // Last record may cross the normal 64-MiB segment target, without pixel conversion.
        {
            artifact::Store store(root);
            auto h = header();
            h.program.bounds.max_bytes = 200ull * 1024 * 1024;
            auto id = store.begin_projected_capture(h);
            auto b = bundle(1);
            b.key.sequence.value = 0;
            b.evidence.key.ordinal.value = 0;
            b.evidence.causal_predecessors.clear();
            auto set = *b.frameset;
            auto image = *set.frames[0];
            constexpr size_t size = 65 * 1024 * 1024;
            memory::BufferBuilder large(size);
            std::fill(large.writable().begin(), large.writable().end(), std::byte{19});
            image.attributes = {{{"org.mantis.pixels", schema::ScalarType::u8, {size}, {1}, "byte"},
                                 std::move(large).publish()}};
            set.frames = {publish(std::move(image))};
            b.frameset = publish(std::move(set));
            store.append_bundle(id, b);
            CHECK(store.get(id).chunks == 1);
            CHECK(std::filesystem::file_size(store.object_path(id)) > artifact::segments::max_segment_bytes);
            auto terminal = evidence_only(1, true);
            store.append_bundle(id, terminal);
            store.finalize(id);
            auto replay = store.bundle(id);
            auto bytes = replay.frameset->frames[0]->attributes[0].buffer.map_read();
            CHECK(bytes && bytes->size() == size && bytes->front() == std::byte{19} &&
                  bytes->back() == std::byte{19});
        }
#if defined(__unix__)
        // Real process termination with complete records still in the OS-visible active tail.
        auto killroot = root / "killed";
        int channel[2];
        CHECK(pipe(channel) == 0);
        auto pid = fork();
        CHECK(pid >= 0);
        if (pid == 0) {
            close(channel[0]);
            try {
                artifact::Store store(killroot);
                auto id = store.begin_projected_capture(header());
                for (unsigned n = 0; n < 4; ++n)
                    store.append_bundle(id, evidence_only(n));
                auto text = id.value;
                CHECK(::write(channel[1], text.data(), text.size()) == static_cast<ssize_t>(text.size()));
                for (;;)
                    pause();
            } catch (...) {
                _exit(2);
            }
        }
        close(channel[1]);
        char identity[256]{};
        auto size = read(channel[0], identity, sizeof(identity));
        close(channel[0]);
        CHECK(size > 0);
        CHECK(kill(pid, SIGKILL) == 0);
        int status{};
        CHECK(waitpid(pid, &status, 0) == pid && WIFSIGNALED(status));
        {
            artifact::Store store(killroot);
            Id id{std::string(identity, static_cast<size_t>(size))};
            CHECK(store.get(id).state == artifact::ArtifactState::recoverable);
            store.recover(id);
            CHECK(store.bundle_summary(id).records == 4 && !store.bundle_summary(id).terminal);
        }
#endif
        std::cout << "Schema-3 durability, mapped lifetime, continuity, recovery, process kill, calibration "
                     "isolation and replay digest "
                  << before << " passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
