#include <Cli.hpp>
#include <ConfigKeys.hpp>
#include <PortScheme.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Cli {

namespace {

// Shared usage text. --version, --verbose, --verify, --dry-run, --config and
// the batch flags, the Apple targets (--target ios / ios-sim, --universal,
// --bundle, --ipa, entitlements and the minimum-version flags) and the porting
// scheme flags (--min-os, --interp, empty --rpath) are all part of this
// 2.0.0 release.
std::string UsageText() {
    return
        "Usage: ryty [--windows|--macos|--ios|--ios-simulator|--target linux|windows|macos|ios|ios-sim] [--ps4|--ps5|--console auto|ps4|ps5] [--windows-diagnostics] [--windows-gui] [--windows-console] [--rytynet] [--skip-syscall-check] [--skip-sce-module] [--exclude-sce-module <file>]... [--to-intel] [unused-filter=0|1|2] [--registry] [--report <file>] [--rpath <path>|--rpath \"\"] [--lazy-binding] [--interp <path>] [--universal] [--bundle] [--ipa] [--macos-min <ver>] [--ios-min <ver>] [--min-os <ver>] [--identifier <id>] [--entitlements <plist>|--entitlements-template <preset>] [--provision <mobileprovision>] [--hardened-runtime] [--autorun] [--verbose] [--verify] [--dry-run] [--config <file>] [--batch <dir> [--batch-out <dir>]] [--version] <input.elf> <output.elf>\n"
        "Example: ryty input.elf output.elf\n"
        "Apple:   ryty --target macos --universal --bundle input.elf ported\n"
        "iOS:     ryty --target ios --ipa --entitlements-template ios-device input.elf ported\n"
        "Sim:     ryty --target ios-sim --bundle input.elf ported\n"
        "Linux:   ryty --interp /lib/ld-musl-x86_64.so.1 --rpath \"\" input.elf ported.elf (musl loader, no RUNPATH)\n"
        "Batch:   ryty --batch ./games --windows ports every ELF in ./games into ./games/ported\n"
        "Config:  ryty --config port.ini input.elf output.elf (the command line overrides the file)";
}

}

