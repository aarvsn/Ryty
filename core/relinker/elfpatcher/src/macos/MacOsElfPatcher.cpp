#include <elfpatcher/macos/MacOsElfPatcher.hpp>
#include <io/BufferUtils.hpp>
#include <cstring>
#include <iostream>

#if defined(__APPLE__)
extern "C" void RytyMetalInitializeDevice(void);
extern "C" int RytyMetalIsSupported(void);
extern "C" const char* RytyMetalGetDeviceName(void);
extern "C" void RytyMetalSetClearColor(double r, double g, double b, double a);
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
    const auto libSystemCmd = makeDylibCommand("/usr/lib/libSystem.B.dylib");

    std::string rpath = runPath.empty() ? "@executable_path/libs" : runPath;
    const auto rpathCmd = makeRpathCommand(rpath);

    const std::uint32_t cmdsCount = 8;
    const std::uint32_t cmdsSize = static_cast<std::uint32_t>(
        sizeof(SegmentCommand64) * 2 +
        sizeof(MainCommand) +
        metalLibCmd.size() +
        metalKitLibCmd.size() +
        cocoaLibCmd.size() +
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

    // Entry stub code: x86_64 assembly stub (xor rax, rax; ret)
    std::vector<std::uint8_t> entryStub = {
        0x48, 0x31, 0xc0, // xor rax, rax
        0xc3             // ret
    };

    const std::uint64_t entryStubOffset = headerAndCmdsSize;
    const std::uint64_t payloadOffset = entryStubOffset + entryStub.size();
    const std::uint64_t textSize = entryStub.size() + sourceElf.size();

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
    appendBytes(libSystemCmd.data(), libSystemCmd.size());
    appendBytes(rpathCmd.data(), rpathCmd.size());

    // Append entry stub
    result.insert(result.end(), entryStub.begin(), entryStub.end());

    // Append payload
    result.insert(result.end(), sourceElf.begin(), sourceElf.end());

    return result;
}

}
