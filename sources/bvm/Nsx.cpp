#include "Shared.h"
#include "NsxProfile.h"
#include "NsxMl.h"
#include "RegFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace Honeycomb;

namespace {

float ReadFp32Lane(Uint128 Value, uint32_t LaneIndex) {
    const unsigned Shift = (3u - LaneIndex) * 32u;
    const uint32_t Bits = static_cast<uint32_t>(Value >> Shift);
    float Lane = 0.0f;
    std::memcpy(&Lane, &Bits, sizeof(Lane));
    return Lane;
}

void WriteFp32Lane(Uint128& Value, uint32_t LaneIndex, float Lane) {
    const unsigned Shift = (3u - LaneIndex) * 32u;
    const Uint128 Mask = (static_cast<Uint128>(0xFFFFFFFFULL) << Shift);
    uint32_t Bits = 0;
    std::memcpy(&Bits, &Lane, sizeof(Bits));
    Value = (Value & ~Mask) | (static_cast<Uint128>(Bits) << Shift);
}

float ReadFp32At(const Cpu& Vm, uint64_t Base, uint32_t Index) {
    uint32_t Bits = Vm.Read32(Base + static_cast<uint64_t>(Index) *
                                          NsxBaselineProfile::ElemBytesFp32);
    float Value = 0.0f;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

void WriteFp32At(Cpu& Vm, uint64_t Base, uint32_t Index, float Value) {
    uint32_t Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    Vm.Write32(Base + static_cast<uint64_t>(Index) * NsxBaselineProfile::ElemBytesFp32,
               Bits);
}

uint64_t DescriptorAddress(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("NSX descriptor op requires imm1 address");
    }
    return Insn.Imm1;
}

void RequireNsxFp32(const DecodedInsn& Insn) {
    if (Insn.Opmode != NsxOpmodeFp32 && Insn.Opmode != 0) {
        throw std::runtime_error(
            "Baseline BVM NSX ops require opmode FP32 (2) or default 0 treated as FP32");
    }
}

void RequireXReg(uint8_t Code) {
    if (Code > 15) {
        throw std::out_of_range("NSX SSX register out of range");
    }
}

float Sigmoid(float X) {
    if (X >= 0.0f) {
        const float ExpNeg = std::exp(-X);
        return 1.0f / (1.0f + ExpNeg);
    }
    const float ExpPos = std::exp(X);
    return ExpPos / (1.0f + ExpPos);
}

float GeluApprox(float X) {
    const float Inner = 0.7978845608f * (X + 0.044715f * X * X * X);
    return 0.5f * X * (1.0f + std::tanh(Inner));
}

uint32_t ReadWinSzPrng(const Cpu& Vm) {
    return static_cast<uint32_t>(
        (Vm.WinSz & NsxBaselineProfile::WinSzPrngMask) >> NsxBaselineProfile::WinSzPrngShift);
}

void WriteWinSzPrng(Cpu& Vm, uint32_t State) {
    Vm.WinSz = (Vm.WinSz & ~NsxBaselineProfile::WinSzPrngMask) |
               (static_cast<uint64_t>(State & 0xFFFFu) << NsxBaselineProfile::WinSzPrngShift);
}

uint32_t LcgStep(uint32_t State) {
    return State * 1664525u + 1013904223u;
}

NsxConvDescriptor LoadNsxConvDescriptor(const Cpu& Vm, uint64_t Address) {
    NsxConvDescriptor Desc{};
    Desc.Input = Vm.Read64(Address);
    Desc.Kernel = Vm.Read64(Address + 8);
    Desc.Output = Vm.Read64(Address + 16);
    const uint64_t Meta = Vm.Read64(Address + 24);
    Desc.Height = static_cast<uint32_t>((Meta >> 48) & 0xFFFF);
    Desc.Width = static_cast<uint32_t>((Meta >> 32) & 0xFFFF);
    Desc.Channels = static_cast<uint32_t>((Meta >> 16) & 0xFFFF);
    if (Desc.Channels == 0) {
        Desc.Channels = 1;
    }
    const uint32_t KwKh = static_cast<uint32_t>(Meta & 0xFFFF);
    Desc.KernelH = (KwKh >> 8) & 0xFF;
    Desc.KernelW = KwKh & 0xFF;
    Desc.Bias = Vm.Read64(Address + 32);
    Desc.Flags = static_cast<uint32_t>(Vm.Read64(Address + 40));
    return Desc;
}

void ValidateNsxConv(const NsxConvDescriptor& Desc) {
    if (Desc.Height == 0 || Desc.Width == 0 || Desc.KernelH == 0 || Desc.KernelW == 0) {
        throw std::runtime_error("NSX CONV2D descriptor dimensions must be non-zero");
    }
    if (Desc.Height > NsxBaselineProfile::MaxDim ||
        Desc.Width > NsxBaselineProfile::MaxDim ||
        Desc.KernelH > NsxBaselineProfile::MaxDim ||
        Desc.KernelW > NsxBaselineProfile::MaxDim ||
        Desc.Channels > NsxBaselineProfile::MaxDim) {
        throw std::runtime_error("NSX CONV2D dimensions exceed baseline limit (4)");
    }
    if (Desc.KernelH > Desc.Height || Desc.KernelW > Desc.Width) {
        throw std::runtime_error("NSX CONV2D kernel larger than input");
    }
}

NsxAttentionDescriptor LoadNsxAttentionDescriptor(const Cpu& Vm, uint64_t Address) {
    NsxAttentionDescriptor Desc{};
    Desc.Q = Vm.Read64(Address);
    Desc.K = Vm.Read64(Address + 8);
    Desc.V = Vm.Read64(Address + 16);
    Desc.Output = Vm.Read64(Address + 24);
    const uint64_t Meta = Vm.Read64(Address + 32);
    Desc.Batch = static_cast<uint32_t>((Meta >> 48) & 0xFFFF);
    if (Desc.Batch == 0) {
        Desc.Batch = 1;
    }
    Desc.Heads = static_cast<uint32_t>((Meta >> 32) & 0xFFFF);
    if (Desc.Heads == 0) {
        Desc.Heads = 1;
    }
    Desc.SeqLen = static_cast<uint32_t>((Meta >> 16) & 0xFFFF);
    Desc.HeadDim = static_cast<uint32_t>(Meta & 0xFFFF);
    Desc.Mask = Vm.Read64(Address + 40);
    Desc.Flags = static_cast<uint32_t>(Vm.Read64(Address + 48));
    return Desc;
}

void ValidateNsxAttention(const NsxAttentionDescriptor& Desc) {
    if (Desc.SeqLen == 0 || Desc.HeadDim == 0) {
        throw std::runtime_error("NSX attention N and D must be non-zero");
    }
    if (Desc.Batch > 1 || Desc.Heads > NsxBaselineProfile::MaxHeads ||
        Desc.SeqLen > NsxBaselineProfile::MaxAttnSeq ||
        Desc.HeadDim > NsxBaselineProfile::MaxDim) {
        throw std::runtime_error("NSX attention dims exceed baseline BVM limits");
    }
}

NsxLstmCellDescriptor LoadNsxLstmDescriptor(const Cpu& Vm, uint64_t Address) {
    NsxLstmCellDescriptor Desc{};
    Desc.Ct = Vm.Read64(Address);
    Desc.Ht = Vm.Read64(Address + 8);
    Desc.CtPrev = Vm.Read64(Address + 16);
    Desc.HtPrev = Vm.Read64(Address + 24);
    Desc.Xt = Vm.Read64(Address + 32);
    Desc.W = Vm.Read64(Address + 40);
    Desc.B = Vm.Read64(Address + 48);
    const uint64_t Meta = Vm.Read64(Address + 56);
    Desc.Hidden = static_cast<uint32_t>((Meta >> 16) & 0xFFFF);
    Desc.Input = static_cast<uint32_t>(Meta & 0xFFFF);
    return Desc;
}

void ValidateNsxLstm(const NsxLstmCellDescriptor& Desc) {
    if (Desc.Hidden == 0 || Desc.Input == 0) {
        throw std::runtime_error("NSX LSTM hidden and input sizes must be non-zero");
    }
    if (Desc.Hidden > NsxBaselineProfile::MaxHidden ||
        Desc.Input > NsxBaselineProfile::MaxInput) {
        throw std::runtime_error("NSX LSTM dims exceed baseline limit (4)");
    }
}

NsxRnnCellDescriptor LoadNsxRnnDescriptor(const Cpu& Vm, uint64_t Address) {
    NsxRnnCellDescriptor Desc{};
    Desc.Ht = Vm.Read64(Address);
    Desc.HtPrev = Vm.Read64(Address + 8);
    Desc.Xt = Vm.Read64(Address + 16);
    Desc.W = Vm.Read64(Address + 24);
    Desc.B = Vm.Read64(Address + 32);
    const uint64_t Meta = Vm.Read64(Address + 40);
    Desc.Hidden = static_cast<uint32_t>((Meta >> 16) & 0xFFFF);
    Desc.Input = static_cast<uint32_t>(Meta & 0xFFFF);
    return Desc;
}

void ValidateNsxRnn(const NsxRnnCellDescriptor& Desc) {
    if (Desc.Hidden == 0 || Desc.Input == 0) {
        throw std::runtime_error("NSX RNN hidden and input sizes must be non-zero");
    }
    if (Desc.Hidden > NsxBaselineProfile::MaxHidden ||
        Desc.Input > NsxBaselineProfile::MaxInput) {
        throw std::runtime_error("NSX RNN dims exceed baseline limit (4)");
    }
}

void RunNsxConv2dCore(Cpu& Vm, const NsxConvDescriptor& Desc, uint8_t Rd,
                      bool Depthwise) {
    const uint32_t HeightOut = Desc.Height - Desc.KernelH + 1;
    const uint32_t WidthOut = Desc.Width - Desc.KernelW + 1;
    const uint32_t OutElems = HeightOut * WidthOut * Desc.Channels;
    std::vector<float> Output(OutElems, 0.0f);
    for (uint32_t C = 0; C < Desc.Channels; ++C) {
        for (uint32_t OutRow = 0; OutRow < HeightOut; ++OutRow) {
            for (uint32_t OutCol = 0; OutCol < WidthOut; ++OutCol) {
                float Sum = 0.0f;
                if (!Depthwise && Desc.Bias != 0) {
                    Sum = ReadFp32At(Vm, Desc.Bias, C);
                }
                for (uint32_t Kh = 0; Kh < Desc.KernelH; ++Kh) {
                    for (uint32_t Kw = 0; Kw < Desc.KernelW; ++Kw) {
                        const uint32_t InRow = OutRow + Kh;
                        const uint32_t InCol = OutCol + Kw;
                        const uint32_t InIdx =
                            (C * Desc.Height * Desc.Width) + InRow * Desc.Width + InCol;
                        uint32_t KerIdx = Kh * Desc.KernelW + Kw;
                        if (Depthwise) {
                            KerIdx += C * Desc.KernelH * Desc.KernelW;
                        }
                        Sum = std::fma(ReadFp32At(Vm, Desc.Input, InIdx),
                                       ReadFp32At(Vm, Desc.Kernel, KerIdx), Sum);
                    }
                }
                if (Desc.Flags == 1) {
                    Sum = std::max(0.0f, Sum);
                }
                const uint32_t OutIdx =
                    (C * HeightOut * WidthOut) + OutRow * WidthOut + OutCol;
                Output[OutIdx] = Sum;
            }
        }
    }
    for (uint32_t Index = 0; Index < OutElems; ++Index) {
        WriteFp32At(Vm, Desc.Output, Index, Output[Index]);
    }
    Uint128 Packed = 0;
    const uint32_t PackCount = std::min(OutElems, NsxBaselineProfile::SsxLaneCount);
    for (uint32_t Index = 0; Index < PackCount; ++Index) {
        WriteFp32Lane(Packed, Index, Output[Index]);
    }
    WriteXReg(Vm, Rd, Packed);
}

void OpNsxActivationLanes(Cpu& Vm, const DecodedInsn& Insn,
                          float (*Fn)(float)) {
    RequireNsxFp32(Insn);
    RequireXReg(Insn.Rd);
    RequireXReg(Insn.Rs);
    Uint128 Dest = 0;
    const Uint128 Src = ReadXReg(Vm, Insn.Rs);
    for (uint32_t Lane = 0; Lane < NsxBaselineProfile::SsxLaneCount; ++Lane) {
        WriteFp32Lane(Dest, Lane, Fn(ReadFp32Lane(Src, Lane)));
    }
    WriteXReg(Vm, Insn.Rd, Dest);
}

void OpNsxSoftmax(Cpu& Vm, const DecodedInsn& Insn) {
    RequireNsxFp32(Insn);
    RequireXReg(Insn.Rd);
    RequireXReg(Insn.Rs);
    float Lanes[NsxBaselineProfile::SsxLaneCount];
    const Uint128 Src = ReadXReg(Vm, Insn.Rs);
    float MaxVal = ReadFp32Lane(Src, 0);
    for (uint32_t Lane = 0; Lane < NsxBaselineProfile::SsxLaneCount; ++Lane) {
        Lanes[Lane] = ReadFp32Lane(Src, Lane);
        MaxVal = std::max(MaxVal, Lanes[Lane]);
    }
    float Sum = 0.0f;
    for (uint32_t Lane = 0; Lane < NsxBaselineProfile::SsxLaneCount; ++Lane) {
        Lanes[Lane] = std::exp(Lanes[Lane] - MaxVal);
        Sum += Lanes[Lane];
    }
    Uint128 Dest = 0;
    for (uint32_t Lane = 0; Lane < NsxBaselineProfile::SsxLaneCount; ++Lane) {
        WriteFp32Lane(Dest, Lane, Lanes[Lane] / Sum);
    }
    WriteXReg(Vm, Insn.Rd, Dest);
}

void OpNsxDropout(Cpu& Vm, const DecodedInsn& Insn) {
    RequireNsxFp32(Insn);
    RequireXReg(Insn.Rd);
    RequireXReg(Insn.Rs);
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("DROPOUT requires imm1 dropout rate (FP32 bits)");
    }
    float Rate = 0.0f;
    const uint32_t RateBits = static_cast<uint32_t>(Insn.Imm1);
    std::memcpy(&Rate, &RateBits, sizeof(Rate));
    uint32_t Prng = ReadWinSzPrng(Vm);
    if (Prng == 0) {
        Prng = 0xACE1u;
    }
    const Uint128 Src = ReadXReg(Vm, Insn.Rs);
    Uint128 Dest = 0;
    for (uint32_t Lane = 0; Lane < NsxBaselineProfile::SsxLaneCount; ++Lane) {
        Prng = LcgStep(Prng);
        const float U = static_cast<float>(Prng & 0xFFFFu) / 65536.0f;
        float Value = ReadFp32Lane(Src, Lane);
        if (U < Rate) {
            Value = 0.0f;
        }
        WriteFp32Lane(Dest, Lane, Value);
    }
    WriteWinSzPrng(Vm, Prng);
    WriteXReg(Vm, Insn.Rd, Dest);
}

