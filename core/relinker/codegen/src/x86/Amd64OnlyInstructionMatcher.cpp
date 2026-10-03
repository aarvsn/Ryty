#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <codegen/x86/DecodedInstruction.hpp>
#include <codegen/x86/Amd64OnlySubstitutionTable.hpp>
#include <codegen/x86/Sse4aLowering.hpp>
#include <codegen/x86/Sse4aOperands.hpp>
#include <codegen/x86/ClzeroLowering.hpp>
#include <codegen/x86/ClzeroOperands.hpp>
#include <codegen/x86/ShaLowering.hpp>
#include <codegen/x86/ShaOperands.hpp>
#include <codegen/x86/StubBodyBuilder.hpp>
#include <codegen/x86/X64OpcodeConstants.hpp>
#include <codegen/CodegenException.hpp>
#include <algorithm>
#include <memory>
#include <span>
#include <vector>

namespace Codegen {

namespace {

using namespace Amd64OnlySubstitutionTable;

constexpr std::size_t kMaxInstructionLength = 15;

Amd64OnlyMatch _unsupported(const Entry& entry, const std::size_t length) {
    return Amd64OnlyMatch{entry.Name, length, Amd64OnlyLowering::Unsupported, {}, {}, 0};
}

const Entry& _sse4aEntry(const Sse4aOperands& operands) {
    if (operands.RegisterForm)
        return operands.Insertq ? kInsertqRegisterForm : kExtrqRegisterForm;
    return operands.Insertq ? kInsertq : kExtrq;
}

const Entry& _shaEntry(const ShaOperands& operands) {
    switch (operands.Operation) {
    case ShaOperation::Sha256Rnds2:
        return kSha256rnds2;
    case ShaOperation::Sha256Msg1:
        return kSha256msg1;
    case ShaOperation::Sha256Msg2:
        return kSha256msg2;
    case ShaOperation::Sha1Rnds4:
        return kSha1rnds4;
    case ShaOperation::Sha1Nexte:
        return kSha1nexte;
    case ShaOperation::Sha1Msg1:
        return kSha1msg1;
    case ShaOperation::Sha1Msg2:
        return kSha1msg2;
    }
    return kSha256rnds2;
}

Amd64OnlyMatch _inPlace(const Entry& entry, const std::size_t length, std::vector<std::uint8_t> replacement) {
    while (replacement.size() < length) {
        const auto& nop = kNops[std::min<std::size_t>(length - replacement.size(), std::size(kNops)) - 1];
        replacement.insert(replacement.end(), nop.Bytes, nop.Bytes + nop.Size);
    }
    return Amd64OnlyMatch{entry.Name, length, Amd64OnlyLowering::InPlace, std::move(replacement), {}, 0};
}

bool _isClzeroOpcode(const DecodedInstruction& instr) {
    const auto pos = instr.OpcodeOffset();
    return pos + 2 < instr.Length && instr.Data[pos] == X64OpcodeConstants::TwoByteOpcodeEscape && instr.Data[pos + 1] == X64OpcodeConstants::TwoByteGrp7 && instr.Data[pos + 2] == 0xFC;
}

bool _validWait(const DecodedInstruction& instr) {
    const auto opcode = instr.Data + instr.OpcodeOffset();
    return instr.Length <= kMaxInstructionLength && std::find(instr.Data, opcode, X64OpcodeConstants::PrefixLock) == opcode;
}

std::uint8_t _rexByte(const DecodedInstruction& instr) {
    std::uint8_t rex = 0;
    const auto opcodeOffset = instr.OpcodeOffset();
    for (std::size_t i = 0; i < opcodeOffset && i < instr.Length; ++i)
        if (instr.Data[i] >= X64OpcodeConstants::RexMin && instr.Data[i] <= X64OpcodeConstants::RexMax)
            rex = instr.Data[i];
    return rex;
}

// Replacement bytes for instructions lowered in place; empty for the rest.
std::optional<std::vector<std::uint8_t>> _inPlaceReplacement(const DecodedInstruction& instr) {
    using namespace X64OpcodeConstants;
    const auto opcodeOffset = instr.OpcodeOffset();

    if (instr.IsFemms())
        return std::vector<std::uint8_t>{TwoByteOpcodeEscape, TwoByteEmms};

    if (instr.IsPrefetchw()) {
        if (opcodeOffset + 2 >= instr.Length)
            return std::nullopt;
        const auto modrm = instr.Data[opcodeOffset + 2];
        if (((modrm >> ModRmModShift) & ModRmModMask) == ModRmModRegister)
            return std::vector<std::uint8_t>{TwoByteOpcodeEscape, TwoByteNopModRm, 0x00};
        return std::vector<std::uint8_t>{TwoByteOpcodeEscape, 0x18, modrm};
    }

    if (instr.IsMcommit())
        return std::vector<std::uint8_t>{TwoByteOpcodeEscape, TwoByteNopModRm, 0x40, 0x00};

    if (instr.IsRdpid()) {
        if (opcodeOffset + 2 >= instr.Length)
            return std::nullopt;
        const auto modrm = instr.Data[opcodeOffset + 2];
        const auto rex = _rexByte(instr);
        const auto rm = static_cast<std::uint8_t>((modrm & ModRmRmMask) | ((rex & 0x1) != 0 ? 8 : 0));
        std::vector<std::uint8_t> replacement;
        if ((rex & 0x1) != 0)
            replacement.push_back(static_cast<std::uint8_t>(RexMin | 0x1));
        replacement.push_back(0x31);
        replacement.push_back(static_cast<std::uint8_t>(0xC0 | ((rm & 7) << 3) | (rm & 7)));
        return replacement;
    }

    if (instr.IsMonitorx() && _validWait(instr))
        return std::vector<std::uint8_t>{};

    if (instr.IsMwaitx() && _validWait(instr))
        return std::vector<std::uint8_t>(kPause.Bytes, kPause.Bytes + kPause.Size);

    return std::nullopt;
}

class Amd64OnlyInstructionMatcher : public IAmd64OnlyInstructionMatcher {
public:
    [[nodiscard]] std::optional<Amd64OnlyMatch> Match(
        const std::uint8_t* data,
        std::size_t length,
        std::span<const std::uint8_t> trailing = {}
    ) const override;

