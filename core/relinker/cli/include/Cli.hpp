#ifndef CORE_RELINKER_CLI_INCLUDE_CLI_HPP
#define CORE_RELINKER_CLI_INCLUDE_CLI_HPP

#include <set>
#include <string>
#include <cstdint>
#include <vector>

namespace Cli {

// Unified Ryty release version. Every component (CLI, Qt6 GUI, Metal bridge,
// RytyNet bridge, web porting studio and the Apple LC_SOURCE_VERSION) reports
// this release: 2.1.0.
constexpr char RYTY_VERSION[] = "2.1.0";

enum class ConsoleMode {
    Auto,
    PS4,
    PS5
};

struct Args {
    bool skipSyscallCheck = false;
    bool skipSceModule = false;
    bool toIntel = false;
    bool writeRegistry = false;
    bool toWindows = false;
    bool toMacOs = false;
    // 2.0.0: iOS target (arm64 Mach-O guest container, .app/.ipa packaging)
    // and the iOS Simulator target (same arm64 container, LC_BUILD_VERSION
    // platform 7, no provisioning requirement).
    bool toIos = false;
    bool toIosSim = false;
    // 2.0.0 Apple options.
    bool universal = false;          // macOS: fat arm64-launcher + x86_64 guest
    bool writeBundle = false;        // macOS/iOS: write a .app bundle
    bool writeIpa = false;           // iOS device: package the .app bundle as .ipa
    bool hardenedRuntime = false;    // CS_RUNTIME flag in the CodeDirectory
    std::string macOsMinVersion = "11.0";  // LC_BUILD_VERSION MinOS (macOS)
    std::string iosMinVersion = "14.0";    // LC_BUILD_VERSION MinOS (iOS family)
    // 2.0.0 --min-os: one flag that sets the Apple deployment target for
    // whichever Apple target is selected (macOS or the iOS family). Applied
    // over macOsMinVersion/iosMinVersion at parse time.
    std::string minOsVersion;
    // 2.0.0 --interp: Linux dynamic loader baked into PT_INTERP. The default
    // is the glibc x86-64 loader; musl hosts use /lib/ld-musl-x86_64.so.1.
    std::string interpPath = "/lib64/ld-linux-x86-64.so.2";
    bool interpSpecified = false; // --interp given explicitly (validated per target)
    std::string identifier;                // CodeDirectory / bundle identifier
    std::string entitlementsPath;          // plist embedded as CSSLOT_ENTITLEMENTS
    std::string entitlementsTemplate;      // built-in preset (see Entitlements.hpp)
    std::string provisionPath;             // embedded.mobileprovision (iOS device)
    bool lazyBinding = false;
    bool autorun = false;
    bool windowsDiagnostics = false;
    bool windowsGui = false;
    bool windowsConsole = false;
    // Experimental: enables the RytyNet online-mode bridge for the ported game
    // (a <output>.rytynet marker is written next to the executable; the runtime
    // also honors RTY_NET_ENABLE=1 at any time).
    bool rytyNet = false;
    // 2.0.0: print "ryty <version>" and exit without requiring an input image.
    bool showVersion = false;
    // 2.0.0: print per-site lowering details, timings and diagnostics.
    bool verbose = false;
    // 2.0.0: re-read the finished output and validate its structure.
    bool verifyOutput = false;
    // 2.0.0: run the whole pipeline but never write the output file
    // (guest modules and reports are written; the executable is not).
    bool dryRun = false;
    ConsoleMode consoleMode = ConsoleMode::Auto;
    std::uint32_t unusedFilterLevel = 0;
    std::string inputPath;
    std::string outputPath;
    std::string reportPath;
    std::string runPath = "$ORIGIN/libs"; // empty disables DT_RUNPATH / LC_RPATH
    std::set<std::string> excludedSceModules;
    // 2.0.0: options file applied before the command line (INI, "key=value").
    std::string configPath;
    // 2.0.0: batch mode - port every ELF image in a folder.
    std::string batchDir;
    std::string batchOutDir;