void OpNsxSwish(Cpu& Vm, const DecodedInsn& Insn, float Beta) {
    RequireNsxFp32(Insn);
    RequireXReg(Insn.Rd);
    RequireXReg(Insn.Rs);
    Uint128 Dest = 0;
    const Uint128 Src = ReadXReg(Vm, Insn.Rs);
    for (uint32_t Lane = 0; Lane < NsxBaselineProfile::SsxLaneCount; ++Lane) {
        const float X = ReadFp32Lane(Src, Lane);
        WriteFp32Lane(Dest, Lane, X * Sigmoid(Beta * X));
    }
    WriteXReg(Vm, Insn.Rd, Dest);
}

void OpNsxConv2d(Cpu& Vm, const DecodedInsn& Insn, bool Depthwise) {
    if (Insn.Rd > 15) {
        throw std::out_of_range("NSX conv destination register out of range");
    }
    const NsxConvDescriptor Desc =
        LoadNsxConvDescriptor(Vm, DescriptorAddress(Insn));
    ValidateNsxConv(Desc);
    RunNsxConv2dCore(Vm, Desc, Insn.Rd, Depthwise);
}

void OpNsxConv1d(Cpu& Vm, const DecodedInsn& Insn) {
    if (Insn.Rd > 15) {
        throw std::out_of_range("NSX CONV1D destination register out of range");
    }
    NsxConvDescriptor Desc = LoadNsxConvDescriptor(Vm, DescriptorAddress(Insn));
    if (Desc.Width == 0) {
        Desc.Width = 1;
    }
    if (Desc.Width != 1 && Desc.Height != 1) {
        throw std::runtime_error("NSX CONV1D requires W=1 or H=1 in descriptor meta");
    }
    ValidateNsxConv(Desc);
    RunNsxConv2dCore(Vm, Desc, Insn.Rd, false);
}

