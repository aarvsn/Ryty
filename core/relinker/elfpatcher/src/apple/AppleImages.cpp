#include <elfpatcher/apple/AppleImages.hpp>
#include <elfpatcher/apple/CodeSignature.hpp>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Elfpatcher::Apple {

namespace {

// ---- Mach-O constants -------------------------------------------------------
constexpr std::uint32_t MH_MAGIC_64 = 0xfeedfacf;
constexpr std::uint32_t FAT_MAGIC = 0xcafebabe;
constexpr std::uint32_t CPU_TYPE_X86_64 = 0x01000007;
constexpr std::uint32_t CPU_TYPE_ARM64 = 0x0100000c;
constexpr std::uint32_t MH_EXECUTE = 0x00000002;
constexpr std::uint32_t MH_NOUNDEFS = 0x00000001;
constexpr std::uint32_t MH_DYLDLINK = 0x00000004;
constexpr std::uint32_t MH_PIE = 0x00200000;

constexpr std::uint32_t LC_SEGMENT_64 = 0x19;
constexpr std::uint32_t LC_LOAD_DYLIB = 0x0c;
constexpr std::uint32_t LC_LOAD_DYLINKER = 0x0e;
constexpr std::uint32_t LC_UUID = 0x1b;
constexpr std::uint32_t LC_MAIN = 0x80000028;
constexpr std::uint32_t LC_SOURCE_VERSION = 0x2a;
constexpr std::uint32_t LC_BUILD_VERSION = 0x32;
constexpr std::uint32_t LC_CODE_SIGNATURE = 0x1d;

// LC_BUILD_VERSION platform values (see MachOOptions::EncodedPlatform).
constexpr std::uint32_t PLATFORM_MACOS = 1;
constexpr std::uint32_t PLATFORM_IOS = 2;
constexpr std::uint32_t PLATFORM_IOSSIMULATOR = 7;

constexpr std::uint64_t VmBase = 0x100000000ULL;   // arm64 main-executable base
constexpr std::uint64_t GuestVmBase = 0x200000000ULL;

// ---- A64 (ARMv8) instruction encoders ---------------------------------------
// Only the instructions the stub needs. The stub uses raw BSD syscalls
// (x16 = 0x02000000 | syscall number, svc #0x80), so it links against
// nothing and needs no dylibs.

constexpr std::uint32_t Svc(uint16_t immediate) { return 0xD4000001u | (static_cast<std::uint32_t>(immediate) << 5); }

constexpr std::uint32_t MovzX(unsigned rd, std::uint32_t imm16) {
    return 0xD2800000u | ((imm16 & 0xFFFFu) << 5) | rd;
}

constexpr std::uint32_t LdrXImm(unsigned rt, unsigned rn, std::uint32_t byteOffset) {
    return 0xF9400000u | ((byteOffset / 8) << 10) | (rn << 5) | rt;
}

constexpr std::uint32_t StrXImm(unsigned rt, unsigned rn, std::uint32_t byteOffset) {
    return 0xF9000000u | ((byteOffset / 8) << 10) | (rn << 5) | rt;
}

constexpr std::uint32_t AddXImm(unsigned rd, bool rdSp, unsigned rn, bool rnSp, std::uint32_t imm12) {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10) | ((rnSp ? 31u : rn) << 5) | (rdSp ? 31u : rd);
}

constexpr std::uint32_t SubXSpImm(std::uint32_t imm12) { return 0xD1000000u | ((imm12 & 0xFFFu) << 10) | (31u << 5) | 31u; }

