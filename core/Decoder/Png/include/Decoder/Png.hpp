#ifndef DECODER_PNG_HPP
#define DECODER_PNG_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Decoder::Png {

enum class ColorType : std::uint8_t {
    Grayscale = 0,
    Rgb = 2,
    Palette = 3,
    GrayscaleAlpha = 4,
    Rgba = 6,
};

struct Header {
    std::uint32_t width;
    std::uint32_t height;
    std::uint8_t bitDepth;
    ColorType colorType;
    bool interlaced;
    bool hasTransparency;
};

struct Image {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<std::uint8_t> pixels;
};

std::optional<Header> ParseHeader(std::span<const std::uint8_t> png);

std::optional<Image> Decode(std::span<const std::uint8_t> png);

std::vector<std::uint8_t> Encode(std::span<const std::uint8_t> pixels, std::uint32_t width, std::uint32_t height,
                                 std::uint32_t channels);

}  // namespace Decoder::Png

#endif  // DECODER_PNG_HPP
