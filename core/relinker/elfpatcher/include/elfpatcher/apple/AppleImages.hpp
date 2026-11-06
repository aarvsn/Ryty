#ifndef ELFPATCHER_APPLE_APPLEIMAGES_HPP
#define ELFPATCHER_APPLE_APPLEIMAGES_HPP

#include <elfpatcher/apple/MachOOptions.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Elfpatcher::Apple {

// 2.0.0 arm64 image builders.
//
// The guest images Ryty ports are x86-64 machine code, so an Apple Silicon
// process must run them through Rosetta 2 (macOS) or the A64 translation
// runtime (iOS). Both need a native arm64 Mach-O container, built here:
//
//   * MakeArm64LauncherSlice - a tiny arm64 executable that re-execs the
//     x86_64 slice of its own fat binary through `/usr/bin/arch -x86_64`
//     (the same posix_spawn binary-preference trick arch(1) uses). Used as
//     the arm64 slice of a `--universal` macOS port. Uses raw BSD syscalls,
//     so the slice needs no dylibs at all.
//   * MakeIosGuestImage - an iOS arm64 executable that embeds the ported
//     guest image as a read-only __GUEST segment (the payload for the A64
//     translation runtime) and prints a diagnostic when that runtime is
//     missing. 16 KB segment alignment, mandatory ad-hoc signature.
//   * MakeUniversalBinary - wraps two thin slices in a fat (universal)
//     Mach-O container (0xCAFEBABE).

std::vector<std::uint8_t> MakeArm64LauncherSlice(const MachOOptions& options, const std::string& version);

std::vector<std::uint8_t> MakeIosGuestImage(const std::vector<std::uint8_t>& guestPayload,
                                            const MachOOptions& options, const std::string& version);

std::vector<std::uint8_t> MakeUniversalBinary(const std::vector<std::uint8_t>& firstSlice,
                                              const std::vector<std::uint8_t>& secondSlice);

}

#endif
