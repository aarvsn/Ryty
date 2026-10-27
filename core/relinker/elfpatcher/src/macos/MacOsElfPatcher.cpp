#include <elfpatcher/macos/MacOsElfPatcher.hpp>
#include <io/BufferUtils.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>

#if defined(__APPLE__)
extern "C" void RytyMetalInitializeDevice(void);
extern "C" int RytyMetalIsSupported(void);
extern "C" const char* RytyMetalGetDeviceName(void);
extern "C" void RytyMetalSetClearColor(double r, double g, double b, double a);
extern "C" void RytyMetalSetDepthStencilEnabled(int enabled);
#else
extern "C" void RytyMetalInitializeDevice(void) {
    // Metal API context stub for non-Apple build environments
}
extern "C" int RytyMetalIsSupported(void) {
    return 1;
}
extern "C" const char* RytyMetalGetDeviceName(void) {
    return "Metal Default Device";
}
extern "C" void RytyMetalSetClearColor(double r, double g, double b, double a) {
    (void)r; (void)g; (void)b; (void)a;
}
extern "C" void RytyMetalSetDepthStencilEnabled(int enabled) {
    (void)enabled;
}
extern "C" void* RytyMetalCreateTexture2D(int width, int height, int pixelFormat) {
    (void)width; (void)height; (void)pixelFormat;
    static int dummyTexture = 1;
    return &dummyTexture;
}
extern "C" void RytyMetalUpdateTexture2D(void* handle, const void* bytes, int bytesPerRow) {
    (void)handle; (void)bytes; (void)bytesPerRow;
}
extern "C" void RytyMetalDestroyTexture2D(void* handle) {
    (void)handle;
}
extern "C" void* RytyMetalCreateBuffer(const void* data, unsigned long length) {
    (void)data; (void)length;
    static int dummyBuffer = 1;
    return &dummyBuffer;
}
extern "C" void RytyMetalUpdateBuffer(void* handle, const void* data, unsigned long length, unsigned long offset) {
    (void)handle; (void)data; (void)length; (void)offset;
}
extern "C" void RytyMetalDestroyBuffer(void* handle) {
    (void)handle;
}
extern "C" void RytyMetalSetViewport(double x, double y, double width, double height, double znear, double zfar) {
    (void)x; (void)y; (void)width; (void)height; (void)znear; (void)zfar;
}
extern "C" void RytyMetalSetScissorRect(unsigned int x, unsigned int y, unsigned int width, unsigned int height) {
    (void)x; (void)y; (void)width; (void)height;
}
extern "C" void RytyMetalSetBlendMode(int blendMode) {
    (void)blendMode;
}
extern "C" void RytyMetalSetCullMode(int cullMode) {
    (void)cullMode;
}
extern "C" void RytyMetalSetTriangleFillMode(int fillMode) {
    (void)fillMode;
}
extern "C" void RytyMetalSetFrontFacingWinding(int winding) {
    (void)winding;
}
extern "C" void RytyMetalSetDepthBias(float depthBias, float slopeScale, float clamp) {
    (void)depthBias; (void)slopeScale; (void)clamp;
}
extern "C" void RytyMetalSetStencilReferenceValue(unsigned int referenceValue) {
    (void)referenceValue;
}
extern "C" void RytyMetalSetRenderPipelineState(void* pipelineState) {
    (void)pipelineState;
}
extern "C" void RytyMetalSetComputePipelineState(void* computePipeline) {
    (void)computePipeline;
}
extern "C" void RytyMetalDispatchThreadgroups(unsigned int threadsX, unsigned int threadsY, unsigned int threadsZ, unsigned int tgX, unsigned int tgY, unsigned int tgZ) {
    (void)threadsX; (void)threadsY; (void)threadsZ; (void)tgX; (void)tgY; (void)tgZ;
}
extern "C" void RytyMetalSetSamplerState(int index, void* sampler) {
    (void)index; (void)sampler;
}
extern "C" void RytyMetalDrawPrimitives(int primitiveType, unsigned int start, unsigned int count) {
    (void)primitiveType; (void)start; (void)count;
}
extern "C" void RytyMetalDrawIndexedPrimitives(int primitiveType, unsigned int indexCount, int indexType, void* indexBuffer, unsigned int indexBufferOffset) {
    (void)primitiveType; (void)indexCount; (void)indexType; (void)indexBuffer; (void)indexBufferOffset;
}
extern "C" unsigned long RytyMetalGetMaxThreadsPerThreadgroup(void) {
    return 1024;
}
extern "C" int RytyMetalSupportsFeatureSet(int featureSet) {
    (void)featureSet;
    return 1;
}
extern "C" void RytyMetalSetSampleCount(int sampleCount) {
    (void)sampleCount;
}
extern "C" void RytyMetalSetRasterizerDiscardEnabled(int enabled) {
    (void)enabled;
}
extern "C" int RytyMetalGetDeviceCapabilities(void) {
    return 0x00010100;
}
#endif

