#include <Cli.hpp>
#include <io/ByteReader.hpp>
#include <io/ByteWriter.hpp>
#include <io/Sha256.hpp>
#include <relinker/parsing/ElfReader.hpp>
#include <relinker/guest/GuestImage.hpp>
#include <relinker/analysis/SyscallScanner.hpp>
#include <elfpatcher/macos/MacOsElfPatcher.hpp>
#include <elfpatcher/apple/MachOOptions.hpp>
#include <elfpatcher/apple/CodeSignature.hpp>
#include <elfpatcher/apple/AppleImages.hpp>
#include <elfpatcher/apple/Entitlements.hpp>
#include <elfpatcher/windows/WindowsElfPatcher.hpp>
#include <elfpatcher/windows/WindowsPeWriter.hpp>
#include <codegen/IInstructionScanner.hpp>
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>

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
void RytyMetalCopyBuffer(void* srcHandle, unsigned long srcOffset, void* dstHandle, unsigned long dstOffset, unsigned long length);
void RytyMetalCopyTexture(void* srcHandle, void* dstHandle, int width, int height);
unsigned long RytyMetalGetDeviceMaxBufferLength(void);
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
#include <algorithm>
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
        const char* argv[] = {"ryty", "--console", "ps5", "--windows", "--windows-console", "in.elf", "out.exe"};
        auto args = Cli::ParseArgs(7, const_cast<char**>(argv));
        assert(args.consoleMode == Cli::ConsoleMode::PS5);
        assert(args.toWindows == true);
        assert(args.windowsConsole == true);
    }
    // 2.0.0 flags
    {
        const char* argv[] = {"ryty", "--version"};
        auto args = Cli::ParseArgs(2, const_cast<char**>(argv));
        assert(args.showVersion == true);
        assert(args.inputPath.empty());
    }
    {
        const char* argv[] = {"ryty", "--verbose", "--verify", "--dry-run", "in.elf", "out.elf"};
        auto args = Cli::ParseArgs(6, const_cast<char**>(argv));
        assert(args.verbose == true);
        assert(args.verifyOutput == true);
        assert(args.dryRun == true);
    }
    {
        const char* argv[] = {"ryty", "--batch", "./games", "--batch-out", "./out", "--windows"};
        auto args = Cli::ParseArgs(6, const_cast<char**>(argv));
        assert(args.batchDir == "./games");
        assert(args.batchOutDir == "./out");
        assert(args.inputPath.empty());
    }
    {
        // --batch with a positional input must be rejected
        const char* argv[] = {"ryty", "--batch", "./games", "in.elf", "out.elf"};
        bool threw = false;
        try { (void)Cli::ParseArgs(5, const_cast<char**>(argv)); } catch (const std::exception&) { threw = true; }
        assert(threw);
    }
    // 2.0.0 Apple flags
    {
        const char* argv[] = {"ryty", "--target", "ios", "--bundle", "--ipa", "in.elf", "out.bin"};
        auto args = Cli::ParseArgs(7, const_cast<char**>(argv));
        assert(args.toIos == true);
        assert(args.toMacOs == false);
        assert(args.writeBundle == true);
        assert(args.writeIpa == true);
        assert(args.TargetName() == "ios");
        assert(args.IsAppleTarget());
    }
    {
        // The iOS Simulator target: --ios-simulator flag and --target ios-sim
        // spelling, device-only flags rejected.
        const char* argv[] = {"ryty", "--target", "ios-sim", "--bundle", "--verify", "in.elf", "out.bin"};
        auto args = Cli::ParseArgs(7, const_cast<char**>(argv));
        assert(args.toIosSim == true);
        assert(args.toIos == false);
        assert(args.TargetName() == "ios-sim");
        assert(args.IsAppleTarget());
        assert(args.IsIosFamily());
        const char* argvFlag[] = {"ryty", "--ios-simulator", "--entitlements-template", "ios-debug", "in.elf", "out.bin"};
        auto argsFlag = Cli::ParseArgs(6, const_cast<char**>(argvFlag));
        assert(argsFlag.toIosSim == true);
        assert(argsFlag.entitlementsTemplate == "ios-debug");
        const char* badIpa[] = {"ryty", "--ios-simulator", "--ipa", "in.elf", "out.bin"};
        bool threw = false;
        try { (void)Cli::ParseArgs(5, const_cast<char**>(badIpa)); } catch (const std::exception&) { threw = true; }
        assert(threw);
        const char* badProvision[] = {"ryty", "--ios-simulator", "--provision", "dev.mobileprovision", "in.elf", "out.bin"};
        threw = false;
        try { (void)Cli::ParseArgs(6, const_cast<char**>(badProvision)); } catch (const std::exception&) { threw = true; }
        assert(threw);
    }
    {
        // Entitlements template conflict with an explicit plist.
        const char* argv[] = {"ryty", "--macos", "--entitlements", "a.plist", "--entitlements-template", "macos-jit", "in.elf", "out.bin"};
        bool threw = false;
        try { (void)Cli::ParseArgs(8, const_cast<char**>(argv)); } catch (const std::exception&) { threw = true; }
        assert(threw);
    }
    {
        // --ipa packages the .app bundle, so it implies --bundle.
        const char* argv[] = {"ryty", "--target", "ios", "--ipa", "in.elf", "out.bin"};
        auto args = Cli::ParseArgs(6, const_cast<char**>(argv));
        assert(args.toIos == true);
        assert(args.writeIpa == true);
        assert(args.writeBundle == true);
    }
    {
        const char* argv[] = {"ryty", "--macos", "--universal", "--hardened-runtime",
                              "--macos-min", "12.3", "--identifier", "com.ryty.test",
                              "--entitlements", "dev.plist", "in.elf", "out.bin"};
        auto args = Cli::ParseArgs(12, const_cast<char**>(argv));
        assert(args.toMacOs == true);
        assert(args.universal == true);
        assert(args.hardenedRuntime == true);
        assert(args.macOsMinVersion == "12.3");
        assert(args.identifier == "com.ryty.test");
        assert(args.entitlementsPath == "dev.plist");
    }
    {
        // Apple option misuse must be rejected.
        const char* argv[] = {"ryty", "--universal", "in.elf", "out.elf"};
        bool threw = false;
        try { (void)Cli::ParseArgs(4, const_cast<char**>(argv)); } catch (const std::exception&) { threw = true; }
        assert(threw);
    }
    {
        const char* argv[] = {"ryty", "--macos", "--ipa", "in.elf", "out.bin"};
        bool threw = false;
        try { (void)Cli::ParseArgs(5, const_cast<char**>(argv)); } catch (const std::exception&) { threw = true; }
        assert(threw);
    }
    {
        const char* argv[] = {"ryty", "--macos", "--macos-min", "not.a.version", "in.elf", "out.bin"};
        bool threw = false;
        try { (void)Cli::ParseArgs(6, const_cast<char**>(argv)); } catch (const std::exception&) { threw = true; }
        assert(threw);
    }
    {
        // --autorun cannot launch Apple targets (they are not host executables).
        const char* argv[] = {"ryty", "--ios", "--autorun", "in.elf", "out.bin"};
        bool threw = false;
        try { (void)Cli::ParseArgs(5, const_cast<char**>(argv)); } catch (const std::exception&) { threw = true; }
        assert(threw);
    }
    std::cout << "  -> PASSED!\n";
}