// ADRP + ADD pair addressing `target` from code at `pc` (virtual addresses).
void AdrpAdd(unsigned rd, std::uint64_t pc, std::uint64_t target, std::uint32_t& adrp, std::uint32_t& add) {
    const std::uint64_t pcPage = pc & ~0xFFFULL;
    const std::uint64_t targetPage = target & ~0xFFFULL;
    std::int64_t pages = 0;
    if (targetPage >= pcPage) pages = static_cast<std::int64_t>((targetPage - pcPage) >> 12);
    else pages = -static_cast<std::int64_t>((pcPage - targetPage) >> 12);
    const std::int64_t imm21 = pages & 0x1FFFFF;
    const std::uint32_t immlo = static_cast<std::uint32_t>(imm21 & 0x3);
    const std::uint32_t immhi = static_cast<std::uint32_t>((imm21 >> 2) & 0x7FFFF);
    adrp = 0x90000000u | (immlo << 29) | (immhi << 5) | rd;
    add = 0x91000000u | ((static_cast<std::uint32_t>(target) & 0xFFFu) << 10) | (rd << 5) | rd;
}

void Patch32(std::vector<std::uint8_t>& code, std::size_t byteOffset, std::uint32_t word) {
    code[byteOffset] = static_cast<std::uint8_t>(word & 0xff);
    code[byteOffset + 1] = static_cast<std::uint8_t>((word >> 8) & 0xff);
    code[byteOffset + 2] = static_cast<std::uint8_t>((word >> 16) & 0xff);
    code[byteOffset + 3] = static_cast<std::uint8_t>((word >> 24) & 0xff);
}

void Append32(std::vector<std::uint8_t>& code, std::uint32_t word) {
    code.push_back(static_cast<std::uint8_t>(word & 0xff));
    code.push_back(static_cast<std::uint8_t>((word >> 8) & 0xff));
    code.push_back(static_cast<std::uint8_t>((word >> 16) & 0xff));
    code.push_back(static_cast<std::uint8_t>((word >> 24) & 0xff));
}

// ---- small Mach-O load command writers --------------------------------------

std::vector<std::uint8_t> SegmentCommandBytes(const char* name, std::uint64_t vaddr, std::uint64_t vsize,
                                              std::uint64_t fileOff, std::uint64_t fileSize,
                                              std::uint32_t prot) {
#pragma pack(push, 1)
    struct SegmentCommand {
        std::uint32_t cmd, cmdSize;
        char segName[16];
        std::uint64_t vaddr, vsize, fileOff, fileSize;
        std::uint32_t maxProt, initProt, nSects, flags;
    };
#pragma pack(pop)
    static_assert(sizeof(SegmentCommand) == 72, "LC_SEGMENT_64 must be 72 bytes");
    std::vector<std::uint8_t> bytes(sizeof(SegmentCommand), 0);
    auto* cmd = reinterpret_cast<SegmentCommand*>(bytes.data());
    cmd->cmd = LC_SEGMENT_64;
    cmd->cmdSize = sizeof(SegmentCommand);
    std::memcpy(cmd->segName, name, std::min<std::size_t>(std::strlen(name), 15));
    cmd->vaddr = vaddr;
    cmd->vsize = vsize;
    cmd->fileOff = fileOff;
    cmd->fileSize = fileSize;
    cmd->maxProt = prot;
    cmd->initProt = prot;
    cmd->nSects = 0;
    cmd->flags = 0;
    return bytes;
}

std::vector<std::uint8_t> DylinkerCommandBytes() {
#pragma pack(push, 1)
    struct Cmd { std::uint32_t cmd, cmdSize, nameOffset; };
#pragma pack(pop)
    const char* path = "/usr/lib/dyld";
    const std::size_t raw = sizeof(Cmd) + std::strlen(path) + 1;
    std::vector<std::uint8_t> bytes((raw + 7) & ~std::size_t(7), 0);
    auto* cmd = reinterpret_cast<Cmd*>(bytes.data());
    cmd->cmd = LC_LOAD_DYLINKER;
    cmd->cmdSize = static_cast<std::uint32_t>(bytes.size());
    cmd->nameOffset = sizeof(Cmd);
    std::memcpy(bytes.data() + sizeof(Cmd), path, std::strlen(path) + 1);
    return bytes;
}

