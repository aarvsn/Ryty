#include <Cli.hpp>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;
#endif

namespace Cli {

int Autorun(const std::string& absPath, bool toWindows) {
    if (!toWindows) {
        std::filesystem::permissions(absPath,
            std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add);
    }

#if defined(_WIN32)
    const auto executablePath = std::filesystem::path(absPath).wstring();
    const auto quotedPath = L"\"" + executablePath + L"\"";
    const wchar_t* arguments[] = {quotedPath.c_str(), nullptr};
    const auto result = _wspawnv(_P_WAIT, executablePath.c_str(), arguments);
    if (result == -1)
        throw std::system_error(errno, std::generic_category(), "Cannot launch executable: " + absPath);
    const int exitCode = static_cast<int>(result);
#else
    pid_t child;
    char* arguments[] = {const_cast<char*>(absPath.c_str()), nullptr};
    const int launchError = posix_spawn(&child, absPath.c_str(), nullptr, nullptr, arguments, environ);
    if (launchError != 0)
        throw std::system_error(launchError, std::generic_category(), "Cannot launch executable: " + absPath);
    int status;
    while (waitpid(child, &status, 0) == -1) {
        if (errno != EINTR)
            throw std::system_error(errno, std::generic_category(), "Cannot wait for executable: " + absPath);
    }
    const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif

    std::cout << "\nExit code: " << exitCode << '\n';

    std::cout << "\nPress Enter to exit...\n";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    return exitCode;
}

}
