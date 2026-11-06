#include <Cli.hpp>
#include <PortScheme.hpp>
#include <io/FileReader.hpp>
#include <io/FileWriter.hpp>
#include <io/Json.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string escapeJson(const std::string& value) {
    return Io::EscapeJsonString(value);
}

// True when the file begins with the ELF magic. Anything else in the batch
// directory (documents, archives, reports, markers) is skipped with a note.
bool LooksLikeElf(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    char magic[4]{};
    stream.read(magic, 4);
    return stream.gcount() == 4 && static_cast<unsigned char>(magic[0]) == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

// Per-file output name: the input stem plus a platform-appropriate extension,
// inside its own folder so several ports never share or overwrite files.
std::string OutputNameFor(const std::filesystem::path& input, bool toWindows) {
    std::string name = input.stem().string();
    if (name.empty()) name = input.filename().string();
    if (toWindows) return name + ".exe";
    return name + ".elf";
}

}

namespace Cli {

int RunBatch(const Args& args) {
    const std::filesystem::path batchDir = std::filesystem::absolute(std::filesystem::path(args.batchDir));
    if (!std::filesystem::is_directory(batchDir)) {
        std::cerr << "FAIL: --batch requires an existing directory: " << batchDir.string() << "\n";
        return 2;
    }
    if (!args.reportPath.empty()) {
        std::cerr << "FAIL: --report cannot be combined with --batch; per-file reports are written into the batch output directory automatically\n";
        return 2;
    }

    const std::filesystem::path outDir = std::filesystem::absolute(std::filesystem::path(args.batchOutDir.empty() ? (batchDir / "ported").string() : args.batchOutDir));
    std::error_code createError;
    std::filesystem::create_directories(outDir, createError);
    if (createError) {
        std::cerr << "FAIL: cannot create the batch output directory: " << outDir.string() << " (" << createError.message() << ")\n";
        return 2;
    }

    std::cout << "Ryty batch " << RYTY_VERSION << ": scanning " << batchDir.string() << "\n";
    std::cout << "System: " << args.TargetName() << "; output directory: " << outDir.string() << "\n";
    // 2.0.0 porting scheme: the batch prints the resolved scheme once for the
    // whole run; every per-file report repeats it under "scheme".
    std::cout << "Porting scheme: " << MakePortScheme(args).Describe() << "\n";

    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(batchDir)) {
        if (!entry.is_regular_file()) continue;
        const auto path = entry.path();
        const auto name = path.filename().string();
        // Metadata this tool itself writes next to a ported executable.
        if (name.ends_with(".registry.json") || name.ends_with(".rytynet") || name.ends_with(".report.json")) continue;
        if (LooksLikeElf(path)) candidates.push_back(path);
    }
    std::sort(candidates.begin(), candidates.end());

    if (candidates.empty()) {
        std::cout << "No ELF images found in the batch directory; nothing to do.\n";
        return 0;
    }

    std::cout << "Found " << candidates.size() << " ELF image(s)\n\n";

    Args perFile = args;
    perFile.reportPath.clear();
    perFile.batchDir.clear();

    std::vector<BatchEntry> entries;
    for (const auto& input : candidates) {
        BatchEntry entry;
        entry.input = input.string();
        entry.inputBytes = std::filesystem::file_size(input);

        const auto folder = outDir / input.stem().string();
        const auto output = folder / OutputNameFor(input, args.toWindows);
        entry.output = output.string();

        std::cout << "[" << (entries.size() + 1) << "/" << candidates.size() << "] " << input.filename().string() << " -> " << std::filesystem::path(entry.output).parent_path().filename().string() << "/" << output.filename().string() << '\n';

        Args fileArgs = perFile;
        fileArgs.reportPath = (folder / (output.filename().string() + ".report.json")).string();
        std::filesystem::create_directories(folder);
        const auto outcome = PortOneFile(fileArgs, input.string(), output.string(), true);
        entry.ok = outcome.ok;
        entry.console = outcome.consoleTarget;
        entry.error = outcome.error;
        entry.trampolines = outcome.trampolines;
        entry.inPlaceReplacements = outcome.inPlaceReplacements;
        entry.guestModules = outcome.guestModules;
        entry.externalReferences = outcome.externalReferences;
        entry.elapsedMs = outcome.elapsedMs;
        entry.outputBytes = outcome.outputBytes;
        entry.warnings = outcome.warnings;
        if (outcome.ok) {
            std::cout << "    OK: " << outcome.consoleTarget << ", " << outcome.trampolines << " stubs, " << outcome.elapsedMs << " ms\n";
        } else {
            std::cout << "    FAILED: " << outcome.error << "\n";
        }
        entries.push_back(std::move(entry));
    }

    // Summary table
    const std::size_t nameWidth = std::min<std::size_t>(48, std::max<std::size_t>(12, [&] {
        std::size_t width = 12;
        for (const auto& entry : entries) width = std::max(width, std::filesystem::path(entry.input).filename().string().size());
        return width;
    }()));

