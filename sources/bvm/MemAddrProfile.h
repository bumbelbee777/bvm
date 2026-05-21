#pragma once

#include <cstdint>

/**
 * Scalar LOAD/STORE addressing via base word fields (baseline `bvm`).
 *
 * - imm1_flag=1: absolute address in imm1 (legacy).
 * - imm1_flag=0, rs!=0: indexed — addr = GPR[rs] + (GPR[rt] if rt!=0) + sign_ext(disp).
 * - imm1_flag=0, rs==0: disp-only absolute (legacy small encoding).
 */
namespace MemAddrProfile {

inline bool UsesIndexedBase(uint8_t Rs, bool Imm1Flag) {
    return !Imm1Flag && Rs != 0;
}

} // namespace MemAddrProfile
