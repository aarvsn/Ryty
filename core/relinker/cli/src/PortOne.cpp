#include <Cli.hpp>
#include <PortScheme.hpp>
#include <domain/Types.hpp>
#include <io/FileReader.hpp>
#include <io/FileWriter.hpp>
#include <io/Json.hpp>
#include <io/Sha256.hpp>
#include <elfpatcher/linux/LinuxElfPatcher.hpp>
#include <elfpatcher/macos/MacOsElfPatcher.hpp>
#include <elfpatcher/apple/CodeSignature.hpp>
#include <elfpatcher/apple/AppleBundle.hpp>
#include <elfpatcher/apple/Entitlements.hpp>
#include <elfpatcher/general/SegmentFilter.hpp>
#include <elfpatcher/general/EntryStubBuilder.hpp>
#include <elfpatcher/general/ProgramHeaderLayoutBuilder.hpp>
#include <elfpatcher/general/SectionHeaderTableBuilder.hpp>
#include <elfpatcher/windows/WindowsElfPatcher.hpp>
#include <io/ByteWriter.hpp>
#include <relinker/parsing/ElfReader.hpp>
#include <relinker/analysis/ValidationPolicy.hpp>
#include <relinker/analysis/SyscallScanner.hpp>
#include <relinker/analysis/CallSiteResolver.hpp>
#include <relinker/analysis/UnusedNidFilter.hpp>
#include <relinker/output/SysVDynamicSectionBuilder.hpp>
#include <relinker/output/CallRegistryWriter.hpp>
#include <relinker/pipeline/RelinkerPipeline.hpp>
#include <relinker/guest/GuestImage.hpp>
#include <codegen/IAmd64OnlyConverter.hpp>
#include <codegen/CodegenException.hpp>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string escapeJson(const std::string& value) {
    return Io::EscapeJsonString(value);
}

// 2.0.0 --verify: structural validation of the finished image. Reads the file
// back from disk so what is verified is exactly what landed on storage.
// Apple outputs additionally get their embedded ad-hoc code signature
// re-verified (every page hash recomputed), plus a check of the LC_BUILD_VERSION
// platform/deployment target, the MH_PIE flag on arm64 slices and the
// CodeDirectory identifier; universal (fat) binaries verify each of their slices.

// Everything --verify knows about the Apple image it is about to inspect.
struct AppleVerifyExpectations {
    std::uint32_t platform = 1;       // LC_BUILD_VERSION platform (1/2/7)
    std::uint32_t minOs = 0x000B0000; // encoded deployment target
    std::string identifier;           // expected CodeDirectory identifier ("" = skip)
    bool requirePie = false;          // arm64 slices must be position independent
};

// 2.0.0 porting scheme: --verify inspects the resolved scheme on every target.
struct LinuxVerifyExpectations {
    std::string interpreter;     // expected PT_INTERP content (--interp)
    bool requireRunPath = true;  // DT_RUNPATH must exist when a run path was set
    bool requireNxStack = true;  // PT_GNU_STACK must be present and non-executable
};

struct WindowsVerifyExpectations {
    bool console = false;        // expected PE subsystem: 3 console / 2 GUI
};

bool VerifyAppleMachO(const std::uint8_t* image, std::size_t size,
                      const AppleVerifyExpectations& expectations, std::string& reason) {
    if (size < 32) { reason = "image too small for a Mach-O header"; return false; }
    std::uint32_t magic = 0;
    std::memcpy(&magic, image, 4);
    if (magic != 0xfeedfacf) { reason = "missing MH_MAGIC_64"; return false; }
    std::uint32_t cpuType = 0;
    std::memcpy(&cpuType, image + 4, sizeof(cpuType));
    const bool arm64 = cpuType == 0x0100000cu /* arm64 */;
    if (!arm64 && cpuType != 0x01000007u /* x86_64 */) { reason = "unsupported Mach-O CPU type"; return false; }
    if (expectations.requirePie && !arm64) { reason = "expected an arm64 slice"; return false; }
    std::uint32_t flags = 0;
    std::memcpy(&flags, image + 24, sizeof(flags));
    if (arm64 && (flags & 0x00200000u) == 0) {
        reason = "arm64 slice must be position independent (MH_PIE)";
        return false;
    }
    std::uint32_t ncmds = 0, sizeofcmds = 0;
    std::memcpy(&ncmds, image + 16, sizeof(ncmds));
    std::memcpy(&sizeofcmds, image + 20, sizeof(sizeofcmds));
    if (32 + static_cast<std::uint64_t>(sizeofcmds) > size) { reason = "load commands out of range"; return false; }
    int buildVersions = 0;
    std::uint32_t platform = 0, minOs = 0, sdk = 0;
    std::size_t offset = 32;
    for (std::uint32_t i = 0; i < ncmds && offset + 8 <= 32 + sizeofcmds; ++i) {
        std::uint32_t cmd = 0, cmdSize = 0;
        std::memcpy(&cmd, image + offset, sizeof(cmd));
        std::memcpy(&cmdSize, image + offset + 4, sizeof(cmdSize));
        if (cmdSize < 8 || offset + cmdSize > 32 + sizeofcmds) { reason = "malformed load command"; return false; }
        if (cmd == 0x32 /* LC_BUILD_VERSION */ && cmdSize >= 24) {
            ++buildVersions;
            std::memcpy(&platform, image + offset + 8, 4);
            std::memcpy(&minOs, image + offset + 12, 4);
            std::memcpy(&sdk, image + offset + 16, 4);
        }
        offset += cmdSize;
    }
    if (buildVersions != 1) { reason = "expected exactly one LC_BUILD_VERSION"; return false; }
    if (platform != expectations.platform) {
        reason = "LC_BUILD_VERSION platform " + std::to_string(platform) + " != expected "
               + std::to_string(expectations.platform);
        return false;
    }
    if (minOs != expectations.minOs) {
        reason = "LC_BUILD_VERSION minOS " + Cli::FormatAppleVersion(minOs) + " != requested "
               + Cli::FormatAppleVersion(expectations.minOs);
        return false;
    }
    if (sdk < minOs) { reason = "LC_BUILD_VERSION SDK is older than minOS"; return false; }
    const auto signature = Elfpatcher::Apple::VerifyAdHocSignature(image, size);
    if (!signature.ok) {
        reason = signature.reason;
        return false;
    }
    if (!expectations.identifier.empty() && signature.identifier != expectations.identifier) {
        reason = "code signature identifier mismatch: " + signature.identifier;
        return false;
    }
    return true;
}