void TestAppleVersionHelpers() {
    std::cout << "[Test] Running TestAppleVersionHelpers...\n";
    assert(Cli::EncodeAppleVersion("11.0") == 0x000B0000u);
    assert(Cli::EncodeAppleVersion("12.3") == 0x000C0300u);
    assert(Cli::EncodeAppleVersion("14.2.1") == 0x000E0201u);
    assert(Cli::EncodeAppleVersion("18") == 0x00120000u);
    assert(Cli::FormatAppleVersion(0x000E0201u) == "14.2.1");
    bool threw = false;
    try { (void)Cli::EncodeAppleVersion("twelve"); } catch (const std::exception&) { threw = true; }
    assert(threw);
    threw = false;
    try { (void)Cli::EncodeAppleVersion("1.2.3.4"); } catch (const std::exception&) { threw = true; }
    assert(threw);
    // LC_SOURCE_VERSION packing: matches the 2.0.0 writer convention.
    assert(Cli::EncodeSourceVersion("2.0.0") == 0x0002000000000000ULL);
    assert(Cli::EncodeSourceVersion("1.2.3") == 0x0001020300000000ULL);
    assert(Cli::EncodeSourceVersion(Cli::RYTY_VERSION) == 0x0002000000000000ULL);
    std::cout << "  -> PASSED!\n";
}

