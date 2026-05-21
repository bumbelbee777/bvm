#pragma once

#include <cstdint>

/**
 * Shift applied to the third ALU operand (rt or imm1) before the operation.
 * Packed in the base word disp field: kind in [8:6], amount in [5:0].
 */
namespace AluShiftProfile {

constexpr int KindShift = 6;
constexpr uint64_t KindMask = 0x7ULL;
constexpr int AmountShift = 0;
constexpr uint64_t AmountMask = 0x3FULL;

enum class Kind : uint8_t {
    None = 0,
    Lsh = 1,
    Rsh = 2,
    Rol = 3,
    Ror = 4,
};

constexpr int32_t PackDisp(Kind ShiftKind, uint32_t Amount) {
    const uint32_t Amt = Amount & 0x3F;
    const uint32_t K = static_cast<uint32_t>(ShiftKind) & 0x7;
    return static_cast<int32_t>((K << KindShift) | Amt);
}

inline Kind KindFromDisp(int32_t Disp) {
    return static_cast<Kind>((static_cast<uint64_t>(Disp) >> KindShift) & KindMask);
}

inline uint32_t AmountFromDisp(int32_t Disp) {
    return static_cast<uint32_t>(static_cast<uint64_t>(Disp) & AmountMask);
}

inline uint64_t Apply(uint64_t Value, Kind ShiftKind, uint32_t Amount) {
    const uint64_t Amt = Amount & 63U;
    if (ShiftKind == Kind::None || Amt == 0) {
        return Value;
    }
    switch (ShiftKind) {
        case Kind::Lsh:
            return Value << Amt;
        case Kind::Rsh:
            return Value >> Amt;
        case Kind::Rol:
            return (Value << Amt) | (Value >> (64U - Amt));
        case Kind::Ror:
            return (Value >> Amt) | (Value << (64U - Amt));
        default:
            return Value;
    }
}

} // namespace AluShiftProfile
