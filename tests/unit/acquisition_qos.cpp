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
        std::cout << "Bounded raw saturation/failure, preview isolation and cooperative shutdown passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
