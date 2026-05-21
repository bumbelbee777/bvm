#pragma once

#include <cstdint>

/**
 * Baseline BVM matrix / tensor / ML profile (Honeycomb 0x180..0x19F, 0x300..).
 *
 * MatMul / Gemm descriptor (32 bytes + optional alpha/beta quad for GEMM):
 *   +0  AddrA
 *   +8  AddrB
 *   +16 AddrC (0 = skip memory store)
 *   +24 Meta: [63:48] ElemBytes, [47:32] M, [31:16] K, [15:0] N
 *
 * Tensor load/store descriptor (32 bytes):
 *   +0  AddrMem
 *   +8  (reserved)
 *   +16 (reserved)
 *   +24 Meta: [63:48] ElemBytes, [47:32] H, [31:16] W, [15:0] Stride
 *
 * Conv2D descriptor (48 bytes):
 *   +0  AddrInput
 *   +8  AddrKernel
 *   +16 AddrOutput
 *   +24 Meta: Elem | H_in | W_in | C_in (C_in=1 baseline)
 *   +32 Meta: KH | KW | 0 | 0
 *   +40 (reserved)
 *
 * QDOT descriptor (40 bytes):
 *   +0  AddrA
 *   +8  AddrB
 *   +16 AddrScore (0 = X-only)
 *   +24 Meta: Elem(=1 int8) | Length | 0
 *   +32 (reserved) / +36 ScaleBits (FP32 scale, big-endian)
 */
struct MatBaselineProfile {
    static constexpr uint32_t MaxDim = 4;
    static constexpr uint32_t ElemBytesFp32 = 4;
    static constexpr uint32_t ElemBytesInt8 = 1;
    static constexpr uint32_t MatDescriptorBytes = 32;
    static constexpr uint32_t GemmDescriptorBytes = 40;
    static constexpr uint32_t TensorDescriptorBytes = 32;
    static constexpr uint32_t ConvDescriptorBytes = 48;
    static constexpr uint32_t QdotDescriptorBytes = 40;
    static constexpr uint32_t MaxAccumulators = 4;
};

struct MatMulDescriptor {
    uint64_t AddrA = 0;
    uint64_t AddrB = 0;
    uint64_t AddrC = 0;
    uint32_t Rows = 0;
    uint32_t Inner = 0;
    uint32_t Cols = 0;
    uint32_t ElemBytes = MatBaselineProfile::ElemBytesFp32;
};

struct GemmDescriptor : MatMulDescriptor {
    float Alpha = 1.0f;
    float Beta = 0.0f;
};

struct TensorDescriptor {
    uint64_t AddrMem = 0;
    uint32_t Height = 0;
    uint32_t Width = 0;
    uint32_t Stride = 0;
    uint32_t ElemBytes = MatBaselineProfile::ElemBytesFp32;
};

struct Conv2DDescriptor {
    uint64_t AddrInput = 0;
    uint64_t AddrKernel = 0;
    uint64_t AddrOutput = 0;
    uint32_t HeightIn = 0;
    uint32_t WidthIn = 0;
    uint32_t ChannelsIn = 1;
    uint32_t KernelH = 0;
    uint32_t KernelW = 0;
    uint32_t ElemBytes = MatBaselineProfile::ElemBytesFp32;
};

struct QdotDescriptor {
    uint64_t AddrA = 0;
    uint64_t AddrB = 0;
    uint64_t AddrScore = 0;
    uint32_t Length = 0;
    uint32_t ElemBytes = MatBaselineProfile::ElemBytesInt8;
    float Scale = 1.0f;
};

inline uint64_t PackMatMeta(uint32_t ElemBytes, uint32_t M, uint32_t K, uint32_t N) {
    return (static_cast<uint64_t>(ElemBytes) << 48) |
           (static_cast<uint64_t>(M) << 32) |
           (static_cast<uint64_t>(K) << 16) |
           static_cast<uint64_t>(N);
}

inline uint64_t PackTensorMeta(uint32_t ElemBytes, uint32_t H, uint32_t W,
                               uint32_t Stride = 0) {
    return (static_cast<uint64_t>(ElemBytes) << 48) |
           (static_cast<uint64_t>(H) << 32) |
           (static_cast<uint64_t>(W) << 16) |
           static_cast<uint64_t>(Stride);
}

inline uint64_t PackConvMetaIn(uint32_t ElemBytes, uint32_t H, uint32_t W,
                               uint32_t C = 1) {
    return (static_cast<uint64_t>(ElemBytes) << 48) |
           (static_cast<uint64_t>(H) << 32) |
           (static_cast<uint64_t>(W) << 16) |
           static_cast<uint64_t>(C);
}

inline uint64_t PackConvMetaKernel(uint32_t Kh, uint32_t Kw) {
    return (static_cast<uint64_t>(Kh) << 16) | static_cast<uint64_t>(Kw);
}

inline uint64_t PackQdotMeta(uint32_t ElemBytes, uint32_t Length) {
    return (static_cast<uint64_t>(ElemBytes) << 48) |
           (static_cast<uint64_t>(Length) << 16);
}