std::vector<std::uint8_t> DylibCommandBytes(const std::string& path) {
#pragma pack(push, 1)
    struct Cmd { std::uint32_t cmd, cmdSize, nameOffset, timestamp, currentVersion, compatibilityVersion; };
#pragma pack(pop)
    const std::size_t raw = sizeof(Cmd) + path.size() + 1;
    std::vector<std::uint8_t> bytes((raw + 7) & ~std::size_t(7), 0);
    auto* cmd = reinterpret_cast<Cmd*>(bytes.data());
    cmd->cmd = LC_LOAD_DYLIB;
    cmd->cmdSize = static_cast<std::uint32_t>(bytes.size());
    cmd->nameOffset = sizeof(Cmd);
    cmd->timestamp = 2;
    cmd->currentVersion = 0x00020100;  // 2.1.0
    cmd->compatibilityVersion = 0x00010000;
    std::memcpy(bytes.data() + sizeof(Cmd), path.c_str(), path.size() + 1);
    return bytes;
}

std::vector<std::uint8_t> BuildVersionBytes(std::uint32_t platform, std::uint32_t minOs, std::uint32_t sdk) {
#pragma pack(push, 1)
    struct Cmd { std::uint32_t cmd, cmdSize, platform, minOs, sdk, nTools; };
#pragma pack(pop)
    std::vector<std::uint8_t> bytes(sizeof(Cmd), 0);
    auto* cmd = reinterpret_cast<Cmd*>(bytes.data());
    cmd->cmd = LC_BUILD_VERSION;
    cmd->cmdSize = sizeof(Cmd);
    cmd->platform = platform;
    cmd->minOs = minOs;
    cmd->sdk = sdk;
    cmd->nTools = 0;
    return bytes;
}

std::vector<std::uint8_t> UuidBytes(const std::vector<std::uint8_t>& seed) {
#pragma pack(push, 1)
    struct Cmd { std::uint32_t cmd, cmdSize; std::uint8_t uuid[16]; };
#pragma pack(pop)
    std::vector<std::uint8_t> bytes(sizeof(Cmd), 0);
    auto* cmd = reinterpret_cast<Cmd*>(bytes.data());
    cmd->cmd = LC_UUID;
    cmd->cmdSize = sizeof(Cmd);
    std::uint64_t h1 = 0x811c9dc5, h2 = 0xcbf29ce484222325ULL;
    for (const std::uint8_t byte : seed) {
        h1 = (h1 ^ byte) * 16777619u;
        h2 = (h2 ^ byte) * 1099511628211ULL;
    }
    std::memcpy(cmd->uuid, &h1, 8);
    std::memcpy(cmd->uuid + 8, &h2, 8);
    return bytes;
}

std::vector<std::uint8_t> SourceVersionBytes(std::uint64_t version) {
#pragma pack(push, 1)
    struct Cmd { std::uint32_t cmd, cmdSize; std::uint64_t version; };
#pragma pack(pop)
    std::vector<std::uint8_t> bytes(sizeof(Cmd), 0);
    auto* cmd = reinterpret_cast<Cmd*>(bytes.data());
    cmd->cmd = LC_SOURCE_VERSION;
    cmd->cmdSize = sizeof(Cmd);
    cmd->version = version;
    return bytes;
}

std::vector<std::uint8_t> MainCommandBytes(std::uint64_t entryOffset) {
#pragma pack(push, 1)
    struct Cmd { std::uint32_t cmd, cmdSize; std::uint64_t entryOffset, stackSize; };
#pragma pack(pop)
    std::vector<std::uint8_t> bytes(sizeof(Cmd), 0);
    auto* cmd = reinterpret_cast<Cmd*>(bytes.data());
    cmd->cmd = LC_MAIN;
    cmd->cmdSize = sizeof(Cmd);
    cmd->entryOffset = entryOffset;
    cmd->stackSize = 0;
    return bytes;
}

