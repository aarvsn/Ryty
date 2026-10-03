#include <codegen/x86/ShaLowering.hpp>
#include <array>
#include <initializer_list>

namespace Codegen {

namespace {

constexpr std::uint8_t kPrefixPacked = 0x66;
constexpr std::uint8_t kShiftRightDwords = 2;
constexpr std::uint8_t kShiftLeftDwords = 6;
constexpr std::uint8_t kShiftRightBytes = 3;
constexpr std::uint8_t kShiftLeftBytes = 7;
constexpr std::uint8_t kWordBits = 32;
constexpr std::uint8_t kRoundKeys = 0;
constexpr std::uint8_t kPunpckldq = 0x62;
constexpr std::uint8_t kPunpcklqdq = 0x6C;
constexpr std::uint8_t kMovdqa = 0x6F;
constexpr std::uint8_t kPshufd = 0x70;
constexpr std::uint8_t kPand = 0xDB;
constexpr std::uint8_t kPandn = 0xDF;
constexpr std::uint8_t kPor = 0xEB;
constexpr std::uint8_t kPxor = 0xEF;
constexpr std::uint8_t kPaddd = 0xFE;

constexpr std::uint32_t kSha1K[4] = {0x5A827999u, 0x6ED9EBA1u, 0x8F1BBCDCu, 0xCA62C1D6u};

StubConstant broadcast(std::uint32_t value) {
    StubConstant constant{};
    for (std::size_t lane = 0; lane < 4; ++lane)
        for (std::size_t byte = 0; byte < 4; ++byte)
            constant[lane * 4 + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    return constant;
}

StubConstant laneMask(const std::uint8_t lane) {
    StubConstant constant{};
    for (std::size_t byte = 0; byte < 4; ++byte)
        constant[lane * 4 + byte] = 0xFF;
    return constant;
}

StubConstant highLanesMask(const std::uint8_t firstLane) {
    StubConstant constant{};
    for (std::size_t lane = firstLane; lane < 4; ++lane)
        for (std::size_t byte = 0; byte < 4; ++byte)
            constant[lane * 4 + byte] = 0xFF;
    return constant;
}

template<std::size_t TCount>
std::array<std::uint8_t, TCount> _scratch(const ShaOperands& operands) {
    std::array<std::uint8_t, TCount> scratch{};
    for (std::uint8_t reg = 0, found = 0; found < TCount; ++reg)
        if (reg != operands.Destination && reg != operands.Source && reg != kRoundKeys) scratch[found++] = reg;
    return scratch;
}

void _op(StubBodyBuilder& body, const std::uint8_t opcode, const std::uint8_t dst, const std::uint8_t src) {
    body.Sse(kPrefixPacked, {0x0F, opcode}, dst, src);
}

void _lane(StubBodyBuilder& body, const std::uint8_t dst, const std::uint8_t src, const std::uint8_t lane) {
    body.SseImm(kPrefixPacked, {0x0F, kPshufd}, dst, src, lane);
}

void _constant(StubBodyBuilder& body, const std::uint8_t opcode, const std::uint8_t reg, const StubConstant& constant) {
    body.RipOperand({0x0F, opcode}, reg, constant);
}

// ------------------------------------------------------------- SHA-256 ---

void _sigma(StubBodyBuilder& body, const std::uint8_t out, const std::uint8_t value, const std::uint8_t tmp, const std::initializer_list<std::uint8_t> rotations, const std::uint8_t shift) {
    bool first = true;
    const auto term = [&](const std::uint8_t extension, const std::uint8_t count) {
        const auto target = first ? out : tmp;
        _op(body, kMovdqa, target, value);
        body.ShiftDwordImm(extension, target, count);
        if (!first)
            _op(body, kPxor, out, tmp);
        first = false;
    };
    for (const auto rotation : rotations) {
        term(kShiftRightDwords, rotation);
        term(kShiftLeftDwords, static_cast<std::uint8_t>(kWordBits - rotation));
    }
    if (shift != 0)
        term(kShiftRightDwords, shift);
}

void _choose(StubBodyBuilder& body, const std::uint8_t out, const std::uint8_t e, const std::uint8_t f, const std::uint8_t g) {
    _op(body, kMovdqa, out, f);
    _op(body, kPxor, out, g);
    _op(body, kPand, out, e);
    _op(body, kPxor, out, g);
}

void _majority(StubBodyBuilder& body, const std::uint8_t x, const std::uint8_t y, const std::uint8_t z, const std::uint8_t tmp) {
    _op(body, kMovdqa, tmp, y);
    _op(body, kPand, tmp, z);
    _op(body, kPxor, y, z);
    _op(body, kPand, y, x);
    _op(body, kPxor, y, tmp);
}

void _bigSigma0(StubBodyBuilder& body, const std::uint8_t out, const std::uint8_t value, const std::uint8_t tmp) {
    _sigma(body, out, value, tmp, {2, 13, 22}, 0);
}

void _bigSigma1(StubBodyBuilder& body, const std::uint8_t out, const std::uint8_t value, const std::uint8_t tmp) {
    _sigma(body, out, value, tmp, {6, 11, 25}, 0);
}

void _smallSigma0(StubBodyBuilder& body, const std::uint8_t out, const std::uint8_t value, const std::uint8_t tmp) {
    _sigma(body, out, value, tmp, {7, 18}, 3);
}

void _smallSigma1(StubBodyBuilder& body, const std::uint8_t out, const std::uint8_t value, const std::uint8_t tmp) {
    _sigma(body, out, value, tmp, {17, 19}, 10);
}

void _emitSha256Rnds2(StubBodyBuilder& body, const ShaOperands& operands) {
    const auto state = operands.Destination;
    const auto words = operands.Source;
    const auto scratch = _scratch<8>(operands);
    const auto a = scratch[0];
    const auto t2 = scratch[1];
    const auto e = scratch[2];
    const auto g = scratch[3];
    const auto t1 = scratch[4];
    const auto x = scratch[5];
    const auto tmp = scratch[6];
    const auto e1 = scratch[7];
    for (const auto reg : scratch)
        body.Spill(reg);
    _lane(body, a, words, 3);
    _bigSigma0(body, t2, a, tmp);
    _lane(body, e, words, 2);
    _lane(body, g, state, 3);
    _majority(body, a, e, g, tmp);
    _op(body, kPaddd, t2, e);
    _lane(body, e, words, 1);
    _lane(body, g, state, 1);
    _bigSigma1(body, t1, e, tmp);
    _choose(body, x, e, words, g);
    _op(body, kPaddd, t1, x);
    _op(body, kPaddd, t1, state);
    _op(body, kPaddd, t1, kRoundKeys);
    _lane(body, e1, state, 2);
    _op(body, kPaddd, e1, t1);
    _op(body, kPaddd, t2, t1);
    _bigSigma1(body, t1, e1, tmp);
    _choose(body, x, e1, e, words);
    _op(body, kPaddd, t1, x);
    _op(body, kPaddd, t1, g);
    _lane(body, x, kRoundKeys, 1);
    _op(body, kPaddd, t1, x);
    _lane(body, g, state, 3);
    _op(body, kPaddd, g, t1);
    _bigSigma0(body, x, t2, tmp);
    _lane(body, e, words, 2);
    _majority(body, t2, a, e, tmp);
    _op(body, kPaddd, x, a);
    _op(body, kPaddd, x, t1);
    _op(body, kPunpckldq, e1, g);
    _op(body, kPunpckldq, t2, x);
    _op(body, kPunpcklqdq, e1, t2);
    _op(body, kMovdqa, state, e1);
    for (auto reg = scratch.rbegin(); reg != scratch.rend(); ++reg)
        body.Restore(*reg);
}

void _emitSha256Msg1(StubBodyBuilder& body, const ShaOperands& operands) {
    const auto dst = operands.Destination;
    const auto src = operands.Source;
    const auto scratch = _scratch<3>(operands);
    const auto next = scratch[0];
    const auto sigma = scratch[1];
    const auto tmp = scratch[2];
    for (const auto reg : scratch)
        body.Spill(reg);
    _op(body, kMovdqa, next, dst);
    body.ShiftImm(kShiftRightBytes, next, 4);
    _op(body, kMovdqa, sigma, src);
    body.ShiftImm(kShiftLeftBytes, sigma, 12);
    _op(body, kPor, next, sigma);
    _smallSigma0(body, sigma, next, tmp);
    _op(body, kPaddd, dst, sigma);
    for (auto reg = scratch.rbegin(); reg != scratch.rend(); ++reg)
        body.Restore(*reg);
}

void _emitSha256Msg2(StubBodyBuilder& body, const ShaOperands& operands) {
    const auto dst = operands.Destination;
    const auto src = operands.Source;
    const auto scratch = _scratch<3>(operands);
    const auto words = scratch[0];
    const auto sigma = scratch[1];
    const auto tmp = scratch[2];
    for (const auto reg : scratch)
        body.Spill(reg);
    _op(body, kMovdqa, words, src);
    body.ShiftImm(kShiftRightBytes, words, 8);
    _smallSigma1(body, sigma, words, tmp);
    _op(body, kPaddd, dst, sigma);
    _op(body, kMovdqa, words, dst);
    body.ShiftImm(kShiftLeftBytes, words, 8);
    _smallSigma1(body, sigma, words, tmp);
    _op(body, kPaddd, dst, sigma);
    for (auto reg = scratch.rbegin(); reg != scratch.rend(); ++reg)
        body.Restore(*reg);
}

// -------------------------------------------------------------- SHA-1 ---

void _emitSha1Nexte(StubBodyBuilder& body, const ShaOperands& operands) {
    const auto dst = operands.Destination;
    const auto src = operands.Source;
    const auto scratch = _scratch<2>(operands);
    const auto rolled = scratch[0];
    const auto tmp = scratch[1];
    for (const auto reg : scratch)
        body.Spill(reg);
    _op(body, kMovdqa, rolled, dst);
    body.ShiftDwordImm(kShiftLeftDwords, rolled, 30);
    _op(body, kMovdqa, tmp, dst);
    body.ShiftDwordImm(kShiftRightDwords, tmp, 2);
    _op(body, kPor, rolled, tmp);
    _constant(body, kPand, rolled, laneMask(3));
    _op(body, kMovdqa, dst, src);
    _op(body, kPaddd, dst, rolled);
    for (auto reg = scratch.rbegin(); reg != scratch.rend(); ++reg)
        body.Restore(*reg);
}

void _emitSha1Msg1(StubBodyBuilder& body, const ShaOperands& operands) {
    const auto dst = operands.Destination;
    const auto src = operands.Source;
    const auto scratch = _scratch<1>(operands);
    const auto copy = scratch[0];
    body.Spill(copy);
    _op(body, kMovdqa, copy, dst);
    _op(body, kMovdqa, dst, src);
    body.ShiftImm(kShiftRightBytes, dst, 8);
    _op(body, kPxor, dst, copy);
    body.ShiftImm(kShiftLeftBytes, copy, 8);
    _op(body, kPxor, dst, copy);
    body.Restore(copy);
}

void _emitSha1Msg2(StubBodyBuilder& body, const ShaOperands& operands) {
    const auto dst = operands.Destination;
    const auto src = operands.Source;
    const auto scratch = _scratch<4>(operands);
    const auto copy = scratch[0];
    const auto rotated = scratch[1];
    const auto tmp = scratch[2];
    const auto low = scratch[3];
    for (const auto reg : scratch)
        body.Spill(reg);
    _op(body, kMovdqa, copy, dst);
    _lane(body, rotated, src, 0x93);
    _op(body, kPxor, rotated, copy);
    _op(body, kMovdqa, dst, rotated);
    body.ShiftDwordImm(kShiftLeftDwords, dst, 1);
    _op(body, kMovdqa, tmp, rotated);
    body.ShiftDwordImm(kShiftRightDwords, tmp, 31);
    _op(body, kPor, dst, tmp);
    _lane(body, rotated, dst, 0xFF);
    _op(body, kPxor, rotated, copy);
    _op(body, kMovdqa, low, rotated);
    body.ShiftDwordImm(kShiftLeftDwords, low, 1);
    _op(body, kMovdqa, tmp, rotated);
    body.ShiftDwordImm(kShiftRightDwords, tmp, 31);
    _op(body, kPor, low, tmp);
    _constant(body, kPand, dst, highLanesMask(1));
    _constant(body, kPand, low, laneMask(0));
    _op(body, kPor, dst, low);
    for (auto reg = scratch.rbegin(); reg != scratch.rend(); ++reg)
        body.Restore(*reg);
}

void _sha1F(StubBodyBuilder& body, const int group, const std::uint8_t out, const std::uint8_t b, const std::uint8_t c, const std::uint8_t d, const std::uint8_t t2) {
    switch (group & 3) {
    case 0:
        _op(body, kMovdqa, out, b);
        _op(body, kPand, out, c);
        _op(body, kMovdqa, t2, b);
        _op(body, kPandn, t2, d);
        _op(body, kPor, out, t2);
        break;
    case 2:
        _op(body, kMovdqa, out, b);
        _op(body, kPand, out, c);
        _op(body, kMovdqa, t2, b);
        _op(body, kPand, t2, d);
        _op(body, kPor, out, t2);
        _op(body, kMovdqa, t2, c);
        _op(body, kPand, t2, d);
        _op(body, kPor, out, t2);
        break;
    default:
        _op(body, kMovdqa, out, b);
        _op(body, kPxor, out, c);
        _op(body, kPxor, out, d);
        break;
    }
}

void _rol(StubBodyBuilder& body, const std::uint8_t out, const std::uint8_t value, const std::uint8_t tmp, const std::uint8_t left, const std::uint8_t right) {
    _op(body, kMovdqa, out, value);
    body.ShiftDwordImm(kShiftLeftDwords, out, left);
    _op(body, kMovdqa, tmp, value);
    body.ShiftDwordImm(kShiftRightDwords, tmp, right);
    _op(body, kPor, out, tmp);
}

void _emitSha1Rnds4(StubBodyBuilder& body, const ShaOperands& operands) {
    const auto dst = operands.Destination;
    const auto src = operands.Source;
    const auto scratch = _scratch<9>(operands);
    std::uint8_t aB = scratch[0];
    std::uint8_t bB = scratch[1];
    std::uint8_t cB = scratch[2];
    std::uint8_t dB = scratch[3];
    std::uint8_t eB = scratch[4];
    const std::uint8_t keys = scratch[5];
    std::uint8_t t1 = scratch[6];
    std::uint8_t t2 = scratch[7];
    std::uint8_t t3 = scratch[8];
    for (const auto reg : scratch)
        body.Spill(reg);

    const int group = operands.Immediate & 3;
    _constant(body, kMovdqa, keys, broadcast(kSha1K[group]));
    _lane(body, aB, dst, 0xFF);
    _lane(body, bB, dst, 0xAA);
    _lane(body, cB, dst, 0x55);
    _lane(body, dB, dst, 0x00);

    for (int round = 0; round < 4; ++round) {
        _sha1F(body, group, t1, bB, cB, dB, t2);
        _rol(body, t2, aB, t3, 5, 27);
        _op(body, kPaddd, t1, t2);
        static constexpr std::uint8_t kWBroadcast[4] = {0xFF, 0xAA, 0x55, 0x00};
        _lane(body, t3, src, kWBroadcast[round]);
        _op(body, kPaddd, t1, t3);
        if (round != 0)
            _op(body, kPaddd, t1, eB);
        _op(body, kPaddd, t1, keys);
        _op(body, kMovdqa, eB, dB);
        _op(body, kMovdqa, dB, cB);
        _rol(body, cB, bB, t3, 30, 2);
        _op(body, kMovdqa, bB, aB);
        _op(body, kMovdqa, aB, t1);
    }

    _constant(body, kPand, aB, laneMask(3));
    _constant(body, kPand, bB, laneMask(2));
    _constant(body, kPand, cB, laneMask(1));
    _constant(body, kPand, dB, laneMask(0));
    _op(body, kPor, aB, bB);
    _op(body, kPor, cB, dB);
    _op(body, kPor, aB, cB);
    _op(body, kMovdqa, dst, aB);

    for (auto reg = scratch.rbegin(); reg != scratch.rend(); ++reg)
        body.Restore(*reg);
}

}

void ShaLowering::EmitOutOfLine(StubBodyBuilder& body, const ShaOperands& operands) const {
    switch (operands.Operation) {
    case ShaOperation::Sha256Rnds2:
        _emitSha256Rnds2(body, operands);
        return;
    case ShaOperation::Sha256Msg1:
        _emitSha256Msg1(body, operands);
        return;
    case ShaOperation::Sha256Msg2:
        _emitSha256Msg2(body, operands);
        return;
    case ShaOperation::Sha1Nexte:
        _emitSha1Nexte(body, operands);
        return;
    case ShaOperation::Sha1Msg1:
        _emitSha1Msg1(body, operands);
        return;
    case ShaOperation::Sha1Msg2:
        _emitSha1Msg2(body, operands);
        return;
    case ShaOperation::Sha1Rnds4:
        _emitSha1Rnds4(body, operands);
        return;
    }
}

LoweredBody ShaLowering::LowerOutOfLine(const ShaOperands& operands, std::span<const std::uint8_t> trailing) const {
    StubBodyBuilder body;
    EmitOutOfLine(body, operands);
    body.Raw(trailing);
    return body.Finish();
}

}
