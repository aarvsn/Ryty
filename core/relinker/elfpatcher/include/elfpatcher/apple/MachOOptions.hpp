#ifndef ELFPATCHER_APPLE_MACHOOPTIONS_HPP
#define ELFPATCHER_APPLE_MACHOOPTIONS_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace Elfpatcher::Apple {

// 2.0.0: options for the Apple Mach-O writers (macOS, iOS and iOS Simulator
// targets). The defaults reproduce the original macOS output so existing ports
// keep bit-for-bit compatible headers apart from the release version fields.
struct MachOOptions {
    enum class Platform {
        MacOs,
        Ios,
        IosSimulator
    };

    Platform platform = Platform::MacOs;
    // macOS only: emit a universal (fat) binary whose arm64 slice is a native
    // Rosetta launcher and whose x86_64 slice is the ported guest image.
    bool universal = false;
    // Encoded Apple version (major << 16 | minor << 8 | patch).
    std::uint32_t minOs = 0x000B0000; // macOS 11.0 / overridden per target
    std::uint32_t sdk = 0x000E0000;   // SDK 14.0 (macOS) / 18.0 (iOS)
    // CodeDirectory identifier. Empty selects the per-target default
    // ("ryty.bundle" for bare macOS ports, "com.ryty.port.<name>" for bundles).
    std::string identifier;
    // Raw entitlements plist bytes embedded as CSSLOT_ENTITLEMENTS. Empty means
    // no entitlements slice (2.0.0 behaviour).
    std::vector<std::uint8_t> entitlements;
    // Sets the CS_RUNTIME flag in the CodeDirectory (hardened runtime).
    bool hardenedRuntime = false;
    // LC_SOURCE_VERSION payload, e.g. 0x0002010000000000 == 2.1.0.0.
    std::uint64_t sourceVersion = 0x0002010000000000ULL;

    [[nodiscard]] bool IsIosFamily() const { return platform != Platform::MacOs; }

    // The platform field of LC_BUILD_VERSION: 1 = macOS, 2 = iOS,
    // 7 = iOS Simulator (PLATFORM_IOSSIMULATOR in the loader headers).
    [[nodiscard]] std::uint32_t EncodedPlatform() const {
        switch (platform) {
            case Platform::Ios: return 2;
            case Platform::IosSimulator: return 7;
            case Platform::MacOs: break;
        }
        return 1;
    }
};

}

#endif
