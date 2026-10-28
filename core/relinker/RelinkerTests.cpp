#include <Cli.hpp>
#include <io/ByteReader.hpp>
#include <io/ByteWriter.hpp>
#include <relinker/parsing/ElfReader.hpp>
#include <relinker/guest/GuestImage.hpp>
#include <relinker/analysis/SyscallScanner.hpp>
#include <elfpatcher/macos/MacOsElfPatcher.hpp>
#include <codegen/IInstructionScanner.hpp>
#include <filesystem>
#include <fstream>

extern "C" {
void RytyMetalInitializeDevice(void);
int RytyMetalIsSupported(void);
const char* RytyMetalGetDeviceName(void);
void RytyMetalSetClearColor(double r, double g, double b, double a);
void RytyMetalSetDepthStencilEnabled(int enabled);
void* RytyMetalCreateTexture2D(int width, int height, int pixelFormat);
void RytyMetalUpdateTexture2D(void* handle, const void* bytes, int bytesPerRow);
void RytyMetalDestroyTexture2D(void* handle);
void* RytyMetalCreateBuffer(const void* data, unsigned long length);
void RytyMetalUpdateBuffer(void* handle, const void* data, unsigned long length, unsigned long offset);
void RytyMetalDestroyBuffer(void* handle);
void RytyMetalSetViewport(double x, double y, double width, double height, double znear, double zfar);
void RytyMetalSetScissorRect(unsigned int x, unsigned int y, unsigned int width, unsigned int height);
void RytyMetalSetBlendMode(int blendMode);
void RytyMetalSetCullMode(int cullMode);
void RytyMetalSetTriangleFillMode(int fillMode);
void RytyMetalSetFrontFacingWinding(int winding);
void RytyMetalSetDepthBias(float depthBias, float slopeScale, float clamp);
void RytyMetalSetStencilReferenceValue(unsigned int referenceValue);
void RytyMetalSetRenderPipelineState(void* pipelineState);
void RytyMetalSetComputePipelineState(void* computePipeline);
void RytyMetalDispatchThreadgroups(unsigned int threadsX, unsigned int threadsY, unsigned int threadsZ, unsigned int tgX, unsigned int tgY, unsigned int tgZ);
void RytyMetalSetSamplerState(int index, void* sampler);
void RytyMetalDrawPrimitives(int primitiveType, unsigned int start, unsigned int count);
void RytyMetalDrawIndexedPrimitives(int primitiveType, unsigned int indexCount, int indexType, void* indexBuffer, unsigned int indexBufferOffset);
unsigned long RytyMetalGetMaxThreadsPerThreadgroup(void);
int RytyMetalSupportsFeatureSet(int featureSet);
void RytyMetalSetSampleCount(int sampleCount);
void RytyMetalSetRasterizerDiscardEnabled(int enabled);
int RytyMetalGetDeviceCapabilities(void);
}
#include <SceTypes.hpp>
#include <prx/libRytyNet/src/RytyNetBridge.hpp>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

extern "C" {
int APS5_VABI RytyNetIsEnabled(void);
std::uint32_t APS5_VABI RytyNetVersion(void);
int APS5_VABI RytyNetSetEnabled(int enable);
}

void TestCliArgs() {
    std::cout << "[Test] Running TestCliArgs...\n";
    {
        const char* argv[] = {"ryty", "--ps4", "--macos", "input.elf", "output.bin"};
        auto args = Cli::ParseArgs(5, const_cast<char**>(argv));
        assert(args.consoleMode == Cli::ConsoleMode::PS4);
        assert(args.toMacOs == true);
        assert(args.inputPath == "input.elf");
        assert(args.outputPath == "output.bin");
    }
    {
        const char* argv[] = {"ryty", "--console", "ps5", "--windows", "in.elf", "out.exe"};
        auto args = Cli::ParseArgs(6, const_cast<char**>(argv));
        assert(args.consoleMode == Cli::ConsoleMode::PS5);
        assert(args.toWindows == true);
    }
    std::cout << "  -> PASSED!\n";
}

