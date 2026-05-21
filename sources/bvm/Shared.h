#pragma once

#include "Machine.h"
#include "../Isa.h"
#include "FsxProfile.h"
#include "KBankProfile.h"
#include "PhysMap.h"
#include "MmioMap.h"
#include "ExceptionProfile.h"
#include "PagingProfile.h"
#include "NumaProfile.h"
#include "Tlb.h"
#include "L1Cache.h"
#include "SsxProfile.h"
#include "CplxProfile.h"
#include "DaxProfile.h"
#include "DaxToken.h"
#include "SecurityProfile.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>

constexpr size_t IoPorts = 256;

extern std::array<uint8_t, IoPorts> Ports;

using Uint128 = unsigned __int128;

class Cpu {
public:
    uint64_t R0 = 0, R1 = 0, R2 = 0, R3 = 0, R4 = 0, R5 = 0, R6 = 0, R7 = 0;
    uint64_t R8 = 0, R9 = 0, R10 = 0, R11 = 0, R12 = 0, R13 = 0, R14 = 0, R15 = 0;
    uint64_t R16 = 0, R17 = 0, R18 = 0, R19 = 0, R20 = 0, R21 = 0, R22 = 0, R23 = 0;
    uint64_t R24 = 0, R25 = 0, R26 = 0, R27 = 0, R28 = 0, R29 = 0, R30 = 0, R31 = 0;
    uint64_t Sp = 0;
    uint64_t Ic = 0;
    uint64_t Fr = 0;
    uint64_t Ptb = 0;
    uint64_t Ivb = 0;
    uint64_t Ivl = 0;
    uint64_t Wind = 0;
    /** Window size ([15:0]) + complex tag ([31:24] prec, [23:16] GPR stride, [39:32] K stride). */
    uint64_t WinSz = CplxProfile::DefaultWinSzMerged;
    uint64_t I0 = 0, I1 = 0, I2 = 0, I3 = 0;
    uint64_t TlbFaultAddress = 0;
    uint64_t TlbFaultAccess = 0;
    uint64_t KMask = KBankProfile::DefaultKMask;
    std::array<uint64_t, KBankProfile::RegisterCount> KBank = {};
    std::array<DaxTokenMeta, DaxProfile::SlotCount> DaxMeta = {};

    uint64_t Cwp = 0;
    uint64_t CanSave = 6;
    uint64_t CanRestore = 0;
    uint64_t OtherWin = 0;
    uint64_t RotBase = 0;
    uint64_t RotSz = 0;
    uint64_t WindowSpillBase = 0;
    std::array<bool, 64> StreamTagDone = {};
    std::array<bool, 64> NumaStreamTagDone = {};
    std::array<uint64_t, NumaProfile::MaxNodes> NumaRemoteWind = {};
    std::array<uint8_t, 8> NumaKBindNode = {};
    std::array<uint64_t, 8> NumaKBindLine = {};
    std::array<bool, 8> NumaKBindActive = {};
    std::array<bool, 64> StreamBindActive = {};
    std::array<uint8_t, 8> StreamBindSlot = {};
    uint64_t HintBarrierEpoch = 0;
    uint64_t HintBarrierCommit = 0;

    std::array<Uint128, SsxBaselineProfile::RegisterCount> XRegs = {};
    Uint128 A0 = 0, A1 = 0, A2 = 0, A3 = 0;
    std::array<double, FsxBaselineProfile::RegisterCount> FRegs = {};

    /** Active SSX element count for scalable ops (`VADD`, `VFMA`, …). */
    uint32_t SsxVectorLength = SsxBaselineProfile::DefaultVectorLength;

    /** NSX autodiff tape base (set by `fwd_start`, consumed by `fwd_end`). */
    uint64_t NsxTapeAddr = 0;
    uint64_t NsxLastTransformerDesc = 0;
    uint64_t NsxLastLossDesc = 0;

