#include "NsxMl.h"
#include "NsxProfile.h"
#include "RegFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace Honeycomb;

namespace {

constexpr uint32_t kTile = NsxBaselineProfile::MaxDim;

float ReadFp32(const Cpu& Vm, uint64_t Base, uint32_t Row, uint32_t Col, uint32_t Stride) {
    uint32_t Bits = Vm.Read32(Base + (static_cast<uint64_t>(Row) * Stride + Col) *
                                         NsxBaselineProfile::ElemBytesFp32);
    float Value = 0.0f;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

void WriteFp32(Cpu& Vm, uint64_t Base, uint32_t Row, uint32_t Col, uint32_t Stride,
               float Value) {
    uint32_t Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    Vm.Write32(Base + (static_cast<uint64_t>(Row) * Stride + Col) *
                       NsxBaselineProfile::ElemBytesFp32,
               Bits);
}

void WriteFp32At(Cpu& Vm, uint64_t Base, uint32_t Index, float Value) {
    uint32_t Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    Vm.Write32(Base + static_cast<uint64_t>(Index) * NsxBaselineProfile::ElemBytesFp32,
               Bits);
}

float ReadFp32At(const Cpu& Vm, uint64_t Base, uint32_t Index) {
    uint32_t Bits = Vm.Read32(Base + static_cast<uint64_t>(Index) *
                                          NsxBaselineProfile::ElemBytesFp32);
    float Value = 0.0f;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

float ReadVec(const Cpu& Vm, uint64_t Base, uint32_t Index) {
    return ReadFp32At(Vm, Base, Index);
}

void WriteVec(Cpu& Vm, uint64_t Base, uint32_t Index, float Value) {
    WriteFp32At(Vm, Base, Index, Value);
}

void WriteFp32Lane(Uint128& Value, uint32_t LaneIndex, float Lane) {
    const unsigned Shift = (3u - LaneIndex) * 32u;
    const Uint128 Mask = (static_cast<Uint128>(0xFFFFFFFFULL) << Shift);
    uint32_t Bits = 0;
    std::memcpy(&Bits, &Lane, sizeof(Bits));
    Value = (Value & ~Mask) | (static_cast<Uint128>(Bits) << Shift);
}

uint64_t DescriptorAddress(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("NSX ML op requires imm1 descriptor address");
    }
    return Insn.Imm1;
}

NsxLossDescriptor LoadLossDescriptor(const Cpu& Vm, uint64_t Address) {
    NsxLossDescriptor Desc{};
    Desc.Pred = Vm.Read64(Address);
    Desc.Target = Vm.Read64(Address + 8);
    Desc.LossOut = Vm.Read64(Address + 16);
    const uint64_t Meta = Vm.Read64(Address + 24);
    Desc.Count = static_cast<uint32_t>((Meta >> 16) & 0xFFFF);
    if (Desc.Count == 0) {
        Desc.Count = NsxBaselineProfile::SsxLaneCount;
    }
    Desc.ClassIndex = static_cast<uint32_t>(Meta & 0xFFFF);
    const uint32_t LambdaBits = Vm.Read32(Address + 28);
    std::memcpy(&Desc.L2Lambda, &LambdaBits, sizeof(Desc.L2Lambda));
    return Desc;
}

NsxTransformerDescriptor LoadTransformerDescriptor(const Cpu& Vm, uint64_t Address) {
    NsxTransformerDescriptor Desc{};
    Desc.Input = Vm.Read64(Address);
    Desc.Output = Vm.Read64(Address + 8);
    Desc.Workspace = Vm.Read64(Address + 16);
    Desc.Wq = Vm.Read64(Address + 24);
    Desc.Wk = Vm.Read64(Address + 32);
    Desc.Wv = Vm.Read64(Address + 40);
    Desc.Wout = Vm.Read64(Address + 48);
    Desc.GradWq = Vm.Read64(Address + 56);
    Desc.GradWk = Vm.Read64(Address + 64);
    Desc.GradWv = Vm.Read64(Address + 72);
    Desc.GradWout = Vm.Read64(Address + 80);
    const uint64_t Meta = Vm.Read64(Address + 88);
    Desc.SeqLen = static_cast<uint32_t>((Meta >> 16) & 0xFFFF);
    Desc.HeadDim = static_cast<uint32_t>(Meta & 0xFFFF);
    if (Desc.SeqLen == 0) {
        Desc.SeqLen = kTile;
    }
    if (Desc.HeadDim == 0) {
        Desc.HeadDim = kTile;
    }
    return Desc;
}

void ValidateTransformer(const NsxTransformerDescriptor& Desc) {
    if (Desc.SeqLen > kTile || Desc.HeadDim > kTile) {
        throw std::runtime_error("NSX transformer N/D exceed baseline limit (4)");
    }
    if (Desc.Workspace == 0) {
        throw std::runtime_error("NSX transformer requires workspace pointer");
    }
}

NsxSgdDescriptor LoadSgdDescriptor(const Cpu& Vm, uint64_t Address) {
    NsxSgdDescriptor Desc{};
    Desc.Weights = Vm.Read64(Address);
    Desc.Grads = Vm.Read64(Address + 8);
    const uint64_t Meta = Vm.Read64(Address + 16);
    Desc.Count = static_cast<uint32_t>(Meta & 0xFFFF);
    if (Desc.Count == 0) {
        Desc.Count = kTile * kTile;
    }
    const uint32_t LrBits = Vm.Read32(Address + 24);
    std::memcpy(&Desc.LearningRate, &LrBits, sizeof(Desc.LearningRate));
    return Desc;
}

void MatMul(const Cpu& Vm, uint64_t A, uint64_t B, std::vector<float>& C, uint32_t M,
            uint32_t K, uint32_t N) {
    C.assign(static_cast<size_t>(M) * N, 0.0f);
    for (uint32_t Row = 0; Row < M; ++Row) {
        for (uint32_t Col = 0; Col < N; ++Col) {
            float Sum = 0.0f;
            for (uint32_t Inner = 0; Inner < K; ++Inner) {
                Sum = std::fma(ReadFp32(Vm, A, Row, Inner, K),
                               ReadFp32(Vm, B, Inner, Col, N), Sum);
            }
            C[static_cast<size_t>(Row) * N + Col] = Sum;
        }
    }
}

void StoreMatrix(Cpu& Vm, uint64_t Base, const std::vector<float>& M, uint32_t Rows,
                 uint32_t Cols) {
    for (uint32_t Row = 0; Row < Rows; ++Row) {
        for (uint32_t Col = 0; Col < Cols; ++Col) {
            WriteFp32(Vm, Base, Row, Col, Cols, M[static_cast<size_t>(Row) * Cols + Col]);
        }
    }
}

void WeightGradFromActivation(Cpu& Vm, uint64_t ActBase, uint64_t DOutBase,
                              uint64_t GradWBase, uint32_t N, uint32_t K, uint32_t M) {
    for (uint32_t Inner = 0; Inner < K; ++Inner) {
        for (uint32_t Col = 0; Col < M; ++Col) {
            float Acc = ReadFp32(Vm, GradWBase, Inner, Col, M);
            for (uint32_t Row = 0; Row < N; ++Row) {
                Acc = std::fma(ReadFp32(Vm, ActBase, Row, Inner, K),
                               ReadFp32(Vm, DOutBase, Row, Col, M), Acc);
            }
            WriteFp32(Vm, GradWBase, Inner, Col, M, Acc);
        }
    }
}

void AttentionForward(Cpu& Vm, uint64_t QBase, uint64_t KBase, uint64_t VBase,
                      uint64_t OutBase, uint64_t ScoresBase, uint32_t N, uint32_t D) {
    const float Scale = 1.0f / std::sqrt(static_cast<float>(D));
    for (uint32_t I = 0; I < N; ++I) {
        float MaxScore = -1e30f;
        std::vector<float> RowScores(N);
        for (uint32_t J = 0; J < N; ++J) {
            float Dot = 0.0f;
            for (uint32_t K = 0; K < D; ++K) {
                Dot = std::fma(ReadFp32(Vm, QBase, I, K, D), ReadFp32(Vm, KBase, J, K, D),
                               Dot);
            }
            RowScores[J] = Dot * Scale;
            MaxScore = std::max(MaxScore, RowScores[J]);
        }
        float SumExp = 0.0f;
        for (uint32_t J = 0; J < N; ++J) {
            RowScores[J] = std::exp(RowScores[J] - MaxScore);
            SumExp += RowScores[J];
        }
        for (uint32_t J = 0; J < N; ++J) {
            const float Prob = RowScores[J] / SumExp;
            WriteFp32(Vm, ScoresBase, I, J, N, Prob);
            for (uint32_t K = 0; K < D; ++K) {
                float Acc = ReadFp32(Vm, OutBase, I, K, D);
                Acc = std::fma(Prob, ReadFp32(Vm, VBase, J, K, D), Acc);
                WriteFp32(Vm, OutBase, I, K, D, Acc);
            }
        }
    }
}

void AttentionBackward(Cpu& Vm, uint64_t ScoresBase, uint64_t QBase, uint64_t KBase,
                       uint64_t VBase, uint64_t OutGradBase, uint64_t QGradBase,
                       uint64_t KGradBase, uint64_t VGradBase, uint32_t N, uint32_t D) {
    const float Scale = 1.0f / std::sqrt(static_cast<float>(D));
    for (uint32_t I = 0; I < N; ++I) {
        std::vector<float> DProb(N, 0.0f);
        for (uint32_t J = 0; J < N; ++J) {
            const float Prob = ReadFp32(Vm, ScoresBase, I, J, N);
            for (uint32_t K = 0; K < D; ++K) {
                const float DOut = ReadFp32(Vm, OutGradBase, I, K, D);
                float Gv = ReadFp32(Vm, VGradBase, J, K, D);
                Gv = std::fma(Prob, DOut, Gv);
                WriteFp32(Vm, VGradBase, J, K, D, Gv);
                DProb[J] = std::fma(ReadFp32(Vm, VBase, J, K, D), DOut, DProb[J]);
            }
        }
        float DotProbD = 0.0f;
        for (uint32_t J = 0; J < N; ++J) {
            const float Prob = ReadFp32(Vm, ScoresBase, I, J, N);
            DotProbD = std::fma(Prob, DProb[J], DotProbD);
        }
        for (uint32_t J = 0; J < N; ++J) {
            const float Prob = ReadFp32(Vm, ScoresBase, I, J, N);
            const float DScore = Prob * (DProb[J] - DotProbD) * Scale;
            for (uint32_t K = 0; K < D; ++K) {
                float Gq = ReadFp32(Vm, QGradBase, I, K, D);
                Gq = std::fma(DScore, ReadFp32(Vm, KBase, J, K, D), Gq);
                WriteFp32(Vm, QGradBase, I, K, D, Gq);
                float Gk = ReadFp32(Vm, KGradBase, J, K, D);
                Gk = std::fma(DScore, ReadFp32(Vm, QBase, I, K, D), Gk);
                WriteFp32(Vm, KGradBase, J, K, D, Gk);
            }
        }
    }
}

void OpNsxMseLoss(Cpu& Vm, const DecodedInsn& Insn) {
    const NsxLossDescriptor Desc =
        LoadLossDescriptor(Vm, DescriptorAddress(Insn));
    float Loss = 0.0f;
    for (uint32_t Index = 0; Index < Desc.Count; ++Index) {
        const float Diff = ReadVec(Vm, Desc.Pred, Index) - ReadVec(Vm, Desc.Target, Index);
        Loss += Diff * Diff;
    }
    Loss /= static_cast<float>(Desc.Count);
    if (Desc.LossOut != 0) {
        WriteFp32At(Vm, Desc.LossOut, 0, Loss);
    }
    Vm.NsxLastLossDesc = DescriptorAddress(Insn);
    if (Insn.Rd <= 15) {
        Uint128 Packed = 0;
        WriteFp32Lane(Packed, 0, Loss);
        WriteXReg(Vm, Insn.Rd, Packed);
    }
}

void OpNsxCrossEntropy(Cpu& Vm, const DecodedInsn& Insn) {
    const NsxLossDescriptor Desc =
        LoadLossDescriptor(Vm, DescriptorAddress(Insn));
    std::vector<float> Logits(Desc.Count);
    float MaxLogit = -1e30f;
    for (uint32_t Index = 0; Index < Desc.Count; ++Index) {
        Logits[Index] = ReadVec(Vm, Desc.Pred, Index);
        MaxLogit = std::max(MaxLogit, Logits[Index]);
    }
    float SumExp = 0.0f;
    for (uint32_t Index = 0; Index < Desc.Count; ++Index) {
        SumExp += std::exp(Logits[Index] - MaxLogit);
    }
    const uint32_t Label =
        (Desc.ClassIndex < Desc.Count) ? Desc.ClassIndex : 0;
    const float Loss =
        -(Logits[Label] - MaxLogit - std::log(SumExp));
    if (Desc.LossOut != 0) {
        WriteFp32At(Vm, Desc.LossOut, 0, Loss);
    }
    Vm.NsxLastLossDesc = DescriptorAddress(Insn);
    if (Insn.Rd <= 15) {
        Uint128 Packed = 0;
        WriteFp32Lane(Packed, 0, Loss);
        WriteXReg(Vm, Insn.Rd, Packed);
    }
}

void OpNsxL2Loss(Cpu& Vm, const DecodedInsn& Insn) {
    const NsxLossDescriptor Desc =
        LoadLossDescriptor(Vm, DescriptorAddress(Insn));
    float Loss = 0.0f;
    for (uint32_t Index = 0; Index < Desc.Count; ++Index) {
        const float W = ReadVec(Vm, Desc.Pred, Index);
        Loss += W * W;
    }
    Loss *= 0.5f * Desc.L2Lambda;
    if (Desc.LossOut != 0) {
        WriteFp32At(Vm, Desc.LossOut, 0, Loss);
    }
    Vm.NsxLastLossDesc = DescriptorAddress(Insn);
    if (Insn.Rd <= 15) {
        Uint128 Packed = 0;
        WriteFp32Lane(Packed, 0, Loss);
        WriteXReg(Vm, Insn.Rd, Packed);
    }
}

void OpNsxTransformer(Cpu& Vm, const DecodedInsn& Insn) {
    if (Insn.Rd > 15) {
        throw std::out_of_range("NSX TRANSFORMER destination register out of range");
    }
    const NsxTransformerDescriptor Desc =
        LoadTransformerDescriptor(Vm, DescriptorAddress(Insn));
    ValidateTransformer(Desc);
    const uint32_t N = Desc.SeqLen;
    const uint32_t D = Desc.HeadDim;
    const uint64_t Ws = Desc.Workspace;
    const uint64_t Wq = Ws + 0;
    const uint64_t Wk = Ws + 64;
    const uint64_t Wv = Ws + 128;
    const uint64_t Scores = Ws + 192;
    const uint64_t Attn = Ws + 256;

    std::vector<float> Qmat;
    MatMul(Vm, Desc.Input, Desc.Wq, Qmat, N, D, D);
    StoreMatrix(Vm, Wq, Qmat, N, D);
    std::vector<float> Kmat;
    MatMul(Vm, Desc.Input, Desc.Wk, Kmat, N, D, D);
    StoreMatrix(Vm, Wk, Kmat, N, D);
    std::vector<float> Vmat;
    MatMul(Vm, Desc.Input, Desc.Wv, Vmat, N, D, D);
    StoreMatrix(Vm, Wv, Vmat, N, D);

    for (uint32_t Row = 0; Row < N; ++Row) {
        for (uint32_t Col = 0; Col < D; ++Col) {
            WriteFp32(Vm, Attn, Row, Col, D, 0.0f);
        }
    }
    AttentionForward(Vm, Wq, Wk, Wv, Attn, Scores, N, D);

    std::vector<float> Outmat;
    MatMul(Vm, Attn, Desc.Wout, Outmat, N, D, D);
    StoreMatrix(Vm, Desc.Output, Outmat, N, D);

    for (uint32_t Row = 0; Row < N; ++Row) {
        WriteVec(Vm, Desc.Output, Row, Outmat[static_cast<size_t>(Row) * D + 0]);
    }

    Vm.NsxLastTransformerDesc = DescriptorAddress(Insn);
    Uint128 Packed = 0;
    for (uint32_t Lane = 0; Lane < std::min(N, NsxBaselineProfile::SsxLaneCount); ++Lane) {
        WriteFp32Lane(Packed, Lane, ReadVec(Vm, Desc.Output, Lane));
    }
    WriteXReg(Vm, Insn.Rd, Packed);
}

void BackpropMseToPred(Cpu& Vm, const NsxLossDescriptor& LossDesc,
                       uint64_t PredGradBase) {
    const float Inv = 2.0f / static_cast<float>(LossDesc.Count);
    for (uint32_t Index = 0; Index < LossDesc.Count; ++Index) {
        const float Diff =
            ReadVec(Vm, LossDesc.Pred, Index) - ReadVec(Vm, LossDesc.Target, Index);
        WriteFp32At(Vm, PredGradBase, Index, Inv * Diff);
    }
}

void BackpropTransformer(Cpu& Vm, const NsxTransformerDescriptor& Desc,
                         uint64_t PredGradBase) {
    const uint32_t N = Desc.SeqLen;
    const uint32_t D = Desc.HeadDim;
    const uint64_t Ws = Desc.Workspace;
    const uint64_t Wq = Ws + 0;
    const uint64_t Wk = Ws + 64;
    const uint64_t Wv = Ws + 128;
    const uint64_t Scores = Ws + 192;
    const uint64_t Attn = Ws + 256;

    std::vector<float> DOut(N * D, 0.0f);
    for (uint32_t Row = 0; Row < N; ++Row) {
        DOut[static_cast<size_t>(Row) * D] = ReadFp32At(Vm, PredGradBase, Row);
    }
    StoreMatrix(Vm, Ws + 320, DOut, N, D);
    const uint64_t OutGrad = Ws + 320;

    if (Desc.GradWout != 0) {
        for (uint32_t Row = 0; Row < D; ++Row) {
            for (uint32_t Col = 0; Col < D; ++Col) {
                WriteFp32(Vm, Desc.GradWout, Row, Col, D, 0.0f);
            }
        }
        WeightGradFromActivation(Vm, Attn, OutGrad, Desc.GradWout, N, D, D);
    }

    std::vector<float> DAttn(static_cast<size_t>(N) * D, 0.0f);
    for (uint32_t Row = 0; Row < N; ++Row) {
        for (uint32_t Col = 0; Col < D; ++Col) {
            float Acc = 0.0f;
            for (uint32_t Inner = 0; Inner < D; ++Inner) {
                Acc = std::fma(ReadFp32(Vm, OutGrad, Row, Inner, D),
                               ReadFp32(Vm, Desc.Wout, Inner, Col, D), Acc);
            }
            DAttn[static_cast<size_t>(Row) * D + Col] = Acc;
        }
    }
    StoreMatrix(Vm, Ws + 320, DAttn, N, D);

    const uint64_t QGrad = Ws + 384;
    const uint64_t KGrad = Ws + 448;
    const uint64_t VGrad = Ws + 512;
    for (uint32_t Index = 0; Index < N * D; ++Index) {
        WriteFp32At(Vm, QGrad, Index, 0.0f);
        WriteFp32At(Vm, KGrad, Index, 0.0f);
        WriteFp32At(Vm, VGrad, Index, 0.0f);
    }
    AttentionBackward(Vm, Scores, Wq, Wk, Wv, Ws + 320, QGrad, KGrad, VGrad, N, D);

    if (Desc.GradWq != 0) {
        for (uint32_t Row = 0; Row < D; ++Row) {
            for (uint32_t Col = 0; Col < D; ++Col) {
                WriteFp32(Vm, Desc.GradWq, Row, Col, D, 0.0f);
            }
        }
        WeightGradFromActivation(Vm, Desc.Input, QGrad, Desc.GradWq, N, D, D);
    }
    if (Desc.GradWk != 0) {
        for (uint32_t Row = 0; Row < D; ++Row) {
            for (uint32_t Col = 0; Col < D; ++Col) {
                WriteFp32(Vm, Desc.GradWk, Row, Col, D, 0.0f);
            }
        }
        WeightGradFromActivation(Vm, Desc.Input, KGrad, Desc.GradWk, N, D, D);
    }
    if (Desc.GradWv != 0) {
        for (uint32_t Row = 0; Row < D; ++Row) {
            for (uint32_t Col = 0; Col < D; ++Col) {
                WriteFp32(Vm, Desc.GradWv, Row, Col, D, 0.0f);
            }
        }
        WeightGradFromActivation(Vm, Desc.Input, VGrad, Desc.GradWv, N, D, D);
    }
}

void OpNsxFwdStart(Cpu& Vm, const DecodedInsn& Insn) {
    Vm.NsxTapeAddr = DescriptorAddress(Insn);
    Vm.NsxLastTransformerDesc = 0;
    Vm.NsxLastLossDesc = 0;
}

void OpNsxFwdEnd(Cpu& Vm) {
    if (Vm.NsxTapeAddr == 0) {
        throw std::runtime_error("fwd_end without active tape (fwd_start)");
    }
    const uint64_t Tape = Vm.NsxTapeAddr;
    const uint64_t TfmDescAddr = Vm.Read64(Tape);
    const uint64_t LossDescAddr = Vm.Read64(Tape + 8);
    const uint64_t PredGrad = Vm.Read64(Tape + 16);
    if (LossDescAddr == 0 || PredGrad == 0) {
        throw std::runtime_error("fwd_end tape missing loss_desc or pred_grad");
    }
    const NsxLossDescriptor LossDesc = LoadLossDescriptor(Vm, LossDescAddr);
    BackpropMseToPred(Vm, LossDesc, PredGrad);
    if (TfmDescAddr != 0) {
        const NsxTransformerDescriptor TfmDesc = LoadTransformerDescriptor(Vm, TfmDescAddr);
        BackpropTransformer(Vm, TfmDesc, PredGrad);
    }
    Vm.NsxTapeAddr = 0;
}

void OpNsxSgd(Cpu& Vm, const DecodedInsn& Insn) {
    const NsxSgdDescriptor Desc = LoadSgdDescriptor(Vm, DescriptorAddress(Insn));
    for (uint32_t Index = 0; Index < Desc.Count; ++Index) {
        const float Weight = ReadFp32At(Vm, Desc.Weights, Index);
        const float Grad = ReadFp32At(Vm, Desc.Grads, Index);
        WriteFp32At(Vm, Desc.Weights, Index, Weight - Desc.LearningRate * Grad);
    }
    if (Insn.Rd <= 15) {
        WriteXReg(Vm, Insn.Rd, 0);
    }
}

} // namespace

void Honeycomb::ExecuteNsxMlOpcode(Cpu& Vm, const DecodedInsn& Insn) {
    switch (Insn.Op) {
        case Opcode::MSE_LOSS:
            OpNsxMseLoss(Vm, Insn);
            return;
        case Opcode::CROSS_ENTROPY:
            OpNsxCrossEntropy(Vm, Insn);
            return;
        case Opcode::L2_LOSS:
            OpNsxL2Loss(Vm, Insn);
            return;
        case Opcode::TRANSFORMER:
            OpNsxTransformer(Vm, Insn);
            return;
        case Opcode::FWD_START:
            OpNsxFwdStart(Vm, Insn);
            return;
        case Opcode::FWD_END:
            OpNsxFwdEnd(Vm);
            return;
        case Opcode::SGD:
            OpNsxSgd(Vm, Insn);
            return;
        case Opcode::BWD_GRAD:
            if (Vm.NsxLastLossDesc == 0) {
                throw std::runtime_error("bwd_grad requires prior mse_loss");
            }
            if (!Insn.Imm1Flag) {
                throw std::runtime_error("bwd_grad requires imm1 pred_grad buffer");
            }
            BackpropMseToPred(Vm, LoadLossDescriptor(Vm, Vm.NsxLastLossDesc), Insn.Imm1);
            return;
        default:
            throw std::runtime_error("Not an NSX ML opcode");
    }
}