void OpNsxPool(Cpu& Vm, const DecodedInsn& Insn, bool MaxPool) {
    if (Insn.Rd > 15) {
        throw std::out_of_range("NSX pool destination register out of range");
    }
    const NsxConvDescriptor Desc =
        LoadNsxConvDescriptor(Vm, DescriptorAddress(Insn));
    ValidateNsxConv(Desc);
    const uint32_t HeightOut = Desc.Height - Desc.KernelH + 1;
    const uint32_t WidthOut = Desc.Width - Desc.KernelW + 1;
    const uint32_t OutElems = HeightOut * WidthOut * Desc.Channels;
    std::vector<float> Output(OutElems, 0.0f);
    const uint32_t WindowElems = Desc.KernelH * Desc.KernelW;
    for (uint32_t C = 0; C < Desc.Channels; ++C) {
        for (uint32_t OutRow = 0; OutRow < HeightOut; ++OutRow) {
            for (uint32_t OutCol = 0; OutCol < WidthOut; ++OutCol) {
                float Acc = MaxPool ? -std::numeric_limits<float>::infinity() : 0.0f;
                for (uint32_t Kh = 0; Kh < Desc.KernelH; ++Kh) {
                    for (uint32_t Kw = 0; Kw < Desc.KernelW; ++Kw) {
                        const uint32_t InRow = OutRow + Kh;
                        const uint32_t InCol = OutCol + Kw;
                        const uint32_t InIdx =
                            (C * Desc.Height * Desc.Width) + InRow * Desc.Width + InCol;
                        const float Value = ReadFp32At(Vm, Desc.Input, InIdx);
                        if (MaxPool) {
                            Acc = std::max(Acc, Value);
                        } else {
                            Acc += Value;
                        }
                    }
                }
                if (!MaxPool && WindowElems > 0) {
                    Acc /= static_cast<float>(WindowElems);
                }
                const uint32_t OutIdx =
                    (C * HeightOut * WidthOut) + OutRow * WidthOut + OutCol;
                Output[OutIdx] = Acc;
            }
        }
    }
    for (uint32_t Index = 0; Index < OutElems; ++Index) {
        WriteFp32At(Vm, Desc.Output, Index, Output[Index]);
    }
    Uint128 Packed = 0;
    const uint32_t PackCount = std::min(OutElems, NsxBaselineProfile::SsxLaneCount);
    for (uint32_t Index = 0; Index < PackCount; ++Index) {
        WriteFp32Lane(Packed, Index, Output[Index]);
    }
    WriteXReg(Vm, Insn.Rd, Packed);
}