void TestPs4Detection() {
    std::cout << "[Test] Running TestPs4Detection...\n";
    {
        // Minimal ELF with PS4 string
        std::vector<std::uint8_t> buffer(128, 0);
        buffer[0] = 0x7f; buffer[1] = 'E'; buffer[2] = 'L'; buffer[3] = 'F';
        std::string ps4Marker = "libkernel.sprx";
        std::memcpy(buffer.data() + 64, ps4Marker.data(), ps4Marker.size());

        Relinker::ElfReader reader(buffer);
        assert(reader.DetectConsoleTarget() == "ps4");
    }
    {
        // Minimal ELF with PS5 string
        std::vector<std::uint8_t> buffer(128, 0);
        buffer[0] = 0x7f; buffer[1] = 'E'; buffer[2] = 'L'; buffer[3] = 'F';
        std::string ps5Marker = "libkernel_sys.prx";
        std::memcpy(buffer.data() + 64, ps5Marker.data(), ps5Marker.size());

        Relinker::ElfReader reader(buffer);
        assert(reader.DetectConsoleTarget() == "ps5");
    }
    std::cout << "  -> PASSED!\n";
}

void TestMacOsPatcher() {
    std::cout << "[Test] Running TestMacOsPatcher...\n";
    // Minimal well-formed ELF64: header + one PT_LOAD (R|X) at vaddr 0x10000000.
    std::vector<std::uint8_t> dummyElf(0x3000, 0);
    const std::uint8_t ident[16] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0x09, 0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(dummyElf.data(), ident, 16);
    auto put16 = [&](std::size_t off, std::uint16_t v) { std::memcpy(dummyElf.data() + off, &v, 2); };
    auto put32 = [&](std::size_t off, std::uint32_t v) { std::memcpy(dummyElf.data() + off, &v, 4); };
    auto put64 = [&](std::size_t off, std::uint64_t v) { std::memcpy(dummyElf.data() + off, &v, 8); };
    put16(16, 2);           // e_type = ET_EXEC
    put16(18, 0x3e);        // e_machine = EM_X86_64
    put32(20, 1);           // e_version
    put64(24, 0x10000000);  // e_entry
    put64(32, 64);          // e_phoff
    put16(52, 64);
    put16(54, 56);
    put16(56, 1);
    put32(64, 1);           // p_type = PT_LOAD
    put32(68, 5);           // p_flags = R|X
    put64(72, 0x1000);      // p_offset
    put64(80, 0x10000000);  // p_vaddr
    put64(88, 0x10000000);  // p_paddr
    put64(96, 0x2000);      // p_filesz
    put64(104, 0x2000);     // p_memsz

    std::vector<Codegen::TrampolineSite> trampolines;
    trampolines.push_back({
        32, // Offset (payload byte 0x1000 + 0x20)
        0x10000020ull, // Address (guest vaddr inside the PT_LOAD)
        5, // Length
        {0x0f, 0x38, 0xc8, 0x01, 0x00}, // Original bytes
        {0x48, 0x31, 0xc0, 0xe9, 0x00, 0x00, 0x00, 0x00}, // Body
        3 // Return branch offset
    });

    Elfpatcher::MacOs::MacOsElfPatcher patcher;
    Domain::SysVDynamicSection dyn;
    auto patched = patcher.Patch(dummyElf, {}, dyn, 0, "@executable_path/libs", false, false, trampolines);
    assert(patched.size() > dummyElf.size());
    // Mach-O MH_MAGIC_64 is 0xfeedfacf (0xcf, 0xfa, 0xed, 0xfe in little endian)
    assert(patched[0] == 0xcf && patched[1] == 0xfa && patched[2] == 0xed && patched[3] == 0xfe);

    // Load command count: one LC_SEGMENT_64 per PT_LOAD (1) plus seg_tramp,
    // __LINKEDIT, LC_MAIN, LC_LOAD_DYLINKER, LC_BUILD_VERSION, 5 dylibs, LC_RPATH,
    // LC_UUID, LC_SOURCE_VERSION, LC_SYMTAB, LC_DYSYMTAB, LC_CODE_SIGNATURE.
    std::uint32_t cmdsCount = 0;
    std::memcpy(&cmdsCount, patched.data() + 16, 4);
    assert(cmdsCount == 1 /* PT_LOAD segments */ + 16);

    // The ad-hoc code signature command (LC_CODE_SIGNATURE, 0x1d) must be present
    // and point at a SuperBlob inside the file.
    bool haveCodeSignature = false;
    std::uint32_t signatureOffset = 0, signatureSize = 0;
    for (std::uint32_t offset = 32; offset + 8 <= 32 + 1024 && offset + 8 <= patched.size();) {
        std::uint32_t cmd = 0, cmdSize = 0;
        std::memcpy(&cmd, patched.data() + offset, 4);
        std::memcpy(&cmdSize, patched.data() + offset + 4, 4);
        if (cmdSize < 8) break;
        if (cmd == 0x1d) { // LC_CODE_SIGNATURE
            std::memcpy(&signatureOffset, patched.data() + offset + 8, 4);
            std::memcpy(&signatureSize, patched.data() + offset + 12, 4);
            haveCodeSignature = signatureOffset != 0 && signatureSize >= 20
                && signatureOffset + signatureSize <= patched.size();
            break;
        }
        offset += cmdSize;
    }
    assert(haveCodeSignature);
    // SuperBlob magic (CSMAGIC_EMBEDDED_SIGNATURE, big endian 0xfade0cc0) and the
    // ad-hoc CodeDirectory (CSMAGIC_CODEDIRECTORY, 0xfade0c02) must be there.
    assert(patched[signatureOffset] == 0xfa && patched[signatureOffset + 1] == 0xde
        && patched[signatureOffset + 2] == 0x0c && patched[signatureOffset + 3] == 0xc0);
    const std::uint32_t codeDirectoryOffset = (static_cast<std::uint32_t>(patched[signatureOffset + 16]) << 24)
        | (static_cast<std::uint32_t>(patched[signatureOffset + 17]) << 16)
        | (static_cast<std::uint32_t>(patched[signatureOffset + 18]) << 8)
        | static_cast<std::uint32_t>(patched[signatureOffset + 19]);
    assert(patched[signatureOffset + codeDirectoryOffset] == 0xfa
        && patched[signatureOffset + codeDirectoryOffset + 1] == 0xde
        && patched[signatureOffset + codeDirectoryOffset + 2] == 0x0c
        && patched[signatureOffset + codeDirectoryOffset + 3] == 0x02);
    // CodeDirectory flags must carry CS_ADHOC (0x2) and the SHA-256 hash type (2).
    const std::uint32_t codeDirectoryFlags = (static_cast<std::uint32_t>(patched[signatureOffset + codeDirectoryOffset + 12]) << 24)
        | (static_cast<std::uint32_t>(patched[signatureOffset + codeDirectoryOffset + 13]) << 16)
        | (static_cast<std::uint32_t>(patched[signatureOffset + codeDirectoryOffset + 14]) << 8)
        | static_cast<std::uint32_t>(patched[signatureOffset + codeDirectoryOffset + 15]);
    assert(codeDirectoryFlags == 0x00000002);
    assert(patched[signatureOffset + codeDirectoryOffset + 37] == 2); // CS_HASHTYPE_SHA256

    // The first command must be an executable LC_SEGMENT_64 ("__TEXT")
    // mapped at the guest's own PT_LOAD vaddr (no rebasing).
    std::uint32_t firstCmd = 0;
    std::memcpy(&firstCmd, patched.data() + 32, 4);
    assert(firstCmd == 0x19); // LC_SEGMENT_64
    char segname[16] = {};
    std::memcpy(segname, patched.data() + 40, 16);
    assert(std::strncmp(segname, "__TEXT", 6) == 0);
    std::uint64_t textVmaddr = 0;
    std::memcpy(&textVmaddr, patched.data() + 56, 8);
    assert(textVmaddr == 0x10000000);

    std::cout << "  -> PASSED!\n";
}