    Uint128 PacKey = 0;
    Uint128 CfiKey = 0;
    Uint128 CapKey = 0;
    Uint128 MemEncryptKey = 0;
    uint64_t StackGuard = 0;
    uint64_t ShadowStackBase = 0;
    uint64_t ShadowStackTop = 0;
    uint64_t ShadowStackPtr = 0;
    uint64_t RopTrapBase = 0;
    uint64_t RopTrapLimit = 0;
    uint64_t SandboxAllowMask = 0;
    uint64_t SandboxDenyMask = 0;
    uint64_t SandboxBase = 0;
    uint64_t SandboxLimit = 0;
    uint64_t VmcsBase = 0;
    uint64_t PcrBase = 0;
    uint64_t IommuBase = 0;
    uint64_t IommuDevice = 0;
    uint64_t IommuFaultAddr = 0;
    uint64_t MemEncryptDomain = 0;
    uint64_t SecurityFaultAddr = 0;
    uint32_t SecurityFaultKind = 0;
    uint32_t ExpectedCfiLabel = 0;
    std::array<uint64_t, SecurityProfile::ShadowStackSlots> ShadowStack{};
    std::array<uint32_t, 4096> CfiLabelTable{};
    bool VmGuestActive = false;
    uint64_t VmHostIc = 0;
    uint64_t VmHostFr = 0;
    Cpu();
    void Reset();
    void Step();
    void Run();
    void Halt();

    uint8_t Read8(uint64_t Addr) const;
    uint16_t Read16(uint64_t Addr) const;
    uint32_t Read32(uint64_t Addr) const;
    uint64_t Read64(uint64_t Addr) const;
    Uint128 Read128(uint64_t Addr) const;

    void Write8(uint64_t Addr, uint8_t Value);
    void Write16(uint64_t Addr, uint16_t Value);
    void Write32(uint64_t Addr, uint32_t Value);
    void Write64(uint64_t Addr, uint64_t Value);
    void Write128(uint64_t Addr, Uint128 Value);

    /// Physical memory (no paging); page walks and `LoadImage` use this path.
    uint64_t Read64Physical(uint64_t PhysicalAddr) const;
    void Write64Physical(uint64_t PhysicalAddr, uint64_t Value);
    /// Remote NUMA domains (`0011`/`0101`/remote K).
    uint64_t ReadRemotePhysical(uint64_t Addr) const;
    void WriteRemotePhysical(uint64_t Addr, uint64_t Value);

    void Push64(uint64_t Value);
    uint64_t Pop64();

    void DumpRegisters() const;
    bool IsHalted() const { return Halted; }

    uint64_t ReadGpr(uint8_t Code) const;
    void WriteGpr(uint8_t Code, uint64_t Value);

    /** Used by atomic `ATCMP`: same flag update as scalar `CMP` (writes `FR`). */
    void UpdateCompareResult(int64_t Diff);

    void ExecuteFsxOpcode(const Honeycomb::DecodedInsn& Insn);
    void ExecuteSsxOpcode(const Honeycomb::DecodedInsn& Insn);
    void ExecuteMatrixOpcode(const Honeycomb::DecodedInsn& Insn);
    void ExecuteKBankOpcode(const Honeycomb::DecodedInsn& Insn);
    void ExecuteDaxOpcode(const Honeycomb::DecodedInsn& Insn);
    void ExecuteNsxOpcode(const Honeycomb::DecodedInsn& Insn);
    void ExecuteSkbOpcode(const Honeycomb::DecodedInsn& Insn);
    void ExecuteCplxOpcode(const Honeycomb::DecodedInsn& Insn);

    uint64_t ReadIndexReg(uint8_t Code) const;
    uint64_t ReadKReg(uint64_t Index) const;
    void WriteKReg(uint64_t Index, uint64_t Value);

    void LoadImage(const uint8_t* Data, size_t Size, uint64_t LoadAddr, uint64_t Entry);

    /// Queue a hardware-style interrupt delivered when interrupts are enabled (EI flag).
    void RaiseInterrupt(uint8_t Vector);
    /// Deliver interrupt vector immediately (IVT must be configured).
    void DeliverInterruptVector(uint8_t Vector);

