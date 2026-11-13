#ifndef CORE_RELINKER_CLI_INCLUDE_HOSTCPU_HPP
#define CORE_RELINKER_CLI_INCLUDE_HOSTCPU_HPP

#include <cstring>
#include <string>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace Cli {

// Host x86-64 CPU vendor from CPUID leaf 0 ("GenuineIntel", "AuthenticAMD",
// ...). Empty on non-x86 hosts or when CPUID is unavailable. Cached because the
// value never changes and the CLI asks for it on every run.
inline const std::string& HostCpuVendor() {
    static const std::string vendor = [] {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        int registers[4] = {0, 0, 0, 0};
        __cpuid(registers, 0);
        char text[13] = {};
        std::memcpy(text + 0, &registers[1], 4);
        std::memcpy(text + 4, &registers[3], 4);
        std::memcpy(text + 8, &registers[2], 4);
        return std::string(text);
#elif defined(__x86_64__) || defined(__i386__)
        unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
        if (__get_cpuid(0, &eax, &ebx, &ecx, &edx) == 0) return std::string();
        char text[13] = {};
        std::memcpy(text + 0, &ebx, 4);
        std::memcpy(text + 4, &edx, 4);
        std::memcpy(text + 8, &ecx, 4);
        return std::string(text);
#else
        return std::string();
#endif
    }();
    return vendor;
}

// True when the host CPU is an Intel part. The Intel conversion only matters
// on x86 hosts; other architectures report false.
inline bool HostIsIntel() {
    return HostCpuVendor() == "GenuineIntel";
}

}

#endif