void OpNsxPoolGlobal(Cpu& Vm, const DecodedInsn& Insn, bool MaxPool) {
    RequireNsxFp32(Insn);
    RequireXReg(Insn.Rd);
    RequireXReg(Insn.Rs);
    const Uint128 Src = ReadXReg(Vm, Insn.Rs);
    float Acc = MaxPool ? ReadFp32Lane(Src, 0) : 0.0f;
    for (uint32_t Lane = 1; Lane < NsxBaselineProfile::SsxLaneCount; ++Lane) {
        const float Value = ReadFp32Lane(Src, Lane);
        if (MaxPool) {
            Acc = std::max(Acc, Value);
        } else {
            Acc += Value;
        }
    }
    if (!MaxPool) {
        Acc /= static_cast<float>(NsxBaselineProfile::SsxLaneCount);
    }
    Uint128 Dest = 0;
    for (uint32_t Lane = 0; Lane < NsxBaselineProfile::SsxLaneCount; ++Lane) {
        WriteFp32Lane(Dest, Lane, Acc);
    }
    WriteXReg(Vm, Insn.Rd, Dest);
}

void OpNsxUpsample(Cpu& Vm, const DecodedInsn& Insn) {
    if (Insn.Rd > 15) {
        throw std::out_of_range("NSX UPSAMPLE destination register out of range");
    }
    const NsxConvDescriptor Desc =
        LoadNsxConvDescriptor(Vm, DescriptorAddress(Insn));
    ValidateNsxConv(Desc);
    const uint32_t Scale = (Desc.Flags == 0) ? 2u : Desc.Flags;
    if (Scale != 2) {
        throw std::runtime_error("Baseline NSX UPSAMPLE supports 2x nearest only (flags=2)");
    }
    const uint32_t OutH = Desc.Height * Scale;
    const uint32_t OutW = Desc.Width * Scale;
    if (OutH > NsxBaselineProfile::MaxDim || OutW > NsxBaselineProfile::MaxDim) {
        throw std::runtime_error("NSX UPSAMPLE output exceeds baseline limit (4)");
    }
    const uint32_t OutElems = OutH * OutW * Desc.Channels;
    std::vector<float> Output(OutElems, 0.0f);
    for (uint32_t C = 0; C < Desc.Channels; ++C) {
        for (uint32_t InRow = 0; InRow < Desc.Height; ++InRow) {
            for (uint32_t InCol = 0; InCol < Desc.Width; ++InCol) {
                const uint32_t InIdx =
                    (C * Desc.Height * Desc.Width) + InRow * Desc.Width + InCol;
                const float Value = ReadFp32At(Vm, Desc.Input, InIdx);
                for (uint32_t Dr = 0; Dr < Scale; ++Dr) {
                    for (uint32_t Dc = 0; Dc < Scale; ++Dc) {
                        const uint32_t OutRow = InRow * Scale + Dr;
                        const uint32_t OutCol = InCol * Scale + Dc;
                        const uint32_t OutIdx =
                            (C * OutH * OutW) + OutRow * OutW + OutCol;
                        Output[OutIdx] = Value;
                    }
                }
            }
        }
    }
    for (uint32_t Index = 0; Index < OutElems; ++Index) {
        WriteFp32At(Vm, Desc.Output, Index, Output[Index]);
    }
    Uint128 Packed = 0;
    const uint32_t PackCount = std::min(OutElems, NsxBaselineProfile::SsxLaneCount);
    for (uint32_t Index = 0; Index < PackCount; ++Index) {
        WriteFp32Lane(Packed, Index, Output[Index]);
    }
    WriteXReg(Vm, Insn.Rd, Packed);
}

