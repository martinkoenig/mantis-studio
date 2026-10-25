// Isolated public-client capability audit. No GUI/runtime implementation dependency.
// This records the accepted runtime's isolation blocker; it does NOT certify safe switching.
#include <chrono>
#include <iostream>
#include <mantis/client.hpp>
#include <thread>
#define CHECK(v)                                                                                             \
    do {                                                                                                     \
        if (!(v))                                                                                            \
            throw std::runtime_error(#v);                                                                    \
    } while (false)
namespace w = mantis::wire::v1;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
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
static std::chrono::nanoseconds ready(const mantis::client::Client &client, const std::string &capture,
                                      const std::string &raw) {
    // Observe real recorded evidence, not elapsed sleep. A two-second receive-clock
    // span gives paced replay a measured minimum lifetime for the busy assertion.
    const auto first = client.preview(capture);
    CHECK(first && first->type == mantis::schema::image);
    auto span = 0ns;
    const auto deadline = Clock::now() + 8s;
    while (Clock::now() < deadline) {
        auto status = healthy(client, capture, raw);
        CHECK(status.active());
        if (status.framesets_committed() >= 64) {
            if (auto last = client.preview(capture)) {
                CHECK(last->type == first->type &&
                      last->header.timestamp.domain.id == first->header.timestamp.domain.id &&
                      last->header.timestamp.domain.name == first->header.timestamp.domain.name);
                CHECK(last->header.sequence.value >= first->header.sequence.value);
                CHECK(last->header.received.nanoseconds >= first->header.received.nanoseconds);
                span = std::chrono::nanoseconds(last->header.received.nanoseconds -
                                                first->header.received.nanoseconds);
                // Preview is produced before recording: do not mistake it for a commit.
                status = healthy(client, capture, raw);
                if (span >= 2s && status.framesets_committed() > last->header.sequence.value) {
                    std::cout << "Observed replay minimum span_ns=" << span.count() << std::endl;
                    return span;
                }
            }
        }
        std::this_thread::sleep_for(25ms); // Bounded observation cadence, not a readiness assumption.
    }
    throw std::runtime_error("Capture evidence deadline: need 64 committed frames and 2s receive span");
}
int main(int argc, char **argv) {
    std::string currentCapture, currentRaw;
    try {
        CHECK(argc == 2);
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
        const auto replaySpan = ready(a, capture, raw);
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
        CHECK(status.framesets_committed() >= 64 &&
              status.framesets_committed() == status.framesets_produced());
        if (!status.finalization_job_id().empty()) {
            const auto finalized = a.wait(status.finalization_job_id(), 10s);
            CHECK(finalized.state() == "Completed" && finalized.result_artifact() == raw);
        }
        const auto finalized = artifact(a, raw);
        CHECK(finalized.id() == raw && finalized.type() == openRaw.type() &&
              finalized.schema_version() == openRaw.schema_version());
        CHECK(finalized.state() == "FINALIZED" && finalized.hash() != openRaw.hash() &&
              !finalized.hash().empty() && finalized.chunks() == status.framesets_committed() &&
              finalized.bytes() > 0);
        const auto replayStarted = Clock::now();
        const auto job = a.replay(raw, true, false);
        bool replayActive = false;
        for (const auto &value : a.snapshot().jobs())
            if (value.id() == job) {
                std::cout << "Before busy assertion: " << value.ShortDebugString() << std::endl;
                CHECK(value.state() == "Queued" || value.state() == "Running");
                replayActive = true;
            }
        CHECK(replayActive);
        refused(a, (root / "busy.mantis").string(), true, mantis::Status::busy);
        // The request began before the job could start. Check that the refusal
        // was established inside the actual recording's minimum replay duration.
        CHECK(Clock::now() - replayStarted < replaySpan);
        const auto replayed = a.wait(job, 10s);
        CHECK(replayed.state() == "Completed" && replayed.result_artifact() == raw);
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
        std::cerr << e.what() << '\n';
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
