#include <Cli.hpp>
#include <io/ByteReader.hpp>
#include <io/ByteWriter.hpp>
#include <relinker/parsing/ElfReader.hpp>
#include <elfpatcher/macos/MacOsElfPatcher.hpp>
#include <codegen/IInstructionScanner.hpp>

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
void RytyMetalDrawPrimitives(int primitiveType, unsigned int start, unsigned int count);
void RytyMetalDrawIndexedPrimitives(int primitiveType, unsigned int indexCount, int indexType, void* indexBuffer, unsigned int indexBufferOffset);
unsigned long RytyMetalGetMaxThreadsPerThreadgroup(void);
int RytyMetalSupportsFeatureSet(int featureSet);
}
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

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
    put16(54, 64);          // e_ehsize
    put16(56, 56);          // e_phentsize
    put16(58, 1);           // e_phnum
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
    // __LINKEDIT, LC_MAIN, LC_LOAD_DYLINKER, LC_BUILD_VERSION, 5 dylibs and LC_RPATH.
    std::uint32_t cmdsCount = 0;
    std::memcpy(&cmdsCount, patched.data() + 16, 4);
    assert(cmdsCount == 1 /* PT_LOAD segments */ + 11);

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

    // Verify Metal bridge initialization and expanded C API functions
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
    RytyMetalDrawPrimitives(3, 0, 6);
    RytyMetalDrawIndexedPrimitives(3, 6, 0, buf, 0);

    assert(RytyMetalGetMaxThreadsPerThreadgroup() > 0);
    assert(RytyMetalSupportsFeatureSet(0) != 0);

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

int main() {
    std::cout << "Starting Ryty Relinker Unit Tests...\n";
    TestCliArgs();
    TestPs4Detection();
    TestMacOsPatcher();
    TestByteIo();
    std::cout << "All Ryty Relinker Unit Tests Passed Successfully!\n";
    return 0;
}
