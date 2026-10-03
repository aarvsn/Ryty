#ifndef ELFPATCHER_MACOS_MACOSELFPATCHER_HPP
#define ELFPATCHER_MACOS_MACOSELFPATCHER_HPP

#include <elfpatcher/general/IElfPatcher.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Elfpatcher::MacOs {

class MacOsElfPatcher : public IElfPatcher {
public:
    MacOsElfPatcher();

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
};

}

#endif