void OpNsxNormalize(Cpu& Vm, const DecodedInsn& Insn) {
    RequireNsxFp32(Insn);
    RequireXReg(Insn.Rd);
    RequireXReg(Insn.Rs);
    RequireXReg(Insn.Rt);
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("LAYERNORM/BATCHNORM require imm1 beta pointer");
    }
    const Uint128 Src = ReadXReg(Vm, Insn.Rs);
    const Uint128 Gamma = ReadXReg(Vm, Insn.Rt);
    const uint64_t BetaAddr = Insn.Imm1;
    float Mean = 0.0f;
    for (uint32_t Lane = 0; Lane < NsxBaselineProfile::SsxLaneCount; ++Lane) {
        Mean += ReadFp32Lane(Src, Lane);
    }
    Mean /= static_cast<float>(NsxBaselineProfile::SsxLaneCount);
    float Var = 0.0f;
    for (uint32_t Lane = 0; Lane < NsxBaselineProfile::SsxLaneCount; ++Lane) {
        const float Delta = ReadFp32Lane(Src, Lane) - Mean;
        Var += Delta * Delta;
    }
    Var /= static_cast<float>(NsxBaselineProfile::SsxLaneCount);
    const float InvStd =
        1.0f / std::sqrt(Var + NsxBaselineProfile::NormEpsilon);
    Uint128 Dest = 0;
    for (uint32_t Lane = 0; Lane < NsxBaselineProfile::SsxLaneCount; ++Lane) {
        const float Norm = (ReadFp32Lane(Src, Lane) - Mean) * InvStd;
        const float Beta = ReadFp32At(Vm, BetaAddr, Lane);
        WriteFp32Lane(Dest, Lane,
                      ReadFp32Lane(Gamma, Lane) * Norm + Beta);
    }
    WriteXReg(Vm, Insn.Rd, Dest);
}