void TestMetalBridge() {
    std::cout << "[Test] Running TestMetalBridge...\n";
    if (RytyMetalIsSupported() == 0) {
        std::cout << "  -> SKIPPED (Metal device unavailable)\n";
        return;
    }
    RytyMetalInitializeDevice();
    assert(RytyMetalIsSupported() != 0);
    assert(RytyMetalGetDeviceName() != nullptr);
    RytyMetalSetClearColor(0.2, 0.2, 0.2, 1.0);
    RytyMetalSetDepthStencilEnabled(1);

    void* tex = RytyMetalCreateTexture2D(256, 256, 80);
    assert(tex != nullptr);
    uint32_t dummyPixel = 0xff0000ff;
    RytyMetalUpdateTexture2D(tex, &dummyPixel, 4);
    RytyMetalDestroyTexture2D(tex);

    void* buf = RytyMetalCreateBuffer(&dummyPixel, sizeof(dummyPixel));
    assert(buf != nullptr);
    RytyMetalUpdateBuffer(buf, &dummyPixel, sizeof(dummyPixel), 0);
    RytyMetalDestroyBuffer(buf);

    RytyMetalSetViewport(0, 0, 1920, 1080, 0.0, 1.0);
    RytyMetalSetScissorRect(0, 0, 1920, 1080);
    RytyMetalSetBlendMode(1);
    RytyMetalSetCullMode(1);
    RytyMetalSetTriangleFillMode(0);
    RytyMetalSetFrontFacingWinding(0);
    RytyMetalSetDepthBias(0.0f, 0.0f, 0.0f);
    RytyMetalSetStencilReferenceValue(0);
    RytyMetalSetRenderPipelineState(nullptr);
    RytyMetalSetComputePipelineState(nullptr);
    RytyMetalDispatchThreadgroups(1, 1, 1, 16, 16, 1);
    RytyMetalSetSamplerState(0, nullptr);
    RytyMetalDrawPrimitives(3, 0, 6);
    RytyMetalDrawIndexedPrimitives(3, 6, 0, buf, 0);

    assert(RytyMetalGetMaxThreadsPerThreadgroup() > 0);
    assert(RytyMetalSupportsFeatureSet(0) != 0);
    RytyMetalSetSampleCount(4);
    RytyMetalSetRasterizerDiscardEnabled(0);
    assert(RytyMetalGetDeviceCapabilities() == 0x00010100);

    std::cout << "  -> PASSED!\n";
}

