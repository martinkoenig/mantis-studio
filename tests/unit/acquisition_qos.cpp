#include <future>
#include <iostream>
#include <mantis/device_runtime.hpp>
using namespace mantis;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("Check failed: ") + #x); } while (false)
class Source final : public device::ImageStream {
    device::Descriptor descriptor_;
    uint64_t sequence_{};
    data::Published image_;
  public:
    std::function<void(uint64_t)> before_emit;
    Source() {
        descriptor_.id = {"fixture"};
        data::Packet image; image.type = schema::image;
        memory::BufferBuilder bytes(4);
        image.attributes.push_back({{"org.mantis.pixels", schema::ScalarType::u8, {2, 2}, {2, 1}, ""}, std::move(bytes).publish()});
        image_ = data::publish(std::move(image));
    }
    const device::Descriptor &descriptor() const override { return descriptor_; }
    bool source_paced() const override { return true; }
    Result<void> start() override { return {}; }
    Result<void> stop() override { return {}; }
    Result<data::Published> next() override {
        data::Packet set; set.type = schema::frameset; set.header.sequence.value = sequence_++;
        if (before_emit) before_emit(set.header.sequence.value);
        set.frames = {image_, image_}; return data::publish(std::move(set));
    }
};
int main() {
    try {
        for (bool writer_failure : {false, true}) {
            Source source;
            std::promise<void> release, error;
            auto gate = release.get_future().share(); auto failed = error.get_future();
            std::atomic_uint count{}; std::atomic_bool signalled{};
            device::Session session({{"capture"}, {{"fixture"}}, {}}, {&source},
                [&](data::Published) {
                    if (++count == 2) { gate.wait(); if (writer_failure) throw std::runtime_error("Injected disk failure"); }
                }, [&](const LogRecord &) { if (!signalled.exchange(true)) error.set_value(); });
            if (writer_failure) release.set_value();
            CHECK(failed.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
            if (!writer_failure) release.set_value();
            session.stop();
            CHECK(!session.active()); CHECK(!session.error().empty());
            CHECK(session.metrics().high_water <= session.queue_capacity());
            CHECK(session.metrics().dropped == 0); // LOSSLESS never silently drops.
            CHECK(session.preview_metrics().high_water == 1);
            CHECK(session.preview_metrics().dropped > 0);
            CHECK(writer_failure ? session.committed() == 1 : session.saturation() == 1);
            CHECK(session.preview()); // immutable latest value remains consumable after stop
        }
        // Cooperative stop wakes the producer blocked behind a full lossless queue.
        Source source;
        std::promise<void> release, writer_entered;
        auto gate = release.get_future().share(); auto entered = writer_entered.get_future();
        std::atomic_uint count{};
        device::Session session({{"capture"}, {{"fixture"}}, {}}, {&source}, [&](data::Published) {
            if (++count == 2) { writer_entered.set_value(); gate.wait(); }
        }, {});
        CHECK(entered.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        auto stopping = std::async(std::launch::async, [&] { session.stop(); });
        release.set_value();
        CHECK(stopping.wait_for(std::chrono::seconds(2)) == std::future_status::ready); stopping.get();
        CHECK(session.metrics().dropped == 0);
        CHECK(session.error().empty());
        CHECK(session.produced() == session.committed());
        // Stop arrives while the next observation is inside acquisition. The
        // observation must be enqueued and drained, rather than cancelled away.
        Source finishing;
        std::promise<void> acquired, emit;
        auto held = emit.get_future().share(); auto ready = acquired.get_future();
        finishing.before_emit = [&](uint64_t sequence) { if (sequence == 1) { acquired.set_value(); held.wait(); } };
        device::Session final_frame({{"capture-final"}, {{"fixture"}}, {}}, {&finishing}, [](data::Published) {}, {});
        CHECK(ready.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        auto final_stop = std::async(std::launch::async, [&] { final_frame.stop(); });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (final_frame.active() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        bool stopped = !final_frame.active(); emit.set_value();
        CHECK(stopped);
        CHECK(final_stop.wait_for(std::chrono::seconds(2)) == std::future_status::ready); final_stop.get();
        CHECK(final_frame.error().empty());
        CHECK(final_frame.produced() == 2 && final_frame.committed() == 2);
        std::cout << "Bounded raw saturation/failure, preview isolation and cooperative shutdown passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
