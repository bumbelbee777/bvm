#include "DmaEngine.h"

#include "DeviceSpecs.h"
#include "GuestMem.h"
#include "MmioMap.h"

namespace {

bool Enabled = true;
uint8_t Status = 0;
uint8_t Control = 0;
uint64_t Src = 0;
uint64_t Dst = 0;
uint64_t Length = 0;

void RunTransfer() {
    Status = DeviceSpecs::Dma::StatusBusy;
    if (Length == 0 || Length > DeviceSpecs::Dma::MaxTransferBytes) {
        Status = DeviceSpecs::Dma::StatusError;
        Control = 0;
        return;
    }
    const bool Ok = GuestMem::CopyRegion(Dst, Src, static_cast<size_t>(Length));
    Status = Ok ? DeviceSpecs::Dma::StatusComplete : DeviceSpecs::Dma::StatusError;
    Control = 0;
}

} // namespace

namespace DmaEngine {

void Reset() {
    Status = 0;
    Control = 0;
    Src = 0;
    Dst = 0;
    Length = 0;
}

void SetEnabled(bool IsEnabled) { Enabled = IsEnabled; }

bool IsEnabled() { return Enabled; }

uint64_t ReadMmioQuad(uint64_t Offset) {
    if (!Enabled) {
        return 0;
    }

    using Quad = MmioWindow::RegisterQuadWord;
    switch (static_cast<Quad>(Offset)) {
        case Quad::DmaStatus:
            return Status;
        case Quad::DmaControl:
            return Control;
        case Quad::DmaSrc:
            return Src;
        case Quad::DmaDst:
            return Dst;
        case Quad::DmaLength:
            return Length;
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
        case Quad::DmaStatus:
            Status = static_cast<uint8_t>(Value & 0xFF);
            break;
        case Quad::DmaControl:
            Control = static_cast<uint8_t>(Value & 0xFF);
            if ((Control & DeviceSpecs::Dma::ControlStart) != 0) {
                RunTransfer();
            }
            break;
        case Quad::DmaSrc:
            Src = Value;
            break;
        case Quad::DmaDst:
            Dst = Value;
            break;
        case Quad::DmaLength:
            Length = Value;
            break;
        default:
            break;
    }
}

} // namespace DmaEngine
