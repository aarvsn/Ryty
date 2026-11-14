#ifndef CODEGEN_X86_AMD64ONLYSUBSTITUTIONTYPES_HPP
#define CODEGEN_X86_AMD64ONLYSUBSTITUTIONTYPES_HPP

#include <domain/Types.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Codegen {

enum class Amd64OnlyLowering : std::uint8_t {
    InPlace,
    Trampoline,
    Unsupported
};

struct Amd64OnlyMatch {
    std::string InstructionName;
    std::size_t Length;
    Amd64OnlyLowering Lowering;
    std::vector<std::uint8_t> ReplacementBytes;
    std::vector<std::uint8_t> StubBody;
    std::size_t ReturnBranchOffset;
};

struct Amd64OnlySubstitutionReport {
    std::string InstructionName;
    Domain::FileByteOffset Offset;
    std::size_t OriginalLength;
    std::size_t ReplacementLength;
    Amd64OnlyLowering Lowering;
};

// An AMD-only instruction still present in an image after conversion. The
// Intel conversion is meant to leave none behind; a non-empty residual list is
// what the CLI turns into a failed port.
struct Amd64OnlyResidualSite {
    std::string InstructionName;
    Domain::FileByteOffset Offset;
};

}

#endif