namespace Elfpatcher::MacOs {

namespace {

constexpr std::uint32_t MH_MAGIC_64 = 0xfeedfacf;
constexpr std::uint32_t CPU_TYPE_X86_64 = 0x01000007;
constexpr std::uint32_t CPU_SUBTYPE_ALL = 0x00000003;
constexpr std::uint32_t MH_EXECUTE = 0x00000002;

constexpr std::uint32_t LC_SEGMENT_64 = 0x19;
constexpr std::uint32_t LC_SYMTAB = 0x02;
constexpr std::uint32_t LC_DYSYMTAB = 0x0b;
constexpr std::uint32_t LC_LOAD_DYLIB = 0x0c;
constexpr std::uint32_t LC_LOAD_DYLINKER = 0x0e;
constexpr std::uint32_t LC_UUID = 0x1b;
constexpr std::uint32_t LC_MAIN = 0x80000028;
constexpr std::uint32_t LC_RPATH = 0x8000001c;
constexpr std::uint32_t LC_SOURCE_VERSION = 0x2a;
constexpr std::uint32_t LC_BUILD_VERSION = 0x32;
constexpr std::uint32_t LC_CODE_SIGNATURE = 0x1d;

struct MachOHeader64 {
    std::uint32_t Magic;
    std::uint32_t CpuType;
    std::uint32_t CpuSubtype;
    std::uint32_t FileType;
    std::uint32_t CmdsCount;
    std::uint32_t CmdsSize;
    std::uint32_t Flags;
    std::uint32_t Reserved;
};

struct SegmentCommand64 {
    std::uint32_t Cmd;
    std::uint32_t CmdSize;
    char SegName[16];
    std::uint64_t VAddr;
    std::uint64_t VSize;
    std::uint64_t FileOff;
    std::uint64_t FileSize;
    std::uint32_t MaxProt;
    std::uint32_t InitProt;
    std::uint32_t NSects;
    std::uint32_t Flags;
};

struct MainCommand {
    std::uint32_t Cmd;
    std::uint32_t CmdSize;
    std::uint64_t EntryOffset;
    std::uint64_t StackSize;
};

struct DylibCommand {
    std::uint32_t Cmd;
    std::uint32_t CmdSize;
    std::uint32_t NameOffset;
    std::uint32_t Timestamp;
    std::uint32_t CurrentVersion;
    std::uint32_t CompatibilityVersion;
};

struct DylinkerCommand {
    std::uint32_t Cmd;
    std::uint32_t CmdSize;
    std::uint32_t NameOffset;
};

struct RpathCommand {
    std::uint32_t Cmd;
    std::uint32_t CmdSize;
    std::uint32_t PathOffset;
};

struct BuildVersionCommand {
    std::uint32_t Cmd;
    std::uint32_t CmdSize;
    std::uint32_t Platform;
    std::uint32_t MinOS;
    std::uint32_t SDK;
    std::uint32_t NTools;
};

struct LinkeditDataCommand {
    std::uint32_t Cmd;
    std::uint32_t CmdSize;
    std::uint32_t DataOff;
    std::uint32_t DataSize;
};

struct UuidCommand {
    std::uint32_t Cmd;
    std::uint32_t CmdSize;
    std::uint8_t Uuid[16];
};

struct SourceVersionCommand {
    std::uint32_t Cmd;
    std::uint32_t CmdSize;
    std::uint64_t Version;
};

struct SymtabCommand {
    std::uint32_t Cmd;
    std::uint32_t CmdSize;
    std::uint32_t SymOff;
    std::uint32_t NSyms;
    std::uint32_t StrOff;
    std::uint32_t StrSize;
};

struct DysymtabCommand {
    std::uint32_t Cmd;
    std::uint32_t CmdSize;
    std::uint32_t ILocalsym;
    std::uint32_t NLocalsym;
    std::uint32_t IExtdefsym;
    std::uint32_t NExtdefsym;
    std::uint32_t IUndefsym;
    std::uint32_t NUndefsym;
    std::uint32_t TocOff;
    std::uint32_t NToc;
    std::uint32_t ModtabOff;
    std::uint32_t NModtab;
    std::uint32_t ExtrefsymOff;
    std::uint32_t NExtrefsym;
    std::uint32_t IndirectsymOff;
    std::uint32_t NIndirectsym;
    std::uint32_t Extreloff;
    std::uint32_t NExtrel;
    std::uint32_t Locreloff;
    std::uint32_t NLocrel;
};

#pragma pack(push, 1)
struct Section64 {
    char SectName[16];
    char SegName[16];
    std::uint64_t Addr;
    std::uint64_t Size;
    std::uint32_t Offset;
    std::uint32_t Align;
    std::uint32_t Reloff;
    std::uint32_t Nreloc;
    std::uint32_t Flags;
    std::uint32_t Reserved1;
    std::uint32_t Reserved2;
    std::uint32_t Reserved3;
};

struct Nlist64 {
    std::uint32_t NStrx;
    std::uint8_t NType;
    std::uint8_t NSect;
    std::uint16_t NDesc;
    std::uint64_t NValue;
};
#pragma pack(pop)

std::vector<std::uint8_t> makeDylibCommand(const std::string& path) {
    const std::size_t rawSize = sizeof(DylibCommand) + path.size() + 1;
    const std::size_t alignedSize = (rawSize + 7) & ~std::size_t(7);
    std::vector<std::uint8_t> buf(alignedSize, 0);
    auto* cmd = reinterpret_cast<DylibCommand*>(buf.data());
    cmd->Cmd = LC_LOAD_DYLIB;
    cmd->CmdSize = static_cast<std::uint32_t>(alignedSize);
    cmd->NameOffset = sizeof(DylibCommand);
    cmd->Timestamp = 2;
    cmd->CurrentVersion = 0x00010000;
    cmd->CompatibilityVersion = 0x00010000;
    std::memcpy(buf.data() + sizeof(DylibCommand), path.data(), path.size());
    return buf;
}

std::vector<std::uint8_t> makeDylinkerCommand(const std::string& path) {
    const std::size_t rawSize = sizeof(DylinkerCommand) + path.size() + 1;
    const std::size_t alignedSize = (rawSize + 7) & ~std::size_t(7);
    std::vector<std::uint8_t> buf(alignedSize, 0);
    auto* cmd = reinterpret_cast<DylinkerCommand*>(buf.data());
    cmd->Cmd = LC_LOAD_DYLINKER;
    cmd->CmdSize = static_cast<std::uint32_t>(alignedSize);
    cmd->NameOffset = sizeof(DylinkerCommand);
    std::memcpy(buf.data() + sizeof(DylinkerCommand), path.data(), path.size());
    return buf;
}

std::vector<std::uint8_t> makeRpathCommand(const std::string& path) {
    const std::size_t rawSize = sizeof(RpathCommand) + path.size() + 1;
    const std::size_t alignedSize = (rawSize + 7) & ~std::size_t(7);
    std::vector<std::uint8_t> buf(alignedSize, 0);
    auto* cmd = reinterpret_cast<RpathCommand*>(buf.data());
    cmd->Cmd = LC_RPATH;
    cmd->CmdSize = static_cast<std::uint32_t>(alignedSize);
    cmd->PathOffset = sizeof(RpathCommand);
    std::memcpy(buf.data() + sizeof(RpathCommand), path.data(), path.size());
    return buf;
}

std::vector<std::uint8_t> makeBuildVersionCommand() {
    std::vector<std::uint8_t> buf(sizeof(BuildVersionCommand), 0);
    auto* cmd = reinterpret_cast<BuildVersionCommand*>(buf.data());
    cmd->Cmd = LC_BUILD_VERSION;
    cmd->CmdSize = sizeof(BuildVersionCommand);
    cmd->Platform = 1; // PLATFORM_MACOS
    cmd->MinOS = 0x000B0000; // macOS 11.0
    cmd->SDK = 0x000E0000; // macOS 14.0
    cmd->NTools = 0;
    return buf;
}

std::vector<std::uint8_t> makeUuidCommand(const std::vector<std::uint8_t>& seed) {
    std::vector<std::uint8_t> buf(sizeof(UuidCommand), 0);
    auto* cmd = reinterpret_cast<UuidCommand*>(buf.data());
    cmd->Cmd = LC_UUID;
    cmd->CmdSize = sizeof(UuidCommand);
    std::uint64_t h1 = 0x811c9dc5, h2 = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0; i < seed.size(); ++i) {
        h1 = (h1 ^ seed[i]) * 16777619u;
        h2 = (h2 ^ seed[i]) * 1099511628211ULL;
    }
    std::memcpy(cmd->Uuid, &h1, 8);
    std::memcpy(cmd->Uuid + 8, &h2, 8);
    return buf;
}

std::vector<std::uint8_t> makeSourceVersionCommand() {
    std::vector<std::uint8_t> buf(sizeof(SourceVersionCommand), 0);
    auto* cmd = reinterpret_cast<SourceVersionCommand*>(buf.data());
    cmd->Cmd = LC_SOURCE_VERSION;
    cmd->CmdSize = sizeof(SourceVersionCommand);
    cmd->Version = 0x0001000001000000ULL; // 1.1.0.0
    return buf;
}

std::vector<std::uint8_t> makeSymtabCommand(std::uint32_t symOff, std::uint32_t nSyms, std::uint32_t strOff, std::uint32_t strSize) {
    std::vector<std::uint8_t> buf(sizeof(SymtabCommand), 0);
    auto* cmd = reinterpret_cast<SymtabCommand*>(buf.data());
    cmd->Cmd = LC_SYMTAB;
    cmd->CmdSize = sizeof(SymtabCommand);
    cmd->SymOff = symOff;
    cmd->NSyms = nSyms;
    cmd->StrOff = strOff;
    cmd->StrSize = strSize;
    return buf;
}

std::vector<std::uint8_t> makeDysymtabCommand(std::uint32_t nSyms) {
    std::vector<std::uint8_t> buf(sizeof(DysymtabCommand), 0);
    auto* cmd = reinterpret_cast<DysymtabCommand*>(buf.data());
    cmd->Cmd = LC_DYSYMTAB;
    cmd->CmdSize = sizeof(DysymtabCommand);
    cmd->IExtdefsym = 0;
    cmd->NExtdefsym = nSyms;
    return buf;
}

std::vector<std::uint8_t> makeCodeSignatureCommand(std::uint32_t offset, std::uint32_t size) {
    std::vector<std::uint8_t> buf(sizeof(LinkeditDataCommand), 0);
    auto* cmd = reinterpret_cast<LinkeditDataCommand*>(buf.data());
    cmd->Cmd = LC_CODE_SIGNATURE;
    cmd->CmdSize = sizeof(LinkeditDataCommand);
    cmd->DataOff = offset;
    cmd->DataSize = size;
    return buf;
}

// ---- Ad-hoc code signing ----------------------------------------------------
//
// macOS refuses to execute completely unsigned binaries (SIGKILL "Code Signature
// Invalid"), so the writer embeds an ad-hoc SuperBlob: a CodeDirectory whose
// per-page SHA-256 hashes are computed over the finished image (everything up
// to the signature itself). This mirrors what `codesign -s -` produces for the
// subset of slots the kernel checks on launch (code directory only, no
// requirements, entitlements or CMS data).

// SHA-256 (FIPS 180-4), self-contained so the patcher stays toolchain-neutral.
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

static void writeBe32(std::vector<std::uint8_t>& buf, std::size_t offset, std::uint32_t val) {
    buf[offset] = static_cast<std::uint8_t>((val >> 24) & 0xff);
    buf[offset + 1] = static_cast<std::uint8_t>((val >> 16) & 0xff);
    buf[offset + 2] = static_cast<std::uint8_t>((val >> 8) & 0xff);
    buf[offset + 3] = static_cast<std::uint8_t>(val & 0xff);
}

static void writeBe64(std::vector<std::uint8_t>& buf, std::size_t offset, std::uint64_t val) {
    writeBe32(buf, offset, static_cast<std::uint32_t>(val >> 32));
    writeBe32(buf, offset + 4, static_cast<std::uint32_t>(val & 0xffffffffu));
}

// Size (in bytes) the CodeDirectory occupies for `binarySize` hashed bytes.
// CodeDirectory v0x20400 (88-byte header, SHA-256 page hashes) + identifier.
constexpr std::uint32_t CodeDirectoryHeaderSize = 88;
constexpr std::uint32_t CodeDirectoryHashSize = 32;

std::uint32_t adHocSignatureSize(std::size_t binarySize, std::size_t identifierLength) {
    const std::uint32_t nCodeSlots = static_cast<std::uint32_t>((binarySize + 4095) / 4096);
    const std::uint32_t codeDirectorySize = CodeDirectoryHeaderSize + static_cast<std::uint32_t>(identifierLength) + 1
        + nCodeSlots * CodeDirectoryHashSize;
    // SuperBlob: magic + length + count, one BlobIndex entry.
    return 12 + 8 + codeDirectorySize;
}

// Builds the ad-hoc SuperBlob over `image[0, binarySize)`. `binarySize` must be
// the file offset the signature itself is placed at (codeLimit), and the
// identifier the bundle name recorded in the CodeDirectory.
std::vector<std::uint8_t> makeAdHocCodeSignatureBlob(const std::uint8_t* image, std::size_t binarySize, const std::string& identifier) {
    const std::uint32_t nCodeSlots = static_cast<std::uint32_t>((binarySize + 4095) / 4096);
    const std::uint32_t identOffset = CodeDirectoryHeaderSize;
    const std::uint32_t hashOffset = identOffset + static_cast<std::uint32_t>(identifier.size()) + 1;
    const std::uint32_t codeDirTotalSize = hashOffset + nCodeSlots * CodeDirectoryHashSize;

    const std::uint32_t superBlobHeaderSize = 12;
    const std::uint32_t blobIndexSize = 8;
    const std::uint32_t totalBlobSize = superBlobHeaderSize + blobIndexSize + codeDirTotalSize;

    std::vector<std::uint8_t> blob(totalBlobSize, 0);

    // SuperBlob header (big endian)
    writeBe32(blob, 0, 0xfade0cc0); // CSMAGIC_EMBEDDED_SIGNATURE
    writeBe32(blob, 4, totalBlobSize);
    writeBe32(blob, 8, 1); // count = 1

    // BlobIndex[0]
    writeBe32(blob, 12, 0); // type = CSSLOT_CODEDIRECTORY
    writeBe32(blob, 16, superBlobHeaderSize + blobIndexSize); // offset to CodeDirectory

    // CodeDirectory (version 0x20400: 88-byte header with exec-segment fields)
    const std::size_t cdOff = superBlobHeaderSize + blobIndexSize;
    writeBe32(blob, cdOff, 0xfade0c02); // CSMAGIC_CODEDIRECTORY
    writeBe32(blob, cdOff + 4, codeDirTotalSize);
    writeBe32(blob, cdOff + 8, 0x00020400); // version (supports SHA-256 + exec seg)
    writeBe32(blob, cdOff + 12, 0x00000002); // flags = CS_ADHOC
    writeBe32(blob, cdOff + 16, hashOffset);
    writeBe32(blob, cdOff + 20, identOffset);
    writeBe32(blob, cdOff + 24, 0); // nSpecialSlots
    writeBe32(blob, cdOff + 28, nCodeSlots);
    writeBe32(blob, cdOff + 32, static_cast<std::uint32_t>(binarySize)); // codeLimit
    blob[cdOff + 36] = CodeDirectoryHashSize; // hashSize (SHA-256)
    blob[cdOff + 37] = 2; // hashType = CS_HASHTYPE_SHA256
    blob[cdOff + 38] = 0; // platform
    blob[cdOff + 39] = 12; // pageSize (2^12 = 4096)
    writeBe32(blob, cdOff + 40, 0); // spare2
    writeBe32(blob, cdOff + 44, 0); // scatterOffset (unused)
    writeBe32(blob, cdOff + 48, 0); // teamIDOffset (none)
    writeBe32(blob, cdOff + 52, 0); // spare3
    // Version 0x20400 extended fields are 64-bit wide.
    writeBe64(blob, cdOff + 56, binarySize); // codeLimit64
    writeBe64(blob, cdOff + 64, 0);          // execSegBase (first executable page)
    writeBe64(blob, cdOff + 72, binarySize); // execSegLimit
    writeBe64(blob, cdOff + 80, 0x00000001); // execSegFlags = CS_EXECSEG_MAIN_BINARY

    // Identifier
    std::memcpy(blob.data() + cdOff + identOffset, identifier.c_str(), identifier.size() + 1);

    // Page hashes: SHA-256 over every 4096-byte page of [0, binarySize). The
    // final partial page is hashed over the bytes it actually has.
    std::uint8_t* hashes = blob.data() + cdOff + hashOffset;
    for (std::uint32_t slot = 0; slot < nCodeSlots; ++slot) {
        const std::size_t pageStart = static_cast<std::size_t>(slot) * 4096;
        const std::size_t pageBytes = std::min<std::size_t>(4096, binarySize - pageStart);
        Sha256 hasher;
        hasher.update(image + pageStart, pageBytes);
        hasher.finish(hashes + slot * CodeDirectoryHashSize);
    }

    return blob;
}

}

