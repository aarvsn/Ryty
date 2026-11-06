#ifndef CODEGEN_X86_SHALOWERING_HPP
#define CODEGEN_X86_SHALOWERING_HPP

#include <codegen/x86/ShaOperands.hpp>
#include <codegen/x86/StubBodyBuilder.hpp>
#include <cstdint>
#include <span>

namespace Codegen {

class ShaLowering {
public:
    void EmitOutOfLine(StubBodyBuilder& body, const ShaOperands& operands) const;
    [[nodiscard]] LoweredBody LowerOutOfLine(const ShaOperands& operands, std::span<const std::uint8_t> trailing = {}) const;
};

}

#endif