void OpNsxAttention(Cpu& Vm, const DecodedInsn& Insn) {
    if (Insn.Rd > 15) {
        throw std::out_of_range("NSX attention destination register out of range");
    }
    const NsxAttentionDescriptor Desc =
        LoadNsxAttentionDescriptor(Vm, DescriptorAddress(Insn));
    ValidateNsxAttention(Desc);
    const uint32_t N = Desc.SeqLen;
    const uint32_t D = Desc.HeadDim;
    std::vector<float> Scores(N * N, 0.0f);
    for (uint32_t I = 0; I < N; ++I) {
        for (uint32_t J = 0; J < N; ++J) {
            float Dot = 0.0f;
            for (uint32_t K = 0; K < D; ++K) {
                Dot = std::fma(ReadFp32At(Vm, Desc.Q, I * D + K),
                               ReadFp32At(Vm, Desc.K, J * D + K), Dot);
            }
            const float Scale = 1.0f / std::sqrt(static_cast<float>(D));
            Scores[static_cast<size_t>(I) * N + J] = Dot * Scale;
        }
    }
    std::vector<float> Out(N * D, 0.0f);
    for (uint32_t I = 0; I < N; ++I) {
        float MaxScore = Scores[static_cast<size_t>(I) * N];
        for (uint32_t J = 1; J < N; ++J) {
            MaxScore = std::max(MaxScore, Scores[static_cast<size_t>(I) * N + J]);
        }
        float SumExp = 0.0f;
        for (uint32_t J = 0; J < N; ++J) {
            const size_t Idx = static_cast<size_t>(I) * N + J;
            Scores[Idx] = std::exp(Scores[Idx] - MaxScore);
            SumExp += Scores[Idx];
        }
        for (uint32_t J = 0; J < N; ++J) {
            Scores[static_cast<size_t>(I) * N + J] /= SumExp;
        }
        for (uint32_t K = 0; K < D; ++K) {
            float Acc = 0.0f;
            for (uint32_t J = 0; J < N; ++J) {
                Acc = std::fma(Scores[static_cast<size_t>(I) * N + J],
                               ReadFp32At(Vm, Desc.V, J * D + K), Acc);
            }
            Out[static_cast<size_t>(I) * D + K] = Acc;
        }
    }
    for (uint32_t Index = 0; Index < N * D; ++Index) {
        WriteFp32At(Vm, Desc.Output, Index, Out[Index]);
    }
    Uint128 Packed = 0;
    const uint32_t PackCount = std::min(N * D, NsxBaselineProfile::SsxLaneCount);
    for (uint32_t Index = 0; Index < PackCount; ++Index) {
        WriteFp32Lane(Packed, Index, Out[Index]);
    }
    WriteXReg(Vm, Insn.Rd, Packed);
}

float LstmWeightAt(const Cpu& Vm, uint64_t Base, uint32_t Row, uint32_t Col,
                   uint32_t Stride) {
    return ReadFp32At(Vm, Base, Row * Stride + Col);
}

void OpNsxLstmCell(Cpu& Vm, const DecodedInsn& Insn) {
    if (Insn.Rd > 15) {
        throw std::out_of_range("NSX LSTM_CELL destination register out of range");
    }
    const NsxLstmCellDescriptor Desc =
        LoadNsxLstmDescriptor(Vm, DescriptorAddress(Insn));
    ValidateNsxLstm(Desc);
    const uint32_t H = Desc.Hidden;
    const uint32_t X = Desc.Input;
    const uint32_t Combined = H + X;
    std::vector<float> Concat(Combined, 0.0f);
    for (uint32_t Index = 0; Index < H; ++Index) {
        Concat[Index] = ReadFp32At(Vm, Desc.HtPrev, Index);
    }
    for (uint32_t Index = 0; Index < X; ++Index) {
        Concat[H + Index] = ReadFp32At(Vm, Desc.Xt, Index);
    }
    auto GateRow = [&](uint32_t GateIndex, uint32_t HiddenIndex) {
        float Sum = ReadFp32At(Vm, Desc.B, GateIndex * H + HiddenIndex);
        const uint32_t Row = GateIndex * H + HiddenIndex;
        for (uint32_t Col = 0; Col < Combined; ++Col) {
            Sum = std::fma(Concat[Col], LstmWeightAt(Vm, Desc.W, Row, Col, Combined), Sum);
        }
        return Sum;
    };
    for (uint32_t Index = 0; Index < H; ++Index) {
        const float Forget = Sigmoid(GateRow(0, Index));
        const float InputGate = Sigmoid(GateRow(1, Index));
        const float OutputGate = Sigmoid(GateRow(2, Index));
        const float Candidate = std::tanh(GateRow(3, Index));
        const float CtPrev = ReadFp32At(Vm, Desc.CtPrev, Index);
        const float CtNew = Forget * CtPrev + InputGate * Candidate;
        const float HtNew = OutputGate * std::tanh(CtNew);
        WriteFp32At(Vm, Desc.Ct, Index, CtNew);
        WriteFp32At(Vm, Desc.Ht, Index, HtNew);
    }
    Uint128 Packed = 0;
    for (uint32_t Index = 0; Index < std::min(H, NsxBaselineProfile::SsxLaneCount); ++Index) {
        WriteFp32Lane(Packed, Index, ReadFp32At(Vm, Desc.Ht, Index));
    }
    WriteXReg(Vm, Insn.Rd, Packed);
}

