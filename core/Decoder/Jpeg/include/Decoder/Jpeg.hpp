#ifndef DECODER_JPEG_HPP
#define DECODER_JPEG_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Decoder::Jpeg {

struct Image {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t channels;
    std::vector<std::uint8_t> pixels;
};

std::vector<std::uint8_t> Encode(std::span<const std::uint8_t> pixels, std::uint32_t width, std::uint32_t height,
                                 std::uint32_t channels, int quality);

std::optional<Image> Decode(std::span<const std::uint8_t> jpeg);

}  // namespace Decoder::Jpeg

#endif  // DECODER_JPEG_HPP