void TestAppleCodeSignature() {
    std::cout << "[Test] Running TestAppleCodeSignature...\n";
    // The verifier locates LC_CODE_SIGNATURE by walking the load commands, so
    // the synthetic images start with a minimal Mach-O 64 header holding one
    // LC_CODE_SIGNATURE command that points at the blob appended below.
    const auto makeSignedImage = [](std::size_t payloadSize, const std::uint8_t* payloadSeed,
                                    const std::string& identifier,
                                    const std::vector<std::uint8_t>* entitlements,
                                    bool hardenedRuntime) {
        std::vector<std::uint8_t> image(payloadSize, 0);
        for (std::size_t i = 0; i < image.size(); ++i) image[i] = static_cast<std::uint8_t>((i * 31) ^ payloadSeed[0]);
        const std::uint32_t signatureSize = Elfpatcher::Apple::AdHocSignatureSize(image.size(), identifier.size(), entitlements);
#pragma pack(push, 1)
        struct Header {
            std::uint32_t magic, cpuType, cpuSubtype, fileType, cmdsCount, cmdsSize, flags, reserved;
        };
        struct SigCmd {
            std::uint32_t cmd, cmdSize, dataOff, dataSize;
        };
#pragma pack(pop)
        Header header{};
        header.magic = 0xfeedfacf;
        header.cpuType = 0x01000007;
        header.cmdsCount = 1;
        header.cmdsSize = sizeof(SigCmd);
        SigCmd sigCmd{};
        sigCmd.cmd = 0x1d; // LC_CODE_SIGNATURE
        sigCmd.cmdSize = sizeof(SigCmd);
        sigCmd.dataOff = static_cast<std::uint32_t>(payloadSize);
        sigCmd.dataSize = signatureSize;
        std::memcpy(image.data(), &header, sizeof(header));
        std::memcpy(image.data() + sizeof(header), &sigCmd, sizeof(sigCmd));

        Elfpatcher::Apple::AdHocSignatureSpec spec;
        spec.image = image.data();
        spec.codeLimit = image.size();
        spec.identifier = identifier;
        spec.entitlements = entitlements;
        spec.hardenedRuntime = hardenedRuntime;
        const auto blob = Elfpatcher::Apple::BuildAdHocSignature(spec);
        assert(blob.size() == signatureSize);
        image.insert(image.end(), blob.begin(), blob.end());
        return image;
    };

    const std::string identifier = "com.ryty.unit-test";
    // Plain ad-hoc signature: verify OK, tamper detected.
    {
        const std::vector<std::uint8_t> seed{0x01};
        const auto image = makeSignedImage(0x3800, seed.data(), identifier, nullptr, false);
        const auto result = Elfpatcher::Apple::VerifyAdHocSignature(image.data(), image.size());
        assert(result.ok);
        assert(result.identifier == identifier);
        auto tampered = image;
        tampered[1234] ^= 0xFF;
        const auto broken = Elfpatcher::Apple::VerifyAdHocSignature(tampered.data(), tampered.size());
        assert(!broken.ok);
        assert(broken.reason.find("hash mismatch") != std::string::npos);
        auto tamperedSignature = image;
        // Flip the last stored page-hash byte (the final blob byte is CD
        // padding, which nothing signs - exactly like codesign).
        tamperedSignature[tamperedSignature.size() - 2] ^= 0xFF;
        assert(!Elfpatcher::Apple::VerifyAdHocSignature(tamperedSignature.data(), tamperedSignature.size()).ok);
    }
    // Entitlements + hardened runtime: the special slot carries the padded
    // blob's digest.
    {
        const std::string plist = "<plist version=\"1.0\"><dict/></plist>";
        const std::vector<std::uint8_t> entitlements(plist.begin(), plist.end());
        const std::vector<std::uint8_t> seed{0x02};
        const auto image = makeSignedImage(0x2000, seed.data(), identifier, &entitlements, true);
        const auto result = Elfpatcher::Apple::VerifyAdHocSignature(image.data(), image.size());
        assert(result.ok);
        assert(result.hasEntitlements);
        // Flip one entitlements byte: the special-slot hash must break.
        auto tampered = image;
        tampered[tampered.size() - entitlements.size() + 5] ^= 0xFF;
        const auto broken = Elfpatcher::Apple::VerifyAdHocSignature(tampered.data(), tampered.size());
        assert(!broken.ok);
        assert(broken.reason.find("entitlements hash mismatch") != std::string::npos);
    }
    std::cout << "  -> PASSED!\n";
}