void OpNsxGruCell(Cpu& Vm, const DecodedInsn& Insn) {
    if (Insn.Rd > 15) {
        throw std::out_of_range("NSX GRU_CELL destination register out of range");
    }
    const NsxLstmCellDescriptor Desc =
        LoadNsxLstmDescriptor(Vm, DescriptorAddress(Insn));
    ValidateNsxLstm(Desc);
    const uint32_t H = Desc.Hidden;
    const uint32_t X = Desc.Input;
    const uint32_t Combined = H + X;
    std::vector<float> Concat(Combined, 0.0f);
    for (uint32_t Index = 0; Index < H; ++Index) {
        Concat[Index] = ReadFp32At(Vm, Desc.HtPrev, Index);
    }
    for (uint32_t Index = 0; Index < X; ++Index) {
        Concat[H + Index] = ReadFp32At(Vm, Desc.Xt, Index);
    }
    auto GateRow = [&](uint32_t GateIndex, uint32_t HiddenIndex) {
        float Sum = ReadFp32At(Vm, Desc.B, GateIndex * H + HiddenIndex);
        const uint32_t Row = GateIndex * H + HiddenIndex;
        for (uint32_t Col = 0; Col < Combined; ++Col) {
            Sum = std::fma(Concat[Col], LstmWeightAt(Vm, Desc.W, Row, Col, Combined), Sum);
        }
        return Sum;
    };
    for (uint32_t Index = 0; Index < H; ++Index) {
        const float Z = Sigmoid(GateRow(0, Index));
        const float R = Sigmoid(GateRow(1, Index));
        float CandidateSum = ReadFp32At(Vm, Desc.B, 2 * H + Index);
        const uint32_t Row = 2 * H + Index;
        for (uint32_t Col = 0; Col < H; ++Col) {
            CandidateSum = std::fma(R * Concat[Col],
                                    LstmWeightAt(Vm, Desc.W, Row, Col, Combined),
                                    CandidateSum);
        }
        for (uint32_t Col = 0; Col < X; ++Col) {
            CandidateSum = std::fma(Concat[H + Col],
                                    LstmWeightAt(Vm, Desc.W, Row, H + Col, Combined),
                                    CandidateSum);
        }
        const float Candidate = std::tanh(CandidateSum);
        const float HtPrev = ReadFp32At(Vm, Desc.HtPrev, Index);
        const float HtNew = (1.0f - Z) * Candidate + Z * HtPrev;
        WriteFp32At(Vm, Desc.Ht, Index, HtNew);
    }
    Uint128 Packed = 0;
    for (uint32_t Index = 0; Index < std::min(H, NsxBaselineProfile::SsxLaneCount); ++Index) {
        WriteFp32Lane(Packed, Index, ReadFp32At(Vm, Desc.Ht, Index));
    }
    WriteXReg(Vm, Insn.Rd, Packed);
}

void OpNsxRnnCell(Cpu& Vm, const DecodedInsn& Insn) {
    if (Insn.Rd > 15) {
        throw std::out_of_range("NSX RNN_CELL destination register out of range");
    }
    const NsxRnnCellDescriptor Desc =
        LoadNsxRnnDescriptor(Vm, DescriptorAddress(Insn));
    ValidateNsxRnn(Desc);
    const uint32_t H = Desc.Hidden;
    const uint32_t X = Desc.Input;
    const uint32_t Combined = H + X;
    std::vector<float> Concat(Combined, 0.0f);
    for (uint32_t Index = 0; Index < H; ++Index) {
        Concat[Index] = ReadFp32At(Vm, Desc.HtPrev, Index);
    }
    for (uint32_t Index = 0; Index < X; ++Index) {
        Concat[H + Index] = ReadFp32At(Vm, Desc.Xt, Index);
    }
    for (uint32_t Index = 0; Index < H; ++Index) {
        float Sum = ReadFp32At(Vm, Desc.B, Index);
        for (uint32_t Col = 0; Col < Combined; ++Col) {
            Sum = std::fma(Concat[Col],
                           LstmWeightAt(Vm, Desc.W, Index, Col, Combined), Sum);
        }
        WriteFp32At(Vm, Desc.Ht, Index, std::tanh(Sum));
    }
    Uint128 Packed = 0;
    for (uint32_t Index = 0; Index < std::min(H, NsxBaselineProfile::SsxLaneCount); ++Index) {
        WriteFp32Lane(Packed, Index, ReadFp32At(Vm, Desc.Ht, Index));
    }
    WriteXReg(Vm, Insn.Rd, Packed);
}

