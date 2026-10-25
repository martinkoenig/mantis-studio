// Isolated public-client capability audit. No GUI/runtime implementation dependency.
// This records the accepted runtime's isolation blocker; it does NOT certify safe switching.
#include <chrono>
#include <iostream>
#include <mantis/client.hpp>
#include <thread>
#ifdef MANTIS_CAPABILITIES_REPLAY_GATE
#include "../fixtures/project-capabilities/gate.hpp"
#endif
#define CHECK(v)                                                                                             \
    do {                                                                                                     \
        if (!(v))                                                                                            \
            throw std::runtime_error(#v);                                                                    \
    } while (false)
namespace w = mantis::wire::v1;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
static void stage(const char *phase, const char *operation) {
    std::cout << "STAGE " << phase << " " << operation << std::endl;
}
static w::Response job_snapshot(const mantis::client::Client &client) {
    const auto response = client.snapshot();
    std::cout << "API snapshot status=OK jobs=" << response.jobs_size() << std::endl;
    return response;
}
static w::Artifact artifact(const mantis::client::Client &client, const std::string &raw) {
    const auto snapshot = client.snapshot();
    for (const auto &value : snapshot.artifacts())
        if (value.id() == raw)
            return value;
    throw std::runtime_error("Capture artifact absent from current project: " + raw);
}
static w::Capture healthy(const mantis::client::Client &client, const std::string &id,
                          const std::string &raw) {
    auto status = client.capture_status(id);
    CHECK(status.id() == id && status.raw_artifact() == raw);
    CHECK(status.error().empty() && status.queue_saturation() == 0);
    CHECK(status.dropped() == 0);
    // Counters are sampled independently while capture is active.
    CHECK(status.frames() <= status.framesets_committed());
    CHECK(status.framesets_produced() >= status.framesets_committed());
    return status;
}
static void evidence(const mantis::client::Client &client, const std::string &capture, const std::string &raw,
                     const char *stage) {
    // Only selected public state, never Request/Endpoint/token serialization.
    std::cout << stage << " capture_id=" << capture << " raw_id=" << raw << std::endl;
    try {
        std::cout << stage << " capture: " << client.capture_status(capture).ShortDebugString() << std::endl;
    } catch (const std::exception &e) {
        std::cout << "Capture diagnostic unavailable: " << e.what() << std::endl;
    }
    const auto snapshot = client.snapshot();
    for (const auto &value : snapshot.artifacts())
        if (value.id() == raw)
            std::cout << stage << " artifact: " << value.ShortDebugString() << std::endl;
    for (const auto &job : snapshot.jobs())
        std::cout << stage << " job: " << job.ShortDebugString() << std::endl;
}
static void ready(const mantis::client::Client &client, const std::string &capture, const std::string &raw) {
    const auto first = client.preview(capture);
    CHECK(first && first->type == mantis::schema::image);
    const auto deadline = Clock::now() + 8s;
    while (Clock::now() < deadline) {
        auto status = healthy(client, capture, raw);
        CHECK(status.active());
        if (status.framesets_committed() >= 3) {
            const auto last = client.preview(capture);
            CHECK(last && last->type == first->type);
            CHECK(last->header.timestamp.domain.id == first->header.timestamp.domain.id &&
                  last->header.timestamp.domain.name == first->header.timestamp.domain.name);
            CHECK(last->header.sequence.value >= first->header.sequence.value);
            CHECK(last->header.received.nanoseconds >= first->header.received.nanoseconds);
            status = healthy(client, capture, raw);
            if (status.framesets_committed() > last->header.sequence.value)
                return;
        }
        std::this_thread::sleep_for(25ms); // Bounded observation, never assumed readiness.
    }
    throw std::runtime_error("Capture evidence deadline: need three committed frames");
}
static w::Job active_job(const w::Response &snapshot, const std::string &id) {
    for (const auto &job : snapshot.jobs())
        if (job.id() == id) {
            CHECK(job.state() == "Queued" || job.state() == "Running");
            return job;
        }
    throw std::runtime_error("Exact replay job missing: " + id);
}
static void reject_invalid_jobs() {
    for (const auto *state : {"missing", "stale", "Completed", "Failed", "Cancelled"}) {
        w::Response snapshot;
        if (std::string_view(state) != "missing") {
            auto *job = snapshot.add_jobs();
            job->set_id(std::string_view(state) == "stale" ? "other" : "expected");
            job->set_state(std::string_view(state) == "stale" ? "Running" : state);
        }
        bool rejected = false;
        try {
            (void)active_job(snapshot, "expected");
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        CHECK(rejected);
    }
    std::cout << "PASS: missing/stale/terminal jobs cannot establish active-job evidence" << std::endl;
}
int main(int argc, char **argv) {
    std::string currentCapture, currentRaw;
    try {
#ifdef MANTIS_CAPABILITIES_REPLAY_GATE
        CHECK(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "--negative-writer"));
        const bool negativeWriter = argc == 4;
        const auto gate = std::stoi(argv[2]);
#else
        CHECK(argc == 2);
#endif
        reject_invalid_jobs();
        const std::filesystem::path root = argv[1];
        const mantis::client::Client a, b;
        const auto open = [&](const mantis::client::Client &c, const std::string &path, bool create) {
            mantis::wire::v1::Request r;
            r.mutable_project_open()->set_path(path);
            r.mutable_project_open()->set_create(create);
            return c.call(r);
        };
        const auto original = a.snapshot().project_path();
        const auto refused = [&](const mantis::client::Client &c, const std::string &path, bool create,
                                 mantis::Status code) {
            bool failed = false;
            try {
                (void)open(c, path, create);
            } catch (const mantis::Failure &e) {
                failed = true;
                CHECK(e.error.code == code);
                std::cout << "Refusal: " << e.error.message << "\n";
            }
            CHECK(failed);
            CHECK(a.snapshot().project_path() == original);
        };
        refused(a, (root / "missing.mantis").string(), false, mantis::Status::not_found);
        refused(a, (root / "Locked.mantis").string(), false, mantis::Status::busy);
        auto bad = mantis::client::Endpoint::environment();
        bad.token = "incorrect-token";
        refused(mantis::client::Client(bad), (root / "denied.mantis").string(), true,
                mantis::Status::invalid_argument);
        // A file-valued ancestor is invalid; failure preserves current identity.
        bool invalid = false;
        try {
            (void)open(a, (root / "file/block.mantis").string(), true);
        } catch (const mantis::Failure &) {
            invalid = true;
        }
        CHECK(invalid && a.snapshot().project_path() == original);
        const auto nulPath = (root / "nul.mantis").string() + std::string("\0suffix", 7);
        bool nulRejected = false;
        try {
            (void)open(a, nulPath, true);
        } catch (const mantis::Failure &) {
            nulRejected = true;
        }
        CHECK(nulRejected && a.snapshot().project_path() == original);
        std::cout << "Embedded NUL rejected; current identity unchanged (partial directory side effects "
                     "require a separate backend policy).\n";
        const auto capture = a.start_capture({"virtual-scanner"});
        currentCapture = capture;
        const auto initial = a.capture_status(capture);
        const auto raw = initial.raw_artifact();
        currentRaw = raw;
        CHECK(!raw.empty());
        evidence(a, capture, raw, "started");
        const auto openRaw = artifact(a, raw);
        CHECK(openRaw.type() == "org.mantis.RawCapture" && openRaw.schema_version() == 1 &&
              openRaw.state() == "OPEN");
        // A healthy but unfinalized capture must not become replayable.
        bool unfinalizedRejected = false;
        try {
            (void)a.replay(raw, true, false);
        } catch (const mantis::Failure &e) {
            CHECK(e.error.code == mantis::Status::incompatible &&
                  e.error.message == "Replay requires a finalized RawCapture");
            unfinalizedRejected = true;
        }
        CHECK(unfinalizedRejected && a.snapshot().jobs_size() == 0);
        stage("begin", "capture-ready");
#ifdef MANTIS_CAPABILITIES_REPLAY_GATE
        if (negativeWriter) {
            // The harness takes a write lock on this test's SQLite database,
            // then releases us to observe the real finite LOSSLESS queue fail.
            qualification::send(gate, 'W');
            CHECK(qualification::receive(gate) == 'L');
            const auto deadline = Clock::now() + 8s;
            while (Clock::now() < deadline) {
                (void)healthy(a, capture, raw); // Must reject the actual writer failure.
                std::this_thread::sleep_for(25ms);
            }
            throw std::runtime_error("Negative writer fixture did not fail capture");
        }
#endif
        ready(a, capture, raw);
        stage("end", "capture-ready");
        evidence(a, capture, raw, "ready");
        CHECK(healthy(a, capture, raw).active());
        refused(a, (root / "B-測定.mantis").string(), true, mantis::Status::busy);
        CHECK(healthy(a, capture, raw).active());
        // Use the public response to inspect capture errors; the void C++ stop
        // helper waits for a job but does not itself throw for Capture.error.
        w::Request stop;
        stop.mutable_capture_stop()->set_id(capture);
        const auto stopped = a.call(stop);
        CHECK(stopped.captures_size() == 1 && stopped.captures(0).id() == capture);
        evidence(a, capture, raw, "stopped");
        const auto status = healthy(a, capture, raw);
        CHECK(!status.active() && status.queue_depth() == 0 &&
              status.frames() == status.framesets_committed());
        CHECK(status.framesets_committed() >= 3 &&
              status.framesets_committed() == status.framesets_produced());
        if (!status.finalization_job_id().empty()) {
            const auto finalized = a.wait(status.finalization_job_id(), 10s);
            CHECK(finalized.state() == "Completed" && finalized.result_artifact() == raw);
        }
        stage("begin", "finalized-artifact");
        const auto finalized = artifact(a, raw);
        CHECK(finalized.id() == raw && finalized.type() == openRaw.type() &&
              finalized.schema_version() == openRaw.schema_version());
        CHECK(finalized.state() == "FINALIZED" && finalized.hash() != openRaw.hash() &&
              !finalized.hash().empty() && finalized.chunks() == status.framesets_committed() &&
              finalized.bytes() > 0);
        stage("end", "finalized-artifact");
        stage("begin", "replay-submit");
#ifdef MANTIS_CAPABILITIES_REPLAY_GATE
        qualification::send(gate, 'A');
#endif
        const auto job = a.replay(raw, true, false);
        std::cout << "API replay status=OK job_id=" << job << std::endl;
        stage("end", "replay-submit");
        stage("begin", "replay-job-inspect");
#ifdef MANTIS_CAPABILITIES_REPLAY_GATE
        CHECK(qualification::receive(gate) == 'R');
#endif
        // Own the Response for the entire iteration. GCC 13 does not extend
        // the temporary owning a protobuf repeated-field reference (P2718).
        const auto snapshot = job_snapshot(a);
        const auto replaying = active_job(snapshot, job);
        std::cout << "Before busy assertion: " << replaying.ShortDebugString() << std::endl;
        stage("end", "replay-job-inspect");
        stage("begin", "job-busy-refusal");
        refused(a, (root / "busy.mantis").string(), true, mantis::Status::busy);
        const auto stillRunning = job_snapshot(a);
        (void)active_job(stillRunning, job);
#ifdef MANTIS_CAPABILITIES_REPLAY_GATE
        qualification::send(gate, 'G');
#endif
        stage("end", "job-busy-refusal");
        stage("begin", "replay-completion");
        const auto replayed = a.wait(job, 10s);
        CHECK(replayed.state() == "Completed" && replayed.result_artifact() == raw);
        stage("end", "replay-completion");
        evidence(a, capture, raw, "replayed");
        const auto next = (root / "B-測定.mantis").string();
        CHECK(open(a, next, true).project_path() == next);
        CHECK(b.snapshot().project_path() == next);
        CHECK(b.snapshot().artifacts().empty());
        CHECK(open(b, next, false).project_path() == next);
        CHECK(open(a, next, true).project_path() == next); // create is not exclusive.
        mantis::wire::v1::Request old;
        old.mutable_preview()->set_id(job);
        const auto ref = a.call(old).data();
        CHECK(!ref.lease_id().empty() && ref.locator().find(next) != std::string::npos);
        std::cout << "BLOCKER VERIFIED: completed replay from A remains readable after both clients confirm "
                     "B; old preview written into "
                  << ref.locator() << "\n";
        mantis::wire::v1::Request release;
        release.mutable_preview_release()->set_id(ref.lease_id());
        (void)a.call(release);
        CHECK(open(b, original, false).project_path() == original);
        CHECK(a.snapshot().project_path() == original);
        CHECK(!a.snapshot().artifacts().empty());
        // Paths are raw host paths, not shell command text. Embedded NUL is a known
        // unresolved filesystem security gap and is never offered by the gated UI.
        const auto literal = (root / "literal-$(never-execute)-`plain`.mantis").string();
        CHECK(open(a, literal, true).project_path() == literal);
        CHECK(b.snapshot().project_path() == literal);
        mantis::wire::v1::Request shutdown;
        shutdown.mutable_shutdown();
        (void)a.call(shutdown);
        std::cout << "PASS: published create/open semantics, two clients, capture/job refusal, "
                     "auth/invalid/missing path, Unicode/literal paths; New/Open remain BLOCKED in UI\n";
    } catch (const std::exception &e) {
        std::cerr << "EXCEPTION: " << e.what() << std::endl;
        if (const auto *failure = dynamic_cast<const mantis::Failure *>(&e))
            std::cerr << "API error status=" << static_cast<unsigned>(failure->error.code) << std::endl;
        if (!currentCapture.empty())
            try {
                evidence(mantis::client::Client{}, currentCapture, currentRaw, "failure");
            } catch (const std::exception &d) {
                std::cerr << "Diagnostic unavailable: " << d.what() << '\n';
            }
        return 1;
    }
    return 0;
}
