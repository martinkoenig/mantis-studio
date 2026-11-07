#include "../../src/device-runtime/qualification-test.hpp"
#include <future>
#include <iostream>
#include <mantis/device_runtime.hpp>
#include <nlohmann/json.hpp>
using namespace mantis;
using namespace std::chrono_literals;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
namespace {
struct Gate {
    std::promise<void> promise;
    std::shared_future<void> future = promise.get_future().share();
    std::atomic_bool opened{};
    void open() {
        if (!opened.exchange(true))
            promise.set_value();
    }
    void wait() const {
        CHECK(future.wait_for(5s) == std::future_status::ready);
    }
};
struct CallbackLease {
    std::atomic_uint &retired;
    explicit CallbackLease(std::atomic_uint &count) : retired(count) {}
    ~CallbackLease() {
        ++retired;
    }
};
struct Control {
    Gate wait_entered, wait_proceed, boundary_entered, boundary_proceed;
    Gate writer_entered, writer_proceed, producer_proceed, primary_reported;
    bool timeout{}, boundary{};
    std::atomic_uint waits{}, checkpoints{}, catches{}, leases_retired{};
    std::weak_ptr<CallbackLease> lease;
    std::string public_error;
    void release() {
        for (auto *gate : {&wait_proceed, &boundary_proceed, &writer_proceed, &producer_proceed})
            gate->open();
    }
};
Control *control{}; // One isolated executable, one serial scenario at a time.
struct Release {
    Control &c;
    ~Release() {
        c.release();
    }
};
struct Evidence {
    std::mutex mutex;
    std::vector<LogRecord> logs;
    bool throwing{};
    Control &c;
    void log(const LogRecord &record) {
        {
            std::lock_guard guard(mutex);
            logs.push_back(record);
        }
        if (record.component == "capture")
            c.primary_reported.open();
        if (throwing)
            throw std::runtime_error("logger secondary failure");
    }
    data::Metadata final(const std::string &primary) {
        std::lock_guard guard(mutex);
        CHECK(logs.size() == 2);
        CHECK(logs[0].component == "capture" && logs[0].message == primary);
        CHECK(logs[1].component == "capture.diagnostics");
        return nlohmann::json::parse(logs[1].message).get<data::Metadata>();
    }
};
class Source final : public device::ImageStream {
    device::Descriptor descriptor_;
    Control &c_;
    uint64_t calls_{};