void UnimplementedNsx(const DecodedInsn& Insn) {
    throw std::runtime_error(
        "Unimplemented NSX opcode in baseline BVM: 0x" +
        std::to_string(static_cast<unsigned>(static_cast<uint16_t>(Insn.Op))));
}

} // namespace

void Cpu::ExecuteNsxOpcode(const DecodedInsn& Insn) {
    switch (Insn.Op) {
        case Opcode::SOFTMAX:
            OpNsxSoftmax(*this, Insn);
            return;
        case Opcode::DROPOUT:
            OpNsxDropout(*this, Insn);
            return;
        case Opcode::NSX_RELU:
            OpNsxActivationLanes(*this, Insn,
                                [](float V) { return std::max(0.0f, V); });
            return;
        case Opcode::GELU:
            OpNsxActivationLanes(*this, Insn, GeluApprox);
            return;
        case Opcode::SWISH: {
            float Beta = 1.0f;
            if (Insn.Disp != 0) {
                const uint32_t Bits = static_cast<uint32_t>(Insn.Disp);
                std::memcpy(&Beta, &Bits, sizeof(Beta));
            }
            OpNsxSwish(*this, Insn, Beta);
            return;
        }
        case Opcode::SILU:
            OpNsxSwish(*this, Insn, 1.0f);
            return;
        case Opcode::NSX_TANH:
            OpNsxActivationLanes(*this, Insn, [](float V) { return std::tanh(V); });
            return;
        case Opcode::SIGMOID:
            OpNsxActivationLanes(*this, Insn, Sigmoid);
            return;
        case Opcode::LAYERNORM:
        case Opcode::BATCHNORM:
            OpNsxNormalize(*this, Insn);
            return;
        case Opcode::NSX_CONV2D:
            OpNsxConv2d(*this, Insn, false);
            return;
        case Opcode::CONV1D:
            OpNsxConv1d(*this, Insn);
            return;
        case Opcode::DWCONV2D:
            OpNsxConv2d(*this, Insn, true);
            return;
        case Opcode::POOL_MAX:
            OpNsxPool(*this, Insn, true);
            return;
        case Opcode::POOL_AVG:
            OpNsxPool(*this, Insn, false);
            return;
        case Opcode::POOL_GLOBAL:
            OpNsxPoolGlobal(*this, Insn, false);
            return;
        case Opcode::UPSAMPLE:
            OpNsxUpsample(*this, Insn);
            return;
        case Opcode::ATTENTION:
        case Opcode::SELF_ATTN:
            OpNsxAttention(*this, Insn);
            return;
        case Opcode::MSE_LOSS:
        case Opcode::CROSS_ENTROPY:
        case Opcode::L2_LOSS:
        case Opcode::TRANSFORMER:
        case Opcode::FWD_START:
        case Opcode::FWD_END:
        case Opcode::SGD:
        case Opcode::BWD_GRAD:
            ExecuteNsxMlOpcode(*this, Insn);
            return;
        case Opcode::LSTM_CELL:
            OpNsxLstmCell(*this, Insn);
            return;
        case Opcode::RNN_CELL:
            OpNsxRnnCell(*this, Insn);
            return;
        case Opcode::GRU_CELL:
            OpNsxGruCell(*this, Insn);
            return;
        case Opcode::CONV3D:
        case Opcode::SEPCONV2D:
        case Opcode::CROSS_ATTN:
        case Opcode::FLASH_ATTN:
        case Opcode::PAGED_ATTN:
        case Opcode::RNN_SEQ:
        case Opcode::LSTM_SEQ:
        case Opcode::GRU_SEQ:
        case Opcode::BIDIR_LSTM:
        case Opcode::FWD_LAYER:
        case Opcode::BWD_START:
        case Opcode::BWD_ALL:
        case Opcode::CKPT:
        case Opcode::CKPT_RESTORE:
        case Opcode::CKPT_DISCARD:
        case Opcode::ADAM:
        case Opcode::LAMB:
        case Opcode::LAPLACIAN_2D:
        case Opcode::LAPLACIAN_3D:
        case Opcode::HESSIAN_2D:
        case Opcode::HESSIAN_3D:
        case Opcode::NEWTON_STEP:
            UnimplementedNsx(Insn);
            return;
        default:
            if (IsNsxOpcode(Insn.Op)) {
                UnimplementedNsx(Insn);
                return;
            }
            throw std::runtime_error("Not an NSX opcode");
    }
}