bool VerifyOutputBytes(const std::vector<std::uint8_t>& image, const std::string& system,
                       const AppleVerifyExpectations& appleExpectations,
                       const LinuxVerifyExpectations& linuxExpectations,
                       const WindowsVerifyExpectations& windowsExpectations, std::string& reason) {
    if (system == "windows") {
        if (image.size() < 0x40 || image[0] != 'M' || image[1] != 'Z') { reason = "missing MZ signature"; return false; }
        std::uint32_t peOffset = 0;
        std::memcpy(&peOffset, image.data() + 0x3c, sizeof(peOffset));
        if (peOffset == 0 || peOffset + 24 > image.size()) { reason = "PE header offset out of range"; return false; }
        if (std::memcmp(image.data() + peOffset, "PE\0\0", 4) != 0) { reason = "PE signature missing"; return false; }
        // 2.0.0 porting scheme: full COFF/optional-header validation.
        std::uint16_t machine = 0, sectionCount = 0, optionalSize = 0, magic = 0, subsystem = 0;
        std::uint32_t entryRva = 0;
        std::memcpy(&machine, image.data() + peOffset + 4, sizeof(machine));
        std::memcpy(&sectionCount, image.data() + peOffset + 6, sizeof(sectionCount));
        std::memcpy(&optionalSize, image.data() + peOffset + 20, sizeof(optionalSize));
        std::memcpy(&magic, image.data() + peOffset + 24, sizeof(magic));
        if (machine != 0x8664) {
            std::ostringstream out;
            out << "not an x86-64 image (machine 0x" << std::hex << machine << ")";
            reason = out.str();
            return false;
        }
        if (sectionCount == 0) { reason = "PE image has no sections"; return false; }
        if (optionalSize < 112 || magic != 0x20b) { reason = "not a PE32+ (x86-64) optional header"; return false; }
        if (peOffset + 24 + 112 > image.size()) { reason = "PE optional header out of range"; return false; }
        std::memcpy(&entryRva, image.data() + peOffset + 24 + 16, sizeof(entryRva));
        std::memcpy(&subsystem, image.data() + peOffset + 24 + 68, sizeof(subsystem));
        if (entryRva == 0) { reason = "PE image has no entry point"; return false; }
        const std::uint16_t expectedSubsystem = windowsExpectations.console ? 3 : 2;
        if (subsystem != expectedSubsystem) {
            reason = std::string("PE subsystem ") + (subsystem == 3 ? "console" : subsystem == 2 ? "GUI" : "other")
                   + " != requested " + (windowsExpectations.console ? "console" : "GUI");
            return false;
        }
        reason = "PE32+ x86-64 header OK, " + std::string(windowsExpectations.console ? "console" : "GUI")
               + " subsystem, " + std::to_string(sectionCount) + " sections";
        return true;
    }
    if (system == "macos" || system == "ios" || system == "ios-sim") {
        if (image.size() < 32) { reason = "image too small for a Mach-O header"; return false; }
        std::uint32_t magic = 0;
        std::memcpy(&magic, image.data(), sizeof(magic));
        if (magic == 0xcafebabe || magic == 0xbebafeca) {
            // Universal (fat) binary. The fat magic is stored big-endian, so a
            // little-endian host reads 0xBEBAFECA; the opposite read order
            // means the file is byte-swapped.
            if (magic != 0xbebafeca) { reason = "unsupported byte-swapped fat magic"; return false; }
            std::uint32_t fatCount = 0;
            std::memcpy(&fatCount, image.data() + 4, 4);
            fatCount = __builtin_bswap32(fatCount);
            if (fatCount == 0 || 8 + fatCount * 20 > image.size()) { reason = "fat header out of range"; return false; }
            for (std::uint32_t slice = 0; slice < fatCount; ++slice) {
                std::uint32_t fields[5];
                std::memcpy(fields, image.data() + 8 + slice * 20, 20);
                for (auto& field : fields) field = __builtin_bswap32(field);
                const std::uint32_t sliceOffset = fields[2];
                const std::uint32_t sliceSize = fields[3];
                const std::uint32_t sliceCpu = fields[0];
                if (static_cast<std::uint64_t>(sliceOffset) + sliceSize > image.size()) {
                    reason = "fat slice out of range";
                    return false;
                }
                AppleVerifyExpectations sliceExpectations = appleExpectations;
                // The arm64 launcher slice signs itself as "ryty.launcher";
                // only the guest slice carries the requested identifier.
                if (sliceCpu == 0x0100000cu) {
                    sliceExpectations.requirePie = true;
                    sliceExpectations.identifier.clear();
                }
                const auto sliceResult = VerifyAppleMachO(image.data() + sliceOffset, sliceSize, sliceExpectations, reason);
                if (!sliceResult) {
                    reason = "universal slice " + std::to_string(slice) + ": " + reason;
                    return false;
                }
            }
            reason = "universal Mach-O OK, " + std::to_string(fatCount) + " slices verified";
            return true;
        }
        if (!VerifyAppleMachO(image.data(), image.size(), appleExpectations, reason)) return false;
        reason = "Mach-O 64 + ad-hoc signature OK";
        return true;
    }
    // linux (SysV ELF64)
    if (image.size() < 64) { reason = "image too small for an ELF header"; return false; }
    if (image[0] != 0x7f || image[1] != 'E' || image[2] != 'L' || image[3] != 'F') { reason = "missing ELF magic"; return false; }
    if (image[4] != 2) { reason = "not a 64-bit ELF"; return false; }
    if (image[5] != 1) { reason = "not little-endian"; return false; }
    std::uint16_t machine = 0, phentSize = 0, phnum = 0;
    std::memcpy(&machine, image.data() + 18, sizeof(machine));
    std::memcpy(&phentSize, image.data() + 54, sizeof(phentSize));
    std::memcpy(&phnum, image.data() + 56, sizeof(phnum));
    if (machine != 0x3e) {
        std::ostringstream out;
        out << "not an x86-64 image (machine 0x" << std::hex << machine << ")";
        reason = out.str();
        return false;
    }
    if (phnum == 0) { reason = "no program headers"; return false; }
    if (phentSize != 56) { reason = "unexpected program header entry size"; return false; }
    std::uint64_t phOff = 0;
    std::memcpy(&phOff, image.data() + 32, sizeof(phOff));
    if (phOff == 0 || phOff + static_cast<std::uint64_t>(phnum) * 56 > image.size()) {
        reason = "program header table out of range";
        return false;
    }
    // 2.0.0 porting scheme: the Linux verify pass walks the program header
    // table and validates the pieces the porting scheme promises: the loader
    // path (--interp), the dynamic section with its run path, and the
    // non-executable stack declaration.
    bool sawInterp = false, sawDynamic = false, sawStack = false, stackExecutable = true;
    std::uint64_t dynamicOffset = 0, dynamicSize = 0;
    std::string interpreter;
    for (std::uint16_t index = 0; index < phnum; ++index) {
        const std::size_t entry = static_cast<std::size_t>(phOff) + index * 56;
        std::uint32_t type = 0, flags = 0;
        std::uint64_t offset = 0, fileSize = 0;
        std::memcpy(&type, image.data() + entry, sizeof(type));
        std::memcpy(&flags, image.data() + entry + 4, sizeof(flags));
        std::memcpy(&offset, image.data() + entry + 8, sizeof(offset));
        std::memcpy(&fileSize, image.data() + entry + 32, sizeof(fileSize));
        if (type == 3 /* PT_INTERP */) {
            sawInterp = true;
            if (offset == 0 || fileSize == 0 || offset + fileSize > image.size()) {
                reason = "PT_INTERP segment out of range";
                return false;
            }
            interpreter.assign(reinterpret_cast<const char*>(image.data() + offset), fileSize);
            while (!interpreter.empty() && interpreter.back() == '\0') interpreter.pop_back();
        } else if (type == 2 /* PT_DYNAMIC */) {
            sawDynamic = true;
            dynamicOffset = offset;
            dynamicSize = fileSize;
        } else if (type == 0x6474e551 /* PT_GNU_STACK */) {
            sawStack = true;
            stackExecutable = (flags & 0x1) != 0;
        }
    }
    if (!sawInterp) { reason = "missing PT_INTERP"; return false; }
    if (interpreter != linuxExpectations.interpreter) {
        reason = "PT_INTERP \"" + interpreter + "\" != requested \"" + linuxExpectations.interpreter + "\"";
        return false;
    }
    if (!sawDynamic) { reason = "missing PT_DYNAMIC"; return false; }
    if (linuxExpectations.requireNxStack && (!sawStack || stackExecutable)) {
        reason = sawStack ? "stack is marked executable (PT_GNU_STACK flags)" : "missing PT_GNU_STACK";
        return false;
    }
    bool sawRunPath = false;
    if (dynamicOffset != 0 && dynamicSize >= 16 && dynamicOffset + dynamicSize <= image.size()) {
        for (std::uint64_t cursor = 0; cursor + 16 <= dynamicSize; cursor += 16) {
            std::int64_t tag = 0;
            std::uint64_t value = 0;
            std::memcpy(&tag, image.data() + dynamicOffset + cursor, sizeof(tag));
            std::memcpy(&value, image.data() + dynamicOffset + cursor + 8, sizeof(value));
            if (tag == 0 /* DT_NULL */) break;
            if (tag == 29 /* DT_RUNPATH */ && value != 0) sawRunPath = true;
        }
    }
    if (linuxExpectations.requireRunPath && !sawRunPath) {
        reason = "missing DT_RUNPATH (a run path was requested)";
        return false;
    }
    reason = "ELF64 x86-64 header OK, " + std::to_string(phnum) + " program headers, PT_INTERP + PT_DYNAMIC verified"
           + (linuxExpectations.requireRunPath ? ", DT_RUNPATH present" : "")
           + (linuxExpectations.requireNxStack ? ", PT_GNU_STACK NX" : "");
    return true;
}

