#ifndef CODEGEN_X86_SHAOPERANDS_HPP
#define CODEGEN_X86_SHAOPERANDS_HPP

#include <cstddef>
#include <cstdint>

namespace Codegen {

enum class ShaOperation : std::uint8_t {
    Sha1Nexte,
    Sha1Msg1,
    Sha1Msg2,
    Sha1Rnds4,
    Sha256Rnds2,
    Sha256Msg1,
    Sha256Msg2
};

struct ShaOperands {
    ShaOperation Operation;
    std::uint8_t Destination;
    std::uint8_t Source;
    std::uint8_t Immediate;
};

[[nodiscard]] ShaOperands DecodeSha(const std::uint8_t* data, std::size_t length);
[[nodiscard]] bool IsShaOpcode(std::uint8_t escape, std::uint8_t opcode);

}

#endif