    bool InUserMode() const;
    void RequireSupervisor(const char* Context) const;

    static constexpr uint32_t PerfMonCounterInstructions = 0;
    static constexpr uint32_t PerfMonCounterCycles = 1;
    static constexpr uint32_t PerfMonCounterInterrupts = 2;

    uint64_t GetPerfCycles() const { return PerfCycles; }

    uint64_t GetL1InstructionMisses() const { return ICache.MissCount(); }
    uint64_t GetL1DataMisses() const { return DCache.MissCount(); }
    void FlushL1Caches();

    /// Last MMIO write to `MmioWindow::RegisterQuadWord::BootControl` (see docs).
    uint64_t MmioBootControlRegister = 0;
    /// Syscall entry PC wired through MMIO `SyscallHandlerIc` register.
    uint64_t MmioSyscallProgramCounter = 0;
    /// Most recent SYSCALL imm (last issued syscall number).
    uint64_t MmioLastSyscallNumberRegister = 0;

private:
    bool Running = false;
    bool Halted = false;

    std::deque<uint8_t> PendingInterruptVectors;

    uint32_t ExceptionFrameDepth = 0;
    std::array<ExceptionProfile::ExceptionFrame, ExceptionProfile::HardwareNestDepth>
        ExceptionFrames{};
    uint32_t SyscallFrameDepth = 0;
    uint64_t PerfInstructionsRetired = 0;
    uint64_t PerfCycles = 0;
    uint64_t PerfInterruptsDelivered = 0;

    mutable Tlb AddressTlb;
    mutable L1InstructionCache ICache;
    mutable L1DataCache DCache;

    void ChargeInstructionCacheMiss(bool Miss);
    void ChargeDataCacheMiss(bool Miss);
    void InvalidateInstructionForDataStore(uint64_t PhysAddr);
    uint16_t ReadInstructionU16(uint64_t VirtAddr);
    uint64_t ReadInstructionU64(uint64_t VirtAddr);

    static constexpr uint32_t MaxSyscallNestDepth = 64;

    bool PagingIsEnabled() const;
    bool SoftwareTlbRefillEnabled() const;
    void FlushAddressTlb();
    void FlushAddressTlbEntry(uint64_t VirtAddr);
    uint64_t TranslateVirtualAddress(uint64_t VirtAddr,
                                     PagingProfile::MemoryAccessKind Kind) const;
    uint64_t WalkPageTables(uint64_t VirtAddr, PagingProfile::MemoryAccessKind Kind);
    void RaisePageFault(uint64_t VirtAddr, const char* Reason);
    void RaiseTlbMiss(uint64_t VirtAddr, PagingProfile::MemoryAccessKind Kind);
    void OpEnablePaging();
    void OpDisablePaging();
    void OpEnableSoftwareTlbRefill();
    void OpDisableSoftwareTlbRefill();
    void OpTlbInsert();
    void OpWireTlbEntry(const Honeycomb::DecodedInsn& Insn);

    void PushExceptionFrame();
    void PopExceptionFrame();

    void OpEnableUser();
    void OpDisableUser();

    Honeycomb::DecodedInsn Fetch();
    void ExecuteCompact(const Honeycomb::DecodedInsn& Insn);
    void Execute(const Honeycomb::DecodedInsn& Insn);

    void DispatchQueuedInterrupt();
    uint64_t ReadPerfCounter(uint64_t Selector) const;

    void ExecuteAtomicOpcode(const Honeycomb::DecodedInsn& Insn);
    void OpPrefetchInsn(const Honeycomb::DecodedInsn& Insn);
    void OpInInsn(const Honeycomb::DecodedInsn& Insn);
    void OpOutInsn(const Honeycomb::DecodedInsn& Insn);
    void OpSatAdd(const Honeycomb::DecodedInsn& Insn);
    void OpSatMul(const Honeycomb::DecodedInsn& Insn);

