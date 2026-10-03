#include <Cli.hpp>
#include <domain/Types.hpp>
#include <io/FileReader.hpp>
#include <io/FileWriter.hpp>
#include <elfpatcher/linux/LinuxElfPatcher.hpp>
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
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string escapeJson(const std::string& value) {
    std::string result;
    for (const char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20) {
                std::ostringstream escape;
                escape << "\\u" << std::hex << static_cast<int>(character);
                result += escape.str();
            } else {
                result += character;
            }
        }
    }
    return result;
}

}

int main(const int argc, char* argv[]) {
    Cli::Args args;
    try {
        args = Cli::ParseArgs(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }

    try {
        std::vector<Codegen::TrampolineSite> trampolines;
        std::vector<Codegen::Amd64OnlySubstitutionReport> substitutionReports;
        std::vector<std::string> warnings;
        auto extension = std::filesystem::path(args.outputPath).extension().string();
        for (auto& character : extension) if (character >= 'A' && character <= 'Z') character = static_cast<char>(character + ('a' - 'A'));
        if (!args.toWindows && extension == ".exe") { std::cerr << "WARNING: Output filename ends with .exe, but --windows was not specified. The output will be a Linux ELF executable.\n"; warnings.push_back("Output filename ends with .exe but --windows was not specified"); }
        Io::FileReader fileReader;
        Io::FileWriter fileWriter;

        auto sourceBytes = fileReader.Read(args.inputPath);
        const std::string absPath = std::filesystem::absolute(args.outputPath).string();

        if (args.toIntel) {
            const auto codeSegments = Relinker::ElfReader(sourceBytes).ReadCodeSegments();
            auto converted = Codegen::MakeAmd64OnlyConverter()->Convert(std::move(sourceBytes), codeSegments);
            sourceBytes = std::move(converted.Bytes);
            trampolines = std::move(converted.Trampolines);
            substitutionReports = std::move(converted.Reports);
            for (const auto& report : substitutionReports)
                std::cout << "Intel substitution: " << report.InstructionName << " at 0x" << std::hex << report.Offset << std::dec << " (" << report.OriginalLength << " bytes) -> " << (report.Lowering == Codegen::Amd64OnlyLowering::InPlace ? "in place " : "stub ") << report.ReplacementLength << " bytes\n";
            std::cout << "Intel conversion: " << converted.ReplacedCount << " in place, " << trampolines.size() << " stubs\n";
        }

        auto elfReader = std::make_shared<Relinker::ElfReader>(sourceBytes);
        const std::shared_ptr<Relinker::ISyscallScanner> syscallScanner = args.skipSyscallCheck ? Relinker::MakeNullSyscallScanner() : Relinker::MakeSyscallScanner();

        const auto pipeline = std::make_shared<Relinker::RelinkerPipeline>(
            elfReader,
            syscallScanner,
            Relinker::MakeCallSiteResolver(),
            std::make_shared<Relinker::ValidationPolicy>(),
            std::make_shared<Relinker::SysVDynamicSectionBuilder>(),
            args.unusedFilterLevel == 2 ? Relinker::MakeStrictUnusedNidFilter() : Relinker::MakeUnusedNidFilter(),
            args.unusedFilterLevel
        );

        std::cout << "System: " << (args.toWindows ? "Windows" : "Linux") << "; unused-filter=" << args.unusedFilterLevel << "\n";
        std::cout << "sce_module/sce_modules processing: " << (args.skipSceModule ? "disabled (--skip-sce-module)" : "enabled") << '\n';
        for (const auto& name : args.excludedSceModules) std::cout << "sce_module excluded: " << name << '\n';
        auto result = pipeline->Relink(sourceBytes);
        for (const auto& patch : result.Patches) {
            if (patch.Offset > sourceBytes.size() || patch.Bytes.size() > sourceBytes.size() - patch.Offset)
                throw Domain::RelinkerException("Relinker patch exceeds source image", patch.Offset);
            for (std::size_t index = 0; index < patch.Bytes.size(); ++index) sourceBytes[patch.Offset + index] = patch.Bytes[index];
        }

        std::vector<Relinker::GuestArtifact> guestArtifacts;
        if (!args.skipSceModule) {
            guestArtifacts = Relinker::GuestModuleBuilder().Build(args.inputPath, absPath, result.DynamicSection, args.toWindows, args.toIntel, *syscallScanner, args.lazyBinding, args.runPath, args.excludedSceModules);
        }

        if (args.writeRegistry) {
            const std::filesystem::path outFsPath(absPath);
            const std::string registryPath = (outFsPath.parent_path() / (outFsPath.stem().string() + ".registry.json")).string();
            fileWriter.Write(registryPath, std::make_shared<Relinker::CallRegistryWriter>()->WriteCallRegistry(result.RegistryEntries));
        }

        auto byteWriter = std::make_shared<Io::ByteWriter>();

        std::shared_ptr<Elfpatcher::IElfPatcher> patcher;
        if (args.toWindows) {
            patcher = std::make_shared<Elfpatcher::Windows::WindowsPePatcher>(args.windowsGui);
        } else {
            patcher = std::make_shared<Elfpatcher::Linux::LinuxElfPatcher>(
                std::make_shared<Elfpatcher::EntryStubBuilder>(),
                std::make_shared<Elfpatcher::ProgramHeaderLayoutBuilder>(
                    std::make_shared<Elfpatcher::SegmentFilter>(),
                    byteWriter
                ),
                std::make_shared<Elfpatcher::SectionHeaderTableBuilder>(byteWriter),
                byteWriter
            );
        }

        const auto executableBytes = patcher->Patch(sourceBytes, result.OriginalHeaders, result.DynamicSection, result.OriginalPltGotVaddr, args.runPath, args.lazyBinding, args.windowsDiagnostics, trampolines);
        for (const auto& artifact : guestArtifacts) {
            std::filesystem::create_directories(artifact.Path.parent_path());
            fileWriter.Write(artifact.Path.string(), artifact.Bytes);
            std::cout << "Guest module: " << artifact.Path.string() << '\n';
        }
        fileWriter.Write(absPath, executableBytes);
        std::cout << "External prx references: " << result.RegistryEntries.size() << "\nOutput file: " << absPath << '\n';
        std::cout << "Expected runtime layout (relative to the output executable):\n"
                  << std::filesystem::path(absPath).filename().string() << "\n"
                  << "libs/\n    *.prx\napp0/\n    <game resources>\n";
        if (!guestArtifacts.empty()) {
            std::cout << "    " << guestArtifacts.front().Path.parent_path().filename().string() << "/\n";
            for (const auto& artifact : guestArtifacts) std::cout << "        " << artifact.Path.filename().string() << '\n';
        }
        std::cout << "Game resources and system libraries must be placed in this layout separately.\n";
        if (args.runPath != "$ORIGIN/libs") std::cout << "Custom library search path (--rpath): " << args.runPath << '\n';

        if (!args.reportPath.empty()) {
            std::ostringstream report;
            report << "{\n";
            report << "  \"schema\": \"ryty-port-report/1\",\n";
            report << "  \"input\": \"" << escapeJson(args.inputPath) << "\",\n";
            report << "  \"output\": \"" << escapeJson(absPath) << "\",\n";
            report << "  \"system\": \"" << (args.toWindows ? "windows" : "linux") << "\",\n";
            report << "  \"options\": {\n";
            report << "    \"toIntel\": " << (args.toIntel ? "true" : "false") << ",\n";
            report << "    \"unusedFilterLevel\": " << args.unusedFilterLevel << ",\n";
            report << "    \"skipSceModule\": " << (args.skipSceModule ? "true" : "false") << ",\n";
            report << "    \"skipSyscallCheck\": " << (args.skipSyscallCheck ? "true" : "false") << ",\n";
            report << "    \"lazyBinding\": " << (args.lazyBinding ? "true" : "false") << ",\n";
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
            report << "    \"inPlaceReplacements\": " << (substitutionReports.size() - trampolines.size()) << ",\n";
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
            for (std::size_t index = 0; index < warnings.size(); ++index) {
                report << "    \"" << escapeJson(warnings[index]) << "\"";
                report << (index + 1 < warnings.size() ? ",\n" : "\n");
            }
            report << "  ]\n";
            report << "}\n";
            fileWriter.Write(args.reportPath, report.str());
            std::cout << "Porting report: " << args.reportPath << '\n';
        }

        if (args.autorun) return Cli::Autorun(absPath, args.toWindows);

    } catch (const Domain::RelinkerException& e) {
        std::cerr << "FAIL: " << e.what();
        if (e.FailureOffset != 0) std::cerr << " (offset 0x" << std::hex << e.FailureOffset << ")";
        std::cerr << "\n";
        return 2;
    } catch (const Codegen::CodegenException& e) {
        std::cerr << "FAIL: " << e.what();
        if (e.FailureOffset != 0) std::cerr << " (offset 0x" << std::hex << e.FailureOffset << ")";
        std::cerr << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 2;
    }

    return 0;
}
