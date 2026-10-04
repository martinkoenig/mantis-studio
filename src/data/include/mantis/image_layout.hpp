#pragma once
#include <mantis/data.hpp>
namespace mantis::data {
inline constexpr std::string_view packed_image_bytes = "org.mantis.image.packed_bytes";
// MIPI RAW10 / Y10P: four high-byte samples followed by their four 2-bit tails.
// This consumer view never mutates or expands the recorded byte storage.
class Y10PView {
    std::span<const std::byte> bytes_;
    uint32_t width_, height_, stride_;
  public:
    Y10PView(std::span<const std::byte>, uint32_t width, uint32_t height, uint32_t row_stride);
    uint16_t sample(uint32_t x, uint32_t y) const;
    void display_row(uint32_t y, std::span<std::byte> output) const;
};
enum class ImagePacking { raw8, mipi_raw10 };
struct ImageLayout { uint32_t width{}, height{}, row_stride{}; ImagePacking packing{}; };
ImageLayout image_layout(const Packet &);
// Presentation-only conversion: Y10P samples >> 2. Output is a tight grayscale
// row, with no intermediate 16-bit image and no involvement in acquisition.
void grayscale_row(const Packet &, const ImageLayout &, uint32_t y, std::span<std::byte> output);
} // namespace mantis::data
