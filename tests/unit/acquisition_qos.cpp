#include <future>
#include <iostream>
#include <mantis/device_runtime.hpp>
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
        return {};
    }
    Result<void> stop() override {
        return {};
    }
    Result<data::Published> next() override {
        data::Packet set;
        set.type = schema::frameset;
        set.header.sequence.value = sequence_++;
        if (before_emit)
            before_emit(set.header.sequence.value);
        set.frames = {image_, image_};
        return data::publish(std::move(set));
    }
};
int main() {
    try {
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
        Unblock cleanup{{&start, &writer, &producer}};
        capacity = session.queue_capacity();
        start.open();
        CHECK(entered.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        CHECK(full.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        CHECK(session.metrics().high_water == session.queue_capacity());
        auto stopping = std::async(std::launch::async, [&] { session.stop(); });
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
        Unblock final_cleanup{{&emit}};
        CHECK(ready.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        auto final_stop = std::async(std::launch::async, [&] { final_frame.stop(); });
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