namespace Internal {

bool ApplyConfigKey(const std::string& rawKey, const std::string& value, Args& args) {
    std::string key;
    for (const char character : rawKey) {
        if (character == '_') key += '-';
        else if (character >= 'A' && character <= 'Z') key += static_cast<char>(character - ('A' - 'a'));
        else key += character;
    }

    if (key == "windows" || key == "target-windows") { if (IsTruth(value)) { args.toWindows = true; args.toMacOs = false; args.toIos = false; args.toIosSim = false; } return true; }
    if (key == "macos" || key == "target-macos") { if (IsTruth(value)) { args.toMacOs = true; args.toWindows = false; args.toIos = false; args.toIosSim = false; } return true; }
    if (key == "ios" || key == "target-ios") { if (IsTruth(value)) { args.toIos = true; args.toWindows = false; args.toMacOs = false; args.toIosSim = false; } return true; }
    if (key == "ios-simulator" || key == "target-ios-sim") { if (IsTruth(value)) { args.toIosSim = true; args.toIos = false; args.toWindows = false; args.toMacOs = false; } return true; }
    if (key == "linux" || key == "target-linux") { if (IsTruth(value)) { args.toWindows = false; args.toMacOs = false; args.toIos = false; args.toIosSim = false; } return true; }
    if (key == "target") {
        if (value == "windows") { args.toWindows = true; args.toMacOs = false; args.toIos = false; args.toIosSim = false; return true; }
        if (value == "macos") { args.toMacOs = true; args.toWindows = false; args.toIos = false; args.toIosSim = false; return true; }
        if (value == "ios") { args.toIos = true; args.toWindows = false; args.toMacOs = false; args.toIosSim = false; return true; }
        if (value == "ios-sim" || value == "ios-simulator") { args.toIosSim = true; args.toIos = false; args.toWindows = false; args.toMacOs = false; return true; }
        if (value == "linux") { args.toWindows = false; args.toMacOs = false; args.toIos = false; args.toIosSim = false; return true; }
        return false;
    }
    if (key == "ps4") { if (IsTruth(value)) args.consoleMode = ConsoleMode::PS4; return true; }
    if (key == "ps5") { if (IsTruth(value)) args.consoleMode = ConsoleMode::PS5; return true; }
    if (key == "console") {
        if (value == "ps4") { args.consoleMode = ConsoleMode::PS4; return true; }
        if (value == "ps5") { args.consoleMode = ConsoleMode::PS5; return true; }
        if (value == "auto") { args.consoleMode = ConsoleMode::Auto; return true; }
        return false;
    }
    if (key == "to-intel") { if (IsTruth(value)) args.toIntel = true; return true; }
    if (key == "unused-filter") {
        if (value.size() != 1 || value[0] < '0' || value[0] > '2') return false;
        args.unusedFilterLevel = static_cast<std::uint32_t>(value[0] - '0');
        return true;
    }
    if (key == "skip-syscall-check") { if (IsTruth(value)) args.skipSyscallCheck = true; return true; }
    if (key == "skip-sce-module") { if (IsTruth(value)) args.skipSceModule = true; return true; }
    if (key == "exclude-sce-module") { if (!value.empty()) args.excludedSceModules.insert(value); return true; }
    if (key == "registry") { if (IsTruth(value)) args.writeRegistry = true; return true; }
    if (key == "report") { args.reportPath = value; return true; }
    if (key == "rpath") { args.runPath = value; return true; } // empty disables the baked-in search path
    if (key == "lazy-binding") { if (IsTruth(value)) args.lazyBinding = true; return true; }
    if (key == "autorun") { if (IsTruth(value)) args.autorun = true; return true; }
    if (key == "windows-diagnostics") { if (IsTruth(value)) args.windowsDiagnostics = true; return true; }
    if (key == "windows-gui") { if (IsTruth(value)) args.windowsGui = true; return true; }
    if (key == "windows-console") { if (IsTruth(value)) args.windowsConsole = true; return true; }
    if (key == "rytynet") { if (IsTruth(value)) args.rytyNet = true; return true; }
    if (key == "verbose") { if (IsTruth(value)) args.verbose = true; return true; }
    if (key == "verify") { if (IsTruth(value)) args.verifyOutput = true; return true; }
    if (key == "dry-run") { if (IsTruth(value)) args.dryRun = true; return true; }
    if (key == "batch") { if (!value.empty()) args.batchDir = value; return true; }
    if (key == "batch-out") { if (!value.empty()) args.batchOutDir = value; return true; }
    // 2.0.0 Apple options.
    if (key == "universal") { if (IsTruth(value)) args.universal = true; return true; }
    if (key == "bundle") { if (IsTruth(value)) args.writeBundle = true; return true; }
    if (key == "ipa") { if (IsTruth(value)) args.writeIpa = true; return true; }
    if (key == "hardened-runtime") { if (IsTruth(value)) args.hardenedRuntime = true; return true; }
    if (key == "macos-min") { if (!value.empty()) args.macOsMinVersion = value; return true; }
    if (key == "ios-min") { if (!value.empty()) args.iosMinVersion = value; return true; }
    if (key == "min-os") { if (!value.empty()) args.minOsVersion = value; return true; }
    if (key == "interp") { if (!value.empty()) { args.interpPath = value; args.interpSpecified = true; } return true; }
    if (key == "identifier") { args.identifier = value; return true; }
    if (key == "entitlements") { if (!value.empty()) args.entitlementsPath = value; return true; }
    if (key == "entitlements-template") { if (!value.empty()) args.entitlementsTemplate = value; return true; }
    if (key == "provision") { if (!value.empty()) args.provisionPath = value; return true; }
    return false;
}

}

std::uint32_t EncodeAppleVersion(const std::string& version) {
    std::uint32_t fields[3] = {0, 0, 0};
    std::size_t field = 0;
    std::string current;
    const auto flushField = [&]() {
        if (current.empty()) throw std::runtime_error("invalid Apple version: " + version);
        for (const char character : current) {
            if (character < '0' || character > '9') throw std::runtime_error("invalid Apple version: " + version);
        }
        const long parsed = std::stol(current);
        if (parsed > 255) throw std::runtime_error("Apple version component out of range: " + current);
        if (field >= 3) throw std::runtime_error("invalid Apple version: " + version);
        fields[field++] = static_cast<std::uint32_t>(parsed);
        current.clear();
    };
    for (const char character : version) {
        if (character == '.') flushField();
        else current += character;
    }
    flushField();
    return (fields[0] << 16) | (fields[1] << 8) | fields[2];
}