    // Target system as a lowercase string ("linux", "windows", "macos",
    // "ios" or "ios-sim").
    [[nodiscard]] std::string TargetName() const {
        if (toIosSim) return "ios-sim";
        if (toIos) return "ios";
        return toMacOs ? "macos" : (toWindows ? "windows" : "linux");
    }
    [[nodiscard]] bool IsIosFamily() const { return toIos || toIosSim; }
    [[nodiscard]] bool IsAppleTarget() const { return toMacOs || IsIosFamily(); }
};

struct Args;
// Applies `configPath` (INI-style "key = value" lines) onto `args`. Keys use
// the long CLI flag names without the leading dashes (e.g. "to-intel=true",
// "rpath=$ORIGIN/libs", "unused-filter=1"). Unknown keys are rejected so a
// typo never silently changes a port. Returns the list of keys that were
// applied, in file order.
std::vector<std::string> ApplyConfigFile(const std::string& configPath, Args& args);

Args ParseArgs(int argc, char* argv[]);

// 2.0.0 Apple version helpers (see PortScheme.hpp for the resolved per-target
// porting scheme shared by the CLI, the reports and the docs). EncodeAppleVersion
// parses "11.0"/"12.3.1" into
// the LC_BUILD_VERSION encoding (major << 16 | minor << 8 | patch) and throws
// std::runtime_error on malformed input. EncodeSourceVersion turns the release
// string "2.1.0" into the LC_SOURCE_VERSION payload (2.1.0.0).
std::uint32_t EncodeAppleVersion(const std::string& version);
std::uint64_t EncodeSourceVersion(const std::string& version);
// Inverse of EncodeAppleVersion for reports and diagnostics.
std::string FormatAppleVersion(std::uint32_t encoded);

int Autorun(const std::string& absPath, bool toWindows);

// Outcome of porting one image. Batch mode aggregates these; single-file mode
// turns a failed outcome into the usual FAIL + exit code 2.
struct PortOutcome {
    bool ok = false;
    std::string consoleTarget;
    std::size_t inPlaceReplacements = 0;
    std::size_t trampolines = 0;
    std::size_t guestModules = 0;
    std::size_t externalReferences = 0;
    std::uintmax_t inputBytes = 0;
    std::uintmax_t outputBytes = 0;
    std::vector<std::string> warnings;
    std::string error;         // "FAIL: ..." reason when ok == false
    std::uint64_t failureOffset = 0;
    double elapsedMs = 0.0;
    bool verified = false;     // --verify result (only meaningful when ok)
    std::string outputPath;
    // 2.0.0 Apple extras (empty unless --bundle/--ipa were used).
    std::string appPath;
    std::string ipaPath;
};

// Ports a single image with the options in `args`. `inputPath`/`outputPath`
// override args.inputPath/args.outputPath (batch mode passes its own pair).
// Never throws for pipeline failures: they land in PortOutcome::error.
PortOutcome PortOneFile(const Args& args, const std::string& inputPath, const std::string& outputPath, bool quiet);

struct BatchEntry {
    std::string input;
    std::string output;
    std::string console;
    bool ok = false;
    bool skipped = false;      // not an ELF image / unreadable
    std::string error;
    std::size_t trampolines = 0;
    std::size_t inPlaceReplacements = 0;
    std::size_t guestModules = 0;
    std::size_t externalReferences = 0;
    double elapsedMs = 0.0;
    std::uintmax_t inputBytes = 0;
    std::uintmax_t outputBytes = 0;
    std::vector<std::string> warnings;
};

// 2.0.0 batch mode: ports every ELF image found in args.batchDir (see
// docs/user/USAGE.md). Returns the process exit code: 0 when every candidate
// ported, 1 when at least one failed, 2 when the batch could not run at all.
int RunBatch(const Args& args);

}

#endif
