#include <PortScheme.hpp>
#include <io/Json.hpp>
#include <cstdio>

namespace Cli {

namespace {

// Apple LC_BUILD_VERSION platform identifiers, mirrored in the patcher.
std::string ApplePlatformName(int target /* 1 macos, 2 ios, 7 ios-sim */) {
    switch (target) {
        case 2: return "iOS (platform 2)";
        case 7: return "iOS Simulator (platform 7)";
        default: return "macOS (platform 1)";
    }
}

std::string AppleSignature(bool hardenedRuntime) {
    return hardenedRuntime ? "ad-hoc CodeDirectory (hardened runtime)" : "ad-hoc CodeDirectory";
}

}

std::string PortScheme::Describe() const {
    std::string text = system + " -> " + format + " (" + architecture + ")";
    if (!applePlatform.empty()) {
        text += ", " + applePlatform;
        if (!minOsVersion.empty()) text += ", minOS " + minOsVersion;
    }
    if (!subsystem.empty()) text += ", " + subsystem + " subsystem";
    if (!signature.empty() && signature != "none") text += ", " + signature;
    if (!layout.empty() && layout != "flat") text += ", " + layout;
    if (!interpreter.empty()) text += ", PT_INTERP " + interpreter;
    if (nxStack) text += ", non-executable stack";
    if (!runPath.empty()) {
        text += system == "linux" ? ", DT_RUNPATH " : system == "windows" ? "" : ", LC_RPATH ";
        if (system != "windows") text += runPath;
    } else if (system == "linux") {
        text += ", no DT_RUNPATH";
    }
    if (lazyBinding) text += ", lazy PLT binding";
    return text;
}

std::vector<std::pair<std::string, std::string>> PortScheme::Fields() const {
    std::vector<std::pair<std::string, std::string>> fields;
    fields.emplace_back("Target", system);
    fields.emplace_back("Output format", format + " (" + architecture + ")");
    if (!applePlatform.empty()) {
        fields.emplace_back("Mach-O platform", applePlatform);
        if (!minOsVersion.empty()) fields.emplace_back("Deployment target", minOsVersion);
        if (!identifier.empty()) fields.emplace_back("Identifier", identifier);
    }
    if (!subsystem.empty()) fields.emplace_back("Subsystem", subsystem);
    fields.emplace_back("Signature", signature);
    fields.emplace_back("Layout", layout);
    if (!interpreter.empty()) fields.emplace_back("Interpreter", interpreter);
    if (nxStack) fields.emplace_back("Stack", "non-executable (PT_GNU_STACK)");
    fields.emplace_back("Library path", runPath.empty() ? "(none)" : runPath);
    fields.emplace_back("PLT binding", lazyBinding ? "lazy" : "bind-now");
    return fields;
}

void AppendSchemeJson(std::ostringstream& report, const PortScheme& scheme) {
    const auto quoted = [](const std::string& value) {
        return "\"" + Io::EscapeJsonString(value) + "\"";
    };
    report << "  \"scheme\": {\n";
    report << "    \"system\": " << quoted(scheme.system) << ",\n";
    report << "    \"format\": " << quoted(scheme.format) << ",\n";
    report << "    \"architecture\": " << quoted(scheme.architecture) << ",\n";
    report << "    \"signature\": " << quoted(scheme.signature) << ",\n";
    report << "    \"layout\": " << quoted(scheme.layout) << ",\n";
    if (!scheme.interpreter.empty()) report << "    \"interpreter\": " << quoted(scheme.interpreter) << ",\n";
    if (!scheme.subsystem.empty()) report << "    \"subsystem\": " << quoted(scheme.subsystem) << ",\n";
    if (!scheme.applePlatform.empty()) {
        report << "    \"applePlatform\": " << quoted(scheme.applePlatform) << ",\n";
        report << "    \"minOsVersion\": " << quoted(scheme.minOsVersion) << ",\n";
        report << "    \"identifier\": " << (scheme.identifier.empty() ? std::string("null") : quoted(scheme.identifier)) << ",\n";
    }
    report << "    \"runPath\": " << quoted(scheme.runPath) << ",\n";
    report << "    \"universal\": " << (scheme.universal ? "true" : "false") << ",\n";
    report << "    \"nxStack\": " << (scheme.nxStack ? "true" : "false") << ",\n";
    report << "    \"lazyBinding\": " << (scheme.lazyBinding ? "true" : "false") << "\n";
    report << "  }";
}

PortScheme MakePortScheme(const Args& args) {
    PortScheme scheme;
    scheme.system = args.TargetName();
    scheme.runPath = args.runPath;
    scheme.lazyBinding = args.lazyBinding;

    if (args.toWindows) {
        scheme.format = "PE32+";
        scheme.architecture = "x86-64";
        scheme.signature = "unsigned";
        scheme.layout = "flat";
        scheme.subsystem = args.windowsConsole ? "console" : "GUI";
        return scheme;
    }

    if (args.IsAppleTarget()) {
        scheme.format = "Mach-O 64";
        scheme.applePlatform = ApplePlatformName(args.toIos ? 2 : (args.toIosSim ? 7 : 1));
        scheme.minOsVersion = args.IsIosFamily() ? args.iosMinVersion : args.macOsMinVersion;
        scheme.signature = AppleSignature(args.hardenedRuntime);
        scheme.identifier = args.identifier; // may be empty: derived from the output name
        if (args.toIosSim) scheme.architecture = "arm64";
        else if (args.toIos) scheme.architecture = "arm64";
        else scheme.architecture = args.universal ? "x86_64 (+ arm64 launcher)" : "x86_64";
        scheme.universal = args.universal && !args.IsIosFamily();
        if (args.writeBundle) scheme.layout = args.toIos && args.writeIpa ? ".app bundle + .ipa" : ".app bundle";
        else scheme.layout = "flat";
        return scheme;
    }

    scheme.format = "SysV ELF64";
    scheme.architecture = "x86-64";
    scheme.signature = "none";
    scheme.layout = "flat";
    scheme.interpreter = args.interpPath;
    scheme.nxStack = true;
    return scheme;
}

bool IsValidAppleIdentifier(const std::string& identifier) {
    if (identifier.empty()) return false;
    if (identifier.front() == '.' || identifier.front() == '-' || identifier.back() == '.' || identifier.back() == '-')
        return false;
    bool afterDot = true; // a segment must not start with a hyphen either
    for (const char character : identifier) {
        if (character == '.') {
            if (afterDot) return false; // empty segment or leading dot
            afterDot = true;
            continue;
        }
        const bool letterOrDigit = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        if (!letterOrDigit && character != '-') return false;
        if (afterDot && character == '-') return false; // segment must start with a letter or digit
        afterDot = false;
    }
    return !afterDot; // no trailing dot
}

std::string MakeDefaultAppleIdentifier(const std::string& executableName) {
    std::string identifier = "com.ryty.port." + executableName;
    std::string sanitized;
    for (const char character : identifier) {
        const bool letterOrDigit = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        if (letterOrDigit || character == '.') sanitized += character;
        else sanitized += '-';
    }
    // Collapse hyphen runs and trim hyphens around dots so the result always
    // passes IsValidAppleIdentifier.
    std::string collapsed;
    for (const char character : sanitized) {
        if (character == '-' && !collapsed.empty() && (collapsed.back() == '-' || collapsed.back() == '.')) continue;
        collapsed += character;
    }
    while (!collapsed.empty() && collapsed.back() == '-') collapsed.pop_back();
    if (collapsed.empty() || collapsed.back() == '.') collapsed += 'x';
    return collapsed;
}

}