void TestByteIo() {
    std::cout << "[Test] Running TestByteIo...\n";
    std::vector<std::uint8_t> buf;
    Io::ByteWriter writer;
    writer.AppendU32(buf, 0x12345678u);
    writer.AppendU64(buf, 0x8765432112345678ull);

    Io::ByteReader reader;
    assert(reader.ReadU32(buf, 0) == 0x12345678u);
    assert(reader.ReadU64(buf, 4) == 0x8765432112345678ull);
    std::cout << "  -> PASSED!\n";
}

void TestRytyNetBridge() {
    std::cout << "[Test] Running TestRytyNetBridge...\n";
    assert(RytyNetVersion() == 0x00010100u);

    bool isHttps = false;
    std::string host, path;
    std::uint16_t port = 0;
    assert(RytyNet::ParseHttpUrl("http://example.com:8080/api/v1", isHttps, host, port, path) == true);
    assert(isHttps == false);
    assert(host == "example.com");
    assert(port == 8080);
    assert(path == "/api/v1");

    RytyNetSetEnabled(1);
    assert(RytyNetIsEnabled() == 1);

    RytyNetSetEnabled(0);
    assert(RytyNetIsEnabled() == 0);

    RytyNetSetEnabled(-1); // restore default
    std::cout << "  -> PASSED!\n";
}

