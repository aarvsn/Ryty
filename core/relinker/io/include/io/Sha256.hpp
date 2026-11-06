#ifndef IO_SHA256_HPP
#define IO_SHA256_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace Io {

// Small standard SHA-256 (FIPS 180-4) used for the porting report digests and
// diagnostics. The macOS code signature path keeps its own dedicated hasher;
// this one is the general-purpose helper for the CLI.
class Sha256 {
public:
    Sha256();
    void Update(const std::uint8_t* data, std::size_t length);
    void Update(const std::vector<std::uint8_t>& data);
    // Finalizes and returns the digest as 64 lowercase hex characters. The
    // instance must not be updated afterwards.
    [[nodiscard]] std::string FinalHex();

private:
    void Compress(const std::uint8_t* block);
    std::uint32_t state[8];
    std::uint64_t bitLength = 0;
    std::uint8_t buffer[64];
    std::size_t bufferLength = 0;
    bool finalized = false;
};

// One-shot helper: SHA-256 of a byte vector as lowercase hex.
[[nodiscard]] std::string Sha256Hex(const std::vector<std::uint8_t>& data);

}

#endif