void TestAppleImages() {
    std::cout << "[Test] Running TestAppleImages...\n";
    Elfpatcher::Apple::MachOOptions options;
    options.minOs = 0x000C0300u;
    // The arm64 launcher slice is a well-formed MH_MAGIC_64 arm64 executable.
    const auto launcher = Elfpatcher::Apple::MakeArm64LauncherSlice(options, "2.0.0");
    assert(launcher.size() > 0x2000);
    std::uint32_t magic = 0, cpuType = 0;
    std::memcpy(&magic, launcher.data(), 4);
    std::memcpy(&cpuType, launcher.data() + 4, 4);
    assert(magic == 0xfeedfacf);
    assert(cpuType == 0x0100000cu);
    const auto launcherVerify = Elfpatcher::Apple::VerifyAdHocSignature(launcher.data(), launcher.size());
    assert(launcherVerify.ok);
    assert(launcherVerify.identifier == "ryty.launcher");

    // The iOS guest image embeds the payload and self-verifies.
    std::vector<std::uint8_t> payload(0x1234, 0x5A);
    const auto iosImage = Elfpatcher::Apple::MakeIosGuestImage(payload, options, "2.0.0");
    std::memcpy(&cpuType, iosImage.data() + 4, 4);
    assert(cpuType == 0x0100000cu);
    assert(iosImage.size() > payload.size());
    // 16 KB segment alignment on iOS: the signature sits on a 16 KB boundary.
    const auto iosVerify = Elfpatcher::Apple::VerifyAdHocSignature(iosImage.data(), iosImage.size());
    assert(iosVerify.ok);
    // The guest payload must be embedded verbatim inside the container.
    const auto payloadAt = std::search(iosImage.begin(), iosImage.end(), payload.begin(), payload.end());
    assert(payloadAt != iosImage.end());
    assert(static_cast<std::size_t>(payloadAt - iosImage.begin()) % 0x4000 == 0);

    // The iOS Simulator container: same arm64 guest layout, but
    // LC_BUILD_VERSION carries PLATFORM_IOSSIMULATOR (7) and the entitlements
    // preset lands in the CSSLOT_ENTITLEMENTS blob of the SuperBlob.
    {
        Elfpatcher::Apple::MachOOptions simOptions = options;
        simOptions.platform = Elfpatcher::Apple::MachOOptions::Platform::IosSimulator;
        const std::string jitPlist = "<plist version=\"1.0\"><dict/></plist>";
        simOptions.entitlements.assign(jitPlist.begin(), jitPlist.end());
        const auto simImage = Elfpatcher::Apple::MakeIosGuestImage(payload, simOptions, "2.0.0");
        std::memcpy(&cpuType, simImage.data() + 4, 4);
        assert(cpuType == 0x0100000cu);
        // Walk the load commands: exactly one LC_BUILD_VERSION, platform 7.
        std::uint32_t ncmds = 0, sizeofcmds = 0;
        std::memcpy(&ncmds, simImage.data() + 16, 4);
        std::memcpy(&sizeofcmds, simImage.data() + 20, 4);
        int buildVersions = 0;
        std::uint32_t simPlatform = 0;
        std::size_t offset = 32;
        for (std::uint32_t i = 0; i < ncmds && offset + 8 <= 32 + sizeofcmds; ++i) {
            std::uint32_t cmd = 0, cmdSize = 0;
            std::memcpy(&cmd, simImage.data() + offset, 4);
            std::memcpy(&cmdSize, simImage.data() + offset + 4, 4);
            assert(cmdSize >= 8);
            if (cmd == 0x32 && cmdSize >= 24) { // LC_BUILD_VERSION
                ++buildVersions;
                std::memcpy(&simPlatform, simImage.data() + offset + 8, 4);
            }
            offset += cmdSize;
        }
        assert(buildVersions == 1);
        assert(simPlatform == 7);
        const auto simVerify = Elfpatcher::Apple::VerifyAdHocSignature(simImage.data(), simImage.size());
        assert(simVerify.ok);
        assert(simVerify.hasEntitlements);
    }

    // Entitlements templates: valid presets produce plists, unknown names throw.
    {
        const auto jit = Elfpatcher::Apple::MakeEntitlementsTemplate("macos-jit", "com.ryty.x");
        const std::string jitText(jit.begin(), jit.end());
        assert(jitText.find("com.apple.security.cs.allow-jit") != std::string::npos);
        const auto device = Elfpatcher::Apple::MakeEntitlementsTemplate("ios-device", "com.ryty.x");
        const std::string deviceText(device.begin(), device.end());
        assert(deviceText.find("get-task-allow") != std::string::npos);
        assert(deviceText.find("<false/>") != std::string::npos);
        bool threw = false;
        try { (void)Elfpatcher::Apple::MakeEntitlementsTemplate("does-not-exist", ""); } catch (const std::exception&) { threw = true; }
        assert(threw);
        assert(!Elfpatcher::Apple::EntitlementsTemplateNames().empty());
    }

    // Universal container: fat header, arm64 first, both slices signed.
    std::vector<std::uint8_t> x86Slice(0x3000, 0);
    std::memcpy(x86Slice.data(), "\xcf\xfa\xed\xfe", 4);
    std::memcpy(x86Slice.data() + 4, "\x07\x00\x00\x01", 4); // CPU_TYPE_X86_64
    const auto fat = Elfpatcher::Apple::MakeUniversalBinary(launcher, x86Slice);
    std::uint32_t fatMagic = 0, fatCount = 0;
    std::memcpy(&fatMagic, fat.data(), 4);
    fatMagic = __builtin_bswap32(fatMagic);
    std::memcpy(&fatCount, fat.data() + 4, 4);
    fatCount = __builtin_bswap32(fatCount);
    assert(fatMagic == 0xcafebabeu);
    assert(fatCount == 2);
    const auto firstType = __builtin_bswap32(*reinterpret_cast<const std::uint32_t*>(fat.data() + 8));
    assert(firstType == 0x0100000cu); // arm64 first
    const auto firstOffset = __builtin_bswap32(*reinterpret_cast<const std::uint32_t*>(fat.data() + 16));
    assert(firstOffset % 4096 == 0);
    std::cout << "  -> PASSED!\n";
}