void TestGuestModulePath() {
    std::cout << "[Test] Running TestGuestModulePath...\n";
    const auto tempDir = std::filesystem::temp_directory_path() / "ryty_guest_path_test";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir / "sce_module");

    std::vector<std::uint8_t> dummyElf(0x1000, 0);
    const std::uint8_t ident[16] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(dummyElf.data(), ident, 16);

    auto put16 = [&](std::size_t off, std::uint16_t v) { std::memcpy(dummyElf.data() + off, &v, 2); };
    auto put32 = [&](std::size_t off, std::uint32_t v) { std::memcpy(dummyElf.data() + off, &v, 4); };
    auto put64 = [&](std::size_t off, std::uint64_t v) { std::memcpy(dummyElf.data() + off, &v, 8); };

    put16(16, 3);          // e_type = ET_DYN
    put16(18, 62);         // e_machine = EM_X86_64
    put32(20, 1);          // e_version
    put64(32, 64);         // e_phoff
    put16(52, 64);         // e_ehsize
    put16(54, 56);         // e_phentsize
    put16(56, 3);          // e_phnum = 3

    // PH 0: PT_LOAD at vaddr 0x1000 (offset 0x100)
    put32(64 + 0*56 + 0, 1);
    put32(64 + 0*56 + 4, 5);
    put64(64 + 0*56 + 8, 0x100);
    put64(64 + 0*56 + 16, 0x1000);
    put64(64 + 0*56 + 24, 0x1000);
    put64(64 + 0*56 + 32, 0x100);
    put64(64 + 0*56 + 40, 0x100);
    put64(64 + 0*56 + 48, 0x100);

    // PH 1: PT_LOAD at vaddr 0x1200 (offset 0x200)
    put32(64 + 1*56 + 0, 1);
    put32(64 + 1*56 + 4, 6);
    put64(64 + 1*56 + 8, 0x200);
    put64(64 + 1*56 + 16, 0x1200);
    put64(64 + 1*56 + 24, 0x1200);
    put64(64 + 1*56 + 32, 0x200);
    put64(64 + 1*56 + 40, 0x200);
    put64(64 + 1*56 + 48, 0x100);

    // PH 2: PT_DYNAMIC at vaddr 0x1200 (offset 0x200)
    put32(64 + 2*56 + 0, 2);
    put32(64 + 2*56 + 4, 6);
    put64(64 + 2*56 + 8, 0x200);
    put64(64 + 2*56 + 16, 0x1200);
    put64(64 + 2*56 + 24, 0x1200);
    put64(64 + 2*56 + 32, 0xa0);
    put64(64 + 2*56 + 40, 0xa0);
    put64(64 + 2*56 + 48, 8);

    // Put a ret instruction (0xc3) at offset 0x100 (vaddr 0x1000)
    dummyElf[0x100] = 0xc3;

    std::size_t dynOff = 0x200;
    auto addTag = [&](std::uint64_t tag, std::uint64_t val) {
        put64(dynOff, tag);
        put64(dynOff + 8, val);
        dynOff += 16;
    };
    addTag(5, 0x1250);  // DT_STRTAB
    addTag(10, 12);     // DT_STRSZ
    addTag(6, 0x1260);  // DT_SYMTAB
    addTag(11, 24);     // DT_SYMENT
    addTag(4, 0x1290);  // DT_HASH
    addTag(0, 0);       // DT_NULL

    // STRTAB at offset 0x250
    std::memcpy(dummyElf.data() + 0x250, "\0myExport\0", 10);

    // SYMTAB at offset 0x260 (Index 0: NULL, Index 1: myExport)
    // Index 1 (offset 0x260 + 24):
    put32(0x260 + 24 + 0, 1);      // st_name = 1
    dummyElf[0x260 + 24 + 4] = 0x12; // st_info = STB_GLOBAL | STT_FUNC
    dummyElf[0x260 + 24 + 5] = 0;    // st_other = 0
    put16(0x260 + 24 + 6, 1);      // st_shndx = 1
    put64(0x260 + 24 + 8, 0x1000); // st_value = 0x1000
    put64(0x260 + 24 + 16, 8);     // st_size = 8

    // DT_HASH at offset 0x290
    put32(0x290, 1); // nbucket = 1
    put32(0x294, 2); // nchain = 2

    const auto inputExe = tempDir / "eboot.elf";
    const auto outputExe = tempDir / "GTSport.exe";
    const auto guestPrx = tempDir / "sce_module" / "libTest.prx";

    {
        std::ofstream f(inputExe, std::ios::binary);
        f.write(reinterpret_cast<const char*>(dummyElf.data()), static_cast<std::streamsize>(dummyElf.size()));
    }
    {
        std::ofstream f(guestPrx, std::ios::binary);
        f.write(reinterpret_cast<const char*>(dummyElf.data()), static_cast<std::streamsize>(dummyElf.size()));
    }

    Domain::SysVDynamicSection dynamic;
    const auto syscallScanner = Relinker::MakeNullSyscallScanner();
    Relinker::GuestModuleBuilder builder;
    const auto artifacts = builder.Build(inputExe, outputExe, dynamic, true, false, *syscallScanner, false, "$ORIGIN/libs", {});

    assert(artifacts.size() == 1);
    std::filesystem::create_directories(artifacts[0].Path.parent_path());
    assert(std::filesystem::exists(artifacts[0].Path.parent_path()));

    std::filesystem::remove_all(tempDir);
    std::cout << "  -> PASSED!\n";
}

int main() {
    std::cout << "Starting Ryty Relinker Unit Tests...\n";
    TestCliArgs();
    TestPs4Detection();
    TestMacOsPatcher();
    TestMetalBridge();
    TestByteIo();
    TestGuestModulePath();
    TestRytyNetBridge();
    std::cout << "All Ryty Relinker Unit Tests Passed Successfully!\n";
    return 0;
}
