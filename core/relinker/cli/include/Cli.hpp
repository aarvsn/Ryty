#ifndef CORE_RELINKER_CLI_INCLUDE_CLI_HPP
#define CORE_RELINKER_CLI_INCLUDE_CLI_HPP

#include <set>
#include <string>
#include <cstdint>

namespace Cli {

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
    bool lazyBinding = false;
    bool autorun = false;
    bool windowsDiagnostics = false;
    bool windowsGui = false;
    bool windowsConsole = false;
    // Experimental: enables the RytyNet online-mode bridge for the ported game
    // (a <output>.rytynet marker is written next to the executable; the runtime
    // also honors RTY_NET_ENABLE=1 at any time).
    bool rytyNet = false;
    ConsoleMode consoleMode = ConsoleMode::Auto;
    std::uint32_t unusedFilterLevel = 0;
    std::string inputPath;
    std::string outputPath;
    std::string reportPath;
    std::string runPath = "$ORIGIN/libs";
    std::set<std::string> excludedSceModules;
};

Args ParseArgs(int argc, char* argv[]);

int Autorun(const std::string& absPath, bool toWindows);

}

#endif
