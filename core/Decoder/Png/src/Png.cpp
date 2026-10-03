#include "Decoder/Png.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <stdexcept>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

namespace Decoder::Png {

namespace {

constexpr std::array<std::uint8_t, 8> SIGNATURE = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr std::size_t CHUNK_OVERHEAD = 12;
constexpr std::size_t IHDR_SIZE = 13;
constexpr std::uint32_t MAX_DIMENSION = 0x7FFFFFFF;

std::uint32_t readBigEndian32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) << 24 | static_cast<std::uint32_t>(bytes[1]) << 16
        | static_cast<std::uint32_t>(bytes[2]) << 8 | static_cast<std::uint32_t>(bytes[3]);
}

bool isChunkType(const std::uint8_t* bytes, const char* type) {
    return std::equal(bytes, bytes + 4, type);
}

bool isValidFormat(std::uint8_t bitDepth, std::uint8_t colorType) {
    switch (colorType) {
    case 0:
        return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16;
    case 3:
        return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8;
    case 2:
    case 4:
    case 6:
        return bitDepth == 8 || bitDepth == 16;
    default:
        return false;
    }
}

void appendBytes(void* context, void* data, int size) {
    auto* output = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    output->insert(output->end(), bytes, bytes + size);
}

}  // namespace

std::optional<Header> ParseHeader(std::span<const std::uint8_t> png) {
    const std::size_t firstChunk = SIGNATURE.size();
    if (png.size() < firstChunk + CHUNK_OVERHEAD + IHDR_SIZE) return std::nullopt;
    if (!std::equal(SIGNATURE.begin(), SIGNATURE.end(), png.begin())) return std::nullopt;
    if (readBigEndian32(&png[firstChunk]) != IHDR_SIZE || !isChunkType(&png[firstChunk + 4], "IHDR")) return std::nullopt;

    const std::uint8_t* ihdr = &png[firstChunk + 8];
    Header header{};
    header.width = readBigEndian32(ihdr);
    header.height = readBigEndian32(ihdr + 4);
    header.bitDepth = ihdr[8];
    header.colorType = static_cast<ColorType>(ihdr[9]);
    header.interlaced = ihdr[12] == 1;
    if (header.width == 0 || header.height == 0 || header.width > MAX_DIMENSION || header.height > MAX_DIMENSION) return std::nullopt;
    if (!isValidFormat(ihdr[8], ihdr[9]) || ihdr[10] != 0 || ihdr[11] != 0 || ihdr[12] > 1) return std::nullopt;

    std::size_t offset = firstChunk + CHUNK_OVERHEAD + IHDR_SIZE;
    while (png.size() - offset >= CHUNK_OVERHEAD) {
        const std::uint32_t length = readBigEndian32(&png[offset]);
        const std::uint8_t* type = &png[offset + 4];
        if (isChunkType(type, "tRNS")) header.hasTransparency = true;
        if (isChunkType(type, "tRNS") || isChunkType(type, "IDAT") || isChunkType(type, "IEND")) break;
        if (length > png.size() - offset - CHUNK_OVERHEAD) break;
        offset += CHUNK_OVERHEAD + length;
    }
    return header;
}

std::optional<Image> Decode(std::span<const std::uint8_t> png) {
    if (png.empty() || png.size() > INT_MAX) return std::nullopt;

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &width, &height, &channels, 4);
    if (!decoded) return std::nullopt;

    Image image{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), {}};
    image.pixels.assign(decoded, decoded + static_cast<std::size_t>(image.width) * image.height * 4);
    stbi_image_free(decoded);
    return image;
}

std::vector<std::uint8_t> Encode(std::span<const std::uint8_t> pixels, std::uint32_t width, std::uint32_t height,
                                 std::uint32_t channels) {
    if (channels < 1 || channels > 4) throw std::invalid_argument("Png::Encode: channels must be 1-4");
    if (width == 0 || height == 0 || static_cast<std::uint64_t>(width) * channels > INT_MAX || height > INT_MAX) {
        throw std::invalid_argument("Png::Encode: unsupported image size");
    }
    if (pixels.size() < static_cast<std::size_t>(width) * height * channels) {
        throw std::invalid_argument("Png::Encode: pixel buffer is too small");
    }

    std::vector<std::uint8_t> output;
    const int stride = static_cast<int>(width * channels);
    const int written = stbi_write_png_to_func(appendBytes, &output, static_cast<int>(width), static_cast<int>(height),
                                               static_cast<int>(channels), pixels.data(), stride);
    if (written == 0) throw std::runtime_error("Png::Encode: encoding failed");
    return output;
}

}  // namespace Decoder::Png