    [[nodiscard]] std::optional<Amd64OnlyMatch> MatchSequence(
        std::span<const std::span<const std::uint8_t>> instructions,
        std::span<const std::uint8_t> trailing
    ) const override;

private:
    Sse4aLowering _lowering;
    ShaLowering _shaLowering;
    ClzeroLowering _clzeroLowering;

    [[nodiscard]] Amd64OnlyMatch _matchMovnts(const DecodedInstruction& instr, const Entry& entry) const;
    [[nodiscard]] Amd64OnlyMatch _matchSse4a(const DecodedInstruction& instr, const Entry& entry, const Entry& registerFormEntry, std::span<const std::uint8_t> trailing) const;
    [[nodiscard]] Amd64OnlyMatch _matchSha(const DecodedInstruction& instr, std::span<const std::uint8_t> trailing) const;
    [[nodiscard]] Amd64OnlyMatch _matchClzero(const DecodedInstruction& instr, std::span<const std::uint8_t> trailing) const;
    [[nodiscard]] Amd64OnlyMatch _matchRdpru(std::span<const std::uint8_t> trailing) const;
};

Amd64OnlyMatch Amd64OnlyInstructionMatcher::_matchMovnts(const DecodedInstruction& instr, const Entry& entry) const {
    using namespace X64OpcodeConstants;
    const auto opcodeOffset = instr.OpcodeOffset();
    if (opcodeOffset + 2 >= instr.Length)
        throw CodegenException("MOVNTSS/MOVNTSD truncated before its ModRM byte");
    const auto modrm = instr.Data[opcodeOffset + 2];
    if (((modrm >> ModRmModShift) & ModRmModMask) == ModRmModRegister)
        throw CodegenException("MOVNTSS/MOVNTSD with a register operand");
    std::vector<std::uint8_t> replacement(instr.Data, instr.Data + instr.Length);
    replacement[opcodeOffset + 1] = kMovsStoreOpcode;
    return Amd64OnlyMatch{entry.Name, instr.Length, Amd64OnlyLowering::InPlace, std::move(replacement), {}, 0};
}

Amd64OnlyMatch Amd64OnlyInstructionMatcher::_matchSse4a(const DecodedInstruction& instr, const Entry& entry, const Entry& registerFormEntry, std::span<const std::uint8_t> trailing) const {
    const auto operands = DecodeSse4a(instr.Data, instr.Length);
    if (!operands.RegisterForm && trailing.empty()) {
        if (auto inPlace = _lowering.LowerInPlace(operands, instr.Length))
            return Amd64OnlyMatch{entry.Name, instr.Length, Amd64OnlyLowering::InPlace, std::move(*inPlace), {}, 0};
    }
    auto body = _lowering.LowerOutOfLine(operands, trailing);
    const auto& name = operands.RegisterForm ? registerFormEntry.Name : entry.Name;
    return Amd64OnlyMatch{name, instr.Length, Amd64OnlyLowering::Trampoline, {}, std::move(body.Bytes), body.ReturnBranchOffset};
}

Amd64OnlyMatch Amd64OnlyInstructionMatcher::_matchSha(const DecodedInstruction& instr, std::span<const std::uint8_t> trailing) const {
    const auto operands = DecodeSha(instr.Data, instr.Length);
    auto body = _shaLowering.LowerOutOfLine(operands, trailing);
    return Amd64OnlyMatch{_shaEntry(operands).Name, instr.Length, Amd64OnlyLowering::Trampoline, {}, std::move(body.Bytes), body.ReturnBranchOffset};
}

Amd64OnlyMatch Amd64OnlyInstructionMatcher::_matchClzero(const DecodedInstruction& instr, std::span<const std::uint8_t> trailing) const {
    auto body = _clzeroLowering.LowerOutOfLine(DecodeClzero(instr.Data, instr.Length), trailing);
    return Amd64OnlyMatch{kClzero.Name, instr.Length, Amd64OnlyLowering::Trampoline, {}, std::move(body.Bytes), body.ReturnBranchOffset};
}

Amd64OnlyMatch Amd64OnlyInstructionMatcher::_matchRdpru(std::span<const std::uint8_t> trailing) const {
    StubBodyBuilder body;
    static const std::uint8_t kZeroMsr[] = {0xB8, 0x00, 0x00, 0x00, 0x00, 0xBA, 0x00, 0x00, 0x00, 0x00};
    body.Raw(std::span<const std::uint8_t>(kZeroMsr, sizeof(kZeroMsr)));
    body.Raw(trailing);
    const auto lowered = body.Finish();
    return Amd64OnlyMatch{kRdpru.Name, 3, Amd64OnlyLowering::Trampoline, {}, std::move(lowered.Bytes), lowered.ReturnBranchOffset};
}

std::optional<Amd64OnlyMatch> Amd64OnlyInstructionMatcher::MatchSequence(
    std::span<const std::span<const std::uint8_t>> instructions,
    std::span<const std::uint8_t> trailing
) const {
    if (instructions.empty())
        return std::nullopt;
    StubBodyBuilder body;
    const char* name = nullptr;
    for (const auto& instruction : instructions) {
        const DecodedInstruction instr{instruction.data(), instruction.size()};
        if (instr.IsExtrq() || instr.IsInsertq()) {
            const auto operands = DecodeSse4a(instr.Data, instr.Length);
            if (name == nullptr)
                name = _sse4aEntry(operands).Name;
            _lowering.EmitOutOfLine(body, operands);
        } else if (instr.IsSha256() || instr.IsSha1() || instr.IsSha1Rnds4()) {
            const auto operands = DecodeSha(instr.Data, instr.Length);
            if (name == nullptr)
                name = _shaEntry(operands).Name;
            _shaLowering.EmitOutOfLine(body, operands);
        } else if (instr.IsClzero()) {
            const auto operands = DecodeClzero(instr.Data, instr.Length);
            if (name == nullptr)
                name = kClzero.Name;
            _clzeroLowering.EmitOutOfLine(body, operands);
        } else if (instr.IsRdpru()) {
            if (name == nullptr)
                name = kRdpru.Name;
            static const std::uint8_t kZeroMsr[] = {0xB8, 0x00, 0x00, 0x00, 0x00, 0xBA, 0x00, 0x00, 0x00, 0x00};
            body.Raw(std::span<const std::uint8_t>(kZeroMsr, sizeof(kZeroMsr)));
        } else if (const auto replacement = _inPlaceReplacement(instr)) {
            if (name == nullptr)
                name = "AMD-only instruction";
            body.Raw(*replacement);
        } else {
            return std::nullopt;
        }
    }
    body.Raw(trailing);
    auto lowered = body.Finish();
    return Amd64OnlyMatch{name, instructions.front().size(), Amd64OnlyLowering::Trampoline, {}, std::move(lowered.Bytes), lowered.ReturnBranchOffset};
}

std::optional<Amd64OnlyMatch> Amd64OnlyInstructionMatcher::Match(
    const std::uint8_t* data,
    std::size_t length,
    std::span<const std::uint8_t> trailing
) const {
    const DecodedInstruction instr{data, length};

    if (instr.IsMovntss())
        return _matchMovnts(instr, kMovntss);

    if (instr.IsMovntsd())
        return _matchMovnts(instr, kMovntsd);

    if (instr.IsExtrq())
        return _matchSse4a(instr, kExtrq, kExtrqRegisterForm, trailing);

    if (instr.IsInsertq())
        return _matchSse4a(instr, kInsertq, kInsertqRegisterForm, trailing);

    if (instr.IsSha256() || instr.IsSha1() || instr.IsSha1Rnds4())
        return _matchSha(instr, trailing);

    if (instr.IsMonitorx())
        return _validWait(instr) ? _inPlace(kMonitorx, length, {}) : _unsupported(kMonitorx, length);

    if (instr.IsMwaitx())
        return _validWait(instr) ? _inPlace(kMwaitx, length, std::vector<std::uint8_t>(kPause.Bytes, kPause.Bytes + kPause.Size)) : _unsupported(kMwaitx, length);

    if (instr.IsClzero())
        return _matchClzero(instr, trailing);

    if (_isClzeroOpcode(instr))
        return _unsupported(kClzero, length);

    if (instr.IsRdpru())
        return _matchRdpru(trailing);

    if (instr.IsMcommit())
        return _inPlace(kMcommit, length, {});

    if (instr.IsFemms())
        return _inPlace(kFemms, length, std::vector<std::uint8_t>{X64OpcodeConstants::TwoByteOpcodeEscape, X64OpcodeConstants::TwoByteEmms});

    if (instr.IsPrefetchw() || instr.IsRdpid()) {
        const auto replacement = _inPlaceReplacement(instr);
        if (!replacement)
            return std::nullopt;
        return _inPlace(instr.IsRdpid() ? kRdpid : kPrefetchw, length, std::move(*replacement));
    }

    return std::nullopt;
}

}

std::unique_ptr<IAmd64OnlyInstructionMatcher> MakeAmd64OnlyInstructionMatcher() {
    return std::make_unique<Amd64OnlyInstructionMatcher>();
}

}