std::vector<std::uint8_t> LinkeditDataBytes(std::uint32_t cmd, std::uint32_t offset, std::uint32_t size) {
#pragma pack(push, 1)
    struct Cmd { std::uint32_t cmd, cmdSize, dataOff, dataSize; };
#pragma pack(pop)
    std::vector<std::uint8_t> bytes(sizeof(Cmd), 0);
    auto* command = reinterpret_cast<Cmd*>(bytes.data());
    command->cmd = cmd;
    command->cmdSize = sizeof(Cmd);
    command->dataOff = offset;
    command->dataSize = size;
    return bytes;
}

std::uint32_t ReadLe32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, 4);
    return value;
}

// ---- stub program -----------------------------------------------------------

// One ADRP/ADD placeholder pair: two consecutive instruction words that get
// relocated to point at `stringOffset` inside the string blob.
struct StubReloc {
    std::size_t byteOffset;      // of the ADRP word (ADD follows at +4)
    unsigned reg;
    std::size_t stringOffset;
};

struct StubProgram {
    std::vector<std::uint8_t> code;
    std::string strings;
    std::vector<StubReloc> relocs;
};

// Builds the stub program. When `launcher` is true it execve's
// "/usr/bin/arch -x86_64 <argv0>" (universal macOS launcher); otherwise it
// only prints `message` and exits with `exitCode` (iOS guest shell).
StubProgram BuildStubProgram(bool launcher, const std::string& message, std::uint8_t exitCode) {
    StubProgram program;
    const std::size_t archOffset = 0;
    const std::size_t flagOffset = archOffset + std::string("/usr/bin/arch").size() + 1;
    const std::size_t messageOffset = flagOffset + std::string("-x86_64").size() + 1;
    program.strings += "/usr/bin/arch"; program.strings.push_back('\0');
    program.strings += "-x86_64"; program.strings.push_back('\0');
    program.strings += message; program.strings.push_back('\0');

    std::size_t ordinal = 0;
    const auto emit = [&](std::uint32_t word) { Append32(program.code, word); ++ordinal; };
    const auto emitAddrPair = [&](unsigned reg, std::size_t stringOffset) {
        StubReloc reloc;
        reloc.byteOffset = program.code.size();
        reloc.reg = reg;
        reloc.stringOffset = stringOffset;
        program.relocs.push_back(reloc);
        emit(MovzX(reg, 0)); // ADRP placeholder
        emit(MovzX(reg, 0)); // ADD placeholder
    };

    if (launcher) {
        // x0=argc, x1=argv, x2=envp at LC_MAIN entry.
        emit(LdrXImm(4, 1, 0));                       // x4 = argv[0]
        emit(SubXSpImm(64));                          // argv2 scratch (16B aligned)
        emitAddrPair(5, archOffset);                  // x5 = &"/usr/bin/arch"
        emitAddrPair(5, flagOffset);                  // x5 = &"-x86_64"
        emitAddrPair(0, archOffset);                  // x0 = &"/usr/bin/arch"
        emit(StrXImm(5, 31, 0));                      // argv2[0] = arch path
        emit(StrXImm(5, 31, 8));                      // argv2[1] = "-x86_64"
        emit(StrXImm(4, 31, 16));                     // argv2[2] = argv[0]
        emit(StrXImm(31, 31, 24));                    // argv2[3] = NULL (xzr)
        emit(AddXImm(1, false, 31, true, 0));         // x1 = sp (argv2)
        emit(MovzX(16, 59));                          // x16 = SYS_execve
        emit(Svc(0x80));                              // execve(...)
    }
    // Diagnostic path: write(2, message, len) then exit(code). Reached when
    // the execve fails (launcher) or unconditionally (iOS guest shell).
    emit(MovzX(0, 2));                                // fd = stderr
    emitAddrPair(1, messageOffset);                   // x1 = &message
    emit(MovzX(2, static_cast<std::uint32_t>(message.size())));
    emit(MovzX(16, 4));                               // x16 = SYS_write
    emit(Svc(0x80));
    emit(MovzX(0, exitCode));
    emit(MovzX(16, 1));                               // x16 = SYS_exit
    emit(Svc(0x80));
    return program;
}