std::uint64_t EncodeSourceVersion(const std::string& version) {
    // LC_SOURCE_VERSION payload. The project packs release fields into 16-bit
    // groups from the top, e.g. this release: 2.0.0 -> 0x0002000000000000.
    std::uint64_t encoded = 0;
    std::size_t field = 0;
    std::string current;
    const auto flushField = [&]() {
        if (current.empty()) throw std::runtime_error("invalid source version: " + version);
        for (const char character : current) {
            if (character < '0' || character > '9') throw std::runtime_error("invalid source version: " + version);
        }
        const long parsed = std::stol(current);
        if (parsed < 0 || parsed > 255) {
            throw std::runtime_error("source version component out of range: " + current);
        }
        if (field >= 5) throw std::runtime_error("invalid source version: " + version);
        encoded |= static_cast<std::uint64_t>(parsed) << (48 - 8 * field);
        ++field;
        current.clear();
    };
    for (const char character : version) {
        if (character == '.') flushField();
        else current += character;
    }
    flushField();
    return encoded;
}

std::string FormatAppleVersion(std::uint32_t encoded) {
    std::ostringstream out;
    out << ((encoded >> 16) & 0xFF) << "." << ((encoded >> 8) & 0xFF) << "." << (encoded & 0xFF);
    return out.str();
}

