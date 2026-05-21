#include "Shared.h"
#include "Machine.h"
#include "ExceptionProfile.h"

#include <stdexcept>

using namespace Honeycomb;

void Cpu::RaiseInterrupt(uint8_t Vector) {
    constexpr size_t MaxPending = 65536;
    if (PendingInterruptVectors.size() >= MaxPending) {
        throw std::runtime_error("Interrupt queue overflow");
    }
    PendingInterruptVectors.push_back(Vector);
}

uint64_t Cpu::ReadPerfCounter(uint64_t Selector) const {
    switch (Selector) {
        case Cpu::PerfMonCounterInstructions:
            return PerfInstructionsRetired;
        case Cpu::PerfMonCounterCycles:
            return PerfCycles;
        case Cpu::PerfMonCounterInterrupts:
            return PerfInterruptsDelivered;
        default:
            return 0;
    }
}

void Cpu::PushExceptionFrame() {
    if (ExceptionFrameDepth >= ExceptionProfile::HardwareNestDepth) {
        throw std::runtime_error(
            "Honeycomb hardware exception stack overflow (max 3 nested frames)");
    }
    ExceptionProfile::ExceptionFrame& Frame = ExceptionFrames[ExceptionFrameDepth];
    Frame.Ic = Ic;
    Frame.Fr = Fr;
    ExceptionFrameDepth++;
}

void Cpu::PopExceptionFrame() {
    if (ExceptionFrameDepth == 0) {
        throw std::runtime_error("IRET without active hardware exception frame");
    }
    ExceptionFrameDepth--;
    const ExceptionProfile::ExceptionFrame& Frame = ExceptionFrames[ExceptionFrameDepth];
    Fr = Frame.Fr;
    Ic = Frame.Ic;
}

void Cpu::DeliverInterruptVector(uint8_t Vector) {
    if (Ivl == 0) {
        throw std::runtime_error("IVT not configured (IVL == 0)");
    }
    const uint64_t Vid = static_cast<uint64_t>(Vector);
    if (Vid >= Ivl) {
        throw std::runtime_error("Interrupt vector out of range (vector >= IVL)");
    }
    if (Ivl > GetRamSize() / 8) {
        throw std::runtime_error("IVL too large for address space");
    }
    const uint64_t TableBytes = Ivl * 8;
    if (Ivb + TableBytes > GetRamSize() || Ivb + TableBytes < Ivb) {
        throw std::runtime_error("IVT byte range extends outside RAM");
    }
    const uint64_t Slot = Vid * 8;
    const uint64_t EntryAddr = Ivb + Slot;
    if (EntryAddr + 8 > GetRamSize() || EntryAddr + 8 > Ivb + TableBytes ||
        EntryAddr < Ivb) {
        throw std::runtime_error("Interrupt vector slot outside IVT");
    }
    const uint64_t Handler = Read64Physical(EntryAddr);
    if (Handler == 0) {
        throw std::runtime_error("Unhandled interrupt (null handler)");
    }

    PushExceptionFrame();
    Fr &= ~StatusFlags::Interrupt;
    Ic = Handler;
    PerfInterruptsDelivered++;
}

void Cpu::DispatchQueuedInterrupt() {
    if (Halted) return;
    if (PendingInterruptVectors.empty()) return;
    if ((Fr & StatusFlags::Interrupt) == 0) return;

    const uint8_t Vector = PendingInterruptVectors.front();
    PendingInterruptVectors.pop_front();
    DeliverInterruptVector(Vector);
}

void Cpu::OpIret() { PopExceptionFrame(); }

void Cpu::OpPerfmon(const DecodedInsn& Insn) {
    const uint64_t Selector = Insn.Imm1Flag ? Insn.Imm1 : 0;
    WriteGpr(Insn.Rd, ReadPerfCounter(Selector));
}