void RelocateStubProgram(StubProgram& program, std::uint64_t codeBase, std::uint64_t stringBase) {
    for (const StubReloc& reloc : program.relocs) {
        std::uint32_t adrp = 0, add = 0;
        AdrpAdd(reloc.reg, codeBase + reloc.byteOffset, stringBase + reloc.stringOffset, adrp, add);
        Patch32(program.code, reloc.byteOffset, adrp);
        Patch32(program.code, reloc.byteOffset + 4, add);
    }
}

// Builds a complete thin arm64 Mach-O around the stub program. `guestPayload`
// non-empty adds the read-only __GUEST segment (iOS).
std::vector<std::uint8_t> AssembleArm64Image(const StubProgram& program, const MachOOptions& options,
                                             const std::string& identifier,
                                             const std::vector<std::uint8_t>& guestPayload) {
    // iOS and iOS Simulator share the arm64 guest-container layout; only the
    // LC_BUILD_VERSION platform field and the page size differ below.
    const bool ios = options.IsIosFamily();
    const std::uint64_t pageMask = ios ? 0x3FFFULL : 0xFFFULL;
    const auto pageRound = [&](std::uint64_t value) { return (value + pageMask) & ~pageMask; };

    const std::uint64_t codeFileOff = ios ? 0x4000ULL : 0x1000ULL;   // code + strings
    const std::uint64_t codeBase = VmBase + codeFileOff;

    std::vector<std::uint8_t> textContent = program.code;
    textContent.insert(textContent.end(), program.strings.begin(), program.strings.end());
    const std::uint64_t textEnd = codeFileOff + textContent.size();
    const std::uint64_t guestFileOff = pageRound(textEnd);
    const std::uint64_t linkeditFileOff = pageRound(guestFileOff + guestPayload.size());
    const std::string signatureIdentifier = identifier.empty() ? "ryty.bundle" : identifier;
    // The entitlements plist (when provided) is embedded as CSSLOT_ENTITLEMENTS
    // in the SuperBlob, exactly like the macOS writer does.
    const std::vector<std::uint8_t>* entitlements =
        options.entitlements.empty() ? nullptr : &options.entitlements;
    const std::uint32_t signatureSize = AdHocSignatureSize(linkeditFileOff, signatureIdentifier.size(), entitlements);

    // ---- load commands ----
    std::vector<std::vector<std::uint8_t>> commands;
    commands.push_back(SegmentCommandBytes("__TEXT", VmBase, pageRound(textEnd), 0,
                                           guestPayload.empty() ? linkeditFileOff : guestFileOff, 5));
    if (!guestPayload.empty()) {
        commands.push_back(SegmentCommandBytes("__GUEST", GuestVmBase, pageRound(guestPayload.size()),
                                               guestFileOff, guestPayload.size(), 1));
    }
    commands.push_back(SegmentCommandBytes("__LINKEDIT", VmBase + linkeditFileOff, pageRound(signatureSize),
                                           linkeditFileOff, signatureSize, 1));
    commands.push_back(MainCommandBytes(codeFileOff));
    commands.push_back(DylinkerCommandBytes());
    // The iOS guest container declares the frameworks the ported game uses
    // once the A64 runtime executes it (Metal works on iOS; UIKit replaces
    // Cocoa and iOS frameworks are unversioned).
    if (!guestPayload.empty()) {
        commands.push_back(DylibCommandBytes("/System/Library/Frameworks/Metal.framework/Metal"));
        commands.push_back(DylibCommandBytes("/System/Library/Frameworks/MetalKit.framework/MetalKit"));
        commands.push_back(DylibCommandBytes("/System/Library/Frameworks/UIKit.framework/UIKit"));
        commands.push_back(DylibCommandBytes("/System/Library/Frameworks/QuartzCore.framework/QuartzCore"));
        commands.push_back(DylibCommandBytes("/usr/lib/libSystem.B.dylib"));
    }
    commands.push_back(BuildVersionBytes(options.EncodedPlatform(), options.minOs, options.sdk));
    // Distinct UUID seeds so the two slices of a universal binary (and the iOS
    // device vs simulator containers) never share an identifier even for the
    // same input image.
    std::vector<std::uint8_t> uuidSeed;
    if (guestPayload.empty()) {
        uuidSeed = {'r', 'y', 't', 'y', 'a', 'r', 'm', '6', '4'};
    } else {
        uuidSeed = guestPayload;
        const char* suffix = options.platform == MachOOptions::Platform::IosSimulator ? "ios-sim" : "ios";
        uuidSeed.insert(uuidSeed.end(), suffix, suffix + std::strlen(suffix));
    }
    commands.push_back(UuidBytes(uuidSeed));
    commands.push_back(SourceVersionBytes(options.sourceVersion));
    commands.push_back(LinkeditDataBytes(LC_CODE_SIGNATURE, static_cast<std::uint32_t>(linkeditFileOff), signatureSize));

    std::size_t cmdsSize = 0;
    for (const auto& command : commands) cmdsSize += command.size();

    // ---- assemble ----
    StubProgram finalProgram = program;
    RelocateStubProgram(finalProgram, codeBase, codeBase + finalProgram.code.size());

    std::vector<std::uint8_t> image;
    image.reserve(static_cast<std::size_t>(linkeditFileOff + signatureSize));
    const auto append = [&](const std::vector<std::uint8_t>& bytes) { image.insert(image.end(), bytes.begin(), bytes.end()); };
    const auto padTo = [&](std::uint64_t target) { while (image.size() < target) image.push_back(0); };

#pragma pack(push, 1)
    struct Header {
        std::uint32_t magic, cpuType, cpuSubtype, fileType, cmdsCount, cmdsSize, flags, reserved;
    };
#pragma pack(pop)
    static_assert(sizeof(Header) == 32, "Mach-O 64 header must be 32 bytes");
    Header header{};
    header.magic = MH_MAGIC_64;
    header.cpuType = CPU_TYPE_ARM64;
    header.cpuSubtype = 0; // CPU_SUBTYPE_ARM64_ALL
    header.fileType = MH_EXECUTE;
    header.cmdsCount = static_cast<std::uint32_t>(commands.size());
    header.cmdsSize = static_cast<std::uint32_t>(cmdsSize);
    header.flags = MH_NOUNDEFS | MH_DYLDLINK | MH_PIE;
    const auto headerBytes = reinterpret_cast<const std::uint8_t*>(&header);
    image.insert(image.end(), headerBytes, headerBytes + sizeof(header));
    for (const auto& command : commands) append(command);

    padTo(codeFileOff);
    image.insert(image.end(), finalProgram.code.begin(), finalProgram.code.end());
    image.insert(image.end(), finalProgram.strings.begin(), finalProgram.strings.end());

    if (!guestPayload.empty()) {
        padTo(guestFileOff);
        image.insert(image.end(), guestPayload.begin(), guestPayload.end());
    }

    padTo(linkeditFileOff);
    if (image.size() != linkeditFileOff) throw std::runtime_error("arm64 writer: layout desync before signature");
    AdHocSignatureSpec spec;
    spec.image = image.data();
    spec.codeLimit = linkeditFileOff;
    spec.identifier = signatureIdentifier;
    spec.entitlements = entitlements;
    spec.hardenedRuntime = options.hardenedRuntime;
    append(BuildAdHocSignature(spec));
    return image;
}

}

