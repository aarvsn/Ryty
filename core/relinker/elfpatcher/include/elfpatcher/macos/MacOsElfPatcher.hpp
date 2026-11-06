#ifndef ELFPATCHER_MACOS_MACOSELFPATCHER_HPP
#define ELFPATCHER_MACOS_MACOSELFPATCHER_HPP

#include <elfpatcher/general/IElfPatcher.hpp>
#include <elfpatcher/apple/MachOOptions.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Elfpatcher::MacOs {

// Apple Mach-O writer (macOS and, since 2.0.0, iOS targets). The default
// options produce the classic macOS output: thin x86_64 executable, ad-hoc
// code signature, macOS 11.0 deployment target.
class MacOsElfPatcher : public IElfPatcher {
public:
    MacOsElfPatcher();

    // 2.0.0: per-port Apple options (iOS target, universal binary, identifier,
    // entitlements, minimum OS version, hardened runtime). Call before Patch().
    void SetOptions(const Apple::MachOOptions& options);

    std::vector<std::uint8_t> Patch(
        const std::vector<std::uint8_t>& sourceElf,
        const std::vector<Domain::ProgramHeader>& originalHeaders,
        const Domain::SysVDynamicSection& dynamicSection,
        std::uint64_t originalPltGotVaddr,
        const std::string& runPath,
        bool lazyBinding,
        bool dependencyDiagnostics,
        const std::vector<Codegen::TrampolineSite>& trampolines
    ) override;

private:
    Apple::MachOOptions options_;
};

}

#endif
