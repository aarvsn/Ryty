#ifndef CODEGEN_IAMD64ONLYCONVERTER_HPP
#define CODEGEN_IAMD64ONLYCONVERTER_HPP

#include <codegen/CodegenTypes.hpp>
#include <domain/Types.hpp>
#include <memory>
#include <vector>

namespace Codegen {

class IAmd64OnlyConverter {
public:
    virtual ~IAmd64OnlyConverter() = default;

    [[nodiscard]] virtual ConvertResult Convert(
        std::vector<std::uint8_t> fileBytes,
        const std::vector<Domain::ProgramHeader>& codeSegments
    ) const = 0;

    // Lists every AMD-only instruction the converter recognizes that is still
    // present in the code segments of `fileBytes`. Convert() is expected to
    // leave no such instruction behind, so a caller runs this on the converted
    // image and treats a non-empty result as a hard failure.
    [[nodiscard]] virtual std::vector<Amd64OnlyResidualSite> ScanResidual(
        const std::vector<std::uint8_t>& fileBytes,
        const std::vector<Domain::ProgramHeader>& codeSegments
    ) const = 0;
};

std::unique_ptr<IAmd64OnlyConverter> MakeAmd64OnlyConverter();

}

#endif
