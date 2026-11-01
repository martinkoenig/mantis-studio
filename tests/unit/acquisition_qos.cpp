#include <future>
#include <iostream>
#include <mantis/device_runtime.hpp>
#include <nlohmann/json.hpp>
#include <vector>
using namespace mantis;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string("Check failed: ") + #x);                                    \
    } while (false)
struct Gate {
    std::promise<void> promise;
    std::shared_future<void> ready = promise.get_future().share();
    std::atomic_bool opened{};
    void open() {
        if (!opened.exchange(true))
            promise.set_value();
    }
    void wait() {
        ready.wait();
    }
};
struct Unblock {
    std::vector<Gate *> gates;
    ~Unblock() {
        for (auto *gate : gates)
            gate->open();
    }
};
class Source final : public device::ImageStream {
    device::Descriptor descriptor_;
    uint64_t sequence_{};
    data::Published image_;

  public:
    std::function<void(uint64_t)> before_emit;
    std::atomic_bool running{};
    std::atomic_uint stops{};
    std::atomic_uint emitted{};
    std::optional<Error> next_error;
    std::optional<Error> start_error;
    std::optional<Error> stop_error;
    std::function<data::Metadata()> diagnostic;
    Source() {
        descriptor_.id = {"fixture"};
        data::Packet image;
        image.type = schema::image;
        memory::BufferBuilder bytes(4);
        image.attributes.push_back(
            {{"org.mantis.pixels", schema::ScalarType::u8, {2, 2}, {2, 1}, ""}, std::move(bytes).publish()});
        image_ = data::publish(std::move(image));
    }
    const device::Descriptor &descriptor() const override {
        return descriptor_;
    }
    bool source_paced() const override {
        return true;
    }
    Result<void> start() override {
        running = true;
        sequence_ = 0;
        emitted = 0;
        if (start_error)
            return std::unexpected(*start_error);
        return {};
    }
    Result<void> stop() override {
        running = false;
        ++stops;
        if (stop_error)
            return std::unexpected(*stop_error);
        return {};
    }
    Result<data::Published> next() override {
        if (next_error)
            return std::unexpected(*next_error);
        data::Packet set;
        set.type = schema::frameset;
        set.header.sequence.value = sequence_++;
        if (before_emit)
            before_emit(set.header.sequence.value);
        set.frames = {image_, image_};
        auto result = data::publish(std::move(set));
        ++emitted;
        return result;
    }
    data::Metadata diagnostics() const override {
        return diagnostic ? diagnostic() : data::Metadata{};
    }
};
void throwing_loggers() {
    for (bool writer_failure : {false, true})
        for (bool nonstandard : {false, true}) {
            Source source;
            Gate writer_failed;
            const std::string cause =
                writer_failure ? "Injected writer startup failure" : "Injected source startup failure";
            if (!writer_failure)
                source.before_emit = [&](uint64_t) { throw std::runtime_error(cause); };
            else
                source.before_emit = [&](uint64_t sequence) {
                    if (sequence == 1)
                        writer_failed.wait();
                };
            std::atomic_uint logged{};
            bool rejected{};
            try {
                device::Session session(
                    {{"throwing-log"}, {{"fixture"}}, {}}, {&source},
                    [&](data::Published) { throw std::runtime_error(cause); },
                    [&](const LogRecord &) {
                        ++logged;
                        writer_failed.open(); // Failure is latched before diagnostic callbacks.
                        if (nonstandard)
                            throw 7;
                        throw std::runtime_error("Injected diagnostic sink failure");
                    });
            } catch (const Failure &failure) {
                rejected = true;
                CHECK(failure.error.code == Status::io);
                CHECK(failure.error.message == cause);
            }
            CHECK(rejected && logged > 0);
            CHECK(!source.running && source.stops > 0);
        }
}
bool inactive(device::Session &session) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (session.active() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    return !session.active();
}
void first_commit_recovery() {
    Source source;
    Gate writer, writer_entered, producer;
    std::promise<void> filled, drained;
    auto full = filled.get_future(), empty = drained.get_future();
    source.before_emit = [&](uint64_t sequence) {
        if (sequence == 1)
            writer_entered.wait();
        if (sequence == 8) {
            filled.set_value();
            producer.wait();
        }
    };
    std::vector<uint64_t> recorded;
    std::unique_ptr<device::Session> session;
    std::future<std::unique_ptr<device::Session>> constructing;
    std::future<void> stopping;
    Unblock cleanup{{&writer, &writer_entered, &producer}};
    constructing = std::async(std::launch::async, [&] {
        return std::make_unique<device::Session>(
            device::CaptureDescriptor{{"startup-recovery"}, {{"fixture"}}, {}},
            std::vector<device::ImageStream *>{&source},
            [&](data::Published packet) {
                if (packet->header.sequence.value == 0) {
                    writer_entered.open();
                    writer.wait();
                }
                recorded.push_back(packet->header.sequence.value);
                if (recorded.size() == 8)
                    drained.set_value();
            },
            LogSink{});
    });
    CHECK(full.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(source.emitted == 8);
    CHECK(constructing.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
    writer.open();
    CHECK(constructing.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    session = constructing.get();
    CHECK(empty.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    stopping = std::async(std::launch::async, [&] { session->stop(); });
    CHECK(inactive(*session));
    producer.open();
    CHECK(stopping.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    stopping.get();
    CHECK(session->error().empty() && session->saturation() == 0);
    CHECK(session->produced() == 9 && session->committed() == 9);
    CHECK(session->metrics().high_water <= 8 && session->metrics().dropped == 0);
    CHECK(recorded == std::vector<uint64_t>({0, 1, 2, 3, 4, 5, 6, 7, 8}));
    const auto evidence = session->diagnostics();
    auto number = [&](const char *key) { return std::stoll(evidence.at(key)); };
    CHECK(number("recorder_producer_started_ns") <= number("recorder_first_acquired_ns"));
    CHECK(number("recorder_first_acquired_ns") <= number("recorder_first_enqueued_ns"));
    CHECK(number("recorder_first_enqueued_ns") <= number("recorder_first_callback_ns"));
    CHECK(number("recorder_first_callback_ns") < number("recorder_first_commit_ns"));
    CHECK(number("recorder_first_append_ns") > 0);
    CHECK(number("recorder_max_append_ns") >= number("recorder_first_append_ns"));
    CHECK(number("recorder_writer_running") == 0 && number("recorder_producer_running") == 0);
    CHECK(number("recorder_cleanup_finished_ns") >= number("recorder_cleanup_started_ns"));
    CHECK(!source.running && source.stops == 1);
}
void startup_saturation(bool writer_failure) {
    Source source;
    Gate writer, entered;
    std::promise<void> error;
    auto failed = error.get_future();
    std::atomic_bool signalled{};
    data::Metadata evidence;
    std::atomic_uint recorded{};
    source.before_emit = [&](uint64_t sequence) {
        if (sequence == 1)
            entered.wait();
    };
    std::future<Error> constructing;
    Unblock cleanup{{&writer, &entered}};
    constructing = std::async(std::launch::async, [&] {
        try {
            device::Session session(
                {{"startup-saturation"}, {{"fixture"}}, {}}, {&source},
                [&](data::Published) {
                    if (recorded.load() == 0) {
                        entered.open();
                        writer.wait();
                        if (writer_failure)
                            throw std::runtime_error("Secondary writer failure");
                    }
                    ++recorded;
                },
                [&](const LogRecord &record) {
                    if (record.component == "capture" && !signalled.exchange(true))
                        error.set_value();
                    if (record.component == "capture.diagnostics") {
                        CHECK(record.message.size() <= 16384);
                        evidence = nlohmann::json::parse(record.message).get<data::Metadata>();
                    }
                });
            return Error{Status::ok, "Unexpected startup success", {}};
        } catch (const Failure &failure) {
            return failure.error;
        }
    });
    CHECK(entered.ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(failed.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    writer.open();
    CHECK(constructing.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    const auto cause = constructing.get();
    CHECK(cause.code == Status::io);
    CHECK(cause.message == "LOSSLESS writer queue saturated; capture failed explicitly");
    CHECK(!source.running && source.stops == 1);
    CHECK(source.emitted == 34 && recorded == (writer_failure ? 0u : 33u));
    CHECK(evidence.at("recorder_error") == cause.message);
    CHECK(evidence.at("recorder_saturation") == "1");
    CHECK(evidence.at("recorder_produced") == "34");
    CHECK(evidence.at("recorder_committed") == (writer_failure ? "0" : "33"));
    CHECK(evidence.at("recorder_secondary_error") == (writer_failure ? "Secondary writer failure" : ""));
    CHECK(evidence.at("recorder_queue_high_water") == "32");
    CHECK(evidence.at("recorder_queue_occupancy") == (writer_failure ? "32" : "0"));
    CHECK(std::stoull(evidence.at("recorder_queue_full_ns")) > 0);
    CHECK(std::stoull(evidence.at("recorder_failed_enqueue_wait_ns")) > 0);
    CHECK(evidence.at("recorder_writer_running") == "0" && evidence.at("recorder_producer_running") == "0");
    CHECK(std::stoll(evidence.at("recorder_cleanup_finished_ns")) >= 0);
}
void source_error_survives_diagnostics() {
    Source source;
    source.next_error = Error{Status::io, "Injected source result failure", "fixture"};
    source.stop_error = Error{Status::io, "Injected source cleanup failure", "fixture"};
    source.diagnostic = []() -> data::Metadata {
        throw std::runtime_error("Injected source diagnostics failure");
    };
    data::Metadata evidence;
    bool rejected{};
    try {
        device::Session session(
            {{"diagnostic-failure"}, {{"fixture"}}, {}}, {&source}, [](data::Published) { CHECK(false); },
            [&](const LogRecord &record) {
                if (record.component == "capture.diagnostics")
                    evidence = nlohmann::json::parse(record.message).get<data::Metadata>();
            });
    } catch (const Failure &failure) {
        rejected = true;
        CHECK(failure.error.message == "Injected source result failure");
    }
    CHECK(rejected && !source.running && source.stops == 1);
    CHECK(evidence.at("recorder_first_acquired_ns") == "-1");
    CHECK(evidence.at("recorder_first_commit_ns") == "-1");
    CHECK(evidence.at("recorder_produced") == "0" && evidence.at("recorder_committed") == "0");
    CHECK(std::stoull(evidence.at("recorder_diagnostic_errors")) >= 1);
    CHECK(evidence.at("recorder_cleanup_errors") == "1");
    CHECK(evidence.at("recorder_error") == "Injected source result failure");
    CHECK(evidence.at("recorder_secondary_error") == "Injected source cleanup failure");
    // The same resource can restart, and a diagnostics exception alone must
    // not turn a valid acquisition into a capture failure.
    source.next_error.reset();
    source.stop_error.reset();
    Gate emit;
    std::promise<void> acquired;
    auto ready = acquired.get_future();
    source.before_emit = [&](uint64_t sequence) {
        if (sequence == 1) {
            acquired.set_value();
            emit.wait();
        }
    };
    device::Session healthy({{"healthy-diagnostics"}, {{"fixture"}}, {}}, {&source}, [](data::Published) {},
                            {});
    std::future<void> stopping;
    Unblock cleanup{{&emit}};
    CHECK(ready.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    stopping = std::async(std::launch::async, [&] { healthy.stop(); });
    CHECK(inactive(healthy));
    emit.open();
    CHECK(stopping.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    stopping.get();
    CHECK(healthy.error().empty());
    CHECK(healthy.produced() == 2 && healthy.committed() == 2);
    CHECK(!source.running && source.stops == 2);
}
void partial_start_cleanup() {
    Source source;
    source.start_error = Error{Status::plugin_failed, "Injected partial start failure", "fixture"};
    data::Metadata evidence;
    bool rejected{};
    try {
        device::Session session(
            {{"partial-start"}, {{"fixture"}}, {}}, {&source}, [](data::Published) { CHECK(false); },
            [&](const LogRecord &record) {
                if (record.component == "capture.diagnostics")
                    evidence = nlohmann::json::parse(record.message).get<data::Metadata>();
            });
    } catch (const Failure &failure) {
        rejected = true;
        CHECK(failure.error.code == Status::plugin_failed);
        CHECK(failure.error.message == "Injected partial start failure");
    }
    CHECK(rejected && !source.running && source.stops == 1);
    CHECK(evidence.at("recorder_writer_started_ns") == "-1");
    CHECK(evidence.at("recorder_producer_started_ns") == "-1");
    CHECK(evidence.at("recorder_source_paced") == "-1");
    CHECK(evidence.at("recorder_produced") == "0" && evidence.at("recorder_committed") == "0");
}
void bounded_error_evidence() {
    Source source;
    std::string cause = std::string(1023, 'x') + "\xc2\xb5"; // Force a UTF-8 boundary at the byte limit.
    for (unsigned i = 0; i < 5000; ++i)
        cause += "\n\"fault\"";
    source.next_error = Error{Status::io, cause, "fixture"};
    std::vector<LogRecord> records;
    bool rejected{};
    try {
        device::Session session(
            {{"bounded-evidence"}, {{"fixture"}}, {}}, {&source}, [](data::Published) { CHECK(false); },
            [&](const LogRecord &record) {
                records.push_back(record);
                if (record.component == "capture")
                    throw std::runtime_error("Injected evidence sink failure");
            });
    } catch (const Failure &failure) {
        rejected = true;
        CHECK(failure.error.message == cause);
    }
    CHECK(rejected && !source.running && source.stops == 1);
    CHECK(records.size() == 2);
    CHECK(records.front().message.size() <= 1024);
    CHECK(!nlohmann::json(records.front().message).dump().empty()); // Strict UTF-8, no replacement handler.
    CHECK(records.back().message.size() <= 16384);
    const auto evidence = nlohmann::json::parse(records.back().message).get<data::Metadata>();
    CHECK(evidence.at("recorder_error_truncated") == "1");
    CHECK(evidence.at("recorder_snapshot_scope") == "final-after-join");
}
void assertion_unwind_cleanup() {
    Source source;
    Gate emit;
    std::promise<void> acquired;
    auto ready = acquired.get_future();
    source.before_emit = [&](uint64_t sequence) {
        if (sequence == 1) {
            acquired.set_value();
            emit.wait();
        }
    };
    device::Session session({{"assertion-unwind"}, {{"fixture"}}, {}}, {&source}, [](data::Published) {}, {});
    bool caught{};
    try {
        // Reverse destruction must release the gate BEFORE the future's join.
        std::future<void> stopping;
        Unblock cleanup{{&emit}};
        CHECK(ready.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        stopping = std::async(std::launch::async, [&] { session.stop(); });
        CHECK(inactive(session));
        throw std::runtime_error("Controlled assertion failure");
    } catch (const std::runtime_error &error) {
        CHECK(std::string_view(error.what()) == "Controlled assertion failure");
        caught = true;
    }
    CHECK(caught && !source.running && source.stops == 1);
    CHECK(session.error().empty());
    CHECK(session.produced() == 2 && session.committed() == 2);
}
int main(int argc, char **argv) {
    try {
        throwing_loggers();
        if (argc == 2 && std::string_view(argv[1]) == "throwing-loggers")
            return 0;
        first_commit_recovery();
        startup_saturation(false);
        startup_saturation(true);
        source_error_survives_diagnostics();
        assertion_unwind_cleanup();
        bounded_error_evidence();
        partial_start_cleanup();
        for (bool writer_failure : {false, true}) {
            Source source;
            Gate start, writer, producer;
            std::promise<void> error;
            auto failed = error.get_future();
            source.before_emit = [&](uint64_t sequence) {
                if (sequence == 1)
                    start.wait();
                if (writer_failure && sequence == 2)
                    producer.wait();
            };
            std::atomic_uint count{};
            std::atomic_bool signalled{};
            device::Session session(
                {{"capture"}, {{"fixture"}}, {}}, {&source},
                [&](data::Published) {
                    if (++count == 2) {
                        writer.wait();
                        if (writer_failure)
                            throw std::runtime_error("Injected disk failure");
                    }
                },
                [&](const LogRecord &) {
                    if (!signalled.exchange(true))
                        error.set_value();
                });
            Unblock cleanup{{&start, &writer, &producer}}; // Before Session destruction on assertion failure.
            start.open();
            if (writer_failure)
                writer.open();
            CHECK(failed.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
            writer.open();
            producer.open();
            session.stop();
            CHECK(!session.active());
            CHECK(!session.error().empty());
            CHECK(session.metrics().high_water <= session.queue_capacity());
            CHECK(session.metrics().dropped == 0); // LOSSLESS never silently drops.
            CHECK(session.preview_metrics().high_water == 1);
            CHECK(session.preview_metrics().dropped > 0);
            CHECK(writer_failure ? session.committed() == 1 : session.saturation() == 1);
            CHECK(session.error() == (writer_failure
                                          ? "Injected disk failure"
                                          : "LOSSLESS writer queue saturated; capture failed explicitly"));
            if (writer_failure)
                CHECK(session.saturation() == 0);
            else {
                CHECK(session.metrics().high_water == 32); // Documented recorder budget, observed under load.
                CHECK(session.produced() == session.committed() + 1); // Exactly the rejected enqueue.
            }
            CHECK(session.preview()); // immutable latest value remains consumable after stop
        }
        // Stop with a demonstrably full lossless queue and an acquired observation.
        Source source;
        Gate start, writer, producer;
        size_t capacity{};
        std::promise<void> writer_entered, queue_full;
        auto entered = writer_entered.get_future();
        auto full = queue_full.get_future();
        source.before_emit = [&](uint64_t sequence) {
            if (sequence == 1)
                start.wait();
            if (sequence == capacity + 2) {
                queue_full.set_value();
                producer.wait();
            }
        };
        std::atomic_uint count{};
        device::Session session({{"capture"}, {{"fixture"}}, {}}, {&source},
                                [&](data::Published) {
                                    if (++count == 2) {
                                        writer_entered.set_value();
                                        writer.wait();
                                    }
                                },
                                {});
        std::future<void> stopping;
        Unblock cleanup{{&start, &writer, &producer}};
        capacity = session.queue_capacity();
        start.open();
        CHECK(entered.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        CHECK(full.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        CHECK(session.metrics().high_water == session.queue_capacity());
        stopping = std::async(std::launch::async, [&] { session.stop(); });
        auto stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (session.active() && std::chrono::steady_clock::now() < stop_deadline)
            std::this_thread::yield();
        const bool stop_requested = !session.active();
        writer.open();
        producer.open();
        CHECK(stop_requested);
        CHECK(stopping.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        stopping.get();
        CHECK(session.metrics().dropped == 0);
        CHECK(session.error().empty());
        CHECK(session.produced() == session.committed());
        // Stop arrives while the next observation is inside acquisition. The
        // observation must be enqueued and drained, rather than cancelled away.
        Source finishing;
        std::promise<void> acquired;
        Gate emit;
        auto ready = acquired.get_future();
        finishing.before_emit = [&](uint64_t sequence) {
            if (sequence == 1) {
                acquired.set_value();
                emit.wait();
            }
        };
        device::Session final_frame({{"capture-final"}, {{"fixture"}}, {}}, {&finishing},
                                    [](data::Published) {}, {});
        std::future<void> final_stop;
        Unblock final_cleanup{{&emit}};
        CHECK(ready.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        final_stop = std::async(std::launch::async, [&] { final_frame.stop(); });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (final_frame.active() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        bool stopped = !final_frame.active();
        emit.open();
        CHECK(stopped);
        CHECK(final_stop.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        final_stop.get();
        CHECK(final_frame.error().empty());
        CHECK(final_frame.produced() == 2 && final_frame.committed() == 2);
        // A source failure before the first commit must retain its cause through
        // constructor cleanup, rather than becoming an empty/initial-frame error.
        Source startup_failure;
        startup_failure.before_emit = [](uint64_t) {
            throw std::runtime_error("Injected source startup failure");
        };
        bool failed_start{};
        try {
            device::Session rejected({{"startup"}, {{"fixture"}}, {}}, {&startup_failure},
                                     [](data::Published) { CHECK(false); }, {});
        } catch (const Failure &error) {
            failed_start = true;
            CHECK(error.error.code == Status::io);
            CHECK(error.error.message == "Injected source startup failure");
        }
        CHECK(failed_start);
        std::cout << "Bounded raw saturation/failure, preview isolation and cooperative shutdown passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