std::vector<std::uint8_t> MakeArm64LauncherSlice(const MachOOptions& options, const std::string& version) {
    (void)version;
    const std::string message =
        "ryty: could not start the x86_64 guest slice of this universal port.\n"
        "Apple Silicon Macs need Rosetta 2:  softwareupdate --install-rosetta\n";
    const StubProgram program = BuildStubProgram(true, message, 1);
    MachOOptions launcherOptions = options;
    if (launcherOptions.identifier.empty()) launcherOptions.identifier = "ryty.launcher";
    return AssembleArm64Image(program, launcherOptions, launcherOptions.identifier, {});
}

std::vector<std::uint8_t> MakeIosGuestImage(const std::vector<std::uint8_t>& guestPayload,
                                            const MachOOptions& options, const std::string& version) {
    (void)version;
    if (guestPayload.empty()) throw std::runtime_error("iOS writer: guest payload is empty");
    const std::string message =
        "ryty: the iOS guest runtime (A64 translation layer) is not installed.\n"
        "This build embeds the ported guest image for the experimental iOS target.\n";
    const StubProgram program = BuildStubProgram(false, message, 42);
    MachOOptions iosOptions = options;
    if (iosOptions.platform == MachOOptions::Platform::MacOs) {
        iosOptions.platform = MachOOptions::Platform::Ios; // always an iOS-family layout
    }
    return AssembleArm64Image(program, iosOptions, iosOptions.identifier, guestPayload);
}

