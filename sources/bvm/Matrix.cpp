#include "Shared.h"
#include "MatProfile.h"
#include "RegFile.h"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace Honeycomb;

namespace {

float ReadFp32Element(const Cpu& Vm, uint64_t Base, uint32_t Row, uint32_t Col,
                      uint32_t InnerDim) {
    const uint64_t Offset =
        static_cast<uint64_t>(Row) * static_cast<uint64_t>(InnerDim) +
        static_cast<uint64_t>(Col);
    const uint32_t Bits = Vm.Read32(Base + Offset * MatBaselineProfile::ElemBytesFp32);
    float Value = 0.0f;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

void WriteFp32Element(Cpu& Vm, uint64_t Base, uint32_t Row, uint32_t Col,
                      uint32_t InnerDim, float Value) {
    const uint64_t Offset =
        static_cast<uint64_t>(Row) * static_cast<uint64_t>(InnerDim) +
        static_cast<uint64_t>(Col);
    uint32_t Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    Vm.Write32(Base + Offset * MatBaselineProfile::ElemBytesFp32, Bits);
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

MatMulDescriptor LoadMatDescriptor(const Cpu& Vm, uint64_t Address) {
    MatMulDescriptor Desc{};
    Desc.AddrA = Vm.Read64(Address);
    Desc.AddrB = Vm.Read64(Address + 8);
    Desc.AddrC = Vm.Read64(Address + 16);
    const uint64_t Meta = Vm.Read64(Address + 24);
    Desc.ElemBytes = static_cast<uint32_t>((Meta >> 48) & 0xFFFF);
    Desc.Rows = static_cast<uint32_t>((Meta >> 32) & 0xFFFF);
    Desc.Inner = static_cast<uint32_t>((Meta >> 16) & 0xFFFF);
    Desc.Cols = static_cast<uint32_t>(Meta & 0xFFFF);
    return Desc;
}

GemmDescriptor LoadGemmDescriptor(const Cpu& Vm, uint64_t Address) {
    GemmDescriptor Desc{};
    const MatMulDescriptor Base = LoadMatDescriptor(Vm, Address);
    Desc.AddrA = Base.AddrA;
    Desc.AddrB = Base.AddrB;
    Desc.AddrC = Base.AddrC;
    Desc.Rows = Base.Rows;
    Desc.Inner = Base.Inner;
    Desc.Cols = Base.Cols;
    Desc.ElemBytes = Base.ElemBytes;
    const uint64_t Tail = Vm.Read64(Address + MatBaselineProfile::MatDescriptorBytes);
    uint32_t AlphaBits = static_cast<uint32_t>(Tail >> 32);
    uint32_t BetaBits = static_cast<uint32_t>(Tail);
    std::memcpy(&Desc.Alpha, &AlphaBits, sizeof(Desc.Alpha));
    std::memcpy(&Desc.Beta, &BetaBits, sizeof(Desc.Beta));
    return Desc;
}

TensorDescriptor LoadTensorDescriptor(const Cpu& Vm, uint64_t Address) {
    TensorDescriptor Desc{};
    Desc.AddrMem = Vm.Read64(Address);
    const uint64_t Meta = Vm.Read64(Address + 24);
    Desc.ElemBytes = static_cast<uint32_t>((Meta >> 48) & 0xFFFF);
    Desc.Height = static_cast<uint32_t>((Meta >> 32) & 0xFFFF);
    Desc.Width = static_cast<uint32_t>((Meta >> 16) & 0xFFFF);
    Desc.Stride = static_cast<uint32_t>(Meta & 0xFFFF);
    if (Desc.Stride == 0) {
        Desc.Stride = Desc.Width;
    }
    return Desc;
}

Conv2DDescriptor LoadConvDescriptor(const Cpu& Vm, uint64_t Address) {
    Conv2DDescriptor Desc{};
    Desc.AddrInput = Vm.Read64(Address);
    Desc.AddrKernel = Vm.Read64(Address + 8);
    Desc.AddrOutput = Vm.Read64(Address + 16);
    const uint64_t MetaIn = Vm.Read64(Address + 24);
    Desc.ElemBytes = static_cast<uint32_t>((MetaIn >> 48) & 0xFFFF);
    Desc.HeightIn = static_cast<uint32_t>((MetaIn >> 32) & 0xFFFF);
    Desc.WidthIn = static_cast<uint32_t>((MetaIn >> 16) & 0xFFFF);
    Desc.ChannelsIn = static_cast<uint32_t>(MetaIn & 0xFFFF);
    if (Desc.ChannelsIn == 0) {
        Desc.ChannelsIn = 1;
    }
    const uint64_t MetaKernel = Vm.Read64(Address + 32);
    Desc.KernelH = static_cast<uint32_t>((MetaKernel >> 16) & 0xFFFF);
    Desc.KernelW = static_cast<uint32_t>(MetaKernel & 0xFFFF);
    return Desc;
}

QdotDescriptor LoadQdotDescriptor(const Cpu& Vm, uint64_t Address) {
    QdotDescriptor Desc{};
    Desc.AddrA = Vm.Read64(Address);
    Desc.AddrB = Vm.Read64(Address + 8);
    Desc.AddrScore = Vm.Read64(Address + 16);
    const uint64_t Meta = Vm.Read64(Address + 24);
    Desc.ElemBytes = MatBaselineProfile::ElemBytesInt8;
    Desc.Length = static_cast<uint32_t>((Meta >> 16) & 0xFFFF);
    if (Desc.Length == 0) {
        Desc.Length = static_cast<uint32_t>(Meta & 0xFFFF);
    }
    const uint32_t ScaleBits = Vm.Read32(Address + 36);
    std::memcpy(&Desc.Scale, &ScaleBits, sizeof(Desc.Scale));
    return Desc;
}

void ValidateTileDims(uint32_t Rows, uint32_t Inner, uint32_t Cols) {
    if (Rows == 0 || Inner == 0 || Cols == 0) {
        throw std::runtime_error("Matrix descriptor dimensions must be non-zero");
    }
    if (Rows > MatBaselineProfile::MaxDim || Inner > MatBaselineProfile::MaxDim ||
        Cols > MatBaselineProfile::MaxDim) {
        throw std::runtime_error("Matrix dimensions exceed baseline BVM tile limit (4)");
    }
}

void ValidateMat(const MatMulDescriptor& Desc) {
    if (Desc.ElemBytes != MatBaselineProfile::ElemBytesFp32) {
        throw std::runtime_error(
            "Baseline BVM matrix ops require ElemBytes=4 (FP32) in descriptor");
    }
    ValidateTileDims(Desc.Rows, Desc.Inner, Desc.Cols);
}

void ValidateTensor(const TensorDescriptor& Desc) {
    if (Desc.ElemBytes != MatBaselineProfile::ElemBytesFp32) {
        throw std::runtime_error("Tensor op requires FP32 ElemBytes=4");
    }
    if (Desc.Height == 0 || Desc.Width == 0 ||
        Desc.Height > MatBaselineProfile::MaxDim ||
        Desc.Width > MatBaselineProfile::MaxDim) {
        throw std::runtime_error("Tensor H/W out of baseline range (1..4)");
    }
}

void ValidateConv(const Conv2DDescriptor& Desc) {
    if (Desc.ElemBytes != MatBaselineProfile::ElemBytesFp32) {
        throw std::runtime_error("CONV2D requires FP32 ElemBytes=4");
    }
    if (Desc.HeightIn == 0 || Desc.WidthIn == 0 || Desc.KernelH == 0 ||
        Desc.KernelW == 0) {
        throw std::runtime_error("CONV2D descriptor dimensions must be non-zero");
    }
    if (Desc.KernelH > Desc.HeightIn || Desc.KernelW > Desc.WidthIn) {
        throw std::runtime_error("CONV2D kernel larger than input");
    }
}

Uint128 PackTileFromMemory(const Cpu& Vm, uint64_t Base, uint32_t Rows, uint32_t Cols) {
    Uint128 Packed = 0;
    uint32_t Index = 0;
    for (uint32_t Row = 0; Row < Rows; ++Row) {
        for (uint32_t Col = 0; Col < Cols; ++Col) {
            if (Index >= 4) {
                return Packed;
            }
            WriteFp32Lane(Packed, Index,
                            ReadFp32Element(Vm, Base, Row, Col, Cols));
            ++Index;
        }
    }
    return Packed;
}

void StoreTileToMemory(Cpu& Vm, uint64_t Base, uint32_t Rows, uint32_t Cols,
                       Uint128 Value) {
    uint32_t Index = 0;
    for (uint32_t Row = 0; Row < Rows; ++Row) {
        for (uint32_t Col = 0; Col < Cols; ++Col) {
            if (Index >= 4) {
                return;
            }
            WriteFp32Element(Vm, Base, Row, Col, Cols, ReadFp32Lane(Value, Index));
            ++Index;
        }
    }
}

Uint128 PackResultTile(const std::vector<float>& Values, uint32_t Rows, uint32_t Cols) {
    Uint128 Packed = 0;
    uint32_t Index = 0;
    for (uint32_t Row = 0; Row < Rows; ++Row) {
        for (uint32_t Col = 0; Col < Cols; ++Col) {
            if (Index >= 4) {
                break;
            }
            const float Value = Values[Index];
            WriteFp32Lane(Packed, Index, Value);
            ++Index;
        }
    }
    return Packed;
}

void WriteResultTile(Cpu& Vm, uint8_t DestReg, const MatMulDescriptor& Desc,
                     const std::vector<float>& Values) {
    WriteXReg(Vm, DestReg, PackResultTile(Values, Desc.Rows, Desc.Cols));
    if (Desc.AddrC != 0) {
        StoreTileToMemory(Vm, Desc.AddrC, Desc.Rows, Desc.Cols,
                          ReadXReg(Vm, DestReg));
    }
}

std::vector<float> MultiplyMatrices(const Cpu& Vm, const MatMulDescriptor& Desc) {
    std::vector<float> Result(static_cast<size_t>(Desc.Rows) * Desc.Cols, 0.0f);
    for (uint32_t Row = 0; Row < Desc.Rows; ++Row) {
        for (uint32_t Col = 0; Col < Desc.Cols; ++Col) {
            float Sum = 0.0f;
            for (uint32_t Inner = 0; Inner < Desc.Inner; ++Inner) {
                const float Left =
                    ReadFp32Element(Vm, Desc.AddrA, Row, Inner, Desc.Inner);
                const float Right =
                    ReadFp32Element(Vm, Desc.AddrB, Inner, Col, Desc.Cols);
                Sum = std::fma(Left, Right, Sum);
            }
            Result[static_cast<size_t>(Row) * Desc.Cols + Col] = Sum;
        }
    }
    return Result;
}

std::vector<float> MultiplyMatricesQ8(const Cpu& Vm, const MatMulDescriptor& Desc,
                                      float Scale) {
    std::vector<float> Result(static_cast<size_t>(Desc.Rows) * Desc.Cols, 0.0f);
    for (uint32_t Row = 0; Row < Desc.Rows; ++Row) {
        for (uint32_t Col = 0; Col < Desc.Cols; ++Col) {
            int32_t Sum = 0;
            for (uint32_t Inner = 0; Inner < Desc.Inner; ++Inner) {
                const uint64_t OffsetA =
                    static_cast<uint64_t>(Row) * Desc.Inner + Inner;
                const uint64_t OffsetB =
                    static_cast<uint64_t>(Inner) * Desc.Cols + Col;
                const int8_t Left =
                    static_cast<int8_t>(Vm.Read8(Desc.AddrA + OffsetA));
                const int8_t Right =
                    static_cast<int8_t>(Vm.Read8(Desc.AddrB + OffsetB));
                Sum += static_cast<int32_t>(Left) * static_cast<int32_t>(Right);
            }
            Result[static_cast<size_t>(Row) * Desc.Cols + Col] = Scale *
                                                                static_cast<float>(Sum);
        }
    }
    return Result;
}

float DotProductPacked(Uint128 Left, Uint128 Right) {
    float Sum = 0.0f;
    for (uint32_t Lane = 0; Lane < 4; ++Lane) {
        Sum = std::fma(ReadFp32Lane(Left, Lane), ReadFp32Lane(Right, Lane), Sum);
    }
    return Sum;
}

uint64_t DescriptorAddress(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("Matrix/ML op requires imm1 descriptor address");
    }
    return Insn.Imm1;
}

} // namespace

void Cpu::ExecuteMatrixOpcode(const DecodedInsn& Insn) {
    switch (Insn.Op) {
        case Opcode::VDOTP: {
            if (Insn.Rd > 7 || Insn.Rs > 7 || Insn.Rt > 7) {
                throw std::out_of_range("VDOTP X register out of range");
            }
            const float Dot = DotProductPacked(ReadXReg(*this, Insn.Rs),
                                               ReadXReg(*this, Insn.Rt));
            Uint128 Dest = 0;
            WriteFp32Lane(Dest, 0, Dot);
            WriteXReg(*this, Insn.Rd, Dest);
            return;
        }
        case Opcode::DOTP_ACC: {
            if (Insn.Rd > 3 || Insn.Rs > 7 || Insn.Rt > 7) {
                throw std::out_of_range("DOTP_ACC register out of range");
            }
            const float Dot = DotProductPacked(ReadXReg(*this, Insn.Rs),
                                               ReadXReg(*this, Insn.Rt));
            Uint128& Acc = AccRegRef(*this, Insn.Rd);
            Acc = PackResultTile({ReadFp32Lane(Acc, 0) + Dot}, 1, 1);
            return;
        }
        case Opcode::TENSOR_LOAD: {
            if (Insn.Rd > 7) {
                throw std::out_of_range("TENSOR_LOAD X register out of range");
            }
            const TensorDescriptor Desc =
                LoadTensorDescriptor(*this, DescriptorAddress(Insn));
            ValidateTensor(Desc);
            WriteXReg(*this, Insn.Rd,
                      PackTileFromMemory(*this, Desc.AddrMem, Desc.Height, Desc.Width));
            return;
        }
        case Opcode::TENSOR_STORE: {
            if (Insn.Rd > 7) {
                throw std::out_of_range("TENSOR_STORE X register out of range");
            }
            const TensorDescriptor Desc =
                LoadTensorDescriptor(*this, DescriptorAddress(Insn));
            ValidateTensor(Desc);
            StoreTileToMemory(*this, Desc.AddrMem, Desc.Height, Desc.Width,
                              ReadXReg(*this, Insn.Rd));
            return;
        }
        case Opcode::CONV2D: {
            if (Insn.Rd > 7) {
                throw std::out_of_range("CONV2D X register out of range");
            }
            const Conv2DDescriptor Desc =
                LoadConvDescriptor(*this, DescriptorAddress(Insn));
            ValidateConv(Desc);
            const uint32_t HeightOut = Desc.HeightIn - Desc.KernelH + 1;
            const uint32_t WidthOut = Desc.WidthIn - Desc.KernelW + 1;
            std::vector<float> Output(static_cast<size_t>(HeightOut) * WidthOut, 0.0f);
            for (uint32_t OutRow = 0; OutRow < HeightOut; ++OutRow) {
                for (uint32_t OutCol = 0; OutCol < WidthOut; ++OutCol) {
                    float Sum = 0.0f;
                    for (uint32_t Kh = 0; Kh < Desc.KernelH; ++Kh) {
                        for (uint32_t Kw = 0; Kw < Desc.KernelW; ++Kw) {
                            const float Input = ReadFp32Element(
                                *this, Desc.AddrInput, OutRow + Kh, OutCol + Kw,
                                Desc.WidthIn);
                            const float Kernel = ReadFp32Element(
                                *this, Desc.AddrKernel, Kh, Kw, Desc.KernelW);
                            Sum = std::fma(Input, Kernel, Sum);
                        }
                    }
                    Output[static_cast<size_t>(OutRow) * WidthOut + OutCol] = Sum;
                }
            }
            MatMulDescriptor OutDesc{};
            OutDesc.Rows = HeightOut;
            OutDesc.Cols = WidthOut;
            OutDesc.Inner = WidthOut;
            OutDesc.AddrC = Desc.AddrOutput;
            WriteResultTile(*this, Insn.Rd, OutDesc, Output);
            return;
        }
        case Opcode::QDOT: {
            if (Insn.Rd > 7) {
                throw std::out_of_range("QDOT X register out of range");
            }
            const QdotDescriptor Desc =
                LoadQdotDescriptor(*this, DescriptorAddress(Insn));
            if (Desc.Length == 0 || Desc.Length > 16) {
                throw std::runtime_error("QDOT length out of baseline range (1..16)");
            }
            int32_t Acc = 0;
            for (uint32_t Index = 0; Index < Desc.Length; ++Index) {
                const int8_t Left =
                    static_cast<int8_t>(Read8(Desc.AddrA + Index));
                const int8_t Right =
                    static_cast<int8_t>(Read8(Desc.AddrB + Index));
                Acc += static_cast<int32_t>(Left) * static_cast<int32_t>(Right);
            }
            const float Score = Desc.Scale * static_cast<float>(Acc);
            Uint128 Dest = 0;
            WriteFp32Lane(Dest, 0, Score);
            WriteXReg(*this, Insn.Rd, Dest);
            if (Desc.AddrScore != 0) {
                uint32_t Bits = 0;
                std::memcpy(&Bits, &Score, sizeof(Bits));
                Write64(Desc.AddrScore, static_cast<uint64_t>(Bits) << 32);
            }
            return;
        }
        case Opcode::MATMUL: {
            if (Insn.Rd > 7) {
                throw std::out_of_range("MATMUL X register out of range");
            }
            const MatMulDescriptor Desc =
                LoadMatDescriptor(*this, DescriptorAddress(Insn));
            ValidateMat(Desc);
            WriteResultTile(*this, Insn.Rd, Desc, MultiplyMatrices(*this, Desc));
            return;
        }
        case Opcode::GEMM_OFFLOAD:
        case Opcode::QGEMM: {
            if (Insn.Rd > 7) {
                throw std::out_of_range("GEMM X register out of range");
            }
            GemmDescriptor Desc = LoadGemmDescriptor(*this, DescriptorAddress(Insn));
            if (Insn.Op == Opcode::QGEMM) {
                if (Desc.ElemBytes != MatBaselineProfile::ElemBytesInt8) {
                    throw std::runtime_error("QGEMM requires int8 ElemBytes=1 in meta");
                }
                ValidateTileDims(Desc.Rows, Desc.Inner, Desc.Cols);
                const float Scale = Desc.Alpha;
                std::vector<float> Product =
                    MultiplyMatricesQ8(*this, Desc, Scale);
                if (Desc.AddrC != 0 && Desc.Beta != 0.0f) {
                    for (uint32_t Row = 0; Row < Desc.Rows; ++Row) {
                        for (uint32_t Col = 0; Col < Desc.Cols; ++Col) {
                            const size_t Index = static_cast<size_t>(Row) * Desc.Cols + Col;
                            const float Existing =
                                ReadFp32Element(*this, Desc.AddrC, Row, Col, Desc.Cols);
                            Product[Index] =
                                Desc.Alpha * Product[Index] + Desc.Beta * Existing;
                        }
                    }
                }
                WriteResultTile(*this, Insn.Rd, Desc, Product);
                return;
            }
            ValidateMat(Desc);
            std::vector<float> Product = MultiplyMatrices(*this, Desc);
            if (Desc.AddrC != 0 && Desc.Beta != 0.0f) {
                for (uint32_t Row = 0; Row < Desc.Rows; ++Row) {
                    for (uint32_t Col = 0; Col < Desc.Cols; ++Col) {
                        const size_t Index = static_cast<size_t>(Row) * Desc.Cols + Col;
                        const float Existing =
                            ReadFp32Element(*this, Desc.AddrC, Row, Col, Desc.Cols);
                        Product[Index] =
                            Desc.Alpha * Product[Index] + Desc.Beta * Existing;
                    }
                }
            } else if (Desc.Alpha != 1.0f) {
                for (float& Value : Product) {
                    Value *= Desc.Alpha;
                }
            }
            WriteResultTile(*this, Insn.Rd, Desc, Product);
            return;
        }
        default:
            throw std::runtime_error("Unimplemented matrix opcode in baseline BVM");
    }
}