void TestConfigFile() {
    std::cout << "[Test] Running TestConfigFile...\n";
    const auto tempDir = std::filesystem::temp_directory_path() / "ryty-config-test";
    std::filesystem::create_directories(tempDir);
    const auto configPath = tempDir / "port.ini";
    {
        std::ofstream file(configPath, std::ios::binary);
        file << "# ryty options\n"
             << "to-intel = true\n"
             << "unused-filter = 2\n"
             << "rpath = $ORIGIN/mylibs\n"
             << "target = windows\n"
             << "windows-console = on\n"
             << "verbose\n"
             << "exclude-sce-module = libA.prx\n";
    }

    Cli::Args args;
    const auto applied = Cli::ApplyConfigFile(configPath.string(), args);
    assert(applied.size() == 7);
    assert(args.toIntel == true);
    assert(args.unusedFilterLevel == 2);
    assert(args.runPath == "$ORIGIN/mylibs");
    assert(args.toWindows == true);
    assert(args.toMacOs == false);
    assert(args.windowsConsole == true);
    assert(args.verbose == true);
    assert(args.excludedSceModules.count("libA.prx") == 1);

    // Unknown keys are rejected, never silently ignored.
    {
        const auto badPath = tempDir / "bad.ini";
        {
            std::ofstream file(badPath, std::ios::binary);
            file << "not-an-option = 1\n";
        }
        Cli::Args fresh;
        bool threw = false;
        try { (void)Cli::ApplyConfigFile(badPath.string(), fresh); } catch (const std::exception&) { threw = true; }
        assert(threw);
    }

    std::filesystem::remove_all(tempDir);
    std::cout << "  -> PASSED!\n";
}

