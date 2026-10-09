// Isolated public-client capability audit. No GUI/runtime implementation dependency.
// This records the accepted runtime's isolation blocker; it does NOT certify safe switching.
#include <iostream>
#include <mantis/client.hpp>
#include <thread>
#define CHECK(v)                                                                                             \
    do {                                                                                                     \
        if (!(v))                                                                                            \
            throw std::runtime_error(#v);                                                                    \
    } while (false)
int main(int argc, char **argv) {
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
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        refused(a, (root / "B-測定.mantis").string(), true, mantis::Status::busy);
        const auto raw = a.capture_status(capture).raw_artifact();
        a.stop_capture(capture);
        const auto status = a.capture_status(capture);
        if (!status.finalization_job_id().empty())
            (void)a.wait(status.finalization_job_id());
        const auto job = a.replay(raw, true, false);
        // A sufficiently long real-time replay keeps the daemon's jobs busy.
        refused(a, (root / "busy.mantis").string(), true, mantis::Status::busy);
        (void)a.wait(job);
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
        return 1;
    }
    return 0;
}
