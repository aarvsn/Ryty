#include <Cli.hpp>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

int main(const int argc, char* argv[]) {
    Cli::Args args;
    try {
        args = Cli::ParseArgs(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }

    if (args.showVersion) {
        std::cout << "ryty " << Cli::RYTY_VERSION << "\n";
        return 0;
    }

    try {
        if (!args.batchDir.empty()) return Cli::RunBatch(args);

        const auto outcome = Cli::PortOneFile(args, args.inputPath, args.outputPath, false);
        if (!outcome.ok) {
            std::cerr << "FAIL: " << outcome.error;
            if (outcome.failureOffset != 0) std::cerr << " (offset 0x" << std::hex << outcome.failureOffset << std::dec << ")";
            std::cerr << "\n";
            return 2;
        }
        if (args.verbose) {
            std::cout << "Elapsed: " << std::fixed << std::setprecision(1) << outcome.elapsedMs << " ms\n";
            std::cout << "Output size: " << outcome.outputBytes << " bytes\n";
        }
        if (args.autorun) return Cli::Autorun(std::filesystem::absolute(args.outputPath).string(), args.toWindows);

    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 2;
    }

    return 0;
}
