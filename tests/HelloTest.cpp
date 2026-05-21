#include "../sources/Isa.h"
#include "../sources/assembler/Assembler.h"
#include "../sources/bvm/Shared.h"
#include "../sources/bvm/CplxProfile.h"
#include "../sources/bvm/PagingProfile.h"
#include "../sources/bvm/NumaProfile.h"
#include "../sources/bvm/SsxCrypto.h"
#include "../sources/bvm/Devices.h"
#include "../sources/bvm/VmConfig.h"
#include "../sources/bvm/Machine.h"
#include "HyperCTest.h"

using namespace Honeycomb;

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace {

std::filesystem::path RepoRoot() {
    return std::filesystem::path(__FILE__).parent_path().parent_path();
}

std::filesystem::path ExamplePath(std::filesystem::path Relative) {
    return RepoRoot() / "examples" / Relative;
}

} // namespace

int main() {
    RunHyperCTests();

    const std::filesystem::path ArithmeticPath = ExamplePath("ArithmeticDemo.s");
    const std::filesystem::path ShiftPath = ExamplePath("ShiftRotateDemo.s");
    const std::filesystem::path FibonacciPath = ExamplePath("FibonacciDemo.s");
    const std::filesystem::path CallStackPath = ExamplePath("CallStackDemo.s");
    const std::filesystem::path InterruptPath = ExamplePath("InterruptDemo.s");
    const std::filesystem::path PushImmPath = ExamplePath("PushImmediateDemo.s");
    const std::filesystem::path AtomicPath = ExamplePath("AtomicDemo.s");
    const std::filesystem::path MmioBootPath = ExamplePath("MmioBootSequenceDemo.s");
    const std::filesystem::path FsxPath = ExamplePath("FsxDemo.s");
    const std::filesystem::path FsxFmaPath = ExamplePath("FsxFmaDemo.s");
    const std::filesystem::path SsxPath = ExamplePath("SsxDemo.s");
    const std::filesystem::path SsxCryptoPath = ExamplePath("SsxCryptoDemo.s");
    const std::filesystem::path HpcLanesPath = ExamplePath("HpcLanesDemo.s");
    const std::filesystem::path VectorOpsPath = ExamplePath("VectorOpsDemo.s");
    const std::filesystem::path FsxMathPath = ExamplePath("FsxMathDemo.s");
    const std::filesystem::path FusedNegPath = ExamplePath("FusedNegDemo.s");
    const std::filesystem::path RecipLanesPath = ExamplePath("RecipLanesDemo.s");
    const std::filesystem::path MatMulPath = ExamplePath("MatMul2x2Demo.s");
    const std::filesystem::path GemmAccumPath = ExamplePath("GemmAccumDemo.s");
    const std::filesystem::path MlInferencePath = ExamplePath("MlInferenceDemo.s");
    const std::filesystem::path NsxPath = ExamplePath("NsxDemo.s");
    const std::filesystem::path KBankPath = ExamplePath("KBankDemo.s");
    const std::filesystem::path DaxPath = ExamplePath("DaxDemo.s");
    const std::filesystem::path DaxStreamPath = ExamplePath("DaxStreamDemo.s");
    const std::filesystem::path DaxCrossCorePath = ExamplePath("DaxCrossCoreDemo.s");
    const std::filesystem::path SkbPath = ExamplePath("SkbDemo.s");
    const std::filesystem::path CplxPath = ExamplePath("CplxDemo.s");
    const std::filesystem::path CplxVectorPath = ExamplePath("CplxVectorDemo.s");
    const std::filesystem::path PagingPath = ExamplePath("PagingDemo.s");
    const std::filesystem::path PagingRefillPath =
        ExamplePath("PagingSoftwareRefillDemo.s");
    const std::filesystem::path ExceptionNestPath = ExamplePath("ExceptionNestDemo.s");
    const std::filesystem::path NumaPath = ExamplePath("NumaDemo.s");
    const std::filesystem::path SecuritySandboxPath =
        ExamplePath("SecuritySandboxDemo.s");
    const std::filesystem::path CompactHybridPath = ExamplePath("CompactHybridDemo.s");
    const std::filesystem::path BlockDemoPath = ExamplePath("BlockDemo.s");
    const std::filesystem::path Usb3DemoPath = ExamplePath("Usb3Demo.s");
    const std::filesystem::path Ac97DemoPath = ExamplePath("Ac97Demo.s");
    const std::filesystem::path UartDemoPath = ExamplePath("UartDemo.s");
    const std::filesystem::path DevicePresentDemoPath = ExamplePath("DevicePresentDemo.s");
    const std::filesystem::path PlatformTimerDemoPath = ExamplePath("PlatformTimerDemo.s");
    const std::filesystem::path PlatformRtcDemoPath = ExamplePath("PlatformRtcDemo.s");

    for (const auto& Entry :
         {&ArithmeticPath, &ShiftPath, &FibonacciPath, &CallStackPath, &InterruptPath,
          &PushImmPath, &AtomicPath, &MmioBootPath, &FsxPath, &FsxFmaPath, &FsxMathPath,
          &SsxPath, &SsxCryptoPath, &HpcLanesPath, &VectorOpsPath, &FusedNegPath,
          &RecipLanesPath,
          &MatMulPath, &GemmAccumPath, &MlInferencePath, &NsxPath, &KBankPath, &DaxPath,
          &DaxStreamPath,
          &DaxCrossCorePath, &SkbPath,
          &CplxPath, &CplxVectorPath, &PagingPath, &NumaPath, &SecuritySandboxPath,
          &CompactHybridPath, &BlockDemoPath, &Usb3DemoPath, &Ac97DemoPath, &UartDemoPath,
          &DevicePresentDemoPath, &PlatformTimerDemoPath, &PlatformRtcDemoPath}) {
        if (!std::filesystem::exists(*Entry)) {
            std::cerr << "Missing example: " << Entry->string() << "\n";
            return 1;
        }
    }

    {
        Cpu PagingProbe;
        PagingProbe.Reset();
        constexpr uint64_t Root = 0x8000;
        constexpr uint64_t L3 = 0x9000;
        constexpr uint64_t L2 = 0xA000;
        constexpr uint64_t TableFlags = PagingProfile::PteFlags::Present |
                                        PagingProfile::PteFlags::Read |
                                        PagingProfile::PteFlags::Write;
        constexpr uint64_t LeafFlags = TableFlags | PagingProfile::PteFlags::Execute;
        PagingProbe.Write64Physical(Root, L3 | TableFlags);
        PagingProbe.Write64Physical(L3, L2 | TableFlags);
        PagingProbe.Write64Physical(L2, LeafFlags | PagingProfile::PteFlags::Huge);
        PagingProbe.Write64Physical(L2 + 8, 0x5000ULL | LeafFlags | PagingProfile::PteFlags::Huge);
        PagingProbe.Write64Physical(0x5000, 0x123456789ABCDEF0ULL);
        PagingProbe.Ptb = Root;
        PagingProbe.Fr |= StatusFlags::Paging;
        assert(PagingProbe.Read64(0x200000) == 0x123456789ABCDEF0ULL);
        PagingProbe.Fr &= ~StatusFlags::Paging;
    }

    {
        Cpu NumaProbe;
        NumaProbe.Reset();
        constexpr uint64_t RemoteDram =
            NumaProfile::RemoteDramBaseAddress | (0ULL << NumaProfile::NodeShift);
        constexpr uint64_t RemoteSkbLine =
            NumaProfile::RemoteSkbBaseAddress | (0ULL << NumaProfile::NodeShift);
        NumaProbe.Write64(RemoteDram, 0x1122334455667788ULL);
        assert(NumaProbe.Read64(RemoteDram) == 0x1122334455667788ULL);
        NumaProbe.Write64(RemoteSkbLine, 0xAABBCCDDEEFF0011ULL);
        Honeycomb::DecodedInsn Bind{};
        Bind.Op = Honeycomb::Opcode::NUMABIND;
        Bind.Imm1Flag = true;
        Bind.Imm1 = RemoteSkbLine;
        Bind.Rs = 0;
        NumaProbe.ExecuteKBankOpcode(Bind);
        assert(NumaProbe.ReadKReg(0) == 0xAABBCCDDEEFF0011ULL);
    }

    {
        Cpu MmioProbeCpu;
        MmioProbeCpu.Reset();
        assert(MmioProbeCpu.Read64(MmioWindow::WindowBasePhysicalAddress) ==
               MmioWindow::DeviceIdentityMagicValue);
        MmioProbeCpu.Write64(MmioWindow::WindowBasePhysicalAddress +
                                 static_cast<uint64_t>(
                                     MmioWindow::RegisterQuadWord::BootControl),
                             0x123ULL);
        assert(MmioProbeCpu.Read64(MmioWindow::WindowBasePhysicalAddress +
                                   static_cast<uint64_t>(
                                       MmioWindow::RegisterQuadWord::BootControl)) ==
               0x123ULL);
    }

    {
        Cpu FsProbe;
        FsProbe.Reset();
        const double Three = 3.0;
        const double Four = 4.0;
        uint64_t Bits = 0;
        std::memcpy(&Bits, &Three, sizeof(Bits));
        FsProbe.Write64(0, Bits);
        std::memcpy(&Bits, &Four, sizeof(Bits));
        FsProbe.Write64(8, Bits);

        Honeycomb::DecodedInsn LoadA{};
        LoadA.Op = Honeycomb::Opcode::FLOAD;
        LoadA.Opmode = Honeycomb::OpmodeFp64;
        LoadA.Imm1Flag = true;
        LoadA.Imm1 = 0;
        LoadA.Rd = 0;
        FsProbe.ExecuteFsxOpcode(LoadA);

        Honeycomb::DecodedInsn LoadB = LoadA;
        LoadB.Rd = 1;
        LoadB.Imm1 = 8;
        FsProbe.ExecuteFsxOpcode(LoadB);

        Honeycomb::DecodedInsn AddInsn{};
        AddInsn.Op = Honeycomb::Opcode::FADD;
        AddInsn.Opmode = Honeycomb::OpmodeFp64;
        AddInsn.Rd = 2;
        AddInsn.Rs = 0;
        AddInsn.Rt = 1;
        FsProbe.ExecuteFsxOpcode(AddInsn);

        assert(std::fabs(FsProbe.FRegs[2] - 7.0) < 1e-9);
    }

    {
        Cpu SsProbe;
        SsProbe.Reset();
        SsProbe.SsxVectorLength = 2;
        const Uint128 Left =
            (static_cast<Uint128>(0x3F800000ULL) << 96) |
            (static_cast<Uint128>(0x40000000ULL) << 64);
        const Uint128 Right =
            (static_cast<Uint128>(0x41200000ULL) << 96) |
            (static_cast<Uint128>(0x41A00000ULL) << 64);
        SsProbe.XRegs[0] = Left;
        SsProbe.XRegs[1] = Right;

        Honeycomb::DecodedInsn VaddInsn{};
        VaddInsn.Op = Honeycomb::Opcode::VADD;
        VaddInsn.Opmode = Honeycomb::OpmodeSsxFp32;
        VaddInsn.Rd = 2;
        VaddInsn.Rs = 0;
        VaddInsn.Rt = 1;
        SsProbe.ExecuteSsxOpcode(VaddInsn);

        uint32_t LaneBits = static_cast<uint32_t>(SsProbe.XRegs[2] >> 96);
        float Lane0 = 0.0f;
        std::memcpy(&Lane0, &LaneBits, sizeof(Lane0));
        LaneBits = static_cast<uint32_t>(SsProbe.XRegs[2] >> 64);
        float Lane1 = 0.0f;
        std::memcpy(&Lane1, &LaneBits, sizeof(Lane1));
        assert(std::fabs(Lane0 - 11.0f) < 1e-4f);
        assert(std::fabs(Lane1 - 22.0f) < 1e-4f);
    }

    {
        Cpu FsProbe;
        FsProbe.Reset();
        FsProbe.FRegs[0] = 10.0;
        FsProbe.FRegs[1] = 3.0;
        Honeycomb::DecodedInsn ModInsn{};
        ModInsn.Op = Honeycomb::Opcode::FMOD;
        ModInsn.Opmode = Honeycomb::OpmodeFp64;
        ModInsn.Rd = 2;
        ModInsn.Rs = 0;
        ModInsn.Rt = 1;
        FsProbe.ExecuteFsxOpcode(ModInsn);
        assert(std::fabs(FsProbe.FRegs[2] - 1.0) < 1e-9);
    }

    {
        Cpu FsProbe;
        FsProbe.Reset();
        FsProbe.FRegs[0] = 4.0;
        Honeycomb::DecodedInsn SqrtInsn{};
        SqrtInsn.Op = Honeycomb::Opcode::FSQRT;
        SqrtInsn.Opmode = Honeycomb::OpmodeFp64;
        SqrtInsn.Rd = 1;
        SqrtInsn.Rs = 0;
        FsProbe.ExecuteFsxOpcode(SqrtInsn);
        assert(std::fabs(FsProbe.FRegs[1] - 2.0) < 1e-9);

        FsProbe.FRegs[0] = -10.0;
        Honeycomb::DecodedInsn AbsInsn{};
        AbsInsn.Op = Honeycomb::Opcode::FABS;
        AbsInsn.Opmode = Honeycomb::OpmodeFp64;
        AbsInsn.Rd = 2;
        AbsInsn.Rs = 0;
        FsProbe.ExecuteFsxOpcode(AbsInsn);
        assert(std::fabs(FsProbe.FRegs[2] - 10.0) < 1e-9);

        FsProbe.FRegs[0] = 4.0;
        Honeycomb::DecodedInsn RsqrtInsn{};
        RsqrtInsn.Op = Honeycomb::Opcode::RSQRT;
        RsqrtInsn.Opmode = Honeycomb::OpmodeFp64;
        RsqrtInsn.Rd = 3;
        RsqrtInsn.Rs = 0;
        FsProbe.ExecuteFsxOpcode(RsqrtInsn);
        assert(std::fabs(FsProbe.FRegs[3] - 0.5) < 1e-9);
    }

    {
        Cpu SsProbe;
        SsProbe.Reset();
        SsProbe.SsxVectorLength = 2;
        SsProbe.XRegs[0] = (static_cast<Uint128>(0x3F800000ULL) << 96) |
                     (static_cast<Uint128>(0x40000000ULL) << 64);
        SsProbe.XRegs[1] = (static_cast<Uint128>(0x40400000ULL) << 96) |
                     (static_cast<Uint128>(0x40800000ULL) << 64);
        Honeycomb::DecodedInsn MulInsn{};
        MulInsn.Op = Honeycomb::Opcode::VMUL;
        MulInsn.Opmode = Honeycomb::OpmodeSsxFp32;
        MulInsn.Rd = 2;
        MulInsn.Rs = 0;
        MulInsn.Rt = 1;
        SsProbe.ExecuteSsxOpcode(MulInsn);
        float Lane0 = 0.0f;
        float Lane1 = 0.0f;
        uint32_t Bits = static_cast<uint32_t>(SsProbe.XRegs[2] >> 96);
        std::memcpy(&Lane0, &Bits, sizeof(Lane0));
        Bits = static_cast<uint32_t>(SsProbe.XRegs[2] >> 64);
        std::memcpy(&Lane1, &Bits, sizeof(Lane1));
        assert(std::fabs(Lane0 - 3.0f) < 1e-3f);
        assert(std::fabs(Lane1 - 8.0f) < 1e-3f);
    }

    {
        Cpu SsProbe;
        SsProbe.Reset();
        SsProbe.SsxVectorLength = 2;
        SsProbe.XRegs[0] = (static_cast<Uint128>(0x41000000ULL) << 96) |
                     (static_cast<Uint128>(0x41400000ULL) << 64);
        SsProbe.XRegs[1] = (static_cast<Uint128>(0x40000000ULL) << 96) |
                     (static_cast<Uint128>(0x40400000ULL) << 64);
        Honeycomb::DecodedInsn DivInsn{};
        DivInsn.Op = Honeycomb::Opcode::VDIV;
        DivInsn.Opmode = Honeycomb::OpmodeSsxFp32;
        DivInsn.Rd = 2;
        DivInsn.Rs = 0;
        DivInsn.Rt = 1;
        SsProbe.ExecuteSsxOpcode(DivInsn);
        float Lane0 = 0.0f;
        float Lane1 = 0.0f;
        uint32_t Bits = static_cast<uint32_t>(SsProbe.XRegs[2] >> 96);
        std::memcpy(&Lane0, &Bits, sizeof(Lane0));
        Bits = static_cast<uint32_t>(SsProbe.XRegs[2] >> 64);
        std::memcpy(&Lane1, &Bits, sizeof(Lane1));
        assert(std::fabs(Lane0 - 4.0f) < 1e-3f);
        assert(std::fabs(Lane1 - 4.0f) < 1e-3f);

        Honeycomb::DecodedInsn MinInsn{};
        MinInsn.Op = Honeycomb::Opcode::VMIN;
        MinInsn.Opmode = Honeycomb::OpmodeSsxFp32;
        MinInsn.Rd = 3;
        MinInsn.Rs = 0;
        MinInsn.Rt = 1;
        SsProbe.ExecuteSsxOpcode(MinInsn);
        Bits = static_cast<uint32_t>(SsProbe.XRegs[3] >> 96);
        std::memcpy(&Lane0, &Bits, sizeof(Lane0));
        Bits = static_cast<uint32_t>(SsProbe.XRegs[3] >> 64);
        std::memcpy(&Lane1, &Bits, sizeof(Lane1));
        assert(std::fabs(Lane0 - 2.0f) < 1e-3f);
        assert(std::fabs(Lane1 - 3.0f) < 1e-3f);

        Honeycomb::DecodedInsn NegInsn{};
        NegInsn.Op = Honeycomb::Opcode::VNEG;
        NegInsn.Opmode = Honeycomb::OpmodeSsxFp32;
        NegInsn.Rd = 4;
        NegInsn.Rs = 0;
        SsProbe.ExecuteSsxOpcode(NegInsn);
        Honeycomb::DecodedInsn AbsInsn{};
        AbsInsn.Op = Honeycomb::Opcode::VABS;
        AbsInsn.Opmode = Honeycomb::OpmodeSsxFp32;
        AbsInsn.Rd = 5;
        AbsInsn.Rs = 4;
        SsProbe.ExecuteSsxOpcode(AbsInsn);
        Bits = static_cast<uint32_t>(SsProbe.XRegs[5] >> 96);
        std::memcpy(&Lane0, &Bits, sizeof(Lane0));
        Bits = static_cast<uint32_t>(SsProbe.XRegs[5] >> 64);
        std::memcpy(&Lane1, &Bits, sizeof(Lane1));
        assert(std::fabs(Lane0 - 8.0f) < 1e-3f);
        assert(std::fabs(Lane1 - 12.0f) < 1e-3f);
    }

    {
        Cpu SsProbe;
        SsProbe.Reset();
        SsProbe.SsxVectorLength = 2;
        SsProbe.XRegs[0] = (static_cast<Uint128>(0x40400000ULL) << 96) |
                     (static_cast<Uint128>(0x40400000ULL) << 64);
        SsProbe.XRegs[1] = (static_cast<Uint128>(0x40000000ULL) << 96) |
                     (static_cast<Uint128>(0x40000000ULL) << 64);
        SsProbe.XRegs[2] = (static_cast<Uint128>(0x3F800000ULL) << 96) |
                     (static_cast<Uint128>(0x3F800000ULL) << 64);
        Honeycomb::DecodedInsn NmaddInsn{};
        NmaddInsn.Op = Honeycomb::Opcode::VFNMADD;
        NmaddInsn.Opmode = Honeycomb::OpmodeSsxFp32;
        NmaddInsn.Rd = 2;
        NmaddInsn.Rs = 1;
        NmaddInsn.Rt = 0;
        SsProbe.ExecuteSsxOpcode(NmaddInsn);
        float Lane0 = 0.0f;
        float Lane1 = 0.0f;
        uint32_t Bits = static_cast<uint32_t>(SsProbe.XRegs[2] >> 96);
        std::memcpy(&Lane0, &Bits, sizeof(Lane0));
        Bits = static_cast<uint32_t>(SsProbe.XRegs[2] >> 64);
        std::memcpy(&Lane1, &Bits, sizeof(Lane1));
        assert(std::fabs(Lane0 + 5.0f) < 1e-3f);
        assert(std::fabs(Lane1 + 5.0f) < 1e-3f);

        SsProbe.XRegs[0] = (static_cast<Uint128>(0x40800000ULL) << 96) |
                     (static_cast<Uint128>(0x40800000ULL) << 64);
        Honeycomb::DecodedInsn RcpInsn{};
        RcpInsn.Op = Honeycomb::Opcode::VRCP;
        RcpInsn.Opmode = Honeycomb::OpmodeSsxFp32;
        RcpInsn.Rd = 1;
        RcpInsn.Rs = 0;
        SsProbe.ExecuteSsxOpcode(RcpInsn);
        Bits = static_cast<uint32_t>(SsProbe.XRegs[1] >> 96);
        std::memcpy(&Lane0, &Bits, sizeof(Lane0));
        assert(std::fabs(Lane0 - 0.25f) < 1e-5f);
    }

    {
        Cpu MatProbe;
        MatProbe.Reset();
        MatProbe.Write64(0x100, 0x3F80000040000000ULL);
        MatProbe.Write64(0x108, 0x4040000040800000ULL);
        MatProbe.Write64(0x110, 0x40A0000040C00000ULL);
        MatProbe.Write64(0x118, 0x40E0000041000000ULL);
        MatProbe.Write64(0x120, 0);
        MatProbe.Write64(0x128, 0);
        MatProbe.Write64(0x200, 0x100);
        MatProbe.Write64(0x208, 0x110);
        MatProbe.Write64(0x210, 0x120);
        MatProbe.Write64(0x218, 0x4000200020002ULL);
        Honeycomb::DecodedInsn MatInsn{};
        MatInsn.Op = Honeycomb::Opcode::MATMUL;
        MatInsn.Opmode = Honeycomb::OpmodeSsxFp32;
        MatInsn.Imm1Flag = true;
        MatInsn.Imm1 = 0x200;
        MatInsn.Rd = 3;
        MatProbe.ExecuteMatrixOpcode(MatInsn);
        assert(MatProbe.Read64(0x120) == 0x4198000041B00000ULL);
        assert(MatProbe.Read64(0x128) == 0x422C000042480000ULL);
    }

    {
        Cpu MlProbe;
        MlProbe.Reset();
        MlProbe.Write64(0x100, 0x3F80000040000000ULL);
        MlProbe.Write64(0x108, 0x4040000040800000ULL);
        MlProbe.Write64(0x110, 0x3F800000BF800000ULL);
        MlProbe.Write64(0x118, 0xBF8000003F800000ULL);
        MlProbe.Write64(0x120, 0);
        MlProbe.Write64(0x200, 0x100);
        MlProbe.Write64(0x208, 0x110);
        MlProbe.Write64(0x210, 0x120);
        MlProbe.Write64(0x218, 0x4000200020001ULL);
        MlProbe.Write64(0x220, 0x20002ULL);
        Honeycomb::DecodedInsn ConvInsn{};
        ConvInsn.Op = Honeycomb::Opcode::CONV2D;
        ConvInsn.Opmode = Honeycomb::OpmodeSsxFp32;
        ConvInsn.Imm1Flag = true;
        ConvInsn.Imm1 = 0x200;
        ConvInsn.Rd = 2;
        MlProbe.ExecuteMatrixOpcode(ConvInsn);
        assert(MlProbe.Read64(0x120) == 0);

        MlProbe.Write8(0x400, 1);
        MlProbe.Write8(0x401, 2);
        MlProbe.Write8(0x402, 3);
        MlProbe.Write8(0x403, 4);
        MlProbe.Write8(0x408, 1);
        MlProbe.Write8(0x409, 1);
        MlProbe.Write8(0x40A, 1);
        MlProbe.Write8(0x40B, 1);
        MlProbe.Write64(0x300, 0x400);
        MlProbe.Write64(0x308, 0x408);
        MlProbe.Write64(0x310, 0x410);
        MlProbe.Write64(0x318, 0x40000ULL);
        MlProbe.Write64(0x320, 0x3F800000ULL);
        Honeycomb::DecodedInsn QInsn{};
        QInsn.Op = Honeycomb::Opcode::QDOT;
        QInsn.Opmode = Honeycomb::OpmodeSsxFp32;
        QInsn.Imm1Flag = true;
        QInsn.Imm1 = 0x300;
        QInsn.Rd = 4;
        MlProbe.ExecuteMatrixOpcode(QInsn);
        const uint32_t ScoreBits = MlProbe.Read32(0x410);
        float Score = 0.0f;
        std::memcpy(&Score, &ScoreBits, sizeof(Score));
        assert(std::fabs(Score - 10.0f) < 1e-3f);
    }

    {
        Cpu SsProbe;
        SsProbe.Reset();
        SsProbe.XRegs[0] = static_cast<Uint128>(0xFFFFFFFFFFFFFFFFULL) << 64;
        SsProbe.XRegs[1] = static_cast<Uint128>(0x00FF00FF00FF00FFULL) << 64;
        Honeycomb::DecodedInsn XorInsn{};
        XorInsn.Op = Honeycomb::Opcode::XXOR;
        XorInsn.Opmode = Honeycomb::OpmodeSsxGeneric;
        XorInsn.Rd = 2;
        XorInsn.Rs = 0;
        XorInsn.Rt = 1;
        SsProbe.ExecuteSsxOpcode(XorInsn);
        assert(SsProbe.XRegs[2] == (static_cast<Uint128>(0xFF00FF00FF00FF00ULL) << 64));
    }

    {
        Cpu CryptoProbe;
        CryptoProbe.Reset();
        const Uint128 Plain =
            (static_cast<Uint128>(0x0011223344556677ULL) << 64) |
            static_cast<Uint128>(0x889900AABBCCDDEEULL);
        const Uint128 Key =
            (static_cast<Uint128>(0x0001020304050607ULL) << 64) |
            static_cast<Uint128>(0x08090A0B0C0D0E0FULL);
        const Uint128 Cipher =
            (static_cast<Uint128>(0x69C4E0D24710AB44ULL) << 64) |
            static_cast<Uint128>(0x3AC346766BCA8C1CULL);
        CryptoProbe.XRegs[0] = Plain;
        CryptoProbe.XRegs[1] = Key;

        Honeycomb::DecodedInsn EncInsn{};
        EncInsn.Op = Honeycomb::Opcode::XCRYPT;
        EncInsn.Opmode = Honeycomb::OpmodeSsxGeneric;
        EncInsn.Rd = 2;
        EncInsn.Rs = 0;
        EncInsn.Rt = 1;
        CryptoProbe.ExecuteSsxOpcode(EncInsn);
        assert(CryptoProbe.XRegs[2] == Cipher);

        Honeycomb::DecodedInsn DecInsn{};
        DecInsn.Op = Honeycomb::Opcode::XDECRYPT;
        DecInsn.Opmode = Honeycomb::OpmodeSsxGeneric;
        DecInsn.Rd = 3;
        DecInsn.Rs = 2;
        DecInsn.Rt = 1;
        CryptoProbe.ExecuteSsxOpcode(DecInsn);
        assert(CryptoProbe.XRegs[3] == Plain);

        Honeycomb::DecodedInsn GenInsn{};
        GenInsn.Op = Honeycomb::Opcode::XGEN;
        GenInsn.Opmode = Honeycomb::OpmodeSsxGeneric;
        GenInsn.Imm1Flag = true;
        GenInsn.Imm1 = 0xC0FFEE;
        GenInsn.Rd = 4;
        CryptoProbe.ExecuteSsxOpcode(GenInsn);
        assert(CryptoProbe.XRegs[4] == SsxCrypto::XgenFromSeed(0xC0FFEE));

        GenInsn.Imm1Flag = false;
        GenInsn.Rs = 0;
        GenInsn.Rd = 5;
        CryptoProbe.ExecuteSsxOpcode(GenInsn);
        assert(CryptoProbe.XRegs[5] == SsxCrypto::XgenFromRegister(Plain));
    }

    {
        AssemblyImage Image = AssembleSourceFile(ArithmeticPath.string());
        assert(!Image.Bytes.empty());

        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();

        assert(Vm.IsHalted());
        assert(Vm.R3 == 0x21436587A9CBEDFFULL);
        assert(Vm.R9 == 15ULL);

        const uint64_t ResultAddr = Image.TextSize + 16;
        assert(Vm.Read64(ResultAddr) == 15ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(ShiftPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        const uint64_t AccumAddr = Image.TextSize;
        assert(Vm.Read64(AccumAddr) == 1ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(FibonacciPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R12 == 55ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(CallStackPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R15 == 40ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(InterruptPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 1ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(PushImmPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R14 == 0x4242424242424242ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(AtomicPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R9 == 1ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(PagingPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xCAFEBABEDEADBEEFULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(PagingRefillPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xFEEDFACEULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(ExceptionNestPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xECEXEC00ULL);
        assert(Vm.Read64(Image.TextSize) == 3ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(NumaPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R0 == 0ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(MmioBootPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xC0FEC0DEULL);
        assert(Vm.R14 == 2ULL);
        assert((Vm.Fr & StatusFlags::UserMode) != 0);
    }

    {
        AssemblyImage Image = AssembleSourceFile(FsxPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(std::fabs(Vm.FRegs[2] - 7.0) < 1e-9);
        assert(Vm.Read64(Image.TextSize + 16) == 0x401C000000000000ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(SsxPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.Read64(Image.TextSize + 32) == 0x4130000041B00000ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(SsxCryptoPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0ULL);
        const uint64_t DataBase = Image.TextSize;
        assert(Vm.Read64(DataBase + 48) == 0x69C4E0D24710AB44ULL);
        assert(Vm.Read64(DataBase + 56) == 0x3AC346766BCA8C1CULL);
        assert(Vm.Read64(DataBase + 64) == 0x0011223344556677ULL);
        assert(Vm.Read64(DataBase + 72) == 0x889900AABBCCDDEEULL);
        assert(Vm.Read64(DataBase + 80) == 0xECE45BABCE870479ULL);
        assert(Vm.Read64(DataBase + 88) == 0xCA8216FA9058D0FAULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(FsxFmaPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.Read64(Image.TextSize + 24) == 0x401C000000000000ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(HpcLanesPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.Read64(Image.TextSize + 24) == 0x3F5DB3D73F9CC471ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(VectorOpsPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 != 0ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(FsxMathPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 != 0ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(FusedNegPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 != 0ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(RecipLanesPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 != 0ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(MatMulPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 != 0ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(GemmAccumPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 != 0ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(MlInferencePath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0x41200000ULL);
        // LossScalar is the first .data label (offset 0 in data = TextSize).
        const uint64_t LossAddr = Image.TextSize;
        uint32_t LossBits = Vm.Read32(LossAddr);
        float FinalLoss = 0.0f;
        std::memcpy(&FinalLoss, &LossBits, sizeof(FinalLoss));
        assert(FinalLoss < 0.35f);
        assert(FinalLoss > 0.0f);
    }

    {
        AssemblyImage Image = AssembleSourceFile(NsxPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0x41200000ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(KBankPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R0 == 0ULL);
        assert(Vm.R1 == 42ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(DaxPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R0 == 48ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(DaxStreamPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R0 != 1ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(DaxCrossCorePath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R0 == 99ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(SkbPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R1 == 52ULL);
        assert(Vm.R2 == 52ULL);
        assert(Vm.R0 != 1ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(CplxPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R0 != 1ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(CplxVectorPath.string());
        Cpu Vm;
        Vm.SsxVectorLength = 4;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R0 != 1ULL);
    }

    {
        Cpu Vm;
        Vm.SsxVectorLength = 4;
        Vm.WriteGpr(0, 0x3F80000000000000ULL);
        Vm.WriteGpr(1, 0x000000003F800000ULL);
        Vm.XRegs[0] = (static_cast<Uint128>(Vm.ReadGpr(0)) << 64) | Vm.ReadGpr(1);
        Vm.XRegs[1] = (static_cast<Uint128>(0x40000000ULL) << 96) |
                      (static_cast<Uint128>(0x40000000ULL) << 64) |
                      (static_cast<Uint128>(0x3F800000ULL) << 32) |
                      static_cast<Uint128>(0x3F800000ULL);
        DecodedInsn Insn{};
        Insn.Op = Opcode::VCADD;
        Insn.Opmode = OpmodeMixed;
        Insn.Disp = CplxProfile::PackCplxDisp(CplxProfile::Precision::Fp32);
        Insn.Rd = 2;
        Insn.Rs = 0;
        Insn.Rt = 1;
        Vm.ExecuteCplxOpcode(Insn);
        float Got = 0.0f;
        const uint32_t Bits = static_cast<uint32_t>(Vm.XRegs[2] >> 96);
        std::memcpy(&Got, &Bits, sizeof(Got));
        assert(std::fabs(Got - 3.0f) < 1e-5f);
    }

    {
        Cpu Vm;
        Vm.KBank[0] = 0x4008000000000000ULL;
        Vm.KBank[1] = 0x4010000000000000ULL;
        DecodedInsn Insn{};
        Insn.Op = Opcode::CKLOAD;
        Insn.Opmode = OpmodeMixed;
        Insn.Disp = CplxProfile::PackCplxDisp(CplxProfile::Precision::Fp64);
        Insn.Rd = 0;
        Insn.Rs = static_cast<uint8_t>(Reg::I0);
        Vm.ExecuteCplxOpcode(Insn);
        double Real = 0.0;
        double Imag = 0.0;
        uint64_t Raw = Vm.ReadGpr(0);
        std::memcpy(&Real, &Raw, sizeof(Real));
        Raw = Vm.ReadGpr(1);
        std::memcpy(&Imag, &Raw, sizeof(Imag));
        assert(std::fabs(Real - 3.0) < 1e-9);
        assert(std::fabs(Imag - 4.0) < 1e-9);
    }

    {
        Cpu Vm;
        Vm.WriteGpr(0, 0x4010000000000000ULL);
        Vm.WriteGpr(1, 0x0000000000000000ULL);
        Vm.WriteGpr(2, 0x4000000000000000ULL);
        Vm.WriteGpr(3, 0x0000000000000000ULL);
        DecodedInsn Insn{};
        Insn.Op = Opcode::CDIV;
        Insn.Opmode = OpmodeMixed;
        Insn.Disp = CplxProfile::PackCplxDisp(CplxProfile::Precision::Fp64);
        Insn.Rd = 2;
        Insn.Rs = 0;
        Insn.Rt = 1;
        Vm.ExecuteCplxOpcode(Insn);
        double Real = 0.0;
        uint64_t Raw = Vm.ReadGpr(4);
        std::memcpy(&Real, &Raw, sizeof(Real));
        assert(std::fabs(Real - 2.0) < 1e-9);
    }

    {
        AssemblyImage Image = AssembleSourceFile(CompactHybridPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xC0FE0001ULL);
    }

    {
        AssemblyImage Image = AssembleSourceFile(SecuritySandboxPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xC0FEC0DEULL);
        const uint64_t FaultKindAddr = Image.TextSize + 8;
        assert(Vm.Read64(FaultKindAddr) == 2ULL);
    }

    {
        InitMachine(VmConfigDefaults());
        Devices::ResetAll();

        const uint64_t MmioBase = GetMmioWindowBase();
        Cpu DeviceProbe;
        DeviceProbe.Reset();
        assert(Devices::GetPresentMask() == 0x3FFULL);
        assert(DeviceProbe.Read64(MmioBase + static_cast<uint64_t>(
                                        MmioWindow::RegisterQuadWord::PlatformDevicePresent)) ==
               0x3FFULL);
        assert(DeviceProbe.Read64(MmioBase + static_cast<uint64_t>(
                                        MmioWindow::RegisterQuadWord::RtcSeconds)) > 0);

        DeviceProbe.Write64(0x5000, 0xAABBCCDD11223344ULL);
        DeviceProbe.Write64(0x6000, 0);
        DeviceProbe.Write64(MmioBase +
                                static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockLba),
                            0);
        DeviceProbe.Write64(MmioBase +
                                static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockBufferPtr),
                            0x5000);
        DeviceProbe.Write64(MmioBase + static_cast<uint64_t>(
                                           MmioWindow::RegisterQuadWord::BlockSectorCount),
                            2);
        DeviceProbe.Write64(MmioBase +
                                static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockCommand),
                            2);
        assert(DeviceProbe.Read64(MmioBase +
                                  static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockStatus)) ==
               1ULL);
        assert(DeviceProbe.Read64(0x5000) == 0xAABBCCDD11223344ULL);

        DeviceProbe.Write64(MmioBase +
                                static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockBufferPtr),
                            0x6000);
        DeviceProbe.Write64(MmioBase +
                                static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockCommand),
                            1);
        assert(DeviceProbe.Read64(MmioBase +
                                  static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockStatus)) ==
               1ULL);
        assert(DeviceProbe.Read64(0x6000) == 0xAABBCCDD11223344ULL);

        DeviceProbe.Write64(MmioBase +
                                static_cast<uint64_t>(MmioWindow::RegisterQuadWord::UartData),
                            static_cast<uint64_t>('Z'));
        assert((DeviceProbe.Read64(MmioBase + static_cast<uint64_t>(
                                                  MmioWindow::RegisterQuadWord::UartLineStatus)) &
                0x20ULL) != 0);
    }

    {
        InitMachine(VmConfigDefaults());
        Devices::ResetAll();

        AssemblyImage Image = AssembleSourceFile(BlockDemoPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xC0FFEE04ULL);
    }

    {
        InitMachine(VmConfigDefaults());
        Devices::ResetAll();

        AssemblyImage Image = AssembleSourceFile(Usb3DemoPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xC0FFEE02ULL);
    }

    {
        InitMachine(VmConfigDefaults());
        Devices::ResetAll();

        AssemblyImage Image = AssembleSourceFile(Ac97DemoPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xC0FFEE03ULL);
    }

    {
        InitMachine(VmConfigDefaults());
        Devices::ResetAll();

        AssemblyImage Image = AssembleSourceFile(UartDemoPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xC0FFEE05ULL);
    }

    {
        InitMachine(VmConfigDefaults());
        Devices::ResetAll();

        AssemblyImage Image = AssembleSourceFile(DevicePresentDemoPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xC0FFEE06ULL);
    }

    {
        InitMachine(VmConfigDefaults());
        Devices::ResetAll();

        AssemblyImage Image = AssembleSourceFile(PlatformTimerDemoPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xC0FFEE07ULL);
    }

    {
        InitMachine(VmConfigDefaults());
        Devices::ResetAll();

        AssemblyImage Image = AssembleSourceFile(PlatformRtcDemoPath.string());
        Cpu Vm;
        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);
        Vm.Run();
        assert(Vm.IsHalted());
        assert(Vm.R10 == 0xC0FFEE08ULL);
    }

    {
        InitMachine(VmConfigDefaults());
        Cpu CacheProbe;
        CacheProbe.Reset();
        const uint64_t TestAddr = 0x2000;
        CacheProbe.Write64(TestAddr, 0xA5A5A5A5A5A5A5A5ULL);
        assert(CacheProbe.Read64(TestAddr) == 0xA5A5A5A5A5A5A5A5ULL);
        assert(CacheProbe.GetL1DataMisses() >= 1);
        const uint64_t MissesAfterFirst = CacheProbe.GetL1DataMisses();
        assert(CacheProbe.Read64(TestAddr) == 0xA5A5A5A5A5A5A5A5ULL);
        assert(CacheProbe.GetL1DataMisses() == MissesAfterFirst);
    }

    std::cout << "hello_test passed\n";
    return 0;
}
