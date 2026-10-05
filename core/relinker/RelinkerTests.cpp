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
    std::vector<std::uint8_t> dummyElf(256, 0x90);
    // Minimal ELF header magic
    dummyElf[0] = 0x7f; dummyElf[1] = 'E'; dummyElf[2] = 'L'; dummyElf[3] = 'F';

    std::vector<Codegen::TrampolineSite> trampolines;
    trampolines.push_back({
        32, // Offset
        0x100000020ull, // Address
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

    // Verify Metal bridge initialization functions
    RytyMetalInitializeDevice();
    assert(RytyMetalIsSupported() != 0);
    assert(RytyMetalGetDeviceName() != nullptr);
    RytyMetalSetClearColor(0.2, 0.2, 0.2, 1.0);
    RytyMetalSetDepthStencilEnabled(1);

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
