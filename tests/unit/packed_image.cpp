#include <mantis/image_layout.hpp>
#include <mantis/data_io.hpp>
#include <array>
#include <algorithm>
#include <iostream>
using namespace mantis;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed: " #x); } while (false)
template<class F> void rejects(F fn) { bool failed{}; try { fn(); } catch (const Failure &) { failed = true; } CHECK(failed); }
int main() {
    try {
        // Kernel MIPI RAW10 ordering: high eight bits, then LSB pairs 0,1,2,3.
        const std::array<std::byte, 13> bytes{std::byte{0}, std::byte{0x55}, std::byte{0xaa}, std::byte{0xff}, std::byte{0xe4},
            std::byte{0xa5}, std::byte{0xa5}, std::byte{0xa5}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0x39}};
        data::Y10PView view(bytes, 4, 2, 8);
        const std::array<uint16_t, 4> first{0, 341, 682, 1023}, second{1, 2, 3, 0};
        for (uint32_t x = 0; x < 4; ++x) { CHECK(view.sample(x, 0) == first[x]); CHECK(view.sample(x, 1) == second[x]); }
        rejects([&] { data::Y10PView invalid(bytes, 3, 2, 8); });
        rejects([&] { data::Y10PView invalid{std::span{bytes}.first(12), 4, 2, 8}; });
        rejects([&] { data::Y10PView invalid(bytes, 4, 2, 4); });
        rejects([&] { (void)view.sample(4, 0); });
        data::Packet image; image.type = schema::image;
        image.header.metadata = {{"fourcc", "Y10P"}, {"org.mantis.image.layout", "mipi-raw10-v1"},
            {"org.mantis.image.bits_per_sample", "10"}, {"org.mantis.image.width", "4"},
            {"org.mantis.image.height", "2"}, {"org.mantis.image.row_stride_bytes", "8"}};
        image.attributes.push_back({{std::string(data::packed_image_bytes), schema::ScalarType::u8, {bytes.size()}, {1}, "byte"}, memory::copy(bytes)});
        auto layout = data::image_layout(image); CHECK(layout.width == 4 && layout.height == 2);
        std::array<std::byte, 4> display{};
        data::grayscale_row(image, layout, 0, display);
        CHECK(std::ranges::equal(display, std::span(bytes).first(4)));
        data::grayscale_row(image, layout, 1, display);
        CHECK(std::ranges::all_of(display, [](auto x) { return x == std::byte{0}; }));
        CHECK(std::ranges::equal(*image.attributes[0].buffer.map_read(), bytes));
        auto wrong = image; wrong.attributes[0].descriptor.shape = {2, 4}; wrong.attributes[0].descriptor.stride = {8, 1};
        rejects([&] { (void)data::image_layout(wrong); });
        auto missing_layout = image;
        missing_layout.header.metadata.erase("org.mantis.image.layout");
        // A mislabeled u8 grid must never make packed RAW10 look like RAW8.
        missing_layout.attributes[0].descriptor = {"org.mantis.pixels", schema::ScalarType::u8, {2, 4}, {8, 1}, "intensity"};
        rejects([&] { (void)data::image_layout(missing_layout); });
        std::cout << "Y10P known vectors, padding, display reduction and raw preservation passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
