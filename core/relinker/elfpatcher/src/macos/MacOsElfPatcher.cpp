#include <elfpatcher/macos/MacOsElfPatcher.hpp>
#include <io/BufferUtils.hpp>
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
#endif

namespace Elfpatcher::MacOs {

namespace {

constexpr std::uint32_t MH_MAGIC_64 = 0xfeedfacf;
constexpr std::uint32_t CPU_TYPE_X86_64 = 0x01000007;
constexpr std::uint32_t CPU_SUBTYPE_ALL = 0x00000003;
constexpr std::uint32_t MH_EXECUTE = 0x00000002;

constexpr std::uint32_t LC_SEGMENT_64 = 0x19;
constexpr std::uint32_t LC_LOAD_DYLIB = 0x0c;
constexpr std::uint32_t LC_LOAD_DYLINKER = 0x0e;
constexpr std::uint32_t LC_MAIN = 0x80000028;
constexpr std::uint32_t LC_RPATH = 0x8000001c;
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

std::vector<std::uint8_t> makeCodeSignatureCommand(std::uint32_t offset, std::uint32_t size) {
    std::vector<std::uint8_t> buf(sizeof(LinkeditDataCommand), 0);
    auto* cmd = reinterpret_cast<LinkeditDataCommand*>(buf.data());
    cmd->Cmd = LC_CODE_SIGNATURE;
    cmd->CmdSize = sizeof(LinkeditDataCommand);
    cmd->DataOff = offset;
    cmd->DataSize = size;
    return buf;
}

static void writeBe32(std::vector<std::uint8_t>& buf, std::size_t offset, std::uint32_t val) {
    buf[offset] = static_cast<std::uint8_t>((val >> 24) & 0xff);
    buf[offset + 1] = static_cast<std::uint8_t>((val >> 16) & 0xff);
    buf[offset + 2] = static_cast<std::uint8_t>((val >> 8) & 0xff);
    buf[offset + 3] = static_cast<std::uint8_t>(val & 0xff);
}

std::vector<std::uint8_t> makeAdHocCodeSignatureBlob(std::size_t binarySize) {
    const std::string identifier = "ryty.bundle";
    const std::uint32_t nCodeSlots = static_cast<std::uint32_t>((binarySize + 4095) / 4096);
    const std::uint32_t codeDirHeaderSize = 44;
    const std::uint32_t hashOffset = codeDirHeaderSize + static_cast<std::uint32_t>(identifier.size()) + 1;
    const std::uint32_t hashSize = 20; // SHA-1 dummy hash
    const std::uint32_t codeDirTotalSize = hashOffset + nCodeSlots * hashSize;

    const std::uint32_t superBlobHeaderSize = 12;
    const std::uint32_t blobIndexSize = 8;
    const std::uint32_t totalBlobSize = superBlobHeaderSize + blobIndexSize + codeDirTotalSize;

    std::vector<std::uint8_t> blob(totalBlobSize, 0);

    // SuperBlob Header (big endian)
    writeBe32(blob, 0, 0xfade0cc0); // CSMAGIC_EMBEDDED_SIGNATURE
    writeBe32(blob, 4, totalBlobSize);
    writeBe32(blob, 8, 1); // count = 1

    // BlobIndex[0]
    writeBe32(blob, 12, 0); // type = CS_CSSLOT_CODEDIRECTORY
    writeBe32(blob, 16, superBlobHeaderSize + blobIndexSize); // offset to CodeDirectory

    // CodeDirectory Header
    const std::size_t cdOff = superBlobHeaderSize + blobIndexSize;
    writeBe32(blob, cdOff, 0xfade0c02); // CSMAGIC_CODEDIRECTORY
    writeBe32(blob, cdOff + 4, codeDirTotalSize);
    writeBe32(blob, cdOff + 8, 0x00020400); // version
    writeBe32(blob, cdOff + 12, 0x00000002); // flags = CS_ADHOC
    writeBe32(blob, cdOff + 16, hashOffset);
    writeBe32(blob, cdOff + 20, codeDirHeaderSize);
    writeBe32(blob, cdOff + 24, 0); // nSpecialSlots
    writeBe32(blob, cdOff + 28, nCodeSlots);
    writeBe32(blob, cdOff + 32, static_cast<std::uint32_t>(binarySize)); // codeLimit
    blob[cdOff + 36] = hashSize; // hashSize
    blob[cdOff + 37] = 1; // hashType = CS_HASHTYPE_SHA1
    blob[cdOff + 38] = 0; // platform
    blob[cdOff + 39] = 12; // pageSize (2^12 = 4096)
    writeBe32(blob, cdOff + 40, 0); // spare1

    // Copy identifier
    std::memcpy(blob.data() + cdOff + codeDirHeaderSize, identifier.c_str(), identifier.size() + 1);

    return blob;
}

}

MacOsElfPatcher::MacOsElfPatcher() = default;

std::vector<std::uint8_t> MacOsElfPatcher::Patch(
    const std::vector<std::uint8_t>& sourceElf,
    const std::vector<Domain::ProgramHeader>& originalHeaders,
    const Domain::SysVDynamicSection& dynamicSection,
    std::uint64_t originalPltGotVaddr,
    const std::string& runPath,
    bool lazyBinding,
    bool dependencyDiagnostics,
    const std::vector<Codegen::TrampolineSite>& trampolines
) {
    std::cout << "[Experimental] Building macOS Mach-O bundle with Metal API bridge...\n";

    // Invoke Metal device initialization symbol to link and initialize Metal API context
    RytyMetalInitializeDevice();

    const auto dylinkerCmd = makeDylinkerCommand("/usr/lib/dyld");
    const auto buildVerCmd = makeBuildVersionCommand();

    const auto metalLibCmd = makeDylibCommand("/System/Library/Frameworks/Metal.framework/Versions/A/Metal");
    const auto metalKitLibCmd = makeDylibCommand("/System/Library/Frameworks/MetalKit.framework/Versions/A/MetalKit");
    const auto cocoaLibCmd = makeDylibCommand("/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa");
    const auto quartzCoreLibCmd = makeDylibCommand("/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore");
    const auto libSystemCmd = makeDylibCommand("/usr/lib/libSystem.B.dylib");

    std::string rpath = runPath.empty() ? "@executable_path/libs" : runPath;
    const auto rpathCmd = makeRpathCommand(rpath);

    // Load Commands:
    // PageZero, Text Segment, Data Segment, LinkEdit Segment, Main, Dylinker, BuildVersion, CodeSig, 5 Dylibs, Rpath
    const std::uint32_t cmdsCount = 14;
    const std::uint32_t cmdsSize = static_cast<std::uint32_t>(
        sizeof(SegmentCommand64) * 4 + // PageZero, Text, Data, LinkEdit
        sizeof(MainCommand) +
        dylinkerCmd.size() +
        buildVerCmd.size() +
        sizeof(LinkeditDataCommand) +
        metalLibCmd.size() +
        metalKitLibCmd.size() +
        cocoaLibCmd.size() +
        quartzCoreLibCmd.size() +
        libSystemCmd.size() +
        rpathCmd.size()
    );

    MachOHeader64 header{};
    header.Magic = MH_MAGIC_64;
    header.CpuType = CPU_TYPE_X86_64;
    header.CpuSubtype = CPU_SUBTYPE_ALL;
    header.FileType = MH_EXECUTE;
    header.CmdsCount = cmdsCount;
    header.CmdsSize = cmdsSize;
    header.Flags = 0x00200085; // MH_NOUNDEFS | MH_DYLDLINK | MH_PIE
    header.Reserved = 0;

    std::vector<std::uint8_t> result;
    result.reserve(sizeof(header) + cmdsSize + 16 + sourceElf.size() + 2048);

    const auto appendBytes = [&](const void* ptr, std::size_t size) {
        const auto* bytePtr = static_cast<const std::uint8_t*>(ptr);
        result.insert(result.end(), bytePtr, bytePtr + size);
    };

    appendBytes(&header, sizeof(header));

    // PageZero segment
    SegmentCommand64 pageZero{};
    pageZero.Cmd = LC_SEGMENT_64;
    pageZero.CmdSize = sizeof(SegmentCommand64);
    std::strncpy(pageZero.SegName, "__PAGEZERO", 16);
    pageZero.VAddr = 0;
    pageZero.VSize = 0x100000000ull;
    pageZero.FileOff = 0;
    pageZero.FileSize = 0;
    pageZero.MaxProt = 0;
    pageZero.InitProt = 0;
    appendBytes(&pageZero, sizeof(pageZero));

    // Calculate header layout and entry stub position
    const std::uint64_t headerAndCmdsSize = sizeof(MachOHeader64) + header.CmdsSize;

    // Read ELF entry point if present
    std::uint64_t realEntryVaddr = 0;
    if (sourceElf.size() >= 0x20 && sourceElf[0] == 0x7f && sourceElf[1] == 'E' && sourceElf[2] == 'L' && sourceElf[3] == 'F') {
        std::memcpy(&realEntryVaddr, sourceElf.data() + 0x18, 8);
    }

    const std::uint64_t entryStubOffset = headerAndCmdsSize;
    constexpr std::size_t entryStubSize = 16;
    const std::uint64_t payloadOffset = entryStubOffset + entryStubSize;

    // Entry stub code: x86_64 assembly stub setting up stack frame and jumping to payload entry
    std::vector<std::uint8_t> entryStub(entryStubSize, 0x90); // NOP fill
    entryStub[0] = 0x48; entryStub[1] = 0x31; entryStub[2] = 0xc0; // xor rax, rax
    entryStub[3] = 0x48; entryStub[4] = 0x89; entryStub[5] = 0xe5; // mov rbp, rsp
    entryStub[6] = 0xe9; // jmp rel32

    std::uint64_t targetEntryVaddr = 0x100000000ull + payloadOffset;
    if (realEntryVaddr != 0) {
        if (realEntryVaddr >= 0x100000000ull) {
            targetEntryVaddr = realEntryVaddr;
        } else {
            targetEntryVaddr = 0x100000000ull + payloadOffset + realEntryVaddr;
        }
    }

    const std::uint64_t nextInsnVaddr = 0x100000000ull + entryStubOffset + 11;
    const auto jmpDisp = static_cast<std::int32_t>(targetEntryVaddr - nextInsnVaddr);
    std::memcpy(entryStub.data() + 7, &jmpDisp, 4);

    // Prepare payload copy for in-place instruction patching
    std::vector<std::uint8_t> payload = sourceElf;

    // Calculate total text size including extra trampoline stubs
    std::size_t trampolineTotalSize = 0;
    for (const auto& site : trampolines) {
        trampolineTotalSize += 16 + site.Body.size();
    }

    const std::uint64_t textSize = entryStubSize + payload.size() + trampolineTotalSize;
    const std::uint64_t textVSize = (textSize + 0xFFF) & ~0xFFFull;

    SegmentCommand64 textSeg{};
    textSeg.Cmd = LC_SEGMENT_64;
    textSeg.CmdSize = sizeof(SegmentCommand64);
    std::strncpy(textSeg.SegName, "__TEXT", 16);
    textSeg.VAddr = 0x100000000ull;
    textSeg.VSize = textVSize;
    textSeg.FileOff = headerAndCmdsSize;
    textSeg.FileSize = textSize;
    textSeg.MaxProt = 7; // VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE
    textSeg.InitProt = 5; // VM_PROT_READ | VM_PROT_EXECUTE
    appendBytes(&textSeg, sizeof(textSeg));

    // Data segment for writable memory
    const std::uint64_t dataVAddr = 0x100000000ull + textVSize;
    constexpr std::uint64_t dataSize = 0x1000;
    SegmentCommand64 dataSeg{};
    dataSeg.Cmd = LC_SEGMENT_64;
    dataSeg.CmdSize = sizeof(SegmentCommand64);
    std::strncpy(dataSeg.SegName, "__DATA", 16);
    dataSeg.VAddr = dataVAddr;
    dataSeg.VSize = dataSize;
    dataSeg.FileOff = headerAndCmdsSize + textSize;
    dataSeg.FileSize = 0; // BSS style data
    dataSeg.MaxProt = 3; // VM_PROT_READ | VM_PROT_WRITE
    dataSeg.InitProt = 3; // VM_PROT_READ | VM_PROT_WRITE
    appendBytes(&dataSeg, sizeof(dataSeg));

    // LinkEdit segment for Code Signature
    const std::uint64_t codeSigFileOff = headerAndCmdsSize + textSize;
    const std::uint64_t codeSigBlobSize = makeAdHocCodeSignatureBlob(codeSigFileOff).size();

    SegmentCommand64 linkeditSeg{};
    linkeditSeg.Cmd = LC_SEGMENT_64;
    linkeditSeg.CmdSize = sizeof(SegmentCommand64);
    std::strncpy(linkeditSeg.SegName, "__LINKEDIT", 16);
    linkeditSeg.VAddr = dataVAddr + dataSize;
    linkeditSeg.VSize = (codeSigBlobSize + 0xFFF) & ~0xFFFull;
    linkeditSeg.FileOff = codeSigFileOff;
    linkeditSeg.FileSize = codeSigBlobSize;
    linkeditSeg.MaxProt = 1; // VM_PROT_READ
    linkeditSeg.InitProt = 1; // VM_PROT_READ
    appendBytes(&linkeditSeg, sizeof(linkeditSeg));

    MainCommand mainCmd{};
    mainCmd.Cmd = LC_MAIN;
    mainCmd.CmdSize = sizeof(MainCommand);
    mainCmd.EntryOffset = entryStubOffset; // Points to valid entry stub instructions
    mainCmd.StackSize = 0;
    appendBytes(&mainCmd, sizeof(mainCmd));

    appendBytes(dylinkerCmd.data(), dylinkerCmd.size());
    appendBytes(buildVerCmd.data(), buildVerCmd.size());

    const auto codeSigCmd = makeCodeSignatureCommand(static_cast<std::uint32_t>(codeSigFileOff), static_cast<std::uint32_t>(codeSigBlobSize));
    appendBytes(codeSigCmd.data(), codeSigCmd.size());

    appendBytes(metalLibCmd.data(), metalLibCmd.size());
    appendBytes(metalKitLibCmd.data(), metalKitLibCmd.size());
    appendBytes(cocoaLibCmd.data(), cocoaLibCmd.size());
    appendBytes(quartzCoreLibCmd.data(), quartzCoreLibCmd.size());
    appendBytes(libSystemCmd.data(), libSystemCmd.size());
    appendBytes(rpathCmd.data(), rpathCmd.size());

    // Append entry stub
    result.insert(result.end(), entryStub.begin(), entryStub.end());

    // Process trampolines if present
    std::vector<std::uint8_t> trampolineBuf;
    trampolineBuf.reserve(trampolineTotalSize);

    for (const auto& site : trampolines) {
        if (site.Offset < payload.size() && site.Length <= payload.size() - site.Offset) {
            while ((result.size() + payload.size() + trampolineBuf.size()) % 16 != 0) {
                trampolineBuf.push_back(0xcc); // INT 3 padding
            }

            const std::uint64_t bodyOff = result.size() + payload.size() + trampolineBuf.size();
            const std::uint64_t bodyVaddr = 0x100000000ull + bodyOff;

            auto body = site.Body;
            const auto returnDisplacement = static_cast<std::int64_t>(site.Address + site.Length) -
                static_cast<std::int64_t>(bodyVaddr + site.ReturnBranchOffset + 5);
            if (site.ReturnBranchOffset + 5 <= body.size()) {
                const auto disp32 = static_cast<std::int32_t>(returnDisplacement);
                std::memcpy(body.data() + site.ReturnBranchOffset + 1, &disp32, 4);
            }

            trampolineBuf.insert(trampolineBuf.end(), body.begin(), body.end());

            // Patch jump in payload
            const std::uint64_t siteVaddr = 0x100000000ull + payloadOffset + site.Offset;
            const auto jumpDisplacement = static_cast<std::int32_t>(bodyVaddr - (siteVaddr + 5));

            std::fill_n(payload.begin() + static_cast<std::ptrdiff_t>(site.Offset), site.Length, 0x90); // NOP
            payload[site.Offset] = 0xe9; // JMP rel32
            std::memcpy(payload.data() + site.Offset + 1, &jumpDisplacement, 4);
        }
    }

    // Append patched payload and trampoline bodies
    result.insert(result.end(), payload.begin(), payload.end());
    result.insert(result.end(), trampolineBuf.begin(), trampolineBuf.end());

    // Append ad-hoc code signature blob
    const auto codeSigBlob = makeAdHocCodeSignatureBlob(codeSigFileOff);
    result.insert(result.end(), codeSigBlob.begin(), codeSigBlob.end());

    return result;
}

}
