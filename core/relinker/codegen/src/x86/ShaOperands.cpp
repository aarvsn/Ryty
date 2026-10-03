#include <codegen/x86/ShaOperands.hpp>
#include <codegen/x86/X64OpcodeConstants.hpp>
#include <codegen/CodegenException.hpp>

namespace Codegen {

using namespace X64OpcodeConstants;

bool IsShaOpcode(const std::uint8_t escape, const std::uint8_t opcode) {
    if (escape == ThreeByteEscape38)
        return opcode >= 0xC8 && opcode <= 0xCD;
    if (escape == ThreeByteEscape3A)
        return opcode == 0xCC;
    return false;
}

ShaOperands DecodeSha(const std::uint8_t* data, const std::size_t length) {
    std::size_t pos = 0;
    std::uint8_t rex = 0;
    std::uint8_t escape = 0;

    while (pos < length) {
        const std::uint8_t b = data[pos];
        if (b >= RexMin && b <= RexMax) {
            rex = b;
            pos += 1;
            continue;
        }
        if (b == PrefixOperandSize || b == PrefixRepne || b == PrefixRep) {
            throw CodegenException("Not a SHA instruction");
        }
        if (b != PrefixLock && b != PrefixAddressSize &&
            b != PrefixSegCs && b != PrefixSegSs && b != PrefixSegDs &&
            b != PrefixSegEs && b != PrefixSegFs && b != PrefixSegGs) {
            break;
        }
        rex = 0;
        pos += 1;
    }

    if (pos + 4 > length || data[pos] != TwoByteOpcodeEscape)
        throw CodegenException("Not a SHA instruction");

    escape = data[pos + 1];
    if ((escape != ThreeByteEscape38 && escape != ThreeByteEscape3A) || !IsShaOpcode(escape, data[pos + 2]))
        throw CodegenException("Not a SHA instruction");

    ShaOperands operands{};
    if (escape == ThreeByteEscape3A) {
        operands.Operation = ShaOperation::Sha1Rnds4;
    } else {
        switch (data[pos + 2]) {
        case 0xC8:
            operands.Operation = ShaOperation::Sha1Nexte;
            break;
        case 0xC9:
            operands.Operation = ShaOperation::Sha1Msg1;
            break;
        case 0xCA:
            operands.Operation = ShaOperation::Sha1Msg2;
            break;
        case 0xCC:
            operands.Operation = ShaOperation::Sha256Msg1;
            break;
        case 0xCD:
            operands.Operation = ShaOperation::Sha256Msg2;
            break;
        default:
            operands.Operation = ShaOperation::Sha256Rnds2;
            break;
        }
    }

    const std::uint8_t modrm = data[pos + 3];
    if (((modrm >> ModRmModShift) & ModRmModMask) != ModRmModRegister) {
        throw CodegenException("SHA instruction with a memory operand");
    }

    operands.Destination = static_cast<std::uint8_t>(((modrm >> ModRmRegShift) & ModRmRegMask) | (((rex & 0x4) != 0) ? 8 : 0));
    operands.Source = static_cast<std::uint8_t>((modrm & ModRmRmMask) | (((rex & 0x1) != 0) ? 8 : 0));
    operands.Immediate = (escape == ThreeByteEscape3A && pos + 5 <= length) ? data[pos + 4] : 0;
    return operands;
}

}
