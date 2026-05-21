#include "PlatformTimer.h"

#include "DeviceSpecs.h"
#include "Machine.h"
#include "MmioMap.h"
#include "Shared.h"

namespace {

bool Enabled = true;
uint64_t Mtime = 0;
uint64_t MtimeCmp = UINT64_MAX;
uint64_t FrequencyHz = DeviceSpecs::Timer::DefaultFrequencyHz;
uint8_t Control = DeviceSpecs::Timer::ControlIrqEnable;
bool IrqPending = false;
uint64_t LastServiceCycles = 0;

} // namespace

namespace PlatformTimer {

void Reset() {
    Mtime = 0;
    MtimeCmp = UINT64_MAX;
    FrequencyHz = GetMachineConfig().TimerFrequencyHz;
    Control = DeviceSpecs::Timer::ControlIrqEnable;
    IrqPending = false;
    LastServiceCycles = 0;
}

void SetEnabled(bool IsEnabled) { Enabled = IsEnabled; }

bool IsEnabled() { return Enabled; }

uint64_t ReadMmioQuad(uint64_t Offset) {
    if (!Enabled) {
        return 0;
    }

    using Quad = MmioWindow::RegisterQuadWord;
    switch (static_cast<Quad>(Offset)) {
        case Quad::Mtime:
            return Mtime;
        case Quad::MtimeCmp:
            return MtimeCmp;
        case Quad::MtimeFreq:
            return FrequencyHz;
        case Quad::MtimeControl:
            return Control | (IrqPending ? 0x80ULL : 0ULL);
        default:
            return 0;
    }
}

void WriteMmioQuad(uint64_t Offset, uint64_t Value) {
    if (!Enabled) {
        return;
    }

    using Quad = MmioWindow::RegisterQuadWord;
    switch (static_cast<Quad>(Offset)) {
        case Quad::MtimeCmp:
            MtimeCmp = Value;
            if (Mtime < MtimeCmp) {
                IrqPending = false;
            }
            break;
        case Quad::MtimeControl:
            Control = static_cast<uint8_t>(Value & 0xFF);
            if ((Value & 0x80ULL) != 0) {
                IrqPending = false;
            }
            break;
        default:
            break;
    }
}

void Service(Cpu& Vm) {
    if (!Enabled) {
        LastServiceCycles = Vm.GetPerfCycles();
        return;
    }

    const uint64_t CyclesPerTick = GetMachineConfig().TimerCyclesPerTick;
    if (CyclesPerTick == 0) {
        LastServiceCycles = Vm.GetPerfCycles();
        return;
    }

    const uint64_t NowCycles = Vm.GetPerfCycles();
    if (LastServiceCycles == 0) {
        LastServiceCycles = NowCycles;
        return;
    }

    const uint64_t Delta = NowCycles - LastServiceCycles;
    LastServiceCycles = NowCycles;
    Mtime += Delta / CyclesPerTick;

    if ((Control & DeviceSpecs::Timer::ControlIrqEnable) != 0 && Mtime >= MtimeCmp &&
        !IrqPending) {
        IrqPending = true;
    }
}

void DeliverInterrupts(Cpu& Vm) {
    if (!Enabled || !IrqPending || (Control & DeviceSpecs::Timer::ControlIrqEnable) == 0) {
        return;
    }
    if (Mtime < MtimeCmp) {
        IrqPending = false;
        return;
    }
    Vm.RaiseInterrupt(DeviceSpecs::Timer::IrqVector);
}

} // namespace PlatformTimer
