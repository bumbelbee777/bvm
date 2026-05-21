#include "Shared.h"
#include "CplxProfile.h"
#include "RegFile.h"

#include <cmath>
#include <complex>
#include <cstring>
#include <stdexcept>
#include <string>

using namespace Honeycomb;

namespace {

struct CplxGprFp64 {
    double Real = 0.0;
    double Imag = 0.0;
};

struct CplxGprFp32 {
    float Real = 0.0f;
    float Imag = 0.0f;
};

[[noreturn]] void UnknownCplxOpcode(Opcode Op) {
    throw std::runtime_error(
        "Unknown complex opcode: 0x" + std::to_string(static_cast<unsigned>(Op)));
}

void WriteScalarGpr(Cpu& Vm, uint8_t Code, double Value) {
    uint64_t Raw = 0;
    std::memcpy(&Raw, &Value, sizeof(Raw));
    Vm.WriteGpr(Code, Raw);
}

double ReadScalarGpr(const Cpu& Vm, uint8_t Code) {
    uint64_t Raw = Vm.ReadGpr(Code);
    double Value = 0.0;
    std::memcpy(&Value, &Raw, sizeof(Value));
    return Value;
}

CplxGprFp64 ComplexDiv(const CplxGprFp64& A, const CplxGprFp64& B) {
    const double Den = B.Real * B.Real + B.Imag * B.Imag;
    if (Den == 0.0) {
        throw std::runtime_error("CDIV division by zero");
    }
    return {(A.Real * B.Real + A.Imag * B.Imag) / Den,
            (A.Imag * B.Real - A.Real * B.Imag) / Den};
}

double ComplexMag(const CplxGprFp64& Z) {
    return std::hypot(Z.Real, Z.Imag);
}

CplxGprFp64 FromStdComplex(const std::complex<double>& Value) {
    return {Value.real(), Value.imag()};
}

uint64_t KComplexOffset(const Cpu& Vm, uint8_t IndexReg) {
    return (Vm.Wind + Vm.ReadIndexReg(IndexReg)) & Vm.KMask;
}

void ReadKComplex(const Cpu& Vm, uint64_t BaseIndex, CplxGprFp64& Out) {
    const uint8_t Stride = CplxProfile::KBankComplexStride(Vm.WinSz);
    uint64_t Raw = Vm.ReadKReg((BaseIndex) & Vm.KMask);
    std::memcpy(&Out.Real, &Raw, sizeof(Out.Real));
    Raw = Vm.ReadKReg((BaseIndex + Stride) & Vm.KMask);
    std::memcpy(&Out.Imag, &Raw, sizeof(Out.Imag));
}

void WriteKComplex(Cpu& Vm, uint64_t BaseIndex, const CplxGprFp64& Value) {
    const uint8_t Stride = CplxProfile::KBankComplexStride(Vm.WinSz);
    uint64_t Raw = 0;
    std::memcpy(&Raw, &Value.Real, sizeof(Raw));
    Vm.WriteKReg((BaseIndex) & Vm.KMask, Raw);
    std::memcpy(&Raw, &Value.Imag, sizeof(Raw));
    Vm.WriteKReg((BaseIndex + Stride) & Vm.KMask, Raw);
}

uint32_t BitReverse(uint32_t Index, uint32_t Bits) {
    uint32_t Out = 0;
    for (uint32_t Bit = 0; Bit < Bits; ++Bit) {
        Out = (Out << 1) | (Index & 1);
        Index >>= 1;
    }
    return Out;
}

bool IsCplxInsn(const DecodedInsn& Insn) {
    return Insn.Opmode == OpmodeMixed &&
           ((Insn.Disp >> 27) & 7) == CplxProfile::CplxSubopTag;
}

CplxProfile::Precision ActivePrecision(const Cpu& Vm, const DecodedInsn& Insn) {
    if (IsCplxInsn(Insn)) {
        return CplxProfile::PrecisionFromDisp(Insn.Disp);
    }
    return CplxProfile::PrecisionFromWinSz(Vm.WinSz);
}

uint8_t ComplexIndex(uint8_t Field) {
    return Field & 0x3F;
}

uint8_t ComplexXIndex(uint8_t Field) {
    return Field & 0x3F;
}

CplxGprFp64 ReadCplxFp64(const Cpu& Vm, uint8_t Index) {
    if (Index > CplxProfile::MaxComplexIndexFp64) {
        throw std::out_of_range("C_FP64 complex index out of range (CR0..CR15)");
    }
    const uint8_t RealCode = static_cast<uint8_t>(Index * 2);
    const uint8_t ImagCode = static_cast<uint8_t>(Index * 2 + 1);
    CplxGprFp64 Value;
    uint64_t Raw = Vm.ReadGpr(RealCode);
    std::memcpy(&Value.Real, &Raw, sizeof(Value.Real));
    Raw = Vm.ReadGpr(ImagCode);
    std::memcpy(&Value.Imag, &Raw, sizeof(Value.Imag));
    return Value;
}

void WriteCplxFp64(Cpu& Vm, uint8_t Index, const CplxGprFp64& Value) {
    const uint8_t RealCode = static_cast<uint8_t>(Index * 2);
    const uint8_t ImagCode = static_cast<uint8_t>(Index * 2 + 1);
    uint64_t Raw = 0;
    std::memcpy(&Raw, &Value.Real, sizeof(Value.Real));
    Vm.WriteGpr(RealCode, Raw);
    std::memcpy(&Raw, &Value.Imag, sizeof(Value.Imag));
    Vm.WriteGpr(ImagCode, Raw);
}

CplxGprFp32 ReadCplxFp32(const Cpu& Vm, uint8_t Index) {
    if (Index > 31) {
        throw std::out_of_range("C_FP32 complex GPR index out of range");
    }
    const uint64_t Bits = Vm.ReadGpr(Index);
    CplxGprFp32 Value;
    std::memcpy(&Value.Real, &Bits, sizeof(Value.Real));
    std::memcpy(&Value.Imag, reinterpret_cast<const uint8_t*>(&Bits) + 4,
                sizeof(Value.Imag));
    return Value;
}

void WriteCplxFp32(Cpu& Vm, uint8_t Index, const CplxGprFp32& Value) {
    uint64_t Bits = 0;
    std::memcpy(&Bits, &Value.Real, sizeof(Value.Real));
    std::memcpy(reinterpret_cast<uint8_t*>(&Bits) + 4, &Value.Imag, sizeof(Value.Imag));
    Vm.WriteGpr(Index, Bits);
}

CplxGprFp64 ReadCplx(const Cpu& Vm, uint8_t Index, CplxProfile::Precision Prec) {
    if (Prec == CplxProfile::Precision::Fp32) {
        const CplxGprFp32 Packed = ReadCplxFp32(Vm, Index);
        return {Packed.Real, Packed.Imag};
    }
    return ReadCplxFp64(Vm, Index);
}

void WriteCplx(Cpu& Vm, uint8_t Index, const CplxGprFp64& Value, CplxProfile::Precision Prec) {
    if (Prec == CplxProfile::Precision::Fp32) {
        CplxGprFp32 Packed;
        Packed.Real = static_cast<float>(Value.Real);
        Packed.Imag = static_cast<float>(Value.Imag);
        WriteCplxFp32(Vm, Index, Packed);
        return;
    }
    WriteCplxFp64(Vm, Index, Value);
}

uint8_t ConjugateFlags(const DecodedInsn& Insn) {
    return static_cast<uint8_t>((Insn.Disp >> 20) & 3);
}

CplxGprFp64 ComplexMul(const CplxGprFp64& A, const CplxGprFp64& B) {
    const std::complex<double> Ca(A.Real, A.Imag);
    const std::complex<double> Cb(B.Real, B.Imag);
    const std::complex<double> Prod = Ca * Cb;
    return {Prod.real(), Prod.imag()};
}

CplxGprFp64 ComplexAdd(const CplxGprFp64& A, const CplxGprFp64& B) {
    return {A.Real + B.Real, A.Imag + B.Imag};
}

CplxGprFp64 ComplexSub(const CplxGprFp64& A, const CplxGprFp64& B) {
    return {A.Real - B.Real, A.Imag - B.Imag};
}

void ApplyConj(CplxGprFp64& Value, uint8_t Flags) {
    if (Flags & 1) {
        Value.Imag = -Value.Imag;
    }
}

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

CplxGprFp32 ReadComplexLaneFp32(Uint128 Value, uint32_t ComplexLane) {
    CplxGprFp32 Out;
    Out.Real = ReadFp32Lane(Value, ComplexLane * 2);
    Out.Imag = ReadFp32Lane(Value, ComplexLane * 2 + 1);
    return Out;
}

void WriteComplexLaneFp32(Uint128& Value, uint32_t ComplexLane, const CplxGprFp32& Z) {
    WriteFp32Lane(Value, ComplexLane * 2, Z.Real);
    WriteFp32Lane(Value, ComplexLane * 2 + 1, Z.Imag);
}

CplxGprFp64 ToFp64(const CplxGprFp32& Z) {
    return {Z.Real, Z.Imag};
}

CplxGprFp32 ToFp32(const CplxGprFp64& Z) {
    return {static_cast<float>(Z.Real), static_cast<float>(Z.Imag)};
}

CplxGprFp32 ComplexMulFp32(const CplxGprFp32& A, const CplxGprFp32& B) {
    const std::complex<float> Ca(A.Real, A.Imag);
    const std::complex<float> Cb(B.Real, B.Imag);
    const std::complex<float> Prod = Ca * Cb;
    return {Prod.real(), Prod.imag()};
}

CplxGprFp32 ComplexDivFp32(const CplxGprFp32& A, const CplxGprFp32& B) {
    return ToFp32(ComplexDiv(ToFp64(A), ToFp64(B)));
}

uint32_t ComplexLaneCount(const Cpu& Vm) {
    uint32_t Vl = Vm.SsxVectorLength;
    if (Vl == 0) {
        Vl = SsxBaselineProfile::DefaultVectorLength;
    }
    const uint32_t Complexes = Vl / 2;
    return Complexes > 0 ? Complexes : 1;
}

void ExecuteVectorComplex(Cpu& Vm, const DecodedInsn& Insn) {
    const uint8_t Xd = ComplexXIndex(Insn.Rd);
    const uint8_t Xs = ComplexXIndex(Insn.Rs);
    const uint8_t Xt = ComplexXIndex(Insn.Rt);
    const uint32_t LaneCount = ComplexLaneCount(Vm);

    switch (Insn.Op) {
        case Opcode::VCADD: {
            Uint128 Dest = ReadXReg(Vm, Xd);
            const Uint128 Src1 = ReadXReg(Vm, Xs);
            const Uint128 Src2 = ReadXReg(Vm, Xt);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp32 A = ReadComplexLaneFp32(Src1, Lane);
                const CplxGprFp32 B = ReadComplexLaneFp32(Src2, Lane);
                WriteComplexLaneFp32(Dest, Lane,
                                     ToFp32(ComplexAdd(ToFp64(A), ToFp64(B))));
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCSUB: {
            Uint128 Dest = ReadXReg(Vm, Xd);
            const Uint128 Src1 = ReadXReg(Vm, Xs);
            const Uint128 Src2 = ReadXReg(Vm, Xt);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp32 A = ReadComplexLaneFp32(Src1, Lane);
                const CplxGprFp32 B = ReadComplexLaneFp32(Src2, Lane);
                WriteComplexLaneFp32(Dest, Lane,
                                     ToFp32(ComplexSub(ToFp64(A), ToFp64(B))));
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCMUL: {
            Uint128 Dest = ReadXReg(Vm, Xd);
            const Uint128 Src1 = ReadXReg(Vm, Xs);
            const Uint128 Src2 = ReadXReg(Vm, Xt);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp32 A = ReadComplexLaneFp32(Src1, Lane);
                const CplxGprFp32 B = ReadComplexLaneFp32(Src2, Lane);
                WriteComplexLaneFp32(Dest, Lane, ComplexMulFp32(A, B));
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCFMA:
        case Opcode::VCMAC: {
            Uint128 Dest = ReadXReg(Vm, Xd);
            const Uint128 Src1 = ReadXReg(Vm, Xs);
            const Uint128 Src2 = ReadXReg(Vm, Xt);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp32 Acc = ReadComplexLaneFp32(Dest, Lane);
                const CplxGprFp32 Prod =
                    ComplexMulFp32(ReadComplexLaneFp32(Src1, Lane),
                                   ReadComplexLaneFp32(Src2, Lane));
                WriteComplexLaneFp32(
                    Dest, Lane,
                    ToFp32(ComplexAdd(ToFp64(Acc), ToFp64(Prod))));
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCFMS: {
            Uint128 Dest = ReadXReg(Vm, Xd);
            const Uint128 Src1 = ReadXReg(Vm, Xs);
            const Uint128 Src2 = ReadXReg(Vm, Xt);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp32 Acc = ReadComplexLaneFp32(Dest, Lane);
                const CplxGprFp32 Prod =
                    ComplexMulFp32(ReadComplexLaneFp32(Src1, Lane),
                                   ReadComplexLaneFp32(Src2, Lane));
                WriteComplexLaneFp32(
                    Dest, Lane,
                    ToFp32(ComplexSub(ToFp64(Acc), ToFp64(Prod))));
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCDIV: {
            Uint128 Dest = ReadXReg(Vm, Xd);
            const Uint128 Src1 = ReadXReg(Vm, Xs);
            const Uint128 Src2 = ReadXReg(Vm, Xt);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                WriteComplexLaneFp32(
                    Dest, Lane,
                    ComplexDivFp32(ReadComplexLaneFp32(Src1, Lane),
                                   ReadComplexLaneFp32(Src2, Lane)));
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCMAG: {
            Uint128 Dest = 0;
            const Uint128 Src = ReadXReg(Vm, Xs);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp32 Z = ReadComplexLaneFp32(Src, Lane);
                WriteFp32Lane(Dest, Lane * 2, std::hypot(Z.Real, Z.Imag));
                WriteFp32Lane(Dest, Lane * 2 + 1, 0.0f);
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCARG: {
            Uint128 Dest = 0;
            const Uint128 Src = ReadXReg(Vm, Xs);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp32 Z = ReadComplexLaneFp32(Src, Lane);
                WriteFp32Lane(Dest, Lane * 2, std::atan2(Z.Imag, Z.Real));
                WriteFp32Lane(Dest, Lane * 2 + 1, 0.0f);
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCSCALE: {
            const float Scale = ReadFp32Lane(ReadXReg(Vm, Xt), 0);
            Uint128 Dest = ReadXReg(Vm, Xd);
            const Uint128 Src = ReadXReg(Vm, Xs);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                CplxGprFp32 Z = ReadComplexLaneFp32(Src, Lane);
                Z.Real *= Scale;
                Z.Imag *= Scale;
                WriteComplexLaneFp32(Dest, Lane, Z);
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCROT: {
            const float Theta = ReadFp32Lane(ReadXReg(Vm, Xt), 0);
            const std::complex<float> W = std::polar(1.0f, Theta);
            Uint128 Dest = ReadXReg(Vm, Xd);
            const Uint128 Src = ReadXReg(Vm, Xs);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp32 Z = ReadComplexLaneFp32(Src, Lane);
                const std::complex<float> Prod =
                    W * std::complex<float>(Z.Real, Z.Imag);
                WriteComplexLaneFp32(Dest, Lane, {Prod.real(), Prod.imag()});
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCLOAD: {
            uint64_t Addr = Insn.Imm1Flag ? Insn.Imm1 : static_cast<uint64_t>(Insn.Disp);
            Uint128 Dest = 0;
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                uint32_t RealBits = Vm.Read32(Addr);
                uint32_t ImagBits = Vm.Read32(Addr + 4);
                float Real = 0.0f;
                float Imag = 0.0f;
                std::memcpy(&Real, &RealBits, sizeof(Real));
                std::memcpy(&Imag, &ImagBits, sizeof(Imag));
                WriteComplexLaneFp32(Dest, Lane, {Real, Imag});
                Addr += 8;
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCSTORE: {
            uint64_t Addr = Insn.Imm1Flag ? Insn.Imm1 : static_cast<uint64_t>(Insn.Disp);
            const Uint128 Src = ReadXReg(Vm, Xd);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp32 Z = ReadComplexLaneFp32(Src, Lane);
                uint32_t RealBits = 0;
                uint32_t ImagBits = 0;
                std::memcpy(&RealBits, &Z.Real, sizeof(RealBits));
                std::memcpy(&ImagBits, &Z.Imag, sizeof(ImagBits));
                Vm.Write32(Addr, RealBits);
                Vm.Write32(Addr + 4, ImagBits);
                Addr += 8;
            }
            break;
        }
        case Opcode::VCGATHER: {
            const uint64_t Base =
                Insn.Imm1Flag ? Insn.Imm1 : static_cast<uint64_t>(Insn.Disp);
            const uint64_t IndexBase = Vm.ReadIndexReg(Xs);
            Uint128 Dest = 0;
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const uint64_t Addr = Base + ((IndexBase + Lane) * 8);
                uint32_t RealBits = Vm.Read32(Addr);
                uint32_t ImagBits = Vm.Read32(Addr + 4);
                float Real = 0.0f;
                float Imag = 0.0f;
                std::memcpy(&Real, &RealBits, sizeof(Real));
                std::memcpy(&Imag, &ImagBits, sizeof(Imag));
                WriteComplexLaneFp32(Dest, Lane, {Real, Imag});
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCSCATTER: {
            const uint64_t Base =
                Insn.Imm1Flag ? Insn.Imm1 : static_cast<uint64_t>(Insn.Disp);
            const uint64_t IndexBase = Vm.ReadIndexReg(Xs);
            const Uint128 Src = ReadXReg(Vm, Xd);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const uint64_t Addr = Base + ((IndexBase + Lane) * 8);
                const CplxGprFp32 Z = ReadComplexLaneFp32(Src, Lane);
                uint32_t RealBits = 0;
                uint32_t ImagBits = 0;
                std::memcpy(&RealBits, &Z.Real, sizeof(RealBits));
                std::memcpy(&ImagBits, &Z.Imag, sizeof(ImagBits));
                Vm.Write32(Addr, RealBits);
                Vm.Write32(Addr + 4, ImagBits);
            }
            break;
        }
        case Opcode::VCSPLAT: {
            float Real = 0.0f;
            float Imag = 0.0f;
            if (Insn.Imm1Flag) {
                std::memcpy(&Real, &Insn.Imm1, sizeof(Real));
                if (Insn.Imm2Flag) {
                    std::memcpy(&Imag, &Insn.Imm2, sizeof(Imag));
                }
            }
            Uint128 Dest = 0;
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                WriteComplexLaneFp32(Dest, Lane, {Real, Imag});
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCDOT: {
            CplxGprFp64 Sum{0.0, 0.0};
            const Uint128 Left = ReadXReg(Vm, Xs);
            const Uint128 Right = ReadXReg(Vm, Xt);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                CplxGprFp64 L = ToFp64(ReadComplexLaneFp32(Left, Lane));
                CplxGprFp64 R = ToFp64(ReadComplexLaneFp32(Right, Lane));
                L.Imag = -L.Imag;
                Sum = ComplexAdd(Sum, ComplexMul(L, R));
            }
            WriteCplx(Vm, ComplexIndex(Insn.Rd), Sum, CplxProfile::Precision::Fp32);
            break;
        }
        case Opcode::VCNORM: {
            double SumSq = 0.0;
            const Uint128 Src = ReadXReg(Vm, Xs);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp64 Z = ToFp64(ReadComplexLaneFp32(Src, Lane));
                SumSq += Z.Real * Z.Real + Z.Imag * Z.Imag;
            }
            WriteScalarGpr(Vm, Insn.Rd, std::sqrt(SumSq));
            break;
        }
        case Opcode::VCREDUCE: {
            const uint64_t Op = Insn.Imm1Flag ? Insn.Imm1 : 0;
            const Uint128 Src = ReadXReg(Vm, Xs);
            if (Op == 2) {
                double SumSq = 0.0;
                for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                    const CplxGprFp64 Z = ToFp64(ReadComplexLaneFp32(Src, Lane));
                    SumSq += Z.Real * Z.Real + Z.Imag * Z.Imag;
                }
                WriteScalarGpr(Vm, Insn.Rd, std::sqrt(SumSq));
            } else {
                CplxGprFp64 Sum{0.0, 0.0};
                for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                    Sum = ComplexAdd(Sum, ToFp64(ReadComplexLaneFp32(Src, Lane)));
                }
                WriteCplx(Vm, ComplexIndex(Insn.Rd), Sum, CplxProfile::Precision::Fp32);
            }
            break;
        }
        case Opcode::VCCMP: {
            const Uint128 Left = ReadXReg(Vm, Xs);
            const Uint128 Right = ReadXReg(Vm, Xt);
            const uint64_t Rel = Insn.Imm1Flag ? Insn.Imm1 : 0;
            uint64_t Mask = 0;
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp64 L = ToFp64(ReadComplexLaneFp32(Left, Lane));
                const CplxGprFp64 R = ToFp64(ReadComplexLaneFp32(Right, Lane));
                bool Pass = false;
                switch (Rel) {
                    case 0:
                        Pass = (L.Real == R.Real && L.Imag == R.Imag);
                        break;
                    case 1:
                        Pass = (L.Real != R.Real || L.Imag != R.Imag);
                        break;
                    case 2:
                        Pass = ComplexMag(L) < ComplexMag(R);
                        break;
                    case 3:
                        Pass = ComplexMag(L) > ComplexMag(R);
                        break;
                    default:
                        break;
                }
                if (Pass) {
                    Mask |= 1ULL << Lane;
                }
            }
            Vm.KMask = Mask;
            break;
        }
        case Opcode::VCSEL: {
            const bool Select = (Vm.KMask & 1) != 0;
            const Uint128 TrueVal = ReadXReg(Vm, Xs);
            const Uint128 FalseVal = ReadXReg(Vm, Xt);
            Uint128 Dest = 0;
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                WriteComplexLaneFp32(
                    Dest, Lane,
                    ReadComplexLaneFp32(Select ? TrueVal : FalseVal, Lane));
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCMIN:
        case Opcode::VCMAX: {
            const bool PickMax = (Insn.Op == Opcode::VCMAX);
            Uint128 Dest = ReadXReg(Vm, Xd);
            const Uint128 A = ReadXReg(Vm, Xs);
            const Uint128 B = ReadXReg(Vm, Xt);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp64 La = ToFp64(ReadComplexLaneFp32(A, Lane));
                const CplxGprFp64 Lb = ToFp64(ReadComplexLaneFp32(B, Lane));
                const double Ma = ComplexMag(La);
                const double Mb = ComplexMag(Lb);
                WriteComplexLaneFp32(
                    Dest, Lane,
                    (PickMax == (Ma >= Mb)) ? ReadComplexLaneFp32(A, Lane)
                                            : ReadComplexLaneFp32(B, Lane));
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCZIP: {
            Uint128 Dest = 0;
            const Uint128 Reals = ReadXReg(Vm, Xs);
            const Uint128 Imags = ReadXReg(Vm, Xt);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                WriteFp32Lane(Dest, Lane * 2, ReadFp32Lane(Reals, Lane * 2));
                WriteFp32Lane(Dest, Lane * 2 + 1, ReadFp32Lane(Imags, Lane * 2));
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCUNZIP: {
            const Uint128 Src = ReadXReg(Vm, Xs);
            Uint128 Reals = 0;
            Uint128 Imags = 0;
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                WriteFp32Lane(Reals, Lane * 2, ReadFp32Lane(Src, Lane * 2));
                WriteFp32Lane(Imags, Lane * 2, ReadFp32Lane(Src, Lane * 2 + 1));
            }
            WriteXReg(Vm, Xd, Reals);
            WriteXReg(Vm, Xt, Imags);
            break;
        }
        case Opcode::VCSWAP: {
            Uint128 Dest = ReadXReg(Vm, Xs);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const float Real = ReadFp32Lane(Dest, Lane * 2);
                const float Imag = ReadFp32Lane(Dest, Lane * 2 + 1);
                WriteFp32Lane(Dest, Lane * 2, Imag);
                WriteFp32Lane(Dest, Lane * 2 + 1, Real);
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCREVERSE: {
            const uint32_t Bits =
                LaneCount > 1 ? static_cast<uint32_t>(std::log2(LaneCount)) : 0;
            Uint128 Dest = 0;
            const Uint128 Src = ReadXReg(Vm, Xs);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                WriteComplexLaneFp32(Dest, BitReverse(Lane, Bits),
                                     ReadComplexLaneFp32(Src, Lane));
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCTWIDDLE: {
            const uint64_t K = Insn.Imm1Flag ? Insn.Imm1 : 0;
            const uint32_t N = LaneCount > 0 ? LaneCount * 2 : 2;
            const double Angle =
                -2.0 * 3.14159265358979323846 * static_cast<double>(K) /
                static_cast<double>(N);
            const std::complex<float> W(static_cast<float>(std::cos(Angle)),
                                        static_cast<float>(std::sin(Angle)));
            Uint128 Dest = ReadXReg(Vm, Xd);
            const Uint128 Src = ReadXReg(Vm, Xs);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp32 Z = ReadComplexLaneFp32(Src, Lane);
                const std::complex<float> Prod =
                    W * std::complex<float>(Z.Real, Z.Imag);
                WriteComplexLaneFp32(Dest, Lane, {Prod.real(), Prod.imag()});
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCCONJ: {
            Uint128 Dest = ReadXReg(Vm, Xd);
            const Uint128 Src = ReadXReg(Vm, Xs);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                CplxGprFp32 Z = ReadComplexLaneFp32(Src, Lane);
                Z.Imag = -Z.Imag;
                WriteComplexLaneFp32(Dest, Lane, Z);
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCBROADCAST: {
            const CplxGprFp64 Scalar =
                ReadCplx(Vm, ComplexIndex(Insn.Rs), CplxProfile::Precision::Fp32);
            const CplxGprFp32 Packed = ToFp32(Scalar);
            Uint128 Dest = 0;
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                WriteComplexLaneFp32(Dest, Lane, Packed);
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCFFT2: {
            const Uint128 Src = ReadXReg(Vm, Xs);
            Uint128 Dest = Src;
            const uint32_t Half = LaneCount / 2;
            if (Half == 0) {
                throw std::runtime_error("VCFFT2 requires at least two complex lanes");
            }
            for (uint32_t Index = 0; Index < Half; ++Index) {
                const CplxGprFp32 A = ReadComplexLaneFp32(Src, Index);
                const CplxGprFp32 B = ReadComplexLaneFp32(Src, Index + Half);
                const CplxGprFp32 W{1.0f, 0.0f};
                const CplxGprFp32 Bw = ComplexMulFp32(B, W);
                WriteComplexLaneFp32(Dest, Index,
                                     ToFp32(ComplexAdd(ToFp64(A), ToFp64(Bw))));
                WriteComplexLaneFp32(Dest, Index + Half,
                                     ToFp32(ComplexSub(ToFp64(A), ToFp64(Bw))));
            }
            WriteXReg(Vm, Xd, Dest);
            break;
        }
        case Opcode::VCFFT4: {
            const uint8_t Xs2 = Xt;
            std::array<CplxGprFp32, 4> Points{};
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                Points[Lane] = ReadComplexLaneFp32(ReadXReg(Vm, Xs), Lane);
                Points[Lane + LaneCount] =
                    ReadComplexLaneFp32(ReadXReg(Vm, Xs2), Lane);
            }
            const std::complex<float> Z0(Points[0].Real, Points[0].Imag);
            const std::complex<float> Z1(Points[1].Real, Points[1].Imag);
            const std::complex<float> Z2(Points[2].Real, Points[2].Imag);
            const std::complex<float> Z3(Points[3].Real, Points[3].Imag);
            const std::complex<float> J(0.0f, 1.0f);
            const std::array<std::complex<float>, 4> Out = {
                Z0 + Z1 + Z2 + Z3,
                Z0 - J * Z1 - Z2 + J * Z3,
                Z0 - Z1 + Z2 - Z3,
                Z0 + J * Z1 - Z2 - J * Z3,
            };
            Uint128 DestLo = 0;
            Uint128 DestHi = 0;
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                WriteComplexLaneFp32(DestLo, Lane,
                                     {Out[Lane].real(), Out[Lane].imag()});
                WriteComplexLaneFp32(DestHi, Lane,
                                     {Out[Lane + LaneCount].real(),
                                      Out[Lane + LaneCount].imag()});
            }
            WriteXReg(Vm, Xd, DestLo);
            WriteXReg(Vm, Xs2, DestHi);
            break;
        }
        default:
            UnknownCplxOpcode(Insn.Op);
    }
}

} // namespace

void Cpu::ExecuteCplxOpcode(const DecodedInsn& Insn) {
    if (!IsCplxInsn(Insn)) {
        throw std::runtime_error("Complex opcode requires opmode=101 and complex subop in disp");
    }

    const CplxProfile::Precision Prec = ActivePrecision(*this, Insn);
    const uint8_t Rd = ComplexIndex(Insn.Rd);
    const uint8_t Rs = ComplexIndex(Insn.Rs);
    const uint8_t Rt = ComplexIndex(Insn.Rt);
    const uint8_t Conj = ConjugateFlags(Insn);

    switch (Insn.Op) {
        case Opcode::VCADD:
        case Opcode::VCSUB:
        case Opcode::VCMUL:
        case Opcode::VCDIV:
        case Opcode::VCFMA:
        case Opcode::VCFMS:
        case Opcode::VCMAC:
        case Opcode::VCMAG:
        case Opcode::VCARG:
        case Opcode::VCCONJ:
        case Opcode::VCSCALE:
        case Opcode::VCROT:
        case Opcode::VCDOT:
        case Opcode::VCNORM:
        case Opcode::VCLOAD:
        case Opcode::VCSTORE:
        case Opcode::VCGATHER:
        case Opcode::VCSCATTER:
        case Opcode::VCBROADCAST:
        case Opcode::VCSPLAT:
        case Opcode::VCREDUCE:
        case Opcode::VCCMP:
        case Opcode::VCSEL:
        case Opcode::VCMIN:
        case Opcode::VCMAX:
        case Opcode::VCZIP:
        case Opcode::VCUNZIP:
        case Opcode::VCSWAP:
        case Opcode::VCREVERSE:
        case Opcode::VCTWIDDLE:
        case Opcode::VCFFT2:
        case Opcode::VCFFT4:
            ExecuteVectorComplex(*this, Insn);
            return;

        case Opcode::CFMA:
        case Opcode::CMAC: {
            CplxGprFp64 Acc = ReadCplx(*this, Rd, Prec);
            CplxGprFp64 Prod = ComplexMul(ReadCplx(*this, Rs, Prec), ReadCplx(*this, Rt, Prec));
            WriteCplx(*this, Rd, ComplexAdd(Acc, Prod), Prec);
            break;
        }
        case Opcode::CFMS: {
            CplxGprFp64 Acc = ReadCplx(*this, Rd, Prec);
            CplxGprFp64 Prod = ComplexMul(ReadCplx(*this, Rs, Prec), ReadCplx(*this, Rt, Prec));
            WriteCplx(*this, Rd, ComplexSub(Acc, Prod), Prec);
            break;
        }
        case Opcode::CNFMA: {
            CplxGprFp64 Acc = ReadCplx(*this, Rd, Prec);
            Acc.Real = -Acc.Real;
            Acc.Imag = -Acc.Imag;
            CplxGprFp64 Prod = ComplexMul(ReadCplx(*this, Rs, Prec), ReadCplx(*this, Rt, Prec));
            WriteCplx(*this, Rd, ComplexAdd(Acc, Prod), Prec);
            break;
        }
        case Opcode::CNFMS: {
            CplxGprFp64 Acc = ReadCplx(*this, Rd, Prec);
            Acc.Real = -Acc.Real;
            Acc.Imag = -Acc.Imag;
            CplxGprFp64 Prod = ComplexMul(ReadCplx(*this, Rs, Prec), ReadCplx(*this, Rt, Prec));
            WriteCplx(*this, Rd, ComplexSub(Acc, Prod), Prec);
            break;
        }
        case Opcode::CADD: {
            CplxGprFp64 Lhs = ReadCplx(*this, Rs, Prec);
            CplxGprFp64 Rhs = ReadCplx(*this, Rt, Prec);
            ApplyConj(Lhs, Conj & 1);
            ApplyConj(Rhs, Conj & 2);
            WriteCplx(*this, Rd, ComplexAdd(Lhs, Rhs), Prec);
            break;
        }
        case Opcode::CSUB: {
            CplxGprFp64 Lhs = ReadCplx(*this, Rs, Prec);
            CplxGprFp64 Rhs = ReadCplx(*this, Rt, Prec);
            ApplyConj(Lhs, Conj & 1);
            ApplyConj(Rhs, Conj & 2);
            WriteCplx(*this, Rd, ComplexSub(Lhs, Rhs), Prec);
            break;
        }
        case Opcode::CMUL: {
            CplxGprFp64 A = ReadCplx(*this, Rs, Prec);
            CplxGprFp64 B = ReadCplx(*this, Rt, Prec);
            ApplyConj(A, Conj & 1);
            ApplyConj(B, Conj & 2);
            WriteCplx(*this, Rd, ComplexMul(A, B), Prec);
            break;
        }
        case Opcode::CCONJ: {
            CplxGprFp64 Value = ReadCplx(*this, Rs, Prec);
            Value.Imag = -Value.Imag;
            WriteCplx(*this, Rd, Value, Prec);
            break;
        }
        case Opcode::CNEG: {
            CplxGprFp64 Value = ReadCplx(*this, Rs, Prec);
            Value.Real = -Value.Real;
            Value.Imag = -Value.Imag;
            WriteCplx(*this, Rd, Value, Prec);
            break;
        }
        case Opcode::CMAG:
        case Opcode::CABS: {
            const CplxGprFp64 Value = ReadCplx(*this, Rs, Prec);
            const double Mag = std::hypot(Value.Real, Value.Imag);
            uint64_t Raw = 0;
            std::memcpy(&Raw, &Mag, sizeof(Raw));
            WriteGpr(Insn.Rd, Raw);
            break;
        }
        case Opcode::CMSQ: {
            const CplxGprFp64 Value = ReadCplx(*this, Rs, Prec);
            const double MagSq = Value.Real * Value.Real + Value.Imag * Value.Imag;
            uint64_t Raw = 0;
            std::memcpy(&Raw, &MagSq, sizeof(Raw));
            WriteGpr(Insn.Rd, Raw);
            break;
        }
        case Opcode::CPACK: {
            CplxGprFp64 Out;
            uint64_t Raw = ReadGpr(Insn.Rs);
            std::memcpy(&Out.Real, &Raw, sizeof(Out.Real));
            Raw = ReadGpr(Insn.Rt);
            std::memcpy(&Out.Imag, &Raw, sizeof(Out.Imag));
            WriteCplx(*this, Rd, Out, Prec);
            break;
        }
        case Opcode::CEXTRACT: {
            const CplxGprFp64 Value = ReadCplx(*this, Rs, Prec);
            const uint64_t Which = Insn.Imm1Flag ? Insn.Imm1 : static_cast<uint64_t>(Insn.Disp);
            const double Scalar = (Which & 1) ? Value.Imag : Value.Real;
            uint64_t Raw = 0;
            std::memcpy(&Raw, &Scalar, sizeof(Raw));
            WriteGpr(Insn.Rd, Raw);
            break;
        }
        case Opcode::CMOVE: {
            WriteCplx(*this, Rd, ReadCplx(*this, Rs, Prec), Prec);
            break;
        }
        case Opcode::CLOAD: {
            const uint64_t Addr = Insn.Imm1Flag ? Insn.Imm1 : static_cast<uint64_t>(Insn.Disp);
            CplxGprFp64 Value;
            uint64_t Raw = Read64(Addr);
            std::memcpy(&Value.Real, &Raw, sizeof(Value.Real));
            Raw = Read64(Addr + 8);
            std::memcpy(&Value.Imag, &Raw, sizeof(Value.Imag));
            WriteCplx(*this, Rd, Value, Prec);
            break;
        }
        case Opcode::CSTORE: {
            const uint64_t Addr = Insn.Imm1Flag ? Insn.Imm1 : static_cast<uint64_t>(Insn.Disp);
            const CplxGprFp64 Value = ReadCplx(*this, Rd, Prec);
            uint64_t Raw = 0;
            std::memcpy(&Raw, &Value.Real, sizeof(Raw));
            Write64(Addr, Raw);
            std::memcpy(&Raw, &Value.Imag, sizeof(Raw));
            Write64(Addr + 8, Raw);
            break;
        }
        case Opcode::CDIV: {
            CplxGprFp64 A = ReadCplx(*this, Rs, Prec);
            CplxGprFp64 B = ReadCplx(*this, Rt, Prec);
            ApplyConj(A, Conj & 1);
            ApplyConj(B, Conj & 2);
            WriteCplx(*this, Rd, ComplexDiv(A, B), Prec);
            break;
        }
        case Opcode::CARG: {
            const CplxGprFp64 Value = ReadCplx(*this, Rs, Prec);
            WriteScalarGpr(*this, Insn.Rd, std::atan2(Value.Imag, Value.Real));
            break;
        }
        case Opcode::CMAGMUL: {
            const CplxGprFp64 A = ReadCplx(*this, Rs, Prec);
            const CplxGprFp64 B = ReadCplx(*this, Rt, Prec);
            WriteScalarGpr(*this, Insn.Rd, ComplexMag(A) * ComplexMag(B));
            break;
        }
        case Opcode::CSCALE: {
            const double Scale = ReadScalarGpr(*this, Insn.Rt);
            CplxGprFp64 Value = ReadCplx(*this, Rs, Prec);
            Value.Real *= Scale;
            Value.Imag *= Scale;
            WriteCplx(*this, Rd, Value, Prec);
            break;
        }
        case Opcode::CROT: {
            const double Theta = ReadScalarGpr(*this, Insn.Rt);
            const std::complex<double> W = std::polar(1.0, Theta);
            const CplxGprFp64 Value = ReadCplx(*this, Rs, Prec);
            WriteCplx(*this, Rd,
                      FromStdComplex(W * std::complex<double>(Value.Real, Value.Imag)),
                      Prec);
            break;
        }
        case Opcode::CEXP: {
            const CplxGprFp64 Value = ReadCplx(*this, Rs, Prec);
            WriteCplx(*this, Rd,
                      FromStdComplex(std::exp(std::complex<double>(Value.Real, Value.Imag))),
                      Prec);
            break;
        }
        case Opcode::CLOG: {
            const CplxGprFp64 Value = ReadCplx(*this, Rs, Prec);
            WriteCplx(*this, Rd,
                      FromStdComplex(std::log(std::complex<double>(Value.Real, Value.Imag))),
                      Prec);
            break;
        }
        case Opcode::CPOW: {
            const CplxGprFp64 Base = ReadCplx(*this, Rs, Prec);
            const CplxGprFp64 Exp = ReadCplx(*this, Rt, Prec);
            WriteCplx(
                *this, Rd,
                FromStdComplex(std::pow(std::complex<double>(Base.Real, Base.Imag),
                                        std::complex<double>(Exp.Real, Exp.Imag))),
                Prec);
            break;
        }
        case Opcode::CSQRT: {
            const CplxGprFp64 Value = ReadCplx(*this, Rs, Prec);
            WriteCplx(*this, Rd,
                      FromStdComplex(std::sqrt(std::complex<double>(Value.Real, Value.Imag))),
                      Prec);
            break;
        }
        case Opcode::CDOT: {
            CplxGprFp64 Sum{0.0, 0.0};
            const Uint128 Left = ReadXReg(*this, ComplexXIndex(Insn.Rs));
            const Uint128 Right = ReadXReg(*this, ComplexXIndex(Insn.Rt));
            const uint32_t LaneCount = ComplexLaneCount(*this);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                CplxGprFp64 L = ToFp64(ReadComplexLaneFp32(Left, Lane));
                CplxGprFp64 R = ToFp64(ReadComplexLaneFp32(Right, Lane));
                L.Imag = -L.Imag;
                Sum = ComplexAdd(Sum, ComplexMul(L, R));
            }
            WriteCplx(*this, Rd, Sum, Prec);
            break;
        }
        case Opcode::CNORM: {
            double SumSq = 0.0;
            const Uint128 Src = ReadXReg(*this, ComplexXIndex(Insn.Rs));
            const uint32_t LaneCount = ComplexLaneCount(*this);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                const CplxGprFp64 Z = ToFp64(ReadComplexLaneFp32(Src, Lane));
                SumSq += Z.Real * Z.Real + Z.Imag * Z.Imag;
            }
            WriteScalarGpr(*this, Insn.Rd, std::sqrt(SumSq));
            break;
        }
        case Opcode::CLOAD_SOA: {
            const uint64_t RealAddr =
                Insn.Imm1Flag ? Insn.Imm1 : static_cast<uint64_t>(Insn.Disp);
            const uint64_t ImagAddr = Insn.Imm2Flag ? Insn.Imm2 : RealAddr + 8;
            CplxGprFp64 Value;
            uint64_t Raw = Read64(RealAddr);
            std::memcpy(&Value.Real, &Raw, sizeof(Value.Real));
            Raw = Read64(ImagAddr);
            std::memcpy(&Value.Imag, &Raw, sizeof(Value.Imag));
            WriteCplx(*this, Rd, Value, Prec);
            break;
        }
        case Opcode::CSTORE_SOA: {
            const uint64_t RealAddr =
                Insn.Imm1Flag ? Insn.Imm1 : static_cast<uint64_t>(Insn.Disp);
            const uint64_t ImagAddr = Insn.Imm2Flag ? Insn.Imm2 : RealAddr + 8;
            const CplxGprFp64 Value = ReadCplx(*this, Rd, Prec);
            uint64_t Raw = 0;
            std::memcpy(&Raw, &Value.Real, sizeof(Raw));
            Write64(RealAddr, Raw);
            std::memcpy(&Raw, &Value.Imag, sizeof(Raw));
            Write64(ImagAddr, Raw);
            break;
        }
        case Opcode::CSWAP: {
            const CplxGprFp64 Left = ReadCplx(*this, Rd, Prec);
            const CplxGprFp64 Right = ReadCplx(*this, Rs, Prec);
            WriteCplx(*this, Rd, Right, Prec);
            WriteCplx(*this, Rs, Left, Prec);
            break;
        }
        case Opcode::CINSERT: {
            const uint64_t Which = Insn.Imm1Flag ? Insn.Imm1 : 0;
            CplxGprFp64 Value = ReadCplx(*this, Rd, Prec);
            const double Scalar = ReadScalarGpr(*this, Insn.Rs);
            if (Which & 1) {
                Value.Imag = Scalar;
            } else {
                Value.Real = Scalar;
            }
            WriteCplx(*this, Rd, Value, Prec);
            break;
        }
        case Opcode::CUNPACK: {
            const CplxGprFp64 Value = ReadCplx(*this, Rs, Prec);
            WriteScalarGpr(*this, Insn.Rd, Value.Real);
            WriteScalarGpr(*this, Insn.Rt, Value.Imag);
            break;
        }
        case Opcode::CSPLAT: {
            float Real = 0.0f;
            float Imag = 0.0f;
            if (Insn.Imm1Flag) {
                std::memcpy(&Real, &Insn.Imm1, sizeof(Real));
            }
            if (Insn.Imm2Flag) {
                std::memcpy(&Imag, &Insn.Imm2, sizeof(Imag));
            }
            Uint128 Dest = 0;
            const uint32_t LaneCount = ComplexLaneCount(*this);
            for (uint32_t Lane = 0; Lane < LaneCount; ++Lane) {
                WriteComplexLaneFp32(Dest, Lane, {Real, Imag});
            }
            WriteXReg(*this, ComplexXIndex(Insn.Rd), Dest);
            break;
        }
        case Opcode::CCEQ:
        case Opcode::CCNE:
        case Opcode::CCMPLT:
        case Opcode::CCMPGT: {
            const CplxGprFp64 L = ReadCplx(*this, Rs, Prec);
            const CplxGprFp64 R = ReadCplx(*this, Rt, Prec);
            bool Pass = false;
            switch (Insn.Op) {
                case Opcode::CCEQ:
                    Pass = (L.Real == R.Real && L.Imag == R.Imag);
                    break;
                case Opcode::CCNE:
                    Pass = (L.Real != R.Real || L.Imag != R.Imag);
                    break;
                case Opcode::CCMPLT:
                    Pass = ComplexMag(L) < ComplexMag(R);
                    break;
                case Opcode::CCMPGT:
                    Pass = ComplexMag(L) > ComplexMag(R);
                    break;
                default:
                    break;
            }
            WriteGpr(Insn.Rd, Pass ? 1 : 0);
            break;
        }
        case Opcode::CSEL: {
            const bool Select = (ReadGpr(Insn.Rt) & 1) != 0;
            WriteCplx(*this, Rd,
                      ReadCplx(*this, Select ? Rs : Rt, Prec), Prec);
            break;
        }
        case Opcode::CMIN:
        case Opcode::CMAX: {
            const CplxGprFp64 A = ReadCplx(*this, Rs, Prec);
            const CplxGprFp64 B = ReadCplx(*this, Rt, Prec);
            const bool PickMax = (Insn.Op == Opcode::CMAX);
            WriteCplx(*this, Rd, (PickMax == (ComplexMag(A) >= ComplexMag(B))) ? A : B,
                      Prec);
            break;
        }
        case Opcode::CKLOAD: {
            CplxGprFp64 Value;
            ReadKComplex(*this, KComplexOffset(*this, Insn.Rs), Value);
            WriteCplx(*this, Rd, Value, Prec);
            break;
        }
        case Opcode::CKSTORE: {
            const CplxGprFp64 Value = ReadCplx(*this, Rd, Prec);
            WriteKComplex(*this, KComplexOffset(*this, Insn.Rs), Value);
            break;
        }
        default:
            UnknownCplxOpcode(Insn.Op);
    }
}