std::vector<std::uint8_t> MakeUniversalBinary(const std::vector<std::uint8_t>& firstSlice,
                                              const std::vector<std::uint8_t>& secondSlice) {
    if (firstSlice.size() < 32 || secondSlice.size() < 32) throw std::runtime_error("universal writer: slice too small");
#pragma pack(push, 1)
    struct FatEntry {
        std::uint32_t cpuType, cpuSubtype, offset, size, align;
    };
#pragma pack(pop)
    FatEntry entries[2];
    const std::vector<std::uint8_t>* slices[2] = {&firstSlice, &secondSlice};
    for (int i = 0; i < 2; ++i) {
        if (ReadLe32(*slices[i], 0) != MH_MAGIC_64) throw std::runtime_error("universal writer: slice is not MH_MAGIC_64");
        entries[i].cpuType = ReadLe32(*slices[i], 4);
        entries[i].cpuSubtype = ReadLe32(*slices[i], 8);
        entries[i].align = 12; // slices are 4 KB aligned internally
    }

    // Apple convention: the arm64 slice comes first so Apple Silicon prefers
    // it; the launcher then re-execs the x86_64 slice through Rosetta.
    FatEntry* arm = nullptr;
    FatEntry* x86 = nullptr;
    const std::vector<std::uint8_t>* armBytes = nullptr;
    const std::vector<std::uint8_t>* x86Bytes = nullptr;
    for (int i = 0; i < 2; ++i) {
        if (entries[i].cpuType == CPU_TYPE_ARM64) { arm = &entries[i]; armBytes = slices[i]; }
        if (entries[i].cpuType == CPU_TYPE_X86_64) { x86 = &entries[i]; x86Bytes = slices[i]; }
    }
    if (!arm || !x86) throw std::runtime_error("universal writer: need one arm64 and one x86_64 slice");

    const std::uint32_t headerSize = 8 + 2 * 20;
    arm->offset = (headerSize + 0xFFFu) & ~0xFFFu;
    arm->size = static_cast<std::uint32_t>(armBytes->size());
    x86->offset = (arm->offset + arm->size + 0xFFFu) & ~0xFFFu;
    x86->size = static_cast<std::uint32_t>(x86Bytes->size());

    const auto writeBe = [](std::vector<std::uint8_t>& out, std::uint32_t value) {
        out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
        out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
        out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
        out.push_back(static_cast<std::uint8_t>(value & 0xff));
    };

    std::vector<std::uint8_t> fat;
    fat.reserve(static_cast<std::size_t>(x86->offset) + x86->size);
    writeBe(fat, FAT_MAGIC);
    writeBe(fat, 2);
    for (const FatEntry* entry : {arm, x86}) {
        writeBe(fat, entry->cpuType);
        writeBe(fat, entry->cpuSubtype);
        writeBe(fat, entry->offset);
        writeBe(fat, entry->size);
        writeBe(fat, entry->align);
    }
    fat.resize(arm->offset, 0);
    fat.insert(fat.end(), armBytes->begin(), armBytes->end());
    fat.resize(x86->offset, 0);
    fat.insert(fat.end(), x86Bytes->begin(), x86Bytes->end());
    return fat;
}

}
