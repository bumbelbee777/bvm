#include "PlatformIrq.h"

#include "DeviceSpecs.h"
#include "MmioMap.h"
#include "Shared.h"

namespace {

bool Enabled = true;
uint8_t Control = 0x01;
uint64_t Pending = 0;
uint64_t EnableMask = 0x7FULL;
uint64_t VectorPack = 0;

uint8_t VectorForLine(uint8_t Line) {
    if (Line >= DeviceSpecs::IrqLine::Count) {
        return DeviceSpecs::IrqLine::DefaultVectorBase;
    }
    const uint64_t Shift = static_cast<uint64_t>(Line) * 8ULL;
    const uint64_t Packed = (VectorPack >> Shift) & 0xFFULL;
    if (Packed == 0) {
        return static_cast<uint8_t>(DeviceSpecs::IrqLine::DefaultVectorBase + Line);
    }
    return static_cast<uint8_t>(Packed);
}

} // namespace

namespace PlatformIrq {

void Reset() {
    Control = 0x01;
    Pending = 0;
    EnableMask = 0x7FULL;
    VectorPack = 0;
    for (uint8_t Line = 0; Line < DeviceSpecs::IrqLine::Count; ++Line) {
        const uint64_t Shift = static_cast<uint64_t>(Line) * 8ULL;
        VectorPack |= static_cast<uint64_t>(DeviceSpecs::IrqLine::DefaultVectorBase + Line)
                      << Shift;
    }
}

void SetEnabled(bool IsEnabled) { Enabled = IsEnabled; }

bool IsEnabled() { return Enabled; }

uint64_t ReadMmioQuad(uint64_t Offset) {
    if (!Enabled) {
        return 0;
    }

    using Quad = MmioWindow::RegisterQuadWord;
    switch (static_cast<Quad>(Offset)) {
        case Quad::IrqPending:
            return Pending;
        case Quad::IrqEnable:
            return EnableMask;
        case Quad::IrqVectors:
            return VectorPack;
        case Quad::IrqControl:
            return Control;
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
        case Quad::IrqPending:
            Pending &= ~Value;
            break;
        case Quad::IrqEnable:
            EnableMask = Value & 0x7FULL;
            break;
        case Quad::IrqVectors:
            VectorPack = Value;
            break;
        case Quad::IrqControl:
            Control = static_cast<uint8_t>(Value & 0xFF);
            break;
        default:
            break;
    }
}

void SignalLine(uint8_t Line) {
    if (!Enabled || Line >= DeviceSpecs::IrqLine::Count) {
        return;
    }
    Pending |= 1ULL << Line;
}

void Deliver(Cpu& Vm) {
    if (!Enabled || (Control & 0x01) == 0 || Pending == 0) {
        return;
    }

    for (uint8_t Line = 0; Line < DeviceSpecs::IrqLine::Count; ++Line) {
        const uint64_t Bit = 1ULL << Line;
        if ((Pending & Bit) == 0 || (EnableMask & Bit) == 0) {
            continue;
        }
        Pending &= ~Bit;
        Vm.RaiseInterrupt(VectorForLine(Line));
    }
}

} // namespace PlatformIrq