void PrintSiteReports(const std::vector<Codegen::Amd64OnlySubstitutionReport>& reports, const std::vector<Codegen::TrampolineSite>& trampolines, bool verbose) {
    if (!verbose) return;
    for (const auto& report : reports) {
        std::cout << "Intel substitution: " << report.InstructionName << " at 0x" << std::hex << report.Offset << std::dec
                  << " (" << report.OriginalLength << " bytes) -> "
                  << (report.Lowering == Codegen::Amd64OnlyLowering::InPlace ? "in place " : "stub ")
                  << report.ReplacementLength << " bytes\n";
    }
    std::cout << "Intel conversion: " << reports.size() << " sites lowered, " << trampolines.size() << " stubs\n";
}

}

namespace Cli {

PortOutcome PortOneFile(const Args& args, const std::string& inputPath, const std::string& outputPath, const bool quiet) {
    PortOutcome outcome;
    outcome.outputPath = std::filesystem::absolute(outputPath).string();
    const auto started = std::chrono::steady_clock::now();
    // --verbose re-enables chatter even when a batch run asks for quiet output.
    const bool talk = !quiet || args.verbose;
    // 2.0.0 porting scheme: resolve the per-target scheme once so the port,
    // its log output and its report all describe the same plan.
    const PortScheme scheme = MakePortScheme(args);

    try {
        std::vector<Codegen::TrampolineSite> trampolines;
        std::vector<Codegen::Amd64OnlySubstitutionReport> substitutionReports;
        std::vector<std::string> warnings;
        auto extension = std::filesystem::path(outputPath).extension().string();
        for (auto& character : extension) if (character >= 'A' && character <= 'Z') character = static_cast<char>(character + ('a' - 'A'));
        if (!args.toWindows && extension == ".exe") {
            if (talk) std::cerr << "WARNING: Output filename ends with .exe, but --windows was not specified. The output will be a Linux ELF executable.\n";
            warnings.push_back("Output filename ends with .exe but --windows was not specified");
        }
        Io::FileReader fileReader;
        Io::FileWriter fileWriter;

        auto sourceBytes = fileReader.Read(inputPath);
        outcome.inputBytes = sourceBytes.size();
        // Digest of the untouched input image, before any relinker patch or
        // instruction lowering mutates the buffer.
        const std::string inputSha256 = Io::Sha256Hex(sourceBytes);
        const std::string absPath = std::filesystem::absolute(outputPath).string();

        // 2.0.0 Apple bundle layout: with --bundle the executable lands inside
        // <stem>.app (Contents/MacOS on macOS, bundle root on iOS) so the guest
        // modules follow it into the bundle (rpath @executable_path/libs).
        const bool bundle = args.writeBundle && args.IsAppleTarget();
        std::string exePath = absPath;
        std::string appPath;
        std::string appExecutableName;
        std::string effectiveIdentifier = args.identifier;
        const std::string minOsVersion = args.IsIosFamily() ? args.iosMinVersion : args.macOsMinVersion;
        if (bundle) {
            const std::filesystem::path outPath(absPath);
            appExecutableName = outPath.stem().string();
            const std::filesystem::path appDir = outPath.parent_path() / (appExecutableName + ".app");
            appPath = appDir.string();
            // The automatic identifier is sanitized so ports of images with
            // awkward file names never produce an invalid bundle identifier.
            if (effectiveIdentifier.empty()) effectiveIdentifier = MakeDefaultAppleIdentifier(appExecutableName);
            exePath = args.IsIosFamily() ? (appDir / appExecutableName).string()
                                         : (appDir / "Contents" / "MacOS" / appExecutableName).string();
        } else if (args.IsAppleTarget() && effectiveIdentifier.empty()) {
            effectiveIdentifier = "ryty.bundle";
        }
        outcome.outputPath = exePath;
        if (bundle && !IsValidAppleIdentifier(effectiveIdentifier))
            throw Domain::RelinkerException("invalid bundle identifier: \"" + effectiveIdentifier
                                           + "\" (use letters, digits, dots and hyphens, e.g. com.example.game)");
        // Reject an unusable provisioning profile before any output byte is
        // written; coverage warnings are reported but do not abort the port.
        if (!args.provisionPath.empty() && args.toIos) {
            const auto provision = Elfpatcher::Apple::ValidateProvisioningProfile(args.provisionPath, effectiveIdentifier);
            for (const std::string& warning : provision.warnings) {
                if (talk) std::cerr << "WARNING: " << warning << "\n";
                warnings.push_back(warning);
            }
        }
        if (args.verbose) {
            std::cout << "Input image: " << inputPath << " (" << sourceBytes.size() << " bytes, sha256 " << inputSha256 << ")\n";
        }

        if (args.toIntel) {
            const auto codeSegments = Relinker::ElfReader(sourceBytes).ReadCodeSegments();
            auto converted = Codegen::MakeAmd64OnlyConverter()->Convert(std::move(sourceBytes), codeSegments);
            sourceBytes = std::move(converted.Bytes);
            trampolines = std::move(converted.Trampolines);
            substitutionReports = std::move(converted.Reports);
            PrintSiteReports(substitutionReports, trampolines, talk);
        }

        auto elfReader = std::make_shared<Relinker::ElfReader>(sourceBytes);

        std::string consoleTargetStr = "ps5";
        if (args.consoleMode == Cli::ConsoleMode::PS4) {
            consoleTargetStr = "ps4";
        } else if (args.consoleMode == Cli::ConsoleMode::PS5) {
            consoleTargetStr = "ps5";
        } else {
            consoleTargetStr = elfReader->DetectConsoleTarget();
        }
        outcome.consoleTarget = consoleTargetStr;

        const std::shared_ptr<Relinker::ISyscallScanner> syscallScanner = args.skipSyscallCheck ? Relinker::MakeNullSyscallScanner() : Relinker::MakeSyscallScanner();

        const auto pipeline = std::make_shared<Relinker::RelinkerPipeline>(
            elfReader,
            syscallScanner,
            Relinker::MakeCallSiteResolver(),
            std::make_shared<Relinker::ValidationPolicy>(),
            std::make_shared<Relinker::SysVDynamicSectionBuilder>(),
            args.unusedFilterLevel == 2 ? Relinker::MakeStrictUnusedNidFilter() : Relinker::MakeUnusedNidFilter(),
            args.unusedFilterLevel,
            consoleTargetStr
        );

        if (talk) {
            std::cout << "Console: " << consoleTargetStr << "; System: " << args.TargetName() << "; unused-filter=" << args.unusedFilterLevel << "\n";
            std::cout << "Porting scheme: " << scheme.Describe() << "\n";
            std::cout << "sce_module/sce_modules processing: " << (args.skipSceModule ? "disabled (--skip-sce-module)" : "enabled") << '\n';
            if (args.rytyNet) std::cout << "RytyNet online mode: enabled (experimental)\n";
            if (args.dryRun) std::cout << "Dry run: the ported executable will not be written\n";
            for (const auto& name : args.excludedSceModules) std::cout << "sce_module excluded: " << name << '\n';
        }

        auto result = pipeline->Relink(sourceBytes);
        for (const auto& patch : result.Patches) {
            if (patch.Offset > sourceBytes.size() || patch.Bytes.size() > sourceBytes.size() - patch.Offset)
                throw Domain::RelinkerException("Relinker patch exceeds source image", patch.Offset);
            for (std::size_t index = 0; index < patch.Bytes.size(); ++index) sourceBytes[patch.Offset + index] = patch.Bytes[index];
        }

        std::vector<Relinker::GuestArtifact> guestArtifacts;
        if (!args.skipSceModule) {
            guestArtifacts = Relinker::GuestModuleBuilder().Build(inputPath, exePath, result.DynamicSection, args.toWindows, args.toIntel, *syscallScanner, args.lazyBinding, args.runPath, args.excludedSceModules);
        }

        auto byteWriter = std::make_shared<Io::ByteWriter>();

        std::shared_ptr<Elfpatcher::IElfPatcher> patcher;
        if (args.toWindows) {
            patcher = std::make_shared<Elfpatcher::Windows::WindowsPePatcher>(args.windowsConsole);
        } else if (args.IsAppleTarget()) {
            auto applePatcher = std::make_shared<Elfpatcher::MacOs::MacOsElfPatcher>();
            Elfpatcher::Apple::MachOOptions options;
            options.platform = args.toIos ? Elfpatcher::Apple::MachOOptions::Platform::Ios
                            : args.toIosSim ? Elfpatcher::Apple::MachOOptions::Platform::IosSimulator
                                            : Elfpatcher::Apple::MachOOptions::Platform::MacOs;
            options.universal = args.universal && !args.IsIosFamily();
            const std::uint32_t minOs = Cli::EncodeAppleVersion(minOsVersion);
            options.minOs = minOs;
            options.sdk = std::max(minOs, args.IsIosFamily() ? 0x00120000u /* iOS 18.0 */ : 0x000E0000u /* macOS 14.0 */);
            options.identifier = effectiveIdentifier;
            options.hardenedRuntime = args.hardenedRuntime;
            options.sourceVersion = Cli::EncodeSourceVersion(RYTY_VERSION);
            if (!args.entitlementsPath.empty()) {
                options.entitlements = fileReader.Read(args.entitlementsPath);
                if (talk) std::cout << "Entitlements: " << args.entitlementsPath << " (" << options.entitlements.size() << " bytes)\n";
            } else if (!args.entitlementsTemplate.empty()) {
                options.entitlements = Elfpatcher::Apple::MakeEntitlementsTemplate(args.entitlementsTemplate, effectiveIdentifier);
                if (talk) std::cout << "Entitlements template: " << args.entitlementsTemplate << " (" << options.entitlements.size() << " bytes)\n";
            }
            applePatcher->SetOptions(options);
            patcher = applePatcher;
        } else {
            auto linuxPatcher = std::make_shared<Elfpatcher::Linux::LinuxElfPatcher>(
                std::make_shared<Elfpatcher::EntryStubBuilder>(),
                std::make_shared<Elfpatcher::ProgramHeaderLayoutBuilder>(
                    std::make_shared<Elfpatcher::SegmentFilter>(),
                    byteWriter
                ),
                std::make_shared<Elfpatcher::SectionHeaderTableBuilder>(byteWriter),
                byteWriter
            );
            // 2.0.0 porting scheme: the loader path baked into PT_INTERP
            // (--interp, default /lib64/ld-linux-x86-64.so.2).
            linuxPatcher->SetInterpreter(args.interpPath);
            patcher = linuxPatcher;
        }

        const auto executableBytes = patcher->Patch(sourceBytes, result.OriginalHeaders, result.DynamicSection, result.OriginalPltGotVaddr, args.runPath, args.lazyBinding, args.windowsDiagnostics, trampolines);
        outcome.outputBytes = executableBytes.size();
        outcome.inPlaceReplacements = substitutionReports.size() > trampolines.size() ? substitutionReports.size() - trampolines.size() : 0;
        outcome.trampolines = trampolines.size();
        outcome.guestModules = guestArtifacts.size();
        outcome.externalReferences = result.RegistryEntries.size();
        outcome.warnings = warnings;

        if (!args.dryRun) {
            if (bundle) {
                // The bundle directories must exist before the Mach-O and the
                // guest modules land inside them.
                std::filesystem::create_directories(std::filesystem::path(exePath).parent_path());
            }
            for (const auto& artifact : guestArtifacts) {
                std::filesystem::create_directories(artifact.Path.parent_path());
                fileWriter.Write(artifact.Path.string(), artifact.Bytes);
                if (talk) std::cout << "Guest module: " << artifact.Path.string() << '\n';
            }
            fileWriter.Write(exePath, executableBytes);

            // 2.0.0: .app bundle skeleton (Info.plist, PkgInfo) and the iOS
            // .ipa archive around it.
            if (bundle) {
                Elfpatcher::Apple::BundleSpec spec;
                spec.ios = args.IsIosFamily();
                spec.simulator = args.toIosSim;
                spec.appPath = appPath;
                spec.executablePath = exePath;
                spec.executableName = appExecutableName;
                spec.bundleIdentifier = effectiveIdentifier;
                spec.displayName = appExecutableName;
                spec.minOsVersion = minOsVersion;
                spec.version = RYTY_VERSION;
                spec.provisionProfile = args.provisionPath;
                Elfpatcher::Apple::CreateBundleSkeleton(spec);
                Elfpatcher::Apple::EmbedProvisioningProfile(spec);
                outcome.appPath = appPath;
                if (talk) {
                    const char* bundleKind = args.toIos ? "iOS app bundle: "
                        : args.toIosSim ? "iOS Simulator app bundle: " : "macOS app bundle: ";
                    std::cout << bundleKind << appPath << '\n';
                }
                if (args.writeIpa) {
                    const std::string ipaPath = (std::filesystem::path(appPath).parent_path() / (appExecutableName + ".ipa")).string();
                    Elfpatcher::Apple::WriteIpa(appPath, ipaPath);
                    outcome.ipaPath = ipaPath;
                    if (talk) std::cout << "IPA archive: " << ipaPath << '\n';
                }
            }

            if (args.writeRegistry) {
                const std::filesystem::path outFsPath(absPath);
                const std::string registryPath = (outFsPath.parent_path() / (outFsPath.stem().string() + ".registry.json")).string();
                fileWriter.Write(registryPath, std::make_shared<Relinker::CallRegistryWriter>()->WriteCallRegistry(result.RegistryEntries));
                if (talk) std::cout << "Call registry: " << registryPath << '\n';
            }

            if (args.rytyNet) {
                const std::filesystem::path outFs(exePath);
                const std::string markerPath = (outFs.parent_path() / (outFs.filename().string() + ".rytynet")).string();
                fileWriter.Write(markerPath, "ryty-net/1 experimental online mode\n");
                if (talk) {
                    std::cout << "WARNING: RytyNet is experimental. The online-mode bridge was enabled for this port;\n"
                              << "the runtime activates it when this marker exists or RTY_NET_ENABLE=1 is set.\n"
                              << "RytyNet marker: " << markerPath << '\n';
                }
                outcome.warnings.push_back("RytyNet online mode is experimental");
            }
        } else if (talk && !guestArtifacts.empty()) {
            std::cout << "Dry run: " << guestArtifacts.size() << " guest module(s) analyzed but not written\n";
        }

        if (args.verifyOutput) {
            AppleVerifyExpectations expectations;
            expectations.platform = args.toIos ? 2 : (args.toIosSim ? 7 : 1);
            expectations.minOs = Cli::EncodeAppleVersion(minOsVersion);
            expectations.identifier = effectiveIdentifier;
            expectations.requirePie = false;
            LinuxVerifyExpectations linuxExpectations;
            linuxExpectations.interpreter = args.interpPath;
            linuxExpectations.requireRunPath = !args.runPath.empty();
            linuxExpectations.requireNxStack = true;
            WindowsVerifyExpectations windowsExpectations;
            windowsExpectations.console = args.windowsConsole;
            if (args.dryRun) {
                std::string reason;
                outcome.verified = VerifyOutputBytes(executableBytes, args.TargetName(), expectations, linuxExpectations, windowsExpectations, reason);
                if (talk) std::cout << "Verification (in memory): " << (outcome.verified ? "OK" : "FAILED") << " - " << reason << '\n';
                if (!outcome.verified) throw Domain::RelinkerException("Output verification failed: " + reason);
            } else {
                const auto written = fileReader.Read(exePath);
                std::string reason;
                outcome.verified = VerifyOutputBytes(written, args.TargetName(), expectations, linuxExpectations, windowsExpectations, reason);
                if (talk) std::cout << "Verification: " << (outcome.verified ? "OK" : "FAILED") << " - " << reason << '\n';
                if (!outcome.verified) throw Domain::RelinkerException("Output verification failed: " + reason);
            }
        }

        if (talk) {
            std::cout << "External prx references: " << result.RegistryEntries.size() << "\n";
            if (args.dryRun) {
                std::cout << "Dry run complete; output not written: " << exePath << '\n';
            } else {
                std::cout << "Output file: " << exePath << '\n';
                std::cout << "Expected runtime layout (relative to the output executable):\n"
                          << std::filesystem::path(exePath).filename().string() << "\n"
                          << "libs/\n    *.prx\napp0/\n    <game resources>\n";
                if (!guestArtifacts.empty()) {
                    std::cout << "    " << guestArtifacts.front().Path.parent_path().filename().string() << "/\n";
                    for (const auto& artifact : guestArtifacts) std::cout << "        " << artifact.Path.filename().string() << '\n';
                }
                std::cout << "Game resources and system libraries must be placed in this layout separately.\n";
                if (args.runPath != "$ORIGIN/libs") std::cout << "Custom library search path (--rpath): " << args.runPath << '\n';
            }
        }

        if (!args.reportPath.empty()) {
            outcome.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            std::ostringstream report;
            report << "{\n";
            report << "  \"schema\": \"ryty-port-report/2\",\n";
            report << "  \"tool\": \"ryty\",\n";
            report << "  \"version\": \"" << RYTY_VERSION << "\",\n";
            // 2.0.0 porting scheme: every report documents the resolved plan.
            AppendSchemeJson(report, scheme);
            report << ",\n";
            report << "  \"input\": \"" << escapeJson(inputPath) << "\",\n";
            report << "  \"output\": \"" << escapeJson(exePath) << "\",\n";
            report << "  \"console\": \"" << escapeJson(consoleTargetStr) << "\",\n";
            report << "  \"system\": \"" << args.TargetName() << "\",\n";
            if (args.IsAppleTarget()) {
                report << "  \"apple\": {\n";
                report << "    \"platform\": \"" << escapeJson(args.TargetName()) << "\",\n";
                report << "    \"universal\": " << (args.universal && !args.IsIosFamily() ? "true" : "false") << ",\n";
                report << "    \"identifier\": \"" << escapeJson(effectiveIdentifier) << "\",\n";
                report << "    \"minimumOsVersion\": \"" << escapeJson(minOsVersion) << "\",\n";
                report << "    \"entitlements\": " << (!args.entitlementsPath.empty() || !args.entitlementsTemplate.empty() ? "true" : "false") << ",\n";
                if (!args.entitlementsTemplate.empty()) {
                    report << "    \"entitlementsTemplate\": \"" << escapeJson(args.entitlementsTemplate) << "\",\n";
                }
                report << "    \"hardenedRuntime\": " << (args.hardenedRuntime ? "true" : "false") << ",\n";
                report << "    \"bundle\": " << (outcome.appPath.empty() ? std::string("null") : "\"" + escapeJson(outcome.appPath) + "\"") << ",\n";
                report << "    \"ipa\": " << (outcome.ipaPath.empty() ? std::string("null") : "\"" + escapeJson(outcome.ipaPath) + "\"") << "\n";
                report << "  },\n";
            }
            report << "  \"dryRun\": " << (args.dryRun ? "true" : "false") << ",\n";
            report << "  \"verified\": " << (outcome.verified ? "true" : "false") << ",\n";
            report << "  \"inputSha256\": \"" << inputSha256 << "\",\n";
            report << "  \"outputSha256\": \"" << (args.dryRun ? Io::Sha256Hex(executableBytes) : Io::Sha256Hex(fileReader.Read(exePath))) << "\",\n";
            report << "  \"elapsedMs\": " << std::fixed << std::setprecision(1) << outcome.elapsedMs << ",\n";
            report << "  \"options\": {\n";
            report << "    \"toIntel\": " << (args.toIntel ? "true" : "false") << ",\n";
            report << "    \"unusedFilterLevel\": " << args.unusedFilterLevel << ",\n";
            report << "    \"skipSceModule\": " << (args.skipSceModule ? "true" : "false") << ",\n";
            report << "    \"skipSyscallCheck\": " << (args.skipSyscallCheck ? "true" : "false") << ",\n";
            report << "    \"lazyBinding\": " << (args.lazyBinding ? "true" : "false") << ",\n";
            report << "    \"rytyNet\": " << (args.rytyNet ? "true" : "false") << ",\n";
            report << "    \"rpath\": \"" << escapeJson(args.runPath) << "\"\n";
            report << "  },\n";
            report << "  \"intelSubstitutions\": [\n";
            for (std::size_t index = 0; index < substitutionReports.size(); ++index) {
                const auto& item = substitutionReports[index];
                report << "    {\"instruction\": \"" << escapeJson(item.InstructionName) << "\", \"offset\": " << item.Offset
                       << ", \"originalLength\": " << item.OriginalLength << ", \"replacementLength\": " << item.ReplacementLength
                       << ", \"lowering\": \"" << (item.Lowering == Codegen::Amd64OnlyLowering::InPlace ? "in-place" : "trampoline") << "\"}";
                report << (index + 1 < substitutionReports.size() ? ",\n" : "\n");
            }
            report << "  ],\n";
            report << "  \"summary\": {\n";
            report << "    \"inPlaceReplacements\": " << outcome.inPlaceReplacements << ",\n";
            report << "    \"trampolines\": " << trampolines.size() << ",\n";
            report << "    \"guestModules\": " << guestArtifacts.size() << ",\n";
            report << "    \"externalReferences\": " << result.RegistryEntries.size() << "\n";
            report << "  },\n";
            report << "  \"guestModules\": [\n";
            for (std::size_t index = 0; index < guestArtifacts.size(); ++index) {
                report << "    \"" << escapeJson(guestArtifacts[index].Path.string()) << "\"";
                report << (index + 1 < guestArtifacts.size() ? ",\n" : "\n");
            }
            report << "  ],\n";
            report << "  \"warnings\": [\n";
            for (std::size_t index = 0; index < outcome.warnings.size(); ++index) {
                report << "    \"" << escapeJson(outcome.warnings[index]) << "\"";
                report << (index + 1 < outcome.warnings.size() ? ",\n" : "\n");
            }
            report << "  ]\n";
            report << "}\n";
            fileWriter.Write(args.reportPath, report.str());
            if (talk) std::cout << "Porting report: " << args.reportPath << '\n';
        }

        outcome.ok = true;
    } catch (const Domain::RelinkerException& e) {
        outcome.ok = false;
        outcome.error = e.what();
        outcome.failureOffset = e.FailureOffset;
    } catch (const Codegen::CodegenException& e) {
        outcome.ok = false;
        outcome.error = e.what();
        outcome.failureOffset = e.FailureOffset;
    } catch (const std::exception& e) {
        outcome.ok = false;
        outcome.error = e.what();
    }

    const auto finished = std::chrono::steady_clock::now();
    outcome.elapsedMs = std::chrono::duration<double, std::milli>(finished - started).count();
    return outcome;
}

}
