#include <Cli.hpp>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc == 1) return 37;
    if (argc != 2) return 1;
    try {
#if defined(_WIN32)
        return Cli::Autorun(argv[1], true);
#else
        return Cli::Autorun(argv[1], false);
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