MacOsElfPatcher::MacOsElfPatcher() = default;

std::vector<std::uint8_t> MacOsElfPatcher::Patch(
    const std::vector<std::uint8_t>& sourceElf,
    const std::vector<Domain::ProgramHeader>&,
    const Domain::SysVDynamicSection&,
    std::uint64_t,
    const std::string& runPath,
    bool, bool,
    const std::vector<Codegen::TrampolineSite>& trampolines
) {
    std::cout << "[Experimental] Building macOS Mach-O bundle with Metal API bridge..." << std::endl;
    RytyMetalInitializeDevice();

    // ---- Fixed load commands (kept from the original writer) ----
    const auto dylinkerCmd = makeDylinkerCommand("/usr/lib/dyld");
    const auto buildVerCmd = makeBuildVersionCommand();
    const auto metalLibCmd = makeDylibCommand("/System/Library/Frameworks/Metal.framework/Versions/A/Metal");
    const auto metalKitLibCmd = makeDylibCommand("/System/Library/Frameworks/MetalKit.framework/Versions/A/MetalKit");
    const auto cocoaLibCmd = makeDylibCommand("/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa");
    const auto quartzCoreLibCmd = makeDylibCommand("/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore");
    const auto libSystemCmd = makeDylibCommand("/usr/lib/libSystem.B.dylib");
    std::vector<std::vector<std::uint8_t>> rpathCmds;
    std::string rpaths = runPath.empty() ? "@executable_path/libs" : runPath;
    std::size_t rstart = 0;
    while (rstart < rpaths.size()) {
        const auto pos = rpaths.find_first_of(":;", rstart);
        const std::string path = rpaths.substr(rstart, pos == std::string::npos ? std::string::npos : pos - rstart);
        if (!path.empty()) {
            rpathCmds.push_back(makeRpathCommand(path));
        }
        if (pos == std::string::npos) break;
        rstart = pos + 1;
    }
    if (rpathCmds.empty()) rpathCmds.push_back(makeRpathCommand("@executable_path/libs"));

    // ---- Parse the incoming (relinked) ELF program headers ----
    struct GuestLoad { std::uint64_t off, vaddr, filesz, memsz; std::uint32_t flags; };
    std::vector<GuestLoad> loads;
    std::uint64_t dynOff = 0, dynSz = 0;
    std::uint64_t e_entry = 0;
    const bool haveElf = sourceElf.size() >= 0x40 && sourceElf[0] == 0x7f && sourceElf[1] == 'E' && sourceElf[2] == 'L' && sourceElf[3] == 'F';
    if (haveElf) {
        std::memcpy(&e_entry, sourceElf.data() + 0x18, 8);
        std::uint64_t phoff = 0; std::memcpy(&phoff, sourceElf.data() + 0x20, 8);
        std::uint16_t phentsize = 0, phnum = 0;
        std::memcpy(&phentsize, sourceElf.data() + 0x36, 2);
        std::memcpy(&phnum, sourceElf.data() + 0x38, 2);
        for (std::uint16_t i = 0; i < phnum && phoff + (i + 1) * phentsize <= sourceElf.size(); ++i) {
            const std::size_t o = phoff + i * phentsize;
            std::uint32_t p_type = 0; std::memcpy(&p_type, sourceElf.data() + o, 4);
            if (p_type == 2) { // PT_DYNAMIC
                std::memcpy(&dynOff, sourceElf.data() + o + 8, 8);
                std::memcpy(&dynSz, sourceElf.data() + o + 32, 8);
            }
            if (p_type != 1) continue;
            GuestLoad s{};
            std::memcpy(&s.flags, sourceElf.data() + o + 4, 4);
            std::memcpy(&s.off, sourceElf.data() + o + 8, 8);
            std::memcpy(&s.vaddr, sourceElf.data() + o + 16, 8);
            std::memcpy(&s.filesz, sourceElf.data() + o + 32, 8);
            std::memcpy(&s.memsz, sourceElf.data() + o + 40, 8);
            loads.push_back(s);
        }
    }
    if (loads.empty()) {
        throw std::runtime_error("macOS writer: input ELF has no PT_LOAD segments; nothing to map");
    }
    std::sort(loads.begin(), loads.end(), [](const GuestLoad& a, const GuestLoad& b) { return a.vaddr < b.vaddr; });

    // ---- Rebase: Apple-Silicon Rosetta reserves the low address space of
    // translated processes, so guests loading below 0x10000000 cannot be
    // mapped at their own vaddrs. Shift such guests to 0x10000000 and apply
    // the ELF's own dynamic relocations so every in-image pointer follows.
    std::vector<std::uint8_t> payload = sourceElf;
    constexpr std::uint64_t PageMask = static_cast<std::uint64_t>(0xFFF);
    const std::uint64_t guestLo = loads.front().vaddr & ~PageMask;
    std::uint64_t guestHi = 0;
    for (const auto& l : loads) guestHi = std::max(guestHi, l.vaddr + l.memsz);
    const std::uint64_t rebase = (guestLo < 0x10000000ull) ? (0x10000000ull - guestLo) : 0;
    if (rebase != 0) {
        std::cerr << "[ryty] rebase: guest [0x" << std::hex << guestLo << "..0x" << guestHi
                  << "] shifted by 0x" << rebase << std::dec << "\n";
    }
    if (rebase != 0 && dynOff != 0 && dynSz > 0) {
        const auto v2o = [&](std::uint64_t v) -> std::size_t {
            for (const auto& l : loads)
                if (l.vaddr <= v && v < l.vaddr + l.filesz)
                    return static_cast<std::size_t>(l.off + (v - l.vaddr));
            return static_cast<std::size_t>(-1);
        };
        std::uint64_t relaV = 0, relaSz = 0, symtabV = 0;
        std::uint64_t jmprelV = 0, pltrelSz = 0;
        const std::size_t dynF = static_cast<std::size_t>(dynOff);
        for (std::uint64_t d = 0; d + 16 <= dynSz; d += 16) {
            std::uint64_t tag = 0, val = 0;
            std::memcpy(&tag, payload.data() + dynF + d, 8);
            std::memcpy(&val, payload.data() + dynF + d + 8, 8);
            if (tag == 0) break;
            if (tag == 7) relaV = val;              // DT_RELA
            else if (tag == 8) relaSz = val;        // DT_RELASZ
            else if (tag == 6) symtabV = val;       // DT_SYMTAB
            else if (tag == 23) jmprelV = val;      // DT_JMPREL
            else if (tag == 2) pltrelSz = val;      // DT_PLTRELSZ
            else if ((tag == 12 || tag == 13) && guestLo <= val && val < guestHi) {
                // DT_INIT / DT_FINI hold function pointers in the old space
                const std::uint64_t fixed = val + rebase;
                std::memcpy(payload.data() + dynF + d + 8, &fixed, 8);
            }
        }
        const std::size_t relaF = v2o(relaV);
        if (relaF != static_cast<std::size_t>(-1) && relaSz > 0 && relaSz % 24 == 0) {
            std::uint64_t applied = 0, skipped = 0;
            for (std::uint64_t r = 0; r + 24 <= relaSz; r += 24) {
                std::uint64_t rOff = 0, rInfo = 0, rAdd = 0;
                std::memcpy(&rOff, payload.data() + relaF + r, 8);
                std::memcpy(&rInfo, payload.data() + relaF + r + 8, 8);
                std::memcpy(&rAdd, payload.data() + relaF + r + 16, 8);
                const std::uint32_t rtype = static_cast<std::uint32_t>(rInfo & 0xffffffffull);
                std::uint64_t value = 0;
                if (rtype == 8) { // R_X86_64_RELATIVE: *ptr = base + addend
                    value = rAdd;
                } else if (rtype == 1 || rtype == 6) { // R_X86_64_64 / GLOB_DAT
                    const std::uint64_t symIdx = rInfo >> 32;
                    if (symtabV == 0) { ++skipped; continue; }
                    const std::size_t symF = v2o(symtabV + symIdx * 24);
                    if (symF == static_cast<std::size_t>(-1)) { ++skipped; continue; }
                    std::uint64_t stShndx = 0, stValue = 0;
                    std::memcpy(&stShndx, payload.data() + symF + 6, 2);
                    std::memcpy(&stValue, payload.data() + symF + 8, 8);
                    if (stShndx == 0 || stValue == 0) { ++skipped; continue; } // external import
                    value = stValue + rAdd;
                } else {
                    ++skipped; continue;
                }
                if (!(guestLo <= value && value < guestHi)) { ++skipped; continue; }
                const std::size_t dstF = v2o(rOff);
                if (dstF == static_cast<std::size_t>(-1)) { ++skipped; continue; }
                const std::uint64_t fixed = value + rebase;
                std::memcpy(payload.data() + dstF, &fixed, 8);
                ++applied;
            }
            std::cerr << "[ryty] rebase: relocations applied=" << applied << " skipped=" << skipped << "\n";
        }
        // PLT/GOT lazy-binding slots (DT_JMPREL, R_X86_64_JUMP_SLOT): the slots
        // initially hold PLT-stub pointers in the old space — shift them too.
        const std::size_t jmpF = v2o(jmprelV);
        if (jmpF != static_cast<std::size_t>(-1) && pltrelSz > 0 && pltrelSz % 24 == 0) {
            std::uint64_t applied = 0, skipped = 0;
            for (std::uint64_t r = 0; r + 24 <= pltrelSz; r += 24) {
                std::uint64_t rOff = 0, rInfo = 0;
                std::memcpy(&rOff, payload.data() + jmpF + r, 8);
                std::memcpy(&rInfo, payload.data() + jmpF + r + 8, 8);
                if ((rInfo & 0xffffffffull) != 7) continue; // R_X86_64_JUMP_SLOT
                const std::size_t slotF = v2o(rOff);
                if (slotF == static_cast<std::size_t>(-1)) { ++skipped; continue; }
                std::uint64_t v = 0;
                std::memcpy(&v, payload.data() + slotF, 8);
                if (guestLo <= v && v < guestHi) {
                    v += rebase;
                    std::memcpy(payload.data() + slotF, &v, 8);
                    ++applied;
                } else {
                    ++skipped;
                }
            }
            std::cerr << "[ryty] rebase: PLT/GOT slots applied=" << applied << " skipped=" << skipped << "\n";
        }
    }

    // ---- Group PT_LOADs whose page-rounded spans overlap ----
    struct GuestSeg {
        std::uint64_t vmaddr = 0, vmsize = 0, filesz = 0, fileoff = 0, payloadOff = 0;
        std::uint32_t prot = 0; bool exec = false, direct = false;
        std::vector<std::uint8_t> image;
    };
    std::vector<GuestSeg> segs;
    {
        std::size_t i = 0;
        while (i < loads.size()) {
            std::size_t j = i;
            std::uint64_t lo = loads[i].vaddr & ~PageMask;
            std::uint64_t hi = (loads[i].vaddr + loads[i].memsz + PageMask) & ~PageMask;
            std::uint32_t pflags = 0;
            while (j < loads.size() && loads[j].vaddr < hi) {
                hi = std::max(hi, (loads[j].vaddr + loads[j].memsz + PageMask) & ~PageMask);
                pflags |= loads[j].flags;
                ++j;
            }
            GuestSeg g{};
            g.vmaddr = lo + rebase;
            g.vmsize = hi - lo;
            g.exec = (pflags & 0x1) != 0; // PF_X
            g.prot = 1;                    // VM_PROT_READ always
            if (pflags & 0x2) g.prot |= 2; // PF_W -> VM_PROT_WRITE
            if (pflags & 0x1) g.prot |= 4; // PF_X -> VM_PROT_EXECUTE
            if ((j - i) == 1 && loads[i].vaddr == lo) {
                // single, page-aligned LOAD: map straight from the ELF payload bytes
                g.direct = true;
                g.payloadOff = loads[i].off;
                g.filesz = loads[i].filesz;
            } else {
                // merged group: build a synthetic vaddr-indexed image (holes = zeroed bss)
                std::uint64_t imgEnd = 0;
                for (std::size_t k = i; k < j; ++k) imgEnd = std::max(imgEnd, loads[k].vaddr + loads[k].filesz);
                g.direct = false;
                g.filesz = imgEnd > lo ? imgEnd - lo : 0;
                g.image.assign(static_cast<std::size_t>(g.filesz), 0);
                for (std::size_t k = i; k < j; ++k) {
                    if (loads[k].filesz == 0) continue;
                    std::memcpy(g.image.data() + (loads[k].vaddr - lo),
                                payload.data() + loads[k].off,
                                static_cast<std::size_t>(loads[k].filesz));
                }
            }
            segs.push_back(g);
            i = j;
        }
    }
    bool haveExec = false;
    for (const auto& g : segs) haveExec |= g.exec;

    // ---- Trampoline + LINKEDIT placement after all guest segments ----
    std::size_t trampolineTotalSize = 0;
    for (const auto& site : trampolines) trampolineTotalSize += 16 + site.Body.size();
    std::uint64_t guestEnd = 0;
    for (const auto& g : segs) guestEnd = std::max(guestEnd, g.vmaddr + g.vmsize);
    const std::uint64_t trampVmaddr = guestEnd;
    const std::uint64_t linkeditVmaddr = (trampVmaddr + trampolineTotalSize + PageMask) & ~PageMask;

    const std::uint64_t entryVA = e_entry + rebase;

    // ---- Build Mach-O Symbol Table and String Table ----
    std::vector<Nlist64> symEntries;
    std::string strTable;
    strTable.push_back('\0'); // Offset 0 is empty string

    // Sym 0: _main entry point
    {
        Nlist64 sym{};
        sym.NStrx = static_cast<std::uint32_t>(strTable.size());
        strTable += "_main";
        strTable.push_back('\0');
        sym.NType = 0x0f; // N_SECT | N_EXT
        sym.NSect = 1;    // __TEXT section
        sym.NDesc = 0;
        sym.NValue = entryVA;
        symEntries.push_back(sym);
    }

    // Sym 1..N: Trampoline sites
    for (std::size_t idx = 0; idx < trampolines.size(); ++idx) {
        Nlist64 sym{};
        sym.NStrx = static_cast<std::uint32_t>(strTable.size());
        strTable += "_trampoline_" + std::to_string(idx);
        strTable.push_back('\0');
        sym.NType = 0x0e; // N_SECT (local)
        sym.NSect = 1;
        sym.NDesc = 0;
        sym.NValue = trampVmaddr + idx * 16;
        symEntries.push_back(sym);
    }

    const auto uuidCmd = makeUuidCommand(sourceElf);
    const auto sourceVerCmd = makeSourceVersionCommand();

    // ---- Ad-hoc code signature layout ----
    // The SuperBlob is placed page-aligned after the symbol/string tables, inside
    // __LINKEDIT (the codesign convention). The CodeDirectory hashes every byte of
    // the file in front of it, so its position and size must be fixed before the
    // load commands are written; the hashes themselves are computed over the
    // finished image after assembly.
    const std::string signatureIdentifier = "ryty.bundle";
    const std::uint32_t signatureCommandCount = 1; // LC_CODE_SIGNATURE

    // ---- Compute load command sizes and the page-aligned payload position ----
    std::uint64_t totalRpathSize = 0;
    for (const auto& cmd : rpathCmds) totalRpathSize += cmd.size();

    const std::uint32_t cmdsCount = static_cast<std::uint32_t>(segs.size() + 2 /*tramp,linkedit*/ + 1 /*main*/
        + 1 /*dylinker*/ + 1 /*buildver*/ + 5 /*dylibs*/ + rpathCmds.size()
        + 1 /*uuid*/ + 1 /*sourcever*/ + 1 /*symtab*/ + 1 /*dysymtab*/
        + signatureCommandCount);
    const std::uint64_t cmdsSizeWithoutSyms = sizeof(SegmentCommand64) * (segs.size() + 2)
        + (haveExec ? sizeof(Section64) : 0)
        + sizeof(MainCommand)
        + dylinkerCmd.size() + buildVerCmd.size()
        + metalLibCmd.size() + metalKitLibCmd.size() + cocoaLibCmd.size()
        + quartzCoreLibCmd.size() + libSystemCmd.size() + totalRpathSize
        + uuidCmd.size() + sourceVerCmd.size()
        + sizeof(LinkeditDataCommand) /* LC_CODE_SIGNATURE */;
    const std::uint64_t cmdsSize = cmdsSizeWithoutSyms + sizeof(SymtabCommand) + sizeof(DysymtabCommand);
    const std::uint64_t headerAll = sizeof(MachOHeader64) + cmdsSize;
    const std::uint64_t payloadFileOff = (headerAll + PageMask) & ~PageMask; // page-aligned file position of the ELF

    // ---- Assign file offsets (all page aligned for clean kernel mappings) ----
    std::uint64_t cursor = payloadFileOff + sourceElf.size();
    cursor = (cursor + PageMask) & ~PageMask;
    for (auto& g : segs) {
        if (g.direct) {
            g.fileoff = payloadFileOff + g.payloadOff;
        } else {
            g.fileoff = cursor;
            cursor += g.filesz;
            cursor = (cursor + PageMask) & ~PageMask;
        }
    }
    const std::uint64_t trampFileOff = cursor;
    cursor += trampolineTotalSize;
    cursor = (cursor + PageMask) & ~PageMask;
    const std::uint64_t linkeditFileOff = cursor;

    const std::uint32_t symTabOffset = static_cast<std::uint32_t>(linkeditFileOff);
    const std::uint32_t nSyms = static_cast<std::uint32_t>(symEntries.size());
    const std::uint32_t symTabBytes = nSyms * sizeof(Nlist64);
    const std::uint32_t strTabOffset = symTabOffset + symTabBytes;
    const std::uint32_t strTabBytes = static_cast<std::uint32_t>(strTable.size());
    const std::uint64_t symstrBytes = static_cast<std::uint64_t>(symTabBytes) + strTabBytes;
    const std::uint64_t signatureFileOff = (linkeditFileOff + symstrBytes + PageMask) & ~PageMask;
    const std::uint32_t signatureSize = adHocSignatureSize(signatureFileOff, signatureIdentifier.size());
    const std::uint64_t linkeditDataSize = (signatureFileOff - linkeditFileOff) + signatureSize;

    const auto symtabCmd = makeSymtabCommand(symTabOffset, nSyms, strTabOffset, strTabBytes);
    const auto dysymtabCmd = makeDysymtabCommand(nSyms);
    const auto codeSignatureCmd = makeCodeSignatureCommand(static_cast<std::uint32_t>(signatureFileOff), signatureSize);

    // ---- Entry point ----
    std::uint64_t entryoff = 0;
    bool foundEntry = false;
    for (const auto& g : segs) {
        if (entryVA >= g.vmaddr && entryVA < g.vmaddr + g.vmsize) {
            entryoff = g.fileoff + (entryVA - g.vmaddr);
            foundEntry = true;
            break;
        }
    }
    if (!foundEntry) throw std::runtime_error("macOS writer: e_entry outside every guest segment");

    // ---- Patch trampoline sites into payload / synthetic images ----
    std::vector<std::uint8_t> trampolineBuf;
    trampolineBuf.reserve(trampolineTotalSize);
    for (const auto& site : trampolines) {
        const std::uint64_t siteVA = site.Address + rebase;
        while (trampolineBuf.size() % 16 != 0) trampolineBuf.push_back(0xcc);
        const std::uint64_t bodyVaddr = trampVmaddr + trampolineBuf.size();
        auto body = site.Body;
        const auto returnDisp = static_cast<std::int64_t>(siteVA + site.Length)
            - static_cast<std::int64_t>(bodyVaddr + site.ReturnBranchOffset + 5);
        if (site.ReturnBranchOffset + 5 <= body.size()) {
            const auto d = static_cast<std::int32_t>(returnDisp);
            std::memcpy(body.data() + site.ReturnBranchOffset + 1, &d, 4);
        }
        trampolineBuf.insert(trampolineBuf.end(), body.begin(), body.end());

        const auto jumpDisp = static_cast<std::int32_t>(
            static_cast<std::int64_t>(bodyVaddr) - static_cast<std::int64_t>(siteVA + 5));
        bool patched = false;
        for (auto& g : segs) {
            if (siteVA < g.vmaddr || siteVA >= g.vmaddr + g.vmsize) continue;
            std::uint8_t* dst = nullptr;
            if (g.direct) dst = payload.data() + (g.payloadOff + (siteVA - g.vmaddr));
            else dst = g.image.data() + (siteVA - g.vmaddr);
            std::fill_n(dst, site.Length, 0x90);
            dst[0] = 0xE9;
            std::memcpy(dst + 1, &jumpDisp, 4);
            patched = true;
            std::cerr << "[ryty] trampoline: site vaddr=0x" << std::hex << siteVA
                      << " -> body 0x" << bodyVaddr << std::dec << "\n";
            break;
        }
        if (!patched) throw std::runtime_error("macOS writer: trampoline site outside guest segments");
    }

    // ---- Assemble the Mach-O ----
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(linkeditFileOff + linkeditDataSize + 0x1000));
    const auto appendBytes = [&](const void* ptr, std::size_t size) {
        const auto* bytePtr = static_cast<const std::uint8_t*>(ptr);
        result.insert(result.end(), bytePtr, bytePtr + size);
    };
    const auto padTo = [&](std::uint64_t target) {
        while (result.size() < target) result.push_back(0);
    };

    MachOHeader64 header{};
    header.Magic = MH_MAGIC_64;
    header.CpuType = CPU_TYPE_X86_64;
    header.CpuSubtype = CPU_SUBTYPE_ALL;
    header.FileType = MH_EXECUTE;
    header.CmdsCount = cmdsCount;
    header.CmdsSize = static_cast<std::uint32_t>(cmdsSize);
    header.Flags = 0x00000005; // MH_NOUNDEFS | MH_DYLDLINK — no MH_PIE: guest absolute addresses
    header.Reserved = 0;
    appendBytes(&header, sizeof(header));

    std::size_t roCount = 0, rwCount = 0;
    for (const auto& g : segs) {
        SegmentCommand64 sc{};
        sc.Cmd = LC_SEGMENT_64;
        sc.CmdSize = sizeof(SegmentCommand64) + (g.exec ? sizeof(Section64) : 0);
        char segName[16] = {};
        if (g.exec) std::strncpy(segName, "__TEXT", 16);
        else if (g.prot & 2) std::snprintf(segName, 16, "__DATA%zu", rwCount++);
        else std::snprintf(segName, 16, "seg_ro%zu", roCount++);
        std::memcpy(sc.SegName, segName, 16);
        sc.VAddr = g.vmaddr; sc.VSize = g.vmsize;
        sc.FileOff = g.fileoff; sc.FileSize = g.filesz;
        sc.MaxProt = g.prot; sc.InitProt = g.prot;
        sc.NSects = g.exec ? 1 : 0;
        appendBytes(&sc, sizeof(sc));
        if (g.exec) {
            Section64 sect{};
            std::memset(sect.SectName, 0, 16); std::memset(sect.SegName, 0, 16);
            std::strncpy(sect.SectName, "__text", 16);
            std::memcpy(sect.SegName, segName, 16);
            sect.Addr = g.vmaddr;
            sect.Size = g.filesz;
            sect.Offset = static_cast<std::uint32_t>(g.fileoff);
            sect.Align = 12; // 2^12 = 4096
            sect.Flags = 0x80000400; // S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS
            appendBytes(&sect, sizeof(sect));
        }
        std::cerr << "[ryty] segment " << segName << ": vmaddr=0x" << std::hex << g.vmaddr
                  << " vmsize=0x" << g.vmsize << " fileoff=0x" << g.fileoff
                  << " filesize=0x" << g.filesz << " prot=" << g.prot << std::dec << "\n";
    }
    {
        SegmentCommand64 sc{};
        sc.Cmd = LC_SEGMENT_64; sc.CmdSize = sizeof(SegmentCommand64);
        std::strncpy(sc.SegName, "seg_tramp", 16);
        sc.VAddr = trampVmaddr;
        sc.VSize = (trampolineTotalSize + PageMask) & ~PageMask;
        sc.FileOff = trampFileOff; sc.FileSize = trampolineTotalSize;
        sc.MaxProt = 5; sc.InitProt = 5;
        appendBytes(&sc, sizeof(sc));
    }
    {
        SegmentCommand64 sc{};
        sc.Cmd = LC_SEGMENT_64; sc.CmdSize = sizeof(SegmentCommand64);
        std::strncpy(sc.SegName, "__LINKEDIT", 16);
        sc.VAddr = linkeditVmaddr; sc.VSize = (linkeditDataSize + PageMask) & ~PageMask;
        sc.FileOff = linkeditFileOff; sc.FileSize = linkeditDataSize;
        sc.MaxProt = 1; sc.InitProt = 1;
        appendBytes(&sc, sizeof(sc));
    }
    {
        MainCommand mainCmd{};
        mainCmd.Cmd = LC_MAIN; mainCmd.CmdSize = sizeof(MainCommand);
        mainCmd.EntryOffset = entryoff;
        mainCmd.StackSize = 0;
        appendBytes(&mainCmd, sizeof(mainCmd));
    }
    appendBytes(dylinkerCmd.data(), dylinkerCmd.size());
    appendBytes(buildVerCmd.data(), buildVerCmd.size());
    appendBytes(uuidCmd.data(), uuidCmd.size());
    appendBytes(sourceVerCmd.data(), sourceVerCmd.size());
    appendBytes(symtabCmd.data(), symtabCmd.size());
    appendBytes(dysymtabCmd.data(), dysymtabCmd.size());
    appendBytes(metalLibCmd.data(), metalLibCmd.size());
    appendBytes(metalKitLibCmd.data(), metalKitLibCmd.size());
    appendBytes(cocoaLibCmd.data(), cocoaLibCmd.size());
    appendBytes(quartzCoreLibCmd.data(), quartzCoreLibCmd.size());
    appendBytes(libSystemCmd.data(), libSystemCmd.size());
    for (const auto& cmd : rpathCmds) {
        appendBytes(cmd.data(), cmd.size());
    }
    appendBytes(codeSignatureCmd.data(), codeSignatureCmd.size());

    // ---- File data: payload, synthetic images, trampolines, symbol table ----
    padTo(payloadFileOff);
    appendBytes(payload.data(), payload.size());
    for (const auto& g : segs) {
        if (g.direct) continue;
        padTo(g.fileoff);
        if (g.filesz > 0) appendBytes(g.image.data(), g.image.size());
    }
    padTo(trampFileOff);
    appendBytes(trampolineBuf.data(), trampolineBuf.size());
    padTo(linkeditFileOff);
    if (!symEntries.empty()) {
        appendBytes(symEntries.data(), symEntries.size() * sizeof(Nlist64));
    }
    if (!strTable.empty()) {
        appendBytes(strTable.data(), strTable.size());
    }

    // ---- Ad-hoc code signature over the finished image ----
    // Everything in front of the signature (header, load commands, segments,
    // trampolines, symbol and string tables) is hashed page by page.
    padTo(signatureFileOff);
    if (result.size() != signatureFileOff) throw std::runtime_error("macOS writer: signature offset does not match the assembled image");
    const auto signature = makeAdHocCodeSignatureBlob(result.data(), signatureFileOff, signatureIdentifier);
    appendBytes(signature.data(), signature.size());

    std::cerr << "[ryty] ad-hoc code signature: offset=0x" << std::hex << signatureFileOff
              << " size=0x" << signatureSize << " pages=" << ((signatureFileOff + 4095) / 4096) << std::dec << "\n";
    std::cerr << "[ryty] e_entry=0x" << std::hex << e_entry << " entryoff=0x" << entryoff
              << std::dec << " | total size " << result.size() << " bytes\n";
    return result;
}

}
