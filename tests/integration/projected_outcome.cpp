#include "../../src/artifact-store/segments.hpp"
#include "../unit/projected_storage_values.hpp"
#include <fstream>
#include <iostream>
#include <mantis/artifact_store.hpp>
#include <mantis/projected_run.hpp>
#include <mantis/replay.hpp>
using namespace storage_fixture;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__));                 \
    } while (false)
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const Failure &) {
        rejected = true;
    }
    CHECK(rejected);
}
template <class F> void corrupt(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const Failure &e) {
        rejected = e.error.code == Status::corrupt;
    }
    CHECK(rejected);
}
std::string file(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void write(const std::filesystem::path &p, std::string_view b) {
    std::ofstream out(p, std::ios::binary);
    out.write(b.data(), static_cast<std::streamsize>(b.size()));
    CHECK(out.good());
}
memory::BufferView view(std::string_view b) {
    return memory::copy({reinterpret_cast<const std::byte *>(b.data()), b.size()});
}
void u64(std::string &b, size_t pos, uint64_t n) {
    CHECK(pos + 8 <= b.size());
    for (unsigned i = 0; i < 8; ++i)
        b[pos + i] = static_cast<char>(n >> (i * 8));
}
void checksum(std::string &b) {
    auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte *>(b.data() + 32), b.size() - 48};
    u64(b, 24, std::stoull(content_hash(bytes).hex, nullptr, 16));
}
AcquisitionBundle completed(uint64_t seq = 0) {
    auto b = bundle(0);
    b.key.sequence.value = seq;
    b.evidence.key.ordinal.value = seq * 4;
    b.published = host(static_cast<int64_t>(seq) * 1000000);
    if (seq)
        b.evidence.causal_predecessors = {{run, {(seq - 1) * 4}}};
    b.evidence.disposition = AcquisitionDisposition::completed;
    return b;
}
void finalized_identity_tests(const std::filesystem::path &root) {
    const auto project = root / "finalized-identity";
    for (unsigned mode = 0; mode < 4; ++mode) {
        auto store = std::make_shared<artifact::Store>(project);
        auto id = store->begin_projected_capture(header());
        store->append_bundle(id, completed());
        const auto original = outcome(RecordedRunDisposition::completed, AcquisitionReason::none);
        store->record_run_outcome(id, {1, original});
        store->finalize(id);
        CHECK(store->run_outcome(id) && store->bundle_summary(id).records == 1);
        const auto directory = project / "objects" / id.value;
        const auto h = file(directory / "run.header"), o = file(directory / "run.outcome");
        if (mode == 0)
            std::filesystem::remove(directory / "run.outcome");
        if (mode == 1) {
            auto changed = original;
            changed.diagnostic = "internally valid replacement";
            const auto bytes = encode(ProjectedCaptureOutcome{1, changed});
            CHECK(read_run_outcome(view(bytes)));
            write(directory / "run.outcome", bytes);
        }
        if (mode == 2) {
            auto changed = header();
            ++changed.config.queue_capacity;
            const auto bytes = encode(changed);
            CHECK(read_capture_header(view(bytes)).config.queue_capacity == changed.config.queue_capacity);
            write(directory / "run.header", bytes);
        }
        if (mode == 3) {
            // Adding a valid outcome to an intentionally outcome-less recovered artifact is also corruption.
            auto recovered = store->begin_projected_capture(header());
            store->append_bundle(recovered, completed());
            store->abandon(recovered);
            store->recover(recovered);
            CHECK(!store->run_outcome(recovered) && !store->bundle_summary(recovered).final_outcome);
            write(project / "objects" / recovered.value / "run.outcome",
                  encode(ProjectedCaptureOutcome{1, original}));
            corrupt([&] { (void)store->run_outcome(recovered); });
            continue;
        }
        store.reset();
        store = std::make_shared<artifact::Store>(project);
        corrupt([&] { (void)store->run_outcome(id); });
        corrupt([&] { (void)store->capture_header(id); });
        corrupt([&] { (void)store->bundle_summary(id); });
        corrupt([&] { (void)store->bundle(id); });
        corrupt([&] { artifact::BundleCaptureReader reader(store, id); });
        corrupt([&] { device::BundleReplay replay(store, id, false); });
        corrupt([&] { store->replay_bundles(id, [](auto) {}); });
        write(directory / "run.header", h);
        write(directory / "run.outcome", o);
        CHECK(store->run_outcome(id) && store->bundle_summary(id).records == 1);
    }
}
void persisted_bound_tests(const std::shared_ptr<artifact::Store> &store, const std::filesystem::path &root) {
    // Exactly six events: three bundles plus three actual triggers. References in bundle 1 add none.
    auto h = header();
    h.program.bounds.max_events = 6;
    auto id = store->begin_projected_capture(h);
    store->append_bundle(id, bundle(0));
    store->append_bundle(id, bundle(1));
    auto extra = bundle(2);
    auto t = extra.triggers.front();
    t.key.sequence.value = 3;
    extra.triggers.push_back(t);
    extra.evidence.triggers.push_back(t.key);
    CHECK(data::validate(extra));
    rejects([&] { store->append_bundle(id, extra); });
    CHECK(store->get(id).state == artifact::ArtifactState::open);
    store->append_bundle(id, bundle(2));
    store->record_run_outcome(id, {3, outcome()});
    store->finalize(id);
    CHECK(store->bundle_summary(id).records == 3);
    // A valid byte codec cannot bypass event bounds during recovery or final verification.
    auto bad = store->begin_projected_capture(h);
    store->append_bundle(bad, bundle(0));
    store->append_bundle(bad, bundle(1));
    store->abandon(bad);
    auto path = root / "objects" / bad.value / "0.segment.part";
    {
        std::ofstream out(path, std::ios::binary | std::ios::app);
        artifact::segments::append(out, extra);
    }
    auto before = file(path);
    rejects([&] { store->recover(bad); });
    CHECK(file(path) == before && store->get(bad).state == artifact::ArtifactState::recoverable);
    h.program.bounds.max_events = 7;
    auto final = store->begin_projected_capture(h);
    for (unsigned n = 0; n < 4; ++n)
        store->append_bundle(final, bundle(n));
    store->record_run_outcome(final, {4, outcome()});
    store->prepare_finalize(final);
    h.program.bounds.max_events = 6;
    write(root / "objects" / final.value / "run.header", encode(h));
    rejects([&] { store->finalize(final); });
    CHECK(store->get(final).state == artifact::ArtifactState::recoverable);
    // Unique command IDs, including late identical evidence; rejected input leaves count/state untouched.
    h = header();
    h.program.bounds.max_commands = 2;
    auto command_bundle = [](uint64_t n, const char *request) {
        auto b = completed(n);
        b.evidence.disposition = AcquisitionDisposition::startup;
        b.evidence.emitters[0].commanded = EmitterCommand{{{request}}, emitter, EmitterState::off, host()};
        return b;
    };
    id = store->begin_projected_capture(h);
    store->append_bundle(id, command_bundle(0, "one"));
    store->append_bundle(id, command_bundle(1, "two"));
    store->append_bundle(id, command_bundle(2, "one"));
    auto contradictory = command_bundle(3, "one");
    auto changed_command = *contradictory.evidence.emitters[0].commanded.get();
    changed_command.state = EmitterState::on;
    contradictory.evidence.emitters[0].commanded = changed_command;
    rejects([&] { store->append_bundle(id, contradictory); });
    auto third = command_bundle(3, "three");
    rejects([&] { store->append_bundle(id, third); });
    CHECK(store->get(id).state == artifact::ArtifactState::open);
    store->append_bundle(id, command_bundle(3, "two"));
    store->record_run_outcome(id, {4, outcome()});
    store->finalize(id);
    CHECK(store->bundle_summary(id).records == 4);
    bad = store->begin_projected_capture(h);
    store->append_bundle(bad, command_bundle(0, "one"));
    store->append_bundle(bad, command_bundle(1, "two"));
    store->append_bundle(bad, command_bundle(2, "one"));
    store->abandon(bad);
    path = root / "objects" / bad.value / "0.segment.part";
    {
        std::ofstream out(path, std::ios::binary | std::ios::app);
        artifact::segments::append(out, third);
    }
    before = file(path);
    rejects([&] { store->recover(bad); });
    CHECK(file(path) == before);
}
void abort_semantic_tests() {
    for (const auto error :
         {RecordedExecutorError{0, 1}, RecordedExecutorError{8, 1}, RecordedExecutorError{7, 0}}) {
        auto o = detailed_outcome();
        o.abort_outcome->error = error;
        rejects([&] { (void)encode(ProjectedCaptureOutcome{0, o}); });
        // Recompute checksum over independently malformed executor-error fields; decode must still reject.
        auto b = encode(ProjectedCaptureOutcome{0, detailed_outcome()});
        const auto category = b.find("cleanup fault") - 24;
        u64(b, category, error.category);
        u64(b, category + 8, error.code);
        checksum(b);
        rejects([&] { (void)read_run_outcome(view(b)); });
    }
    auto on = detailed_outcome();
    auto changed_command = *on.abort_outcome->emitters[0].commanded.get();
    changed_command.state = EmitterState::on;
    on.abort_outcome->emitters[0].commanded = changed_command;
    rejects([&] { (void)encode(ProjectedCaptureOutcome{0, on}); });
    auto b = encode(ProjectedCaptureOutcome{0, detailed_outcome()});
    b[b.find("off-request") + std::string_view("off-request").size() + 8 + 4] = char(1);
    checksum(b);
    rejects([&] { (void)read_run_outcome(view(b)); });
    for (unsigned presence = 0; presence < 3; ++presence) {
        auto o = detailed_outcome();
        if (presence == 0)
            o.abort_outcome->emitters[0].commanded = Unknown{};
        if (presence == 1)
            o.abort_outcome->emitters[0].commanded = Unavailable{};
        auto encoded = encode(ProjectedCaptureOutcome{0, o});
        CHECK(encode(*read_run_outcome(view(encoded))) == encoded);
    }
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        auto root = std::filesystem::temp_directory_path() / Id::random().value;
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() { std::filesystem::remove_all(p); }
        } cleanup{root};
        std::filesystem::create_directories(root);
        const auto golden = file(std::filesystem::path(argv[1]) / "run-outcome3.bin");
        CHECK(golden == encode(ProjectedCaptureOutcome{4, detailed_outcome()}));
        auto decoded = read_run_outcome(view(golden));
        CHECK(decoded && encode(*decoded) == golden);
        const auto &abort = *decoded->outcome.abort_outcome;
        CHECK(abort.run.get() && *abort.run.get() == run);
        CHECK(abort.fenced_generation.presence() == Presence::unavailable);
        CHECK(abort.inhibited.get() && *abort.inhibited.get());
        CHECK(abort.stale_work_fenced.presence() == Presence::unknown);
        CHECK(abort.off_requested.get() && *abort.off_requested.get());
        CHECK(abort.error.category == 7 && abort.error.code == 23);
        CHECK(abort.emitters[0].commanded.get()->state == EmitterState::off);
        CHECK(abort.emitters[0].acknowledged.presence() == Presence::unavailable);
        CHECK(abort.emitters[0].observed.presence() == Presence::unknown);
        CHECK(abort.emitters[0].exposure_effective[0].state.presence() == Presence::unknown);
        CHECK(abort.emitters[0].exposure_effective[1].state.presence() == Presence::unavailable);
        for (size_t cut = 0; cut < golden.size(); ++cut) {
            auto partial = view(std::string_view(golden).substr(0, cut));
            CHECK(!read_run_outcome(partial, true));
            rejects([&] { (void)read_run_outcome(partial); });
        }
        // Established false and Unknown/Unavailable are all independently representable.
        for (unsigned p = 0; p < 3; ++p) {
            auto value = detailed_outcome();
            auto a = *value.abort_outcome;
            if (p == 0) {
                a.inhibited = false;
                a.stale_work_fenced = false;
                a.off_requested = false;
            }
            if (p == 1) {
                a.run = Unavailable{};
                a.fenced_generation = Unknown{};
                a.inhibited = Unknown{};
                a.off_requested = Unavailable{};
            }
            if (p == 2) {
                a.run = Unknown{};
                a.fenced_generation = value.generation;
                a.stale_work_fenced = true;
            }
            value.abort_outcome = a;
            auto bytes = encode(ProjectedCaptureOutcome{0, value});
            auto round = read_run_outcome(view(bytes));
            CHECK(round && encode(*round) == bytes);
            if (p == 0)
                CHECK(round->outcome.abort_outcome->off_requested.get() &&
                      !*round->outcome.abort_outcome->off_requested.get());
        }
        { // Outer absence is structural; it is never Unknown evidence.
            auto o = outcome(RecordedRunDisposition::completed, AcquisitionReason::none);
            auto round = read_run_outcome(view(encode(ProjectedCaptureOutcome{0, o})));
            CHECK(round && !round->outcome.initiating_error && !round->outcome.abort_error &&
                  !round->outcome.stop_error && !round->outcome.close_error && !round->outcome.abort_outcome);
        }
        // An exact L3 snapshot fixture cannot be serialized while FAILED cleanup is still pending.
        device::ProjectedRunSnapshot snapshot;
        snapshot.identity = {run, header().generation};
        snapshot.state = device::ProjectedState::failed;
        snapshot.terminal.reason = AcquisitionReason::rejected;
        snapshot.terminal.initiating_error = Error{Status::incompatible, "prepare", "executor"};
        rejects([&] { (void)device::recorded_run_outcome(snapshot); });
        snapshot.cleanup_resolved = true;
        CHECK(device::recorded_run_outcome(snapshot).outcome.initiating_error->code == Status::incompatible);
        auto store = std::make_shared<artifact::Store>(root);
        auto empty = store->begin_projected_capture(header());
        rejects([&] { store->finalize(empty); });
        rejects([&] { store->prepare_finalize(empty); });
        CHECK(store->get(empty).state == artifact::ArtifactState::open);
        store->record_run_outcome(empty, device::recorded_run_outcome(snapshot));
        CHECK(store->finalize(empty).state == artifact::ArtifactState::finalized);
        CHECK(store->bundle_summary(empty).records == 0 &&
              store->bundle_summary(empty).final_outcome->disposition == RecordedRunDisposition::failed);
        // Executor completion is ordinary evidence; only the separate daemon outcome closes recording.
        auto id = store->begin_projected_capture(header());
        store->append_bundle(id, completed());
        rejects([&] { store->finalize(id); });
        rejects([&] { store->prepare_finalize(id); });
        CHECK(store->get(id).state == artifact::ArtifactState::open && !store->run_outcome(id));
        auto later = completed(1);
        later.evidence.disposition = AcquisitionDisposition::startup;
        store->append_bundle(id, later);
        auto o = detailed_outcome();
        auto wrong = o;
        wrong.generation = {{"foreign"}};
        rejects([&] { store->record_run_outcome(id, {2, wrong}); });
        CHECK(store->get(id).state == artifact::ArtifactState::open);
        store->record_run_outcome(id, {2, o});
        rejects([&] { store->record_run_outcome(id, {2, o}); });
        rejects([&] { store->append_bundle(id, completed(2)); });
        CHECK(store->get(id).state == artifact::ArtifactState::open);
        store->finalize(id);
        CHECK(encode(store->bundle(id)) == encode(completed()));
        CHECK(encode(ProjectedCaptureOutcome{2, *store->run_outcome(id)}) ==
              encode(ProjectedCaptureOutcome{2, o}));
        {
            artifact::BundleCaptureReader reader(store, id);
            device::BundleReplay replay(store, id, false);
            CHECK(reader.final_outcome() && replay.final_outcome());
            CHECK(reader.final_outcome()->abort_outcome->emitters[0].observed.presence() ==
                  Presence::unknown);
        }
        // Changing only daemon outcome changes aggregate hash and byte accounting, not bundle bytes.
        std::string bundle_bytes;
        Hash previous_hash;
        for (bool failed : {false, true}) {
            auto capture = store->begin_projected_capture(header());
            store->append_bundle(capture, completed());
            auto value = outcome(RecordedRunDisposition::completed, AcquisitionReason::none);
            if (failed) {
                value.disposition = RecordedRunDisposition::failed;
                value.reason = AcquisitionReason::cleanup_failure;
                value.stop_error = Error{Status::io, "stop failed", "executor"};
            }
            const auto before = encode(completed());
            store->record_run_outcome(capture, {1, value});
            auto a = store->finalize(capture);
            CHECK(a.bytes == encode(header()).size() +
                                 std::filesystem::file_size(store->object_path(capture)) +
                                 encode(ProjectedCaptureOutcome{1, value}).size());
            CHECK(encode(store->bundle(capture)) == before);
            if (failed) {
                CHECK(a.hash != previous_hash);
                CHECK(before == bundle_bytes);
            } else {
                previous_hash = a.hash;
                bundle_bytes = before;
            }
        }
        for (auto disposition : {AcquisitionDisposition::completed, AcquisitionDisposition::failed,
                                 AcquisitionDisposition::stopped, AcquisitionDisposition::cancelled}) {
            auto capture = store->begin_projected_capture(header());
            auto executor = completed();
            executor.evidence.disposition = disposition;
            executor.evidence.reason =
                disposition == AcquisitionDisposition::failed      ? AcquisitionReason::device_failure
                : disposition == AcquisitionDisposition::stopped   ? AcquisitionReason::user_stop
                : disposition == AcquisitionDisposition::cancelled ? AcquisitionReason::user_cancel
                                                                   : AcquisitionReason::none;
            store->append_bundle(capture, executor);
            store->abandon(capture);
            store->recover(capture);
            auto summary = store->bundle_summary(capture);
            CHECK(!summary.final_outcome && summary.last_executor_disposition == disposition);
            CHECK(encode(store->bundle(capture)) == encode(executor));
        }
        finalized_identity_tests(root);
        persisted_bound_tests(store, root);
        abort_semantic_tests();
        store.reset();
        // Complete corrupted outcomes are refused; incomplete provisional envelopes remain absent.
        for (unsigned mode = 0; mode < 19; ++mode) {
            Id capture;
            {
                artifact::Store s(root);
                capture = s.begin_projected_capture(header());
                s.append_bundle(capture, completed());
                s.abandon(capture);
            }
            auto bytes = encode(ProjectedCaptureOutcome{1, detailed_outcome()});
            const auto directory = root / "objects" / capture.value;
            if (mode == 0)
                bytes[0] = 'X';
            if (mode == 1)
                u64(bytes, 8, 2);
            if (mode == 2)
                u64(bytes, 16, UINT64_MAX);
            if (mode == 3)
                bytes[24] ^= 1;
            if (mode == 4)
                bytes.back() ^= 1;
            if (mode == 5) {
                bytes[68] = char(255);
                checksum(bytes);
            } // disposition
            if (mode == 6) {
                bytes[70] = char(3);
                checksum(bytes);
            } // error presence
            if (mode == 7) {
                u64(bytes, 32, 2);
                checksum(bytes);
            } // prefix count
            if (mode == 8) {
                bytes[48] = 'X';
                checksum(bytes);
            } // RunId
            if (mode == 9) {
                bytes[59] = 'X';
                checksum(bytes);
            } // execution GenerationId
            if (mode == 10)
                bytes += 'x';
            if (mode == 11) {
                bytes[bytes.find("executor") - 8 - 12 - 8 - 1] = char(255);
                checksum(bytes);
            }
            if (mode == 12) {
                bytes[126] = char(2); // Established boolean must be exactly false=0/true=1.
                checksum(bytes);
            }
            if (mode == 13) {
                u64(bytes, bytes.find("cleanup fault") - 24, uint64_t{1} << 32);
                checksum(bytes);
            }
            if (mode == 14) {
                bytes[68] = char(0); // Completed cannot override recorded cleanup faults.
                checksum(bytes);
            }
            if (mode == 15)
                bytes[24] ^= 1; // Complete corrupt provisional outcome is never absence.
            if (mode == 16)
                bytes.resize(bytes.size() - 1); // Published truncation is corruption.
            if (mode == 17)
                bytes.resize(max_run_outcome_bytes + 1, 'x');
            if (mode == 18)
                u64(bytes, 16, bytes.size() - 48 + 1);
            const auto outcome_path =
                directory / ((mode == 15 || mode == 18) ? "run.outcome.part" : "run.outcome");
            write(outcome_path, bytes);
            const auto part_before = file(directory / "0.segment.part");
            artifact::Store s(root);
            if (mode != 7 && mode != 15 && mode != 18)
                rejects([&] { (void)s.run_outcome(capture); });
            rejects([&] { s.recover(capture); });
            CHECK(s.get(capture).state == artifact::ArtifactState::recoverable);
            CHECK(file(outcome_path) == bytes && file(directory / "0.segment.part") == part_before);
        }
        const auto complete = encode(
            ProjectedCaptureOutcome{1, outcome(RecordedRunDisposition::completed, AcquisitionReason::none)});
        for (size_t cut : {size_t{0}, size_t{4}, size_t{8}, size_t{15}, size_t{23}, size_t{31}, size_t{50},
                           complete.size() - 1, complete.size()}) {
            Id capture;
            {
                artifact::Store s(root);
                capture = s.begin_projected_capture(header());
                s.append_bundle(capture, completed());
                s.abandon(capture);
            }
            auto directory = root / "objects" / capture.value;
            write(directory / "run.outcome.part", std::string_view(complete).substr(0, cut));
            artifact::Store s(root);
            CHECK(s.recover(capture).state == artifact::ArtifactState::finalized);
            const auto summary = s.bundle_summary(capture);
            CHECK(summary.last_executor_disposition == AcquisitionDisposition::completed);
            CHECK(bool(summary.final_outcome) == (cut == complete.size()));
            CHECK(encode(s.bundle(capture)) == encode(completed()));
            if (cut != complete.size()) {
                CHECK(!std::filesystem::exists(directory / "run.outcome"));
                CHECK(!std::filesystem::exists(directory / "run.outcome.part"));
            }
        }
        std::cout << "Final daemon outcome, full AbortOutcome, immutable integrity, prefix binding and exact "
                     "bundle preservation passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
