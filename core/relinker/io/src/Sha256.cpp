#include <io/Sha256.hpp>
#include <cstring>

namespace Io {

namespace {

constexpr std::uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr std::uint32_t RotateRight(const std::uint32_t value, const unsigned amount) {
    return (value >> amount) | (value << (32 - amount));
}

}

Sha256::Sha256() {
    state[0] = 0x6a09e667u;
    state[1] = 0xbb67ae85u;
    state[2] = 0x3c6ef372u;
    state[3] = 0xa54ff53au;
    state[4] = 0x510e527fu;
    state[5] = 0x9b05688cu;
    state[6] = 0x1f83d9abu;
    state[7] = 0x5be0cd19u;
    std::memset(buffer, 0, sizeof(buffer));
}

void Sha256::Compress(const std::uint8_t* block) {
    std::uint32_t w[64];
    for (unsigned index = 0; index < 16; ++index) {
        w[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24)
            | (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16)
            | (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8)
            | static_cast<std::uint32_t>(block[index * 4 + 3]);
    }
    for (unsigned index = 16; index < 64; ++index) {
        const std::uint32_t s0 = RotateRight(w[index - 15], 7) ^ RotateRight(w[index - 15], 18) ^ (w[index - 15] >> 3);
        const std::uint32_t s1 = RotateRight(w[index - 2], 17) ^ RotateRight(w[index - 2], 19) ^ (w[index - 2] >> 10);
        w[index] = w[index - 16] + s0 + w[index - 7] + s1;
    }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (unsigned index = 0; index < 64; ++index) {
        const std::uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + ch + K[index] + w[index];
        const std::uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void Sha256::Update(const std::uint8_t* data, std::size_t length) {
    if (finalized) return;
    bitLength += static_cast<std::uint64_t>(length) * 8u;
    while (length > 0) {
        const std::size_t take = std::min(length, sizeof(buffer) - bufferLength);
        std::memcpy(buffer + bufferLength, data, take);
        bufferLength += take;
        data += take;
        length -= take;
        if (bufferLength == sizeof(buffer)) {
            Compress(buffer);
            bufferLength = 0;
        }
    }
}

void Sha256::Update(const std::vector<std::uint8_t>& data) {
    Update(data.data(), data.size());
}

std::string Sha256::FinalHex() {
    if (!finalized) {
        const std::uint64_t totalBits = bitLength;
        const std::uint8_t padding = 0x80;
        Update(&padding, 1);
        bitLength = totalBits; // padding must not count toward the message length
        const std::uint8_t zero = 0;
        while (bufferLength != 56) Update(&zero, 1);
        std::uint8_t lengthBytes[8];
        for (unsigned index = 0; index < 8; ++index)
            lengthBytes[index] = static_cast<std::uint8_t>((totalBits >> (56 - index * 8)) & 0xff);
        bitLength = 0; // the length block itself is not message data
        Update(lengthBytes, sizeof(lengthBytes));
        finalized = true;
    }

    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (const std::uint32_t word : state) {
        for (int shift = 28; shift >= 0; shift -= 4)
            hex += digits[(word >> shift) & 0xf];
    }
    return hex;
}

std::string Sha256Hex(const std::vector<std::uint8_t>& data) {
    Sha256 hash;
    hash.Update(data);
    return hash.FinalHex();
}

}
