#ifndef CORE_RELINKER_CLI_INCLUDE_PORTSCHEME_HPP
#define CORE_RELINKER_CLI_INCLUDE_PORTSCHEME_HPP

#include <Cli.hpp>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Cli {

// 2.0.0 porting scheme. A PortScheme is the resolved, per-target description
// of exactly how one port is carried out: the output format and architecture,
// the loader entry (interpreter / subsystem / Mach-O platform), the signature
// applied to the image, the on-disk layout and the library search path. It is
// the single source of truth shared by the CLI output, the JSON port report,
// the batch runner and the docs, so every front end describes a port the same
// way.
struct PortScheme {
    std::string system;        // "linux", "windows", "macos", "ios", "ios-sim"
    std::string format;        // "SysV ELF64", "PE32+", "Mach-O 64"
    std::string architecture;  // "x86-64", "x86_64 (+ arm64 launcher)", "arm64"
    std::string signature;     // "none", "unsigned", "ad-hoc CodeDirectory", ...
    std::string layout;        // "flat", ".app bundle", ".app bundle + .ipa"
    std::string interpreter;   // Linux only: PT_INTERP loader path
    std::string subsystem;     // Windows only: "GUI" or "console"
    std::string applePlatform; // Apple only: "macOS (platform 1)", ...
    std::string minOsVersion;  // Apple only: deployment target, e.g. "11.0"
    std::string identifier;    // Apple only: CodeDirectory / bundle id ("" if deferred)
    std::string runPath;       // library search path baked into the image
    bool universal = false;    // macOS fat binary (arm64 launcher + x86_64 guest)
    bool nxStack = false;      // Linux: PT_GNU_STACK emitted (non-executable stack)
    bool lazyBinding = false;  // PLT binding mode reported alongside the scheme
    bool toIntel = false;      // AMD-only instructions lowered for Intel hosts

    // One-line human description, e.g.
    //   "macOS -> Mach-O 64 (x86_64), platform 1 (macOS), minOS 11.0,
    //    ad-hoc CodeDirectory, .app bundle".
    [[nodiscard]] std::string Describe() const;

    // Ordered (label, value) pairs for the GUI scheme label and the web
    // studio scheme card.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> Fields() const;
};

// Resolves the porting scheme implied by `args`. Never throws: the arguments
// have already been validated by ParseArgs when this is called.
[[nodiscard]] PortScheme MakePortScheme(const Args& args);

// Appends the "scheme" object (without the trailing comma) to a JSON port or
// batch report so every report documents the porting scheme that produced it.
void AppendSchemeJson(std::ostringstream& report, const PortScheme& scheme);

// True when `identifier` is a syntactically valid Apple bundle identifier:
// only letters, digits, dots and hyphens, no empty dot-separated segment and
// no leading/trailing dot or hyphen. Used to reject a broken --identifier
// before any output byte is written.
[[nodiscard]] bool IsValidAppleIdentifier(const std::string& identifier);

// Derives the default bundle identifier for a ported executable name
// ("com.ryty.port.<name>") and replaces characters a bundle identifier cannot
// carry (spaces, underscores, ...) with hyphens so an automatic identifier is
// always valid. An explicit --identifier is validated, never rewritten.
[[nodiscard]] std::string MakeDefaultAppleIdentifier(const std::string& executableName);

}

#endif