    uint64_t MmioDispatchReadQuadWord(uint64_t PhysicalAddress) const;
    void MmioDispatchWriteQuadWord(uint64_t PhysicalAddress, uint64_t Value);

    void OpSyscallInsn(const Honeycomb::DecodedInsn& Insn);
    void OpSysretInsn();
    void OpCallEnter(uint64_t TaggedTarget);
    void OpRetLeave();

    void SetCompareFlags(int64_t Diff);
    void SetZeroSign(uint64_t Result);

    void OpAdd(const Honeycomb::DecodedInsn& Insn);
    void OpSub(const Honeycomb::DecodedInsn& Insn);
    void OpMul(const Honeycomb::DecodedInsn& Insn);
    void OpDiv(const Honeycomb::DecodedInsn& Insn);
    void OpMod(const Honeycomb::DecodedInsn& Insn);
    void OpAnd(const Honeycomb::DecodedInsn& Insn);
    void OpOr(const Honeycomb::DecodedInsn& Insn);
    void OpXor(const Honeycomb::DecodedInsn& Insn);
    void OpNot(const Honeycomb::DecodedInsn& Insn);
    void OpCmp(const Honeycomb::DecodedInsn& Insn);
    void OpLoad(const Honeycomb::DecodedInsn& Insn);
    void OpStore(const Honeycomb::DecodedInsn& Insn);
    void OpFsxLoad(const Honeycomb::DecodedInsn& Insn);
    void OpFsxStore(const Honeycomb::DecodedInsn& Insn);
    void OpSsxLoad(const Honeycomb::DecodedInsn& Insn);
    void OpSsxStore(const Honeycomb::DecodedInsn& Insn);
    void OpSsxBroadcast(const Honeycomb::DecodedInsn& Insn);
    void OpIncDec(Honeycomb::Opcode Op, const Honeycomb::DecodedInsn& Insn);
    void OpBranch(const Honeycomb::DecodedInsn& Insn);
    void OpShiftRotate(Honeycomb::Opcode Op, const Honeycomb::DecodedInsn& Insn);
    void OpSwapRegs(const Honeycomb::DecodedInsn& Insn);
    void OpCopyReg(const Honeycomb::DecodedInsn& Insn);
    void OpPushReg(const Honeycomb::DecodedInsn& Insn);
    void OpPushImm(const Honeycomb::DecodedInsn& Insn);
    void OpPopReg(const Honeycomb::DecodedInsn& Insn);
    void OpIret();

    void WriteIndexReg(uint8_t Code, uint64_t Value);
    void OpKwind(const Honeycomb::DecodedInsn& Insn);
    void OpKload(const Honeycomb::DecodedInsn& Insn);
    void OpKstore(const Honeycomb::DecodedInsn& Insn);
    void OpKloadi(const Honeycomb::DecodedInsn& Insn);
    void OpKstorei(const Honeycomb::DecodedInsn& Insn);
    uint64_t ComputeKAddress(const Honeycomb::DecodedInsn& Insn, uint64_t Index) const;
    void OpKrot(const Honeycomb::DecodedInsn& Insn);
    void OpIgload(const Honeycomb::DecodedInsn& Insn);
    void OpIgstore(const Honeycomb::DecodedInsn& Insn);
    void OpKreload(const Honeycomb::DecodedInsn& Insn);
    void OpKspill(const Honeycomb::DecodedInsn& Insn);
    void OpXspill(const Honeycomb::DecodedInsn& Insn);
    void OpXreload(const Honeycomb::DecodedInsn& Insn);
    void OpSavwin();
    void OpReswin();
    void OpFlushwin(const Honeycomb::DecodedInsn& Insn);
    void OpRestwin(const Honeycomb::DecodedInsn& Insn);
    void OpRdwin(const Honeycomb::DecodedInsn& Insn);
    void OpWrwin(const Honeycomb::DecodedInsn& Insn);
    void MemcpyToKBank(uint64_t DstKIndex, uint64_t SrcAddr, uint64_t ByteCount);
    void MemcpyFromKBank(uint64_t SrcKIndex, uint64_t DstAddr, uint64_t ByteCount);
    void OpStream(const Honeycomb::DecodedInsn& Insn, bool ToMemory);
    void OpStreamWait(const Honeycomb::DecodedInsn& Insn);
    void OpStreamBind(const Honeycomb::DecodedInsn& Insn);
    void OpStreamUnbind(const Honeycomb::DecodedInsn& Insn);

