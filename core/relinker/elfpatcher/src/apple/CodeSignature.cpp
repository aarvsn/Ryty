#include <elfpatcher/apple/CodeSignature.hpp>
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace Elfpatcher::Apple {

namespace {

constexpr std::uint32_t CSMAGIC_EMBEDDED_SIGNATURE = 0xfade0cc0;
constexpr std::uint32_t CSMAGIC_CODEDIRECTORY = 0xfade0c02;
constexpr std::uint32_t CSSLOT_CODEDIRECTORY = 0;
constexpr std::uint32_t CSSLOT_ENTITLEMENTS = 5;
constexpr std::uint32_t CodeDirectoryHeaderSize = 88;
constexpr std::uint32_t CodeDirectoryHashSize = 32;
constexpr std::uint32_t HashPage = 4096;
constexpr std::uint8_t CS_HASHTYPE_SHA256 = 2;
constexpr std::uint32_t CS_ADHOC = 0x2;
constexpr std::uint32_t CS_RUNTIME = 0x10000;
constexpr std::uint64_t CS_EXECSEG_MAIN_BINARY = 0x1;

// SHA-256 (FIPS 180-4), self-contained so the writers stay toolchain-neutral.
struct Sha256 {
    std::uint32_t state[8];
    std::uint64_t bits;
    std::uint8_t block[64];
    std::size_t used;

    Sha256() : state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}, bits(0), used(0) {}

    static std::uint32_t rotr(std::uint32_t value, std::uint32_t count) { return (value >> count) | (value << (32 - count)); }

    void compress(const std::uint8_t* chunk) {
        static constexpr std::uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t w[64];
        for (std::uint32_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(chunk[i * 4]) << 24) | (static_cast<std::uint32_t>(chunk[i * 4 + 1]) << 16)
                 | (static_cast<std::uint32_t>(chunk[i * 4 + 2]) << 8) | static_cast<std::uint32_t>(chunk[i * 4 + 3]);
        }
        for (std::uint32_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (std::uint32_t i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + s1 + ch + K[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void update(const std::uint8_t* data, std::size_t size) {
        bits += static_cast<std::uint64_t>(size) * 8;
        while (size > 0) {
            const std::size_t take = std::min(size, sizeof(block) - used);
            std::memcpy(block + used, data, take);
            used += take;
            data += take;
            size -= take;
            if (used == sizeof(block)) {
                compress(block);
                used = 0;
            }
        }
    }

    void finish(std::uint8_t digest[32]) {
        const std::uint64_t totalBits = bits;
        const std::uint8_t pad = 0x80;
        update(&pad, 1);
        const std::uint8_t zero = 0;
        while (used != 56) update(&zero, 1);
        std::uint8_t length[8];
        for (std::uint32_t i = 0; i < 8; ++i) length[i] = static_cast<std::uint8_t>((totalBits >> (56 - i * 8)) & 0xff);
        std::memcpy(block + 56, length, 8);
        compress(block);
        used = 0;
        for (std::uint32_t i = 0; i < 8; ++i) {
            digest[i * 4] = static_cast<std::uint8_t>((state[i] >> 24) & 0xff);
            digest[i * 4 + 1] = static_cast<std::uint8_t>((state[i] >> 16) & 0xff);
            digest[i * 4 + 2] = static_cast<std::uint8_t>((state[i] >> 8) & 0xff);
            digest[i * 4 + 3] = static_cast<std::uint8_t>(state[i] & 0xff);
        }
    }
};

std::uint32_t readBe32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24) | (static_cast<std::uint32_t>(data[1]) << 16)
         | (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
}

std::uint64_t readBe64(const std::uint8_t* data) {
    return (static_cast<std::uint64_t>(readBe32(data)) << 32) | readBe32(data + 4);
}

void writeBe32(std::vector<std::uint8_t>& buf, std::size_t offset, std::uint32_t val) {
    buf[offset] = static_cast<std::uint8_t>((val >> 24) & 0xff);
    buf[offset + 1] = static_cast<std::uint8_t>((val >> 16) & 0xff);
    buf[offset + 2] = static_cast<std::uint8_t>((val >> 8) & 0xff);
    buf[offset + 3] = static_cast<std::uint8_t>(val & 0xff);
}

void writeBe64(std::vector<std::uint8_t>& buf, std::size_t offset, std::uint64_t val) {
    writeBe32(buf, offset, static_cast<std::uint32_t>(val >> 32));
    writeBe32(buf, offset + 4, static_cast<std::uint32_t>(val & 0xffffffffu));
}

// Number of special hash slots the CodeDirectory reserves. Slot -5 holds the
// entitlements digest; codesign reserves one slot per possible special slice
// up to the highest occupied one, so an entitlements slice reserves five.
constexpr std::uint32_t SpecialSlotCount(bool hasEntitlements) { return hasEntitlements ? 5 : 0; }

std::uint32_t CodeDirectorySize(std::size_t identifierLength, bool hasEntitlements, std::size_t codeLimit) {
    const std::uint32_t nCodeSlots = static_cast<std::uint32_t>((codeLimit + HashPage - 1) / HashPage);
    return CodeDirectoryHeaderSize + static_cast<std::uint32_t>(identifierLength) + 1
        + SpecialSlotCount(hasEntitlements) * CodeDirectoryHashSize
        + nCodeSlots * CodeDirectoryHashSize;
}

void HashRange(const std::uint8_t* data, std::size_t size, std::uint8_t out[32]) {
    Sha256 hasher;
    hasher.update(data, size);
    hasher.finish(out);
}

}

std::uint32_t AdHocSignatureSize(std::size_t codeLimit, std::size_t identifierLength,
                                 const std::vector<std::uint8_t>* entitlements) {
    const bool hasEntitlements = entitlements != nullptr && !entitlements->empty();
    const std::uint32_t count = hasEntitlements ? 2 : 1;
    const std::uint32_t superBlobHeaderSize = 12 + count * 8;
    const std::uint32_t codeDirectorySize = CodeDirectorySize(identifierLength, hasEntitlements, codeLimit);
    const std::uint32_t paddedEntitlements = hasEntitlements
        ? static_cast<std::uint32_t>((entitlements->size() + 3u) & ~std::size_t(3)) : 0;
    return superBlobHeaderSize + ((codeDirectorySize + 3u) & ~3u) + paddedEntitlements;
}

std::vector<std::uint8_t> BuildAdHocSignature(const AdHocSignatureSpec& spec) {
    const bool hasEntitlements = spec.entitlements != nullptr && !spec.entitlements->empty();
    const std::uint32_t specialSlots = SpecialSlotCount(hasEntitlements);
    const std::uint32_t nCodeSlots = static_cast<std::uint32_t>((spec.codeLimit + HashPage - 1) / HashPage);
    const std::uint32_t identOffset = CodeDirectoryHeaderSize;
    const std::uint32_t specialHashOffset = identOffset + static_cast<std::uint32_t>(spec.identifier.size()) + 1;
    const std::uint32_t hashOffset = specialHashOffset + specialSlots * CodeDirectoryHashSize;
    const std::uint32_t codeDirectorySize = hashOffset + nCodeSlots * CodeDirectoryHashSize;

    const std::uint32_t count = hasEntitlements ? 2 : 1;
    const std::uint32_t superBlobHeaderSize = 12 + count * 8;
    const std::uint32_t codeDirectoryOffset = superBlobHeaderSize;
    // Every blob in the SuperBlob is 4-byte aligned (codesign convention).
    const std::uint32_t codeDirectoryPadded = (codeDirectorySize + 3u) & ~3u;
    std::uint32_t entitlementsOffset = 0;
    std::uint32_t entitlementsBlobSize = 0;
    std::vector<std::uint8_t> entitlementsPadded;
    if (hasEntitlements) {
        entitlementsBlobSize = static_cast<std::uint32_t>(spec.entitlements->size());
        entitlementsPadded = *spec.entitlements;
        while (entitlementsPadded.size() % 4 != 0) entitlementsPadded.push_back(0);
        entitlementsBlobSize = static_cast<std::uint32_t>(entitlementsPadded.size());
        entitlementsOffset = codeDirectoryOffset + codeDirectoryPadded;
    }
    const std::uint32_t totalBlobSize = hasEntitlements
        ? entitlementsOffset + entitlementsBlobSize
        : codeDirectoryOffset + codeDirectoryPadded;

    std::vector<std::uint8_t> blob(totalBlobSize, 0);

    // SuperBlob header (big endian).
    writeBe32(blob, 0, CSMAGIC_EMBEDDED_SIGNATURE);
    writeBe32(blob, 4, totalBlobSize);
    writeBe32(blob, 8, count);

    // BlobIndex[0]: the CodeDirectory.
    writeBe32(blob, 12, CSSLOT_CODEDIRECTORY);
    writeBe32(blob, 16, codeDirectoryOffset);
    if (hasEntitlements) {
        writeBe32(blob, 20, CSSLOT_ENTITLEMENTS);
        writeBe32(blob, 24, entitlementsOffset);
    }

    // CodeDirectory (version 0x20400: 88-byte header with exec-segment fields).
    const std::size_t cdOff = codeDirectoryOffset;
    writeBe32(blob, cdOff, CSMAGIC_CODEDIRECTORY);
    writeBe32(blob, cdOff + 4, codeDirectorySize);
    writeBe32(blob, cdOff + 8, 0x00020400); // version (supports SHA-256 + exec seg)
    writeBe32(blob, cdOff + 12, CS_ADHOC | (spec.hardenedRuntime ? CS_RUNTIME : 0));
    writeBe32(blob, cdOff + 16, hashOffset);
    writeBe32(blob, cdOff + 20, identOffset);
    writeBe32(blob, cdOff + 24, specialSlots);
    writeBe32(blob, cdOff + 28, nCodeSlots);
    writeBe32(blob, cdOff + 32, static_cast<std::uint32_t>(spec.codeLimit)); // codeLimit
    blob[cdOff + 36] = CodeDirectoryHashSize; // hashSize (SHA-256)
    blob[cdOff + 37] = CS_HASHTYPE_SHA256;
    blob[cdOff + 38] = 0; // platform
    blob[cdOff + 39] = 12; // pageSize (2^12 = 4096)
    writeBe32(blob, cdOff + 40, 0); // spare2
    writeBe32(blob, cdOff + 44, 0); // scatterOffset (unused)
    writeBe32(blob, cdOff + 48, 0); // teamIDOffset (none)
    writeBe32(blob, cdOff + 52, 0); // spare3
    // Version 0x20400 extended fields are 64-bit wide.
    writeBe64(blob, cdOff + 56, spec.codeLimit); // codeLimit64
    writeBe64(blob, cdOff + 64, 0);              // execSegBase (first executable page)
    writeBe64(blob, cdOff + 72, spec.codeLimit); // execSegLimit
    writeBe64(blob, cdOff + 80, CS_EXECSEG_MAIN_BINARY);

    // Identifier.
    std::memcpy(blob.data() + cdOff + identOffset, spec.identifier.c_str(), spec.identifier.size() + 1);

    // Special slots: stored immediately before the code hashes; slot -T sits at
    // hashOffset - T * 32. Only -5 (entitlements) is occupied here; the other
    // slots carry the SHA-256 of the empty string, matching codesign.
    std::uint8_t emptyHash[32];
    HashRange(nullptr, 0, emptyHash);
    for (std::uint32_t slot = 1; slot <= specialSlots; ++slot) {
        std::uint8_t* dst = blob.data() + cdOff + hashOffset - slot * CodeDirectoryHashSize;
        std::memcpy(dst, emptyHash, CodeDirectoryHashSize);
    }
    if (hasEntitlements) {
        std::uint8_t* dst = blob.data() + cdOff + hashOffset - 5 * CodeDirectoryHashSize;
        HashRange(entitlementsPadded.data(), entitlementsPadded.size(), dst);
    }

    // Page hashes: SHA-256 over every 4096-byte page of [0, codeLimit). The
    // final partial page is hashed over the bytes it actually has.
    std::uint8_t* hashes = blob.data() + cdOff + hashOffset;
    for (std::uint32_t slot = 0; slot < nCodeSlots; ++slot) {
        const std::size_t pageStart = static_cast<std::size_t>(slot) * HashPage;
        const std::size_t pageBytes = std::min<std::size_t>(HashPage, spec.codeLimit - pageStart);
        HashRange(spec.image + pageStart, pageBytes, hashes + slot * CodeDirectoryHashSize);
    }

    // Entitlements blob (raw plist bytes, padded to 4 bytes like codesign).
    if (hasEntitlements) {
        std::memcpy(blob.data() + entitlementsOffset, entitlementsPadded.data(), entitlementsPadded.size());
    }

    return blob;
}

bool FindCodeSignature(const std::uint8_t* image, std::size_t imageSize,
                       std::uint32_t& signatureOffset, std::uint32_t& signatureSize) {
    if (imageSize < 32) return false;
    std::uint32_t magic = 0;
    std::memcpy(&magic, image, 4);
    if (magic != 0xfeedfacf) return false;
    std::uint32_t ncmds = 0, sizeofcmds = 0;
    std::memcpy(&ncmds, image + 16, 4);
    std::memcpy(&sizeofcmds, image + 20, 4);
    if (32 + static_cast<std::uint64_t>(sizeofcmds) > imageSize) return false;
    std::size_t offset = 32;
    for (std::uint32_t i = 0; i < ncmds && offset + 8 <= 32 + sizeofcmds; ++i) {
        std::uint32_t cmd = 0, size = 0;
        std::memcpy(&cmd, image + offset, 4);
        std::memcpy(&size, image + offset + 4, 4);
        if (size < 8 || offset + size > 32 + sizeofcmds) return false;
        if (cmd == 0x1d) { // LC_CODE_SIGNATURE
            if (size < 16 || offset + 16 > imageSize) return false;
            std::memcpy(&signatureOffset, image + offset + 8, 4);
            std::memcpy(&signatureSize, image + offset + 12, 4);
            return true;
        }
        offset += size;
    }
    return false;
}

SignatureVerifyResult VerifyAdHocSignature(const std::uint8_t* image, std::size_t imageSize) {
    SignatureVerifyResult result;
    std::uint32_t signatureOffset = 0, signatureSize = 0;
    if (!FindCodeSignature(image, imageSize, signatureOffset, signatureSize)) {
        result.reason = "no embedded LC_CODE_SIGNATURE";
        return result;
    }
    if (static_cast<std::uint64_t>(signatureOffset) + signatureSize != imageSize) {
        result.reason = "signature does not close the file";
        return result;
    }
    const std::uint8_t* sig = image + signatureOffset;
    if (signatureSize < 20 || readBe32(sig) != CSMAGIC_EMBEDDED_SIGNATURE) {
        result.reason = "SuperBlob magic missing";
        return result;
    }
    const std::uint32_t totalLength = readBe32(sig + 4);
    const std::uint32_t count = readBe32(sig + 8);
    if (totalLength != signatureSize) {
        result.reason = "SuperBlob length mismatch";
        return result;
    }
    if (count < 1 || 12 + count * 8 > signatureSize) {
        result.reason = "SuperBlob index out of range";
        return result;
    }

    std::uint32_t codeDirectoryOffset = 0;
    std::uint32_t entitlementsOffset = 0, entitlementsLength = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t type = readBe32(sig + 12 + i * 8);
        const std::uint32_t offset = readBe32(sig + 12 + i * 8 + 4);
        if (type == CSSLOT_CODEDIRECTORY) codeDirectoryOffset = offset;
        if (type == CSSLOT_ENTITLEMENTS) {
            entitlementsOffset = offset;
            entitlementsLength = 1; // resolved from the next index or blob length below
        }
    }
    if (codeDirectoryOffset == 0 || codeDirectoryOffset >= signatureSize) {
        result.reason = "CodeDirectory slot missing";
        return result;
    }
    if (entitlementsOffset != 0 && count >= 2) {
        // Entitlements blob length: distance to the superblob end (it is the
        // last blob in the embedded signature).
        entitlementsLength = signatureSize - entitlementsOffset;
        result.hasEntitlements = true;
    }

    const std::uint8_t* cd = sig + codeDirectoryOffset;
    if (codeDirectoryOffset + CodeDirectoryHeaderSize > signatureSize) {
        result.reason = "CodeDirectory header truncated";
        return result;
    }
    if (readBe32(cd) != CSMAGIC_CODEDIRECTORY) {
        result.reason = "CodeDirectory magic missing";
        return result;
    }
    const std::uint32_t cdLength = readBe32(cd + 4);
    if (codeDirectoryOffset + cdLength > signatureSize) {
        result.reason = "CodeDirectory length out of range";
        return result;
    }
    const std::uint32_t version = readBe32(cd + 8);
    if (version < 0x00020400) {
        result.reason = "CodeDirectory version too old";
        return result;
    }
    const std::uint32_t flags = readBe32(cd + 12);
    if ((flags & CS_ADHOC) == 0) {
        result.reason = "CodeDirectory is not ad-hoc";
        return result;
    }
    const std::uint32_t hashOffset = readBe32(cd + 16);
    const std::uint32_t identOffset = readBe32(cd + 20);
    const std::uint32_t nSpecialSlots = readBe32(cd + 24);
    const std::uint32_t nCodeSlots = readBe32(cd + 28);
    const std::uint32_t codeLimit = readBe32(cd + 32);
    if (cd[36] != CodeDirectoryHashSize || cd[37] != CS_HASHTYPE_SHA256) {
        result.reason = "unexpected hash type";
        return result;
    }
    if (cd[39] != 12) {
        result.reason = "unexpected page size";
        return result;
    }
    if (codeLimit != signatureOffset) {
        result.reason = "codeLimit does not cover the signed image";
        return result;
    }
    if (hashOffset < identOffset || hashOffset > cdLength) {
        result.reason = "hash array out of range";
        return result;
    }
    if (hashOffset < nSpecialSlots * CodeDirectoryHashSize
        || hashOffset - nSpecialSlots * CodeDirectoryHashSize < identOffset + 2) {
        result.reason = "identifier out of range";
        return result;
    }
    {
        const std::uint8_t* identifierBytes = cd + identOffset;
        const std::size_t maxIdentifier = hashOffset - nSpecialSlots * CodeDirectoryHashSize - identOffset;
        std::size_t length = 0;
        while (length < maxIdentifier && identifierBytes[length] != 0) ++length;
        if (length == 0 || length >= maxIdentifier) {
            result.reason = "identifier not terminated";
            return result;
        }
        result.identifier.assign(reinterpret_cast<const char*>(identifierBytes), length);
    }
    if (readBe64(cd + 80) != CS_EXECSEG_MAIN_BINARY) {
        result.reason = "missing main-binary exec-segment flag";
        return result;
    }

    // Special slot -5 must carry the entitlements digest when present.
    if (result.hasEntitlements) {
        if (nSpecialSlots < 5 || hashOffset < 5 * CodeDirectoryHashSize) {
            result.reason = "entitlements special slot missing";
            return result;
        }
        if (entitlementsOffset + entitlementsLength > signatureSize) {
            result.reason = "entitlements blob out of range";
            return result;
        }
        std::uint8_t expected[32];
        HashRange(sig + entitlementsOffset, entitlementsLength, expected);
        const std::uint8_t* actual = cd + hashOffset - 5 * CodeDirectoryHashSize;
        if (std::memcmp(expected, actual, CodeDirectoryHashSize) != 0) {
            result.reason = "entitlements hash mismatch";
            return result;
        }
    }

    // Page hashes over [0, codeLimit).
    if (static_cast<std::uint64_t>(nCodeSlots) * HashPage < codeLimit || codeLimit == 0) {
        result.reason = "code slot count mismatch";
        return result;
    }
    if (static_cast<std::uint64_t>(codeDirectoryOffset) + hashOffset + static_cast<std::uint64_t>(nCodeSlots) * CodeDirectoryHashSize > signatureSize) {
        result.reason = "page hash array out of range";
        return result;
    }
    const std::uint8_t* hashes = cd + hashOffset;
    for (std::uint32_t slot = 0; slot < nCodeSlots; ++slot) {
        const std::size_t pageStart = static_cast<std::size_t>(slot) * HashPage;
        const std::size_t pageBytes = std::min<std::size_t>(HashPage, codeLimit - pageStart);
        std::uint8_t expected[32];
        HashRange(image + pageStart, pageBytes, expected);
        if (std::memcmp(expected, hashes + slot * CodeDirectoryHashSize, CodeDirectoryHashSize) != 0) {
            result.reason = "page " + std::to_string(slot) + " hash mismatch";
            return result;
        }
    }

    result.ok = true;
    result.reason = "ad-hoc signature OK (" + result.identifier + ")";
    return result;
}

}