  public:
    std::atomic_uint starts{}, stops{}, next_running{}, callback_running{};
    std::atomic_bool running{};
    bool start_failure{}, cleanup_throw{}, diagnostics_throw{};
    explicit Source(Control &c) : c_(c) {
        descriptor_.id = {"startup-source"};
    }
    const device::Descriptor &descriptor() const override {
        return descriptor_;
    }
    bool source_paced() const override {
        return true;
    }
    Result<void> start() override {
        ++starts;
        running = true;
        if (start_failure)
            return std::unexpected(Error{Status::plugin_failed, "source start rejected", "fixture"});
        return {};
    }
    Result<void> stop() override {
        // These are real callback lifetimes, independent of Session's counters.
        CHECK(next_running == 0 && callback_running == 0);
        CHECK(!c_.lease.expired()); // Callback ownership outlives source cleanup.
        CHECK(stops.fetch_add(1) == 0);
        running = false;
        if (cleanup_throw)
            throw std::runtime_error("source cleanup secondary failure");
        return {};
    }
    Result<data::Published> next() override {
        ++next_running;
        struct Retire {
            std::atomic_uint &count;
            ~Retire() {
                --count;
            }
        } retire{next_running};
        if (calls_++ != 0) {
            c_.producer_proceed.wait();
            return data::Published{};
        }
        data::Packet image;
        image.type = schema::image;
        memory::BufferBuilder bytes(4);
        image.attributes = {
            {{"org.mantis.pixels", schema::ScalarType::u8, {4}, {1}, ""}, std::move(bytes).publish()}};
        return data::publish(std::move(image));
    }
    data::Metadata diagnostics() const override {
        if (diagnostics_throw)
            throw std::runtime_error("source diagnostic secondary failure");
        return {};
    }
};
void terminal(const data::Metadata &d, const std::string &primary, const std::string &secondary,
              bool committed, bool workers = true) {
    CHECK(d.at("recorder_error") == primary);
    CHECK(d.at("recorder_secondary_error") == secondary);
    CHECK(d.at("recorder_snapshot_scope") == "final-after-join");
    CHECK(d.at("recorder_writer_running") == "0" && d.at("recorder_producer_running") == "0");
    CHECK(d.at("recorder_committed") == (committed ? "1" : "0"));
    auto ns = [&](const char *key) { return std::stoll(d.at(key)); };
    CHECK(ns("recorder_first_error_ns") >= 0);
    CHECK(ns("recorder_cleanup_started_ns") >= ns("recorder_first_error_ns"));
    CHECK(ns("recorder_cleanup_finished_ns") >= ns("recorder_cleanup_started_ns"));
    CHECK((ns("recorder_first_commit_ns") >= 0) == committed);
    if (workers)
        CHECK(ns("recorder_writer_started_ns") >= 0 && ns("recorder_producer_started_ns") >= 0);
    else
        CHECK(ns("recorder_writer_started_ns") == -1 && ns("recorder_producer_started_ns") == -1);
    if (!secondary.empty())
        CHECK(ns("recorder_secondary_error_ns") >= ns("recorder_first_error_ns"));
}
void failure(const std::string &mode, bool noisy = false) {
    Control c;
    control = &c;
    c.timeout = mode == "timeout-first" || mode == "unlock-boundary";
    c.boundary = mode == "unlock-boundary";
    Source source(c);
    source.start_failure = mode == "start-failure";
    source.cleanup_throw = noisy;
    source.diagnostics_throw = noisy;
    Evidence evidence{{}, {}, noisy, c};
    struct Outcome {
        Status status;
        std::string message;
    };
    std::future<Outcome> constructing;
    // Destructed before the future joins, including every failing assertion.
    Release release{c};
    constructing = std::async(std::launch::async, [&] {
        auto lease = std::make_shared<CallbackLease>(c.leases_retired);
        c.lease = lease;
        try {
            device::Session session(
                {{"startup-" + mode}, {{"startup-source"}}, {}}, {&source},
                [&, lease = std::move(lease)](data::Published) {
                    ++source.callback_running;
                    struct Retire {
                        std::atomic_uint &count;
                        ~Retire() {
                            --count;
                        }
                    } retire{source.callback_running};
                    c.writer_entered.open();
                    c.writer_proceed.wait();
                    throw std::runtime_error("writer primary failure");
                },
                [&](const LogRecord &record) { evidence.log(record); });
        } catch (const Failure &e) {
            return Outcome{e.error.code, e.error.message};
        }
        return Outcome{Status::ok, "unexpected constructor success"};
    });
    std::string primary = "writer primary failure", secondary;
    if (source.start_failure) {
        primary = "source start rejected";
    } else {
        c.wait_entered.wait();
        c.writer_entered.wait();
        if (mode == "writer-first" || mode == "callback-failure") {
            c.writer_proceed.open();
            c.primary_reported.wait();
            // Writer's error is accepted before constructor wait examines it.
            c.wait_proceed.open();
        } else if (mode == "unlock-boundary") {
            c.wait_proceed.open();
            c.boundary_entered.wait();
            c.writer_proceed.open();
            c.primary_reported.wait();
            c.boundary_proceed.open();
            secondary = "Device did not provide an initial frame";
        } else {
            CHECK(mode == "timeout-first");
            c.wait_proceed.open();
            c.primary_reported.wait(); // Timeout has already latched/reporting.
            c.writer_proceed.open();
            primary = "Device did not provide an initial frame";
            secondary = "writer primary failure";
        }
        c.producer_proceed.open();
    }
    CHECK(constructing.wait_for(5s) == std::future_status::ready);
    auto result = constructing.get();
    CHECK(result.status == (source.start_failure ? Status::plugin_failed : Status::io));
    CHECK(result.message == primary); // Original fixed-timeout throw fails here.
    CHECK(c.public_error == primary && c.catches == 1);
    CHECK(c.lease.expired() && c.leases_retired == 1);
    if (noisy && secondary.empty())
        secondary = "source cleanup secondary failure";
    const auto d = evidence.final(primary);
    terminal(d, primary, secondary, false, !source.start_failure);
    CHECK(d.at("recorder_cleanup_errors") == (noisy ? "1" : "0"));
    CHECK(!noisy || std::stoll(d.at("recorder_diagnostic_errors")) > 0);
    CHECK(source.starts == 1 && source.stops == 1 && !source.running);
    CHECK(source.next_running == 0 && source.callback_running == 0);
    CHECK(c.waits == (source.start_failure ? 0 : 1));
    CHECK(c.checkpoints == (c.timeout ? 1 : 0));
}
void success() {
    Control c;
    control = &c;
    Source source(c);
    std::future<std::unique_ptr<device::Session>> constructing;
    std::unique_ptr<device::Session> session;
    Release release{c};
    constructing = std::async(std::launch::async, [&] {
        auto lease = std::make_shared<CallbackLease>(c.leases_retired);
        c.lease = lease;
        return std::make_unique<device::Session>(
            device::CaptureDescriptor{{"successful-first-callback"}, {{"startup-source"}}, {}},
            std::vector<device::ImageStream *>{&source},
            [&, lease = std::move(lease)](data::Published) {
                ++source.callback_running;
                struct Retire {
                    std::atomic_uint &count;
                    ~Retire() {
                        --count;
                    }
                } retire{source.callback_running};
                c.writer_entered.open();
                c.writer_proceed.wait();
            },
            LogSink{});
    });
    c.wait_entered.wait();
    c.writer_entered.wait();
    CHECK(constructing.wait_for(0s) == std::future_status::timeout);
    c.writer_proceed.open();
    c.wait_proceed.open();
    CHECK(constructing.wait_for(5s) == std::future_status::ready);
    session = constructing.get();
    CHECK(session->active() && session->committed() == 1 && session->error().empty());
    CHECK(session->first()->type == schema::image && session->first()->attributes[0].buffer.size() == 4);
    c.producer_proceed.open();
    session->stop();
    session->stop();
    CHECK(!session->active() && session->error().empty());
    auto d = session->diagnostics();
    CHECK(d.at("recorder_error").empty() && d.at("recorder_secondary_error").empty());
    CHECK(d.at("recorder_snapshot_scope") == "final-after-join");
    CHECK(d.at("recorder_writer_running") == "0" && d.at("recorder_producer_running") == "0");
    CHECK(c.waits == 1 && c.checkpoints == 0);
    session.reset();
    CHECK(c.lease.expired() && c.leases_retired == 1 && c.catches == 0);
    CHECK(source.stops == 1 && !source.running && source.next_running == 0 && source.callback_running == 0);
}
} // namespace
namespace mantis::device::startup_test {
bool wait(std::condition_variable &cv, std::unique_lock<std::mutex> &lock, std::chrono::seconds budget,
          const std::function<bool()> &ready) {
    CHECK(budget == 5s); // The seam observes, rather than changes, production's budget.
    ++control->waits;
    control->wait_entered.open();
    lock.unlock();
    control->wait_proceed.wait();
    lock.lock();
    if (control->timeout) {
        CHECK(!ready());
        return false; // Controlled expiry; elapsed time is never an ordering oracle.
    }
    CHECK(cv.wait_for(lock, 5s, ready));
    return true;
}
void checkpoint(std::string_view point, const Session &session) {
    if (point == "startup-catch") {
        ++control->catches;
        control->public_error = session.error();
        CHECK(!session.active() && session.committed() == 0);
        bool unavailable{};
        try {
            (void)session.first();
        } catch (const Failure &error) {
            CHECK(error.error.code == Status::busy);
            unavailable = true;
        }
        CHECK(unavailable);
        return;
    }
    CHECK(point == "timeout-before-fail");
    ++control->checkpoints;
    if (control->boundary) {
        control->boundary_entered.open();
        control->boundary_proceed.wait();
    }
}
} // namespace mantis::device::startup_test
int main(int argc, char **argv) {
    try {
        if (argc == 2) {
            CHECK(std::string_view(argv[1]) == "unlock-boundary");
            failure("unlock-boundary");
        } else {
            failure("writer-first");
            failure("timeout-first");
            failure("unlock-boundary");
            failure("callback-failure");
            failure("callback-failure", true);
            failure("timeout-first", true);
            failure("start-failure");
            failure("start-failure", true);
            success();
        }
        control = nullptr;
        std::cout << "Session first-error orderings, committed readiness, throwing sinks/cleanup and retired "
                     "ownership PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