    void ResetNuma();
    bool TryReadNumaBoundK(uint64_t Index, uint64_t& Value) const;
    bool TryWriteNumaBoundK(uint64_t Index, uint64_t Value);
    void OpNumaBind(const Honeycomb::DecodedInsn& Insn);
    void OpNumaStream(const Honeycomb::DecodedInsn& Insn, bool ToRemote);
    void OpNumaFence();
    void OpNumaAdv(const Honeycomb::DecodedInsn& Insn);
    void OpNumaAtomic(const Honeycomb::DecodedInsn& Insn);
    void OpHintBarrier();
    void OpHintInsn(const Honeycomb::DecodedInsn& Insn);

    void ResetSkb();
    static bool IsSkbPhysicalAddress(uint64_t Addr);
    uint64_t ReadSkbPhysical(uint64_t Addr) const;
    void WriteSkbPhysical(uint64_t Addr, uint64_t Value);
    bool TryReadSkbBoundK(uint64_t Index, uint64_t& Value) const;
    bool TryWriteSkbBoundK(uint64_t Index, uint64_t Value);
    void SkbSetLineHomeNode(uint64_t Line, uint8_t Node);
    void SkbSetLineFlags(uint64_t Line, uint8_t Flags);
    void SkbReplicateLineToNode(uint8_t Node, uint64_t Line);
    void OpSkbLoad(const Honeycomb::DecodedInsn& Insn);
    void OpSkbStore(const Honeycomb::DecodedInsn& Insn);
    void OpSkbZero(const Honeycomb::DecodedInsn& Insn);
    void OpSkbFlush(const Honeycomb::DecodedInsn& Insn);
    void OpSkbInvalidate(const Honeycomb::DecodedInsn& Insn);
    void OpSkbPin(const Honeycomb::DecodedInsn& Insn, bool Pin);
    void OpSkbStatus(const Honeycomb::DecodedInsn& Insn);
    void OpSkbBind(const Honeycomb::DecodedInsn& Insn);
    void OpSkbUnbind(const Honeycomb::DecodedInsn& Insn);
    uint64_t SkbAtomicReadWord(uint64_t Line, uint64_t Off) const;
    void SkbAtomicWriteWord(uint64_t Line, uint64_t Off, uint64_t Value);
    void OpSkbAtLoad(const Honeycomb::DecodedInsn& Insn);
    void OpSkbAtStore(const Honeycomb::DecodedInsn& Insn);
    void OpSkbAtAdd(const Honeycomb::DecodedInsn& Insn);
    void OpSkbAtCas(const Honeycomb::DecodedInsn& Insn);
    void OpSkbAtSwap(const Honeycomb::DecodedInsn& Insn);
    void OpSkbBulk(const Honeycomb::DecodedInsn& Insn, bool ToDram);
    void OpSkbPrefetch(const Honeycomb::DecodedInsn& Insn);
    void OpSkbMove(const Honeycomb::DecodedInsn& Insn);
    void OpSkbAtReduce(const Honeycomb::DecodedInsn& Insn);
    uint64_t ReadKPhysical(uint64_t Addr) const;
    void WriteKPhysical(uint64_t Addr, uint64_t Value);

    void OpPerfmon(const Honeycomb::DecodedInsn& Insn);
    void SetFlagsAfterAdd(uint64_t Lhs, uint64_t Rhs, uint64_t Result);
    void SetFlagsAfterSub(uint64_t Lhs, uint64_t Rhs, uint64_t Result);
};
