#pragma once

#include <cstdint>

/**
 * Baseline BVM NSX profile (Honeycomb 0x600..0x6FF).
 *
 * Conv descriptor (56 bytes):
 *   +0  input, +8 kernel, +16 output, +24 meta, +32 bias, +40 flags, +48 reserved
 *   meta: [63:48] H, [47:32] W, [31:16] C, [15:0] KH|KW (KH high 8, KW low 8)
 *
 * Attention descriptor (64 bytes):
 *   +0 Q, +8 K, +16 V, +24 output, +32 meta, +40 mask, +48 flags, +56 reserved
 *   meta: [63:48] B, [47:32] H_heads, [31:16] N, [15:0] D
 *
 * LSTM cell descriptor (64 bytes):
 *   +0 ct, +8 ht, +16 ct_1, +24 ht_1, +32 xt, +40 W, +48 b, +56 meta
 *   meta: [31:16] hidden, [15:0] input
 */
struct NsxBaselineProfile {
    static constexpr uint32_t MaxDim = 4;
    static constexpr uint32_t MaxSeqLen = 4;
    static constexpr uint32_t MaxHidden = 4;
    static constexpr uint32_t MaxInput = 4;
    static constexpr uint32_t MaxHeads = 2;
    static constexpr uint32_t MaxAttnSeq = 4;
    static constexpr uint32_t ElemBytesFp32 = 4;
    static constexpr float NormEpsilon = 1e-5f;
    static constexpr uint32_t ConvDescriptorBytes = 56;
    static constexpr uint32_t AttentionDescriptorBytes = 64;
    static constexpr uint32_t LstmCellDescriptorBytes = 64;
    static constexpr uint32_t RnnCellDescriptorBytes = 48;
    static constexpr uint32_t LossDescriptorBytes = 32;
    static constexpr uint32_t TransformerDescriptorBytes = 96;
    static constexpr uint32_t SgdDescriptorBytes = 32;
    static constexpr uint32_t TapeDescriptorBytes = 32;
    static constexpr uint32_t GruGateCount = 3;
    static constexpr uint32_t SsxLaneCount = 4;
    static constexpr uint32_t TransformerWorkspaceBytes = 576;
    /** Dropout PRNG state in WINSZ[47:32]. */
    static constexpr unsigned WinSzPrngShift = 32;
    static constexpr uint64_t WinSzPrngMask = 0xFFFFULL << WinSzPrngShift;
};

struct NsxConvDescriptor {
    uint64_t Input = 0;
    uint64_t Kernel = 0;
    uint64_t Output = 0;
    uint32_t Height = 0;
    uint32_t Width = 0;
    uint32_t Channels = 1;
    uint32_t KernelH = 0;
    uint32_t KernelW = 0;
    uint64_t Bias = 0;
    uint32_t Flags = 0;
};

struct NsxAttentionDescriptor {
    uint64_t Q = 0;
    uint64_t K = 0;
    uint64_t V = 0;
    uint64_t Output = 0;
    uint32_t Batch = 1;
    uint32_t Heads = 1;
    uint32_t SeqLen = 0;
    uint32_t HeadDim = 0;
    uint64_t Mask = 0;
    uint32_t Flags = 0;
};

struct NsxLstmCellDescriptor {
    uint64_t Ct = 0;
    uint64_t Ht = 0;
    uint64_t CtPrev = 0;
    uint64_t HtPrev = 0;
    uint64_t Xt = 0;
    uint64_t W = 0;
    uint64_t B = 0;
    uint32_t Hidden = 0;
    uint32_t Input = 0;
};

/** RNN cell descriptor (48 bytes): ht, ht_1, xt, W, b, meta. */
struct NsxRnnCellDescriptor {
    uint64_t Ht = 0;
    uint64_t HtPrev = 0;
    uint64_t Xt = 0;
    uint64_t W = 0;
    uint64_t B = 0;
    uint32_t Hidden = 0;
    uint32_t Input = 0;
};

inline uint64_t PackNsxConvMeta(uint32_t H, uint32_t W, uint32_t C, uint32_t Kh,
                                uint32_t Kw) {
    return (static_cast<uint64_t>(H) << 48) | (static_cast<uint64_t>(W) << 32) |
           (static_cast<uint64_t>(C) << 16) |
           (static_cast<uint64_t>((Kh & 0xFF) << 8) | (Kw & 0xFF));
}

inline uint64_t PackNsxAttnMeta(uint32_t B, uint32_t Heads, uint32_t N, uint32_t D) {
    return (static_cast<uint64_t>(B) << 48) | (static_cast<uint64_t>(Heads) << 32) |
           (static_cast<uint64_t>(N) << 16) | static_cast<uint64_t>(D);
}

inline uint64_t PackNsxLstmMeta(uint32_t Hidden, uint32_t Input) {
    return (static_cast<uint64_t>(Hidden) << 16) | static_cast<uint64_t>(Input);
}

/** Loss descriptor (32 B): pred, target, loss_out, meta [count | flags]. */
struct NsxLossDescriptor {
    uint64_t Pred = 0;
    uint64_t Target = 0;
    uint64_t LossOut = 0;
    uint32_t Count = 4;
    uint32_t ClassIndex = 0;
    float L2Lambda = 0.0f;
};

/** Transformer block (96 B): input, output, workspace, Wq..Wout, grad_W* , meta N|D. */
struct NsxTransformerDescriptor {
    uint64_t Input = 0;
    uint64_t Output = 0;
    uint64_t Workspace = 0;
    uint64_t Wq = 0;
    uint64_t Wk = 0;
    uint64_t Wv = 0;
    uint64_t Wout = 0;
    uint64_t GradWq = 0;
    uint64_t GradWk = 0;
    uint64_t GradWv = 0;
    uint64_t GradWout = 0;
    uint32_t SeqLen = 4;
    uint32_t HeadDim = 4;
};

/** SGD descriptor (32 B): weights, grads, meta count, lr (FP32 bits in +24). */
struct NsxSgdDescriptor {
    uint64_t Weights = 0;
    uint64_t Grads = 0;
    uint32_t Count = 16;
    float LearningRate = 0.01f;
};

/** Autodiff tape (32 B): transformer desc, loss desc, pred_grad, flags. */
struct NsxTapeDescriptor {
    uint64_t TransformerDesc = 0;
    uint64_t LossDesc = 0;
    uint64_t PredGrad = 0;
    uint32_t Flags = 0;
};

inline uint64_t PackNsxLossMeta(uint32_t Count, uint32_t ClassIndex = 0) {
    return (static_cast<uint64_t>(Count) << 16) | static_cast<uint64_t>(ClassIndex);
}

inline uint64_t PackNsxTransformerMeta(uint32_t N, uint32_t D) {
    return (static_cast<uint64_t>(N) << 16) | static_cast<uint64_t>(D);
}