    std::cout << "\n== Batch summary " << std::string(std::max<std::size_t>(2, 66 - nameWidth), '=') << "\n";
    std::cout << "  " << std::left << std::setw(static_cast<int>(nameWidth)) << "File" << "  "
              << std::setw(7) << "Console" << "  "
              << std::setw(7) << "Result" << "  "
              << std::right << std::setw(9) << "Stubs" << "  "
              << std::setw(9) << "Guests" << "  "
              << std::setw(10) << "Time (ms)" << '\n';
    std::cout << "  " << std::string(nameWidth, '-') << "  " << std::string(7, '-') << "  " << std::string(7, '-') << "  "
              << std::string(9, '-') << "  " << std::string(9, '-') << "  " << std::string(10, '-') << '\n';
    for (const auto& entry : entries) {
        auto name = std::filesystem::path(entry.input).filename().string();
        if (name.size() > nameWidth) name = name.substr(0, nameWidth - 3) + "...";
        std::cout << "  " << std::left << std::setw(static_cast<int>(nameWidth)) << name << "  "
                  << std::setw(7) << (entry.console.empty() ? "-" : entry.console) << "  "
                  << std::setw(7) << (entry.ok ? "OK" : "FAILED") << "  "
                  << std::right << std::setw(9) << entry.trampolines << "  "
                  << std::setw(9) << entry.guestModules << "  "
                  << std::setw(10) << std::fixed << std::setprecision(1) << entry.elapsedMs << '\n';
        if (!entry.ok && !entry.error.empty()) std::cout << "      reason: " << entry.error << '\n';
    }

    const std::size_t ported = static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(), [](const BatchEntry& entry) { return entry.ok; }));
    const std::size_t failed = entries.size() - ported;
    std::cout << ported << " ported, " << failed << " failed, " << entries.size() << " total\n";

    // Batch report JSON
    std::ostringstream report;
    report << "{\n";
    report << "  \"schema\": \"ryty-batch-report/1\",\n";
    report << "  \"tool\": \"ryty\",\n";
    report << "  \"version\": \"" << RYTY_VERSION << "\",\n";
    AppendSchemeJson(report, MakePortScheme(args));
    report << ",\n";
    report << "  \"inputDirectory\": \"" << escapeJson(batchDir.string()) << "\",\n";
    report << "  \"outputDirectory\": \"" << escapeJson(outDir.string()) << "\",\n";
    report << "  \"system\": \"" << args.TargetName() << "\",\n";
    report << "  \"options\": {\n";
    report << "    \"toIntel\": " << (args.toIntel ? "true" : "false") << ",\n";
    report << "    \"unusedFilterLevel\": " << args.unusedFilterLevel << ",\n";
    report << "    \"skipSceModule\": " << (args.skipSceModule ? "true" : "false") << ",\n";
    report << "    \"skipSyscallCheck\": " << (args.skipSyscallCheck ? "true" : "false") << ",\n";
    report << "    \"lazyBinding\": " << (args.lazyBinding ? "true" : "false") << ",\n";
    report << "    \"rytyNet\": " << (args.rytyNet ? "true" : "false") << ",\n";
    report << "    \"rpath\": \"" << escapeJson(args.runPath) << "\"\n";
    report << "  },\n";
    report << "  \"entries\": [\n";
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        report << "    {\n";
        report << "      \"input\": \"" << escapeJson(entry.input) << "\",\n";
        report << "      \"output\": \"" << escapeJson(entry.output) << "\",\n";
        report << "      \"console\": \"" << escapeJson(entry.console) << "\",\n";
        report << "      \"ok\": " << (entry.ok ? "true" : "false") << ",\n";
        report << "      \"error\": \"" << escapeJson(entry.error) << "\",\n";
        report << "      \"inputBytes\": " << entry.inputBytes << ",\n";
        report << "      \"outputBytes\": " << entry.outputBytes << ",\n";
        report << "      \"elapsedMs\": " << std::fixed << std::setprecision(1) << entry.elapsedMs << ",\n";
        report << "      \"trampolines\": " << entry.trampolines << ",\n";
        report << "      \"inPlaceReplacements\": " << entry.inPlaceReplacements << ",\n";
        report << "      \"guestModules\": " << entry.guestModules << ",\n";
        report << "      \"externalReferences\": " << entry.externalReferences << ",\n";
        report << "      \"warnings\": [";
        for (std::size_t warning = 0; warning < entry.warnings.size(); ++warning) {
            report << (warning == 0 ? "\n" : ",\n") << "        \"" << escapeJson(entry.warnings[warning]) << "\"";
        }
        report << (entry.warnings.empty() ? "" : "\n      ") << "]\n";
        report << "    }" << (index + 1 < entries.size() ? "," : "") << "\n";
    }
    report << "  ],\n";
    report << "  \"summary\": {\n";
    report << "    \"total\": " << entries.size() << ",\n";
    report << "    \"ported\": " << ported << ",\n";
    report << "    \"failed\": " << failed << "\n";
    report << "  }\n";
    report << "}\n";

    const std::string reportPath = (outDir / "batch-report.json").string();
    Io::FileWriter writer;
    writer.Write(reportPath, report.str());
    std::cout << "Batch report: " << reportPath << '\n';

    return failed == 0 ? 0 : 1;
}

}