Args ParseArgs(int argc, char* argv[]) {
    Args args;
    bool unusedFilterSpecified = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version" || arg == "-V") {
            args.showVersion = true;
        } else if (arg == "--verbose") {
            args.verbose = true;
        } else if (arg == "--verify") {
            args.verifyOutput = true;
        } else if (arg == "--dry-run") {
            args.dryRun = true;
        } else if (arg == "--config") {
            if (i + 1 >= argc)
                throw std::runtime_error("--config requires a file path");
            args.configPath = argv[++i];
            // The file is applied at the point it appears: options parsed
            // later (from the file or the command line) override earlier ones,
            // so an explicit flag after --config always wins.
            Cli::ApplyConfigFile(args.configPath, args);
        } else if (arg == "--batch") {
            if (i + 1 >= argc)
                throw std::runtime_error("--batch requires an input directory");
            args.batchDir = argv[++i];
        } else if (arg == "--batch-out") {
            if (i + 1 >= argc)
                throw std::runtime_error("--batch-out requires an output directory");
            args.batchOutDir = argv[++i];
        } else if (arg == "--skip-syscall-check") {
            args.skipSyscallCheck = true;
        } else if (arg == "--skip-sce-module") {
            args.skipSceModule = true;
        } else if (arg == "--exclude-sce-module") {
            if (i + 1 >= argc)
                throw std::runtime_error("--exclude-sce-module requires a file name");
            args.excludedSceModules.insert(argv[++i]);
        } else if (arg == "--to-intel") {
            args.toIntel = true;
        } else if (arg.rfind("unused-filter=", 0) == 0) {
            const std::string value = arg.substr(14);
            if (unusedFilterSpecified || value.size() != 1 || value[0] < '0' || value[0] > '2')
                throw std::runtime_error("unused-filter must be specified once with a value of 0, 1 or 2");
            args.unusedFilterLevel = static_cast<std::uint32_t>(value[0] - '0');
            unusedFilterSpecified = true;
        } else if (arg == "--registry") {
            args.writeRegistry = true;
        } else if (arg == "--report") {
            if (i + 1 >= argc)
                throw std::runtime_error("--report requires a file path");
            args.reportPath = argv[++i];
        } else if (arg == "--rpath") {
            if (i + 1 >= argc)
                throw std::runtime_error("--rpath requires a value (pass \"\" to disable the baked-in search path)");
            args.runPath = argv[++i];
        } else if (arg == "--min-os") {
            if (i + 1 >= argc)
                throw std::runtime_error("--min-os requires a version (e.g. 12.0)");
            args.minOsVersion = argv[++i];
        } else if (arg == "--interp") {
            if (i + 1 >= argc)
                throw std::runtime_error("--interp requires a loader path (e.g. /lib/ld-musl-x86_64.so.1)");
            args.interpPath = argv[++i];
            args.interpSpecified = true;
        } else if (arg == "--windows") {
            args.toWindows = true;
        } else if (arg == "--macos") {
            args.toMacOs = true;
        } else if (arg == "--ios") {
            args.toIos = true;
        } else if (arg == "--ios-simulator") {
            args.toIosSim = true;
        } else if (arg == "--universal") {
            args.universal = true;
        } else if (arg == "--bundle") {
            args.writeBundle = true;
        } else if (arg == "--ipa") {
            args.writeIpa = true;
        } else if (arg == "--hardened-runtime") {
            args.hardenedRuntime = true;
        } else if (arg == "--macos-min") {
            if (i + 1 >= argc) throw std::runtime_error("--macos-min requires a version (e.g. 12.0)");
            args.macOsMinVersion = argv[++i];
        } else if (arg == "--ios-min") {
            if (i + 1 >= argc) throw std::runtime_error("--ios-min requires a version (e.g. 14.0)");
            args.iosMinVersion = argv[++i];
        } else if (arg == "--identifier") {
            if (i + 1 >= argc) throw std::runtime_error("--identifier requires a bundle identifier");
            args.identifier = argv[++i];
        } else if (arg == "--entitlements") {
            if (i + 1 >= argc) throw std::runtime_error("--entitlements requires a plist file");
            args.entitlementsPath = argv[++i];
        } else if (arg == "--entitlements-template") {
            if (i + 1 >= argc) throw std::runtime_error("--entitlements-template requires a preset name (macos-app-sandbox, macos-jit, ios-device, ios-debug)");
            args.entitlementsTemplate = argv[++i];
        } else if (arg == "--provision") {
            if (i + 1 >= argc) throw std::runtime_error("--provision requires a .mobileprovision file");
            args.provisionPath = argv[++i];
        } else if (arg == "--target") {
            if (i + 1 >= argc)
                throw std::runtime_error("--target requires a system name (linux, windows, macos, ios, ios-sim)");
            const std::string target = argv[++i];
            if (target == "windows" || target == "WINDOWS") { args.toWindows = true; args.toMacOs = false; args.toIos = false; args.toIosSim = false; }
            else if (target == "macos" || target == "MACOS" || target == "macOS") { args.toMacOs = true; args.toWindows = false; args.toIos = false; args.toIosSim = false; }
            else if (target == "ios" || target == "IOS" || target == "iOS") { args.toIos = true; args.toWindows = false; args.toMacOs = false; args.toIosSim = false; }
            else if (target == "ios-sim" || target == "IOS-SIM" || target == "ios-simulator") { args.toIosSim = true; args.toIos = false; args.toWindows = false; args.toMacOs = false; }
            else if (target == "linux" || target == "LINUX") { args.toWindows = false; args.toMacOs = false; args.toIos = false; args.toIosSim = false; }
            else throw std::runtime_error("unknown target system: " + target);
        } else if (arg == "--ps4") {
            args.consoleMode = ConsoleMode::PS4;
        } else if (arg == "--ps5") {
            args.consoleMode = ConsoleMode::PS5;
        } else if (arg == "--console") {
            if (i + 1 >= argc)
                throw std::runtime_error("--console requires a mode (auto, ps4, ps5)");
            const std::string mode = argv[++i];
            if (mode == "ps4" || mode == "PS4") args.consoleMode = ConsoleMode::PS4;
            else if (mode == "ps5" || mode == "PS5") args.consoleMode = ConsoleMode::PS5;
            else if (mode == "auto" || mode == "AUTO") args.consoleMode = ConsoleMode::Auto;
            else throw std::runtime_error("unknown console mode: " + mode);
        } else if (arg == "--lazy-binding") {
            args.lazyBinding = true;
        } else if (arg == "--autorun") {
            args.autorun = true;
        } else if (arg == "--windows-diagnostics") {
            args.windowsDiagnostics = true;
        } else if (arg == "--windows-gui") {
            args.windowsGui = true;
        } else if (arg == "--windows-console") {
            args.windowsConsole = true;
        } else if (arg == "--rytynet") {
            args.rytyNet = true;
        } else if (arg.rfind("--", 0) == 0 || arg == "unused-filter") {
            throw std::runtime_error("unknown option: " + arg);
        } else if (args.inputPath.empty()) {
            args.inputPath = arg;
        } else if (args.outputPath.empty()) {
            args.outputPath = arg;
        } else {
            throw std::runtime_error("unexpected argument: " + arg);
        }
    }

    if (args.skipSceModule && !args.excludedSceModules.empty())
        throw std::runtime_error("--exclude-sce-module conflicts with --skip-sce-module");

    {
        int targetCount = (args.toWindows ? 1 : 0) + (args.toMacOs ? 1 : 0) + (args.toIos ? 1 : 0) + (args.toIosSim ? 1 : 0);
        if (targetCount > 1)
            throw std::runtime_error("--windows, --macos, --ios and --ios-simulator cannot be used together");
    }

    if (args.windowsDiagnostics && !args.toWindows)
        throw std::runtime_error("--windows-diagnostics requires --windows");

    if (args.windowsGui && !args.toWindows)
        throw std::runtime_error("--windows-gui requires --windows");

    if (args.windowsConsole && !args.toWindows)
        throw std::runtime_error("--windows-console requires --windows");

    // 2.0.0 Apple option validation.
    if (args.universal && !args.toMacOs)
        throw std::runtime_error("--universal requires the macOS target (--macos or --target macos)");
    if (args.writeBundle && !args.IsAppleTarget())
        throw std::runtime_error("--bundle requires the macOS, iOS or iOS Simulator target");
    if (args.writeIpa && !args.toIos)
        throw std::runtime_error("--ipa requires the iOS device target (--ios or --target ios)");
    if (!args.entitlementsPath.empty() && !args.IsAppleTarget())
        throw std::runtime_error("--entitlements requires the macOS, iOS or iOS Simulator target");
    if (!args.entitlementsTemplate.empty() && !args.IsAppleTarget())
        throw std::runtime_error("--entitlements-template requires the macOS, iOS or iOS Simulator target");
    if (!args.entitlementsTemplate.empty() && !args.entitlementsPath.empty())
        throw std::runtime_error("--entitlements-template cannot be combined with --entitlements");
    if (args.hardenedRuntime && !args.IsAppleTarget())
        throw std::runtime_error("--hardened-runtime requires the macOS, iOS or iOS Simulator target");
    if (!args.provisionPath.empty() && !args.toIos)
        throw std::runtime_error("--provision requires the iOS device target (--ios or --target ios)");
    if (!args.provisionPath.empty() && !args.writeBundle)
        throw std::runtime_error("--provision requires --bundle (the profile is embedded in the .app)");
    // An .ipa packages the .app bundle, so asking for one implies the other.
    if (args.writeIpa) args.writeBundle = true;
    // 2.0.0 --min-os: unified Apple deployment target. It applies to the
    // selected Apple target at port time and must not fight the per-target
    // flags.
    if (!args.minOsVersion.empty()) {
        if (!args.IsAppleTarget())
            throw std::runtime_error("--min-os requires the macOS, iOS or iOS Simulator target");
        if (!args.macOsMinVersion.empty() && args.macOsMinVersion != "11.0" && args.macOsMinVersion != args.minOsVersion)
            throw std::runtime_error("--min-os cannot be combined with --macos-min");
        if (!args.iosMinVersion.empty() && args.iosMinVersion != "14.0" && args.iosMinVersion != args.minOsVersion)
            throw std::runtime_error("--min-os cannot be combined with --ios-min");
        EncodeAppleVersion(args.minOsVersion);
        args.macOsMinVersion = args.minOsVersion;
        args.iosMinVersion = args.minOsVersion;
    }
    if (args.interpSpecified && (args.toWindows || args.IsAppleTarget()))
        throw std::runtime_error("--interp requires the Linux target");
    if (!args.identifier.empty() && args.IsAppleTarget() && !Cli::IsValidAppleIdentifier(args.identifier))
        throw std::runtime_error("invalid bundle identifier: \"" + args.identifier
                                 + "\" (use letters, digits, dots and hyphens, e.g. com.example.game)");
    // Validate the version strings up front so a typo never reaches a port.
    EncodeAppleVersion(args.macOsMinVersion);
    EncodeAppleVersion(args.iosMinVersion);
    if (args.autorun && args.IsAppleTarget())
        throw std::runtime_error("--autorun supports the Linux and Windows targets only");

    // --version prints and exits; no input/output or batch directory required.
    if (args.showVersion) return args;

    if (!args.batchDir.empty() && !args.inputPath.empty())
        throw std::runtime_error("--batch cannot be combined with a positional <input> file");

    if (args.batchDir.empty() && !args.batchOutDir.empty())
        throw std::runtime_error("--batch-out requires --batch");

    if (args.dryRun && args.autorun)
        throw std::runtime_error("--autorun cannot launch a --dry-run port");

    const bool needsImage = args.batchDir.empty();
    if (needsImage && (args.inputPath.empty() || args.outputPath.empty()))
        throw std::runtime_error(UsageText());

    return args;
}

}
