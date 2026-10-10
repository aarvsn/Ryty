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
    std::string rpath = runPath.empty() ? "@executable_path/libs" : runPath;
    const auto rpathCmd = makeRpathCommand(rpath);

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

    // ---- Compute load command sizes and the page-aligned payload position ----
    const std::uint32_t cmdsCount = static_cast<std::uint32_t>(segs.size() + 2 /*tramp,linkedit*/ + 1 /*main*/
        + 1 /*dylinker*/ + 1 /*buildver*/ + 5 /*dylibs*/ + 1 /*rpath*/);
    const std::uint64_t cmdsSize = sizeof(SegmentCommand64) * (segs.size() + 2)
        + (haveExec ? sizeof(Section64) : 0)
        + sizeof(MainCommand)
        + dylinkerCmd.size() + buildVerCmd.size()
        + metalLibCmd.size() + metalKitLibCmd.size() + cocoaLibCmd.size()
        + quartzCoreLibCmd.size() + libSystemCmd.size() + rpathCmd.size();
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

    // ---- Entry point ----
    std::uint64_t entryoff = 0;
    bool foundEntry = false;
    const std::uint64_t entryVA = e_entry + rebase;
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
    result.reserve(static_cast<std::size_t>(linkeditFileOff + 0x1000));
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
        sc.VAddr = linkeditVmaddr; sc.VSize = 0x1000;
        sc.FileOff = linkeditFileOff; sc.FileSize = 0; // codesign(1) fills this
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
    appendBytes(metalLibCmd.data(), metalLibCmd.size());
    appendBytes(metalKitLibCmd.data(), metalKitLibCmd.size());
    appendBytes(cocoaLibCmd.data(), cocoaLibCmd.size());
    appendBytes(quartzCoreLibCmd.data(), quartzCoreLibCmd.size());
    appendBytes(libSystemCmd.data(), libSystemCmd.size());
    appendBytes(rpathCmd.data(), rpathCmd.size());

    // ---- File data: payload, synthetic images, trampolines ----
    padTo(payloadFileOff);
    appendBytes(payload.data(), payload.size());
    for (const auto& g : segs) {
        if (g.direct) continue;
        padTo(g.fileoff);
        if (g.filesz > 0) appendBytes(g.image.data(), g.image.size());
    }
    padTo(trampFileOff);
    appendBytes(trampolineBuf.data(), trampolineBuf.size());
    padTo(linkeditFileOff); // __LINKEDIT must start within the file even when empty

    std::cerr << "[ryty] e_entry=0x" << std::hex << e_entry << " entryoff=0x" << entryoff
              << std::dec << " | total size " << result.size() << " bytes\n";
    return result;
}

}
