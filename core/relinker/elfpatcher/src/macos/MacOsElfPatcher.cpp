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

struct RpathCommand {
    std::uint32_t Cmd;
    std::uint32_t CmdSize;
    std::uint32_t PathOffset;
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

    const auto metalLibCmd = makeDylibCommand("/System/Library/Frameworks/Metal.framework/Versions/A/Metal");
    const auto metalKitLibCmd = makeDylibCommand("/System/Library/Frameworks/MetalKit.framework/Versions/A/MetalKit");
    const auto cocoaLibCmd = makeDylibCommand("/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa");
    const auto quartzCoreLibCmd = makeDylibCommand("/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore");
    const auto libSystemCmd = makeDylibCommand("/usr/lib/libSystem.B.dylib");

    std::string rpath = runPath.empty() ? "@executable_path/libs" : runPath;
    const auto rpathCmd = makeRpathCommand(rpath);

    const std::uint32_t cmdsCount = 9;
    const std::uint32_t cmdsSize = static_cast<std::uint32_t>(
        sizeof(SegmentCommand64) * 2 +
        sizeof(MainCommand) +
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
    result.reserve(sizeof(header) + cmdsSize + 16 + sourceElf.size());

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

    SegmentCommand64 textSeg{};
    textSeg.Cmd = LC_SEGMENT_64;
    textSeg.CmdSize = sizeof(SegmentCommand64);
    std::strncpy(textSeg.SegName, "__TEXT", 16);
    textSeg.VAddr = 0x100000000ull;
    textSeg.VSize = (textSize + 0xFFF) & ~0xFFFull;
    textSeg.FileOff = headerAndCmdsSize;
    textSeg.FileSize = textSize;
    textSeg.MaxProt = 7; // VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE
    textSeg.InitProt = 5; // VM_PROT_READ | VM_PROT_EXECUTE
    appendBytes(&textSeg, sizeof(textSeg));

    MainCommand mainCmd{};
    mainCmd.Cmd = LC_MAIN;
    mainCmd.CmdSize = sizeof(MainCommand);
    mainCmd.EntryOffset = entryStubOffset; // Points to valid entry stub instructions
    mainCmd.StackSize = 0;
    appendBytes(&mainCmd, sizeof(mainCmd));

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

    return result;
}

}