void TestSha256() {
    std::cout << "[Test] Running TestSha256...\n";
    // FIPS 180-4 vectors
    {
        const std::vector<std::uint8_t> empty;
        assert(Io::Sha256Hex(empty) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    }
    {
        const std::string input = "abc";
        const std::vector<std::uint8_t> bytes(input.begin(), input.end());
        assert(Io::Sha256Hex(bytes) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }
    {
        // Two-block message ('abcdbcde' + 'cdefdefgdefgdefgdefgdefgdefgdefg')
        const std::string input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        const std::vector<std::uint8_t> bytes(input.begin(), input.end());
        assert(Io::Sha256Hex(bytes) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    }
    {
        // Incremental update across block boundaries must match the one-shot
        std::vector<std::uint8_t> bytes(1000);
        for (std::size_t index = 0; index < bytes.size(); ++index) bytes[index] = static_cast<std::uint8_t>(index * 7 + 1);
        Io::Sha256 incremental;
        for (std::size_t index = 0; index < bytes.size(); index += 37) {
            const auto chunk = std::min<std::size_t>(37, bytes.size() - index);
            incremental.Update(bytes.data() + index, chunk);
        }
        assert(incremental.FinalHex() == Io::Sha256Hex(bytes));
    }
    std::cout << "  -> PASSED!\n";
}

void TestWindowsPeSubsystem() {
    std::cout << "[Test] Running TestWindowsPeSubsystem...\n";
    std::vector<Elfpatcher::Windows::PeSection> sections;
    sections.push_back({".text", Elfpatcher::Windows::LoadRva, 0x20000020u, {0xc3, 0x90, 0x90, 0x90}});
    std::array<Elfpatcher::Windows::PeDirectory, 16> directories{};

    // Default: GUI subsystem (2)
    {
        Elfpatcher::Windows::WindowsPeWriter writer;
        auto pe = writer.Write(sections, Elfpatcher::Windows::LoadRva, directories, false);
        std::uint16_t subsystem = 0;
        std::memcpy(&subsystem, pe.data() + 0x80 + 24 + 68, 2);
        assert(subsystem == 2); // IMAGE_SUBSYSTEM_WINDOWS_GUI
    }

    // Windows Console requested: Console subsystem (3)
    {
        Elfpatcher::Windows::WindowsPeWriter writer;
        auto pe = writer.Write(sections, Elfpatcher::Windows::LoadRva, directories, true);
        std::uint16_t subsystem = 0;
        std::memcpy(&subsystem, pe.data() + 0x80 + 24 + 68, 2);
        assert(subsystem == 3); // IMAGE_SUBSYSTEM_WINDOWS_CUI
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
    void* dstTex = RytyMetalCreateTexture2D(256, 256, 80);
    assert(dstTex != nullptr);
    RytyMetalCopyTexture(tex, dstTex, 256, 256);
    RytyMetalDestroyTexture2D(dstTex);
    RytyMetalDestroyTexture2D(tex);

    void* buf = RytyMetalCreateBuffer(&dummyPixel, sizeof(dummyPixel));
    assert(buf != nullptr);
    RytyMetalUpdateBuffer(buf, &dummyPixel, sizeof(dummyPixel), 0);
    void* dstBuf = RytyMetalCreateBuffer(nullptr, sizeof(dummyPixel));
    assert(dstBuf != nullptr);
    RytyMetalCopyBuffer(buf, 0, dstBuf, 0, sizeof(dummyPixel));
    RytyMetalDestroyBuffer(dstBuf);
    RytyMetalDestroyBuffer(buf);

    assert(RytyMetalGetDeviceMaxBufferLength() > 0);

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
    assert(RytyMetalGetDeviceCapabilities() == 0x00020000);

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
    assert(RytyNetVersion() == 0x00020000u);

    bool isHttps = false;
    std::string host, path;
    std::uint16_t port = 0;
    assert(RytyNet::ParseHttpUrl("http://example.com:8080/api/v1", isHttps, host, port, path) == true);
    assert(isHttps == false);
    assert(host == "example.com");
    assert(port == 8080);
    assert(path == "/api/v1");

    // Test GetHeader helper
    RytyNet::HttpResponse resp;
    resp.headers = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nLocation: http://example.com/login\r\nServer: RytyServer/1.0\r\n\r\n";
    assert(resp.GetHeader("content-type") == "text/html; charset=utf-8");
    assert(resp.GetHeader("LOCATION") == "http://example.com/login");
    assert(resp.GetHeader("Server") == "RytyServer/1.0");
    assert(resp.GetHeader("NonExistent") == "");

    RytyNetSetEnabled(1);
    assert(RytyNetIsEnabled() == 1);

    // Test HTTPS request handling in bridge
    RytyNet::HttpRequest httpsReq;
    httpsReq.isHttps = true;
    httpsReq.host = "example.com";
    httpsReq.port = 443;
    httpsReq.path = "/";
    auto httpsResp = RytyNet::PerformHttpRequest(httpsReq, 1000);
    assert(!httpsResp.ok);
    assert(httpsResp.error == "HTTPS not supported");

    RytyNetSetEnabled(0);
    assert(RytyNetIsEnabled() == 0);

    RytyNetSetEnabled(-1); // restore default
    std::cout << "  -> PASSED!\n";
}

void TestIntelCpuLowering() {
    std::cout << "[Test] Running TestIntelCpuLowering...\n";
    auto matcher = Codegen::MakeAmd64OnlyInstructionMatcher();

    // 1. AMD SVM instructions (3 bytes each)
    {
        const std::uint8_t skinit[] = {0x0F, 0x01, 0xDE};
        auto match = matcher->Match(skinit, sizeof(skinit));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "SKINIT");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
        assert(match->ReplacementBytes.size() == 3);
        assert(match->ReplacementBytes[0] == 0x0F && match->ReplacementBytes[1] == 0x1F && match->ReplacementBytes[2] == 0x00);
    }
    {
        const std::uint8_t clgi[] = {0x0F, 0x01, 0xDD};
        auto match = matcher->Match(clgi, sizeof(clgi));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "CLGI");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
    }
    {
        const std::uint8_t stgi[] = {0x0F, 0x01, 0xDC};
        auto match = matcher->Match(stgi, sizeof(stgi));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "STGI");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
    }
    {
        const std::uint8_t invlpga[] = {0x0F, 0x01, 0xDF};
        auto match = matcher->Match(invlpga, sizeof(invlpga));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "INVLPGA");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
    }
    {
        const std::uint8_t vmrun[] = {0x0F, 0x01, 0xD8};
        auto match = matcher->Match(vmrun, sizeof(vmrun));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "VMRUN");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
    }
    {
        const std::uint8_t vmload[] = {0x0F, 0x01, 0xDA};
        auto match = matcher->Match(vmload, sizeof(vmload));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "VMLOAD");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
    }
    {
        const std::uint8_t vmsave[] = {0x0F, 0x01, 0xDB};
        auto match = matcher->Match(vmsave, sizeof(vmsave));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "VMSAVE");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
    }

    // 2. 3DNow! PREFETCH -> PREFETCHT0
    {
        const std::uint8_t prefetch3d[] = {0x0F, 0x0D, 0x00}; // prefetch [rax]
        auto match = matcher->Match(prefetch3d, sizeof(prefetch3d));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "PREFETCH");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
        assert(match->ReplacementBytes.size() == 3);
        assert(match->ReplacementBytes[1] == 0x18); // PREFETCHT0 opcode
        assert((match->ReplacementBytes[2] & 0x38) == 0x08); // ModRM reg = 1
    }

    // 3. CLFLUSHOPT -> NOP + CLFLUSH
    {
        const std::uint8_t clflushopt[] = {0x66, 0x0F, 0xAE, 0x38}; // clflushopt [rax]
        auto match = matcher->Match(clflushopt, sizeof(clflushopt));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "CLFLUSHOPT");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
        assert(match->ReplacementBytes[0] == 0x90); // 66 prefix replaced by NOP
        assert(match->ReplacementBytes[1] == 0x0F && match->ReplacementBytes[2] == 0xAE);
    }

    // 4. CLWB -> NOP + CLFLUSH
    {
        const std::uint8_t clwb[] = {0x66, 0x0F, 0xAE, 0x30}; // clwb [rax] (modrm reg = 6)
        auto match = matcher->Match(clwb, sizeof(clwb));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "CLWB");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
        assert(match->ReplacementBytes[0] == 0x90);
        assert((match->ReplacementBytes[3] & 0x38) == 0x38); // ModRM reg converted to 7 (CLFLUSH)
    }

    // 4b. CLDEMOTE -> NOP
    {
        const std::uint8_t cldemote[] = {0x0F, 0x1C, 0x00}; // cldemote [rax]
        auto match = matcher->Match(cldemote, sizeof(cldemote));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "CLDEMOTE");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
    }

    // 5. User-level wait instructions
    {
        const std::uint8_t umonitor[] = {0xF3, 0x0F, 0xAE, 0xF0}; // umonitor eax
        auto match = matcher->Match(umonitor, sizeof(umonitor));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "UMONITOR");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
    }
    {
        const std::uint8_t umwait[] = {0xF2, 0x0F, 0xAE, 0xF0}; // umwait eax
        auto match = matcher->Match(umwait, sizeof(umwait));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "UMWAIT");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
        assert(match->ReplacementBytes[0] == 0xF3 && match->ReplacementBytes[1] == 0x90); // PAUSE
    }
    {
        const std::uint8_t tpause[] = {0x66, 0x0F, 0xAE, 0xF0}; // tpause eax
        auto match = matcher->Match(tpause, sizeof(tpause));
        assert(match.has_value());
        assert(std::string(match->InstructionName) == "TPAUSE");
        assert(match->Lowering == Codegen::Amd64OnlyLowering::InPlace);
        assert(match->ReplacementBytes[0] == 0xF3 && match->ReplacementBytes[1] == 0x90); // PAUSE
    }

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
    TestAppleVersionHelpers();
    TestAppleCodeSignature();
    TestAppleImages();
    TestConfigFile();
    TestSha256();
    TestWindowsPeSubsystem();
    TestPs4Detection();
    TestMacOsPatcher();
    TestMetalBridge();
    TestByteIo();
    TestGuestModulePath();
    TestRytyNetBridge();
    TestIntelCpuLowering();
    std::cout << "All Ryty Relinker Unit Tests Passed Successfully!\n";
    return 0;
}
