#pragma once

#include "KBankProfile.h"

#include <cstdint>

/** Honeycomb v0.7-CPLX — complex register aliases (baseline BVM subset). */
namespace CplxProfile {

constexpr uint8_t CplxSubopTag = 4u;
constexpr int32_t CplxDispBase = static_cast<int32_t>(CplxSubopTag << 27);

enum class Precision : uint8_t {
    Fp64 = 0,
    Fp32 = 1,
    Fp16 = 2,
    Bf16 = 3,
    Int32 = 4,
    Int64 = 5
};

/** Default merged `WINSZ`: window size 32, complex tag fields zero (FP64, stride 1). */
constexpr uint64_t DefaultWinSzMerged = KBankProfile::DefaultWinSz;

constexpr uint8_t GprStrideFp64 = 1;
constexpr uint8_t MaxComplexIndexFp64 = 15;

/** C_FP32 in a 128-bit SSX register: four FP32 lanes → two complex values. */
constexpr uint32_t Fp32LanesPerXReg = 4;
constexpr uint32_t ComplexCountFp32PerXReg = 2;

/** `WINSZ` bit map (merged window size + former `CTYPE` tag). */
constexpr unsigned WinSzPrecisionShift = 24;
constexpr unsigned WinSzGprStrideShift = 16;
constexpr unsigned WinSzKStrideShift = 32;

inline int32_t PackCplxDisp(Precision Prec) {
    return CplxDispBase | (static_cast<int32_t>(Prec) << 24);
}

inline Precision PrecisionFromDisp(int32_t Disp) {
    return static_cast<Precision>((Disp >> 24) & 7);
}

inline uint8_t GprComplexStride(uint64_t WinSz) {
    const uint8_t Stride = static_cast<uint8_t>((WinSz >> WinSzGprStrideShift) & 0xFF);
    return Stride != 0 ? Stride : GprStrideFp64;
}

inline uint8_t KBankComplexStride(uint64_t WinSz) {
    const uint8_t Stride = static_cast<uint8_t>((WinSz >> WinSzKStrideShift) & 0xFF);
    return Stride != 0 ? Stride : GprStrideFp64;
}

inline Precision PrecisionFromWinSz(uint64_t WinSz) {
    return static_cast<Precision>((WinSz >> WinSzPrecisionShift) & 0xFF);
}

} // namespace CplxProfile
