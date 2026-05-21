#pragma once

#include <cstdint>

/** 16-bit compact encoding (Honeycomb Compact Addendum). */
namespace CompactProfile {

enum class CompactOp : uint8_t {
    NOP = 0x00,
    ADD = 0x01,
    SUB = 0x02,
    MOV = 0x03,
    LDI = 0x04,
    LW = 0x05,
    SW = 0x06,
    BZ = 0x07,
    BNZ = 0x08,
    CALL = 0x09,
    RET = 0x0A,
    PUSH = 0x0B,
    POP = 0x0C,
    CMPI = 0x0D,
    JMP = 0x0E,
    MOVS = 0x0F,
    HLT = 0x10,
};

constexpr int32_t SignExt5(uint8_t Imm5) {
    return (Imm5 & 0x10) ? static_cast<int32_t>(Imm5) - 32 : static_cast<int32_t>(Imm5);
}

constexpr int32_t SignExt10(uint16_t Imm10) {
    return (Imm10 & 0x200) ? static_cast<int32_t>(Imm10) - 1024
                           : static_cast<int32_t>(Imm10);
}

constexpr int32_t SignExt9(uint16_t Imm9) {
    return (Imm9 & 0x100) ? static_cast<int32_t>(Imm9) - 512
                            : static_cast<int32_t>(Imm9);
}

/** CALL [15:10]=opcode, [9]=mode (1=full), [8:0]=PC-relative slot offset. */
constexpr uint16_t CallModeBit = 1u << 9;

inline uint16_t PackCall(uint16_t Offset9, bool ToFull) {
    return static_cast<uint16_t>((static_cast<uint16_t>(CompactOp::CALL) << 10) |
                                 (ToFull ? CallModeBit : 0) | (Offset9 & 0x1FF));
}

inline uint16_t PackRr(CompactOp Op, uint8_t Rd, uint8_t Rs) {
    return static_cast<uint16_t>((static_cast<uint16_t>(Op) << 10) |
                                 (static_cast<uint16_t>(Rd & 0x1F) << 5) |
                                 (Rs & 0x1F));
}

inline uint16_t PackRi(CompactOp Op, uint8_t Rd, uint8_t Imm5) {
    return PackRr(Op, Rd, Imm5);
}

} // namespace CompactProfile
