#include "Display.h"
#include "DeviceSpecs.h"
#include "GuestMem.h"
#include "HostIo.h"
#include "Machine.h"
#include "MmioMap.h"
#include "PlatformIrq.h"

namespace {

bool Enabled = true;
uint32_t Version = DeviceSpecs::Gop::Version100;
uint32_t HorizontalResolution = 320;
uint32_t VerticalResolution = 240;
uint32_t PixelFormat = DeviceSpecs::Gop::PixelRedGreenBlueReserved8BitPerColor;
uint32_t PixelsPerScanLine = 320;
uint64_t FrameBufferBase = 0;
uint64_t FrameBufferSize = 0;
uint8_t Control = 0;
uint8_t Status = DeviceSpecs::Gop::StatusReady;
uint32_t PixelMaskR = 0x00FF0000;
uint32_t PixelMaskG = 0x0000FF00;
uint32_t PixelMaskB = 0x000000FF;
uint32_t PixelMaskReserved = 0xFF000000;
uint64_t BackBufferBase = 0;
uint32_t StrideBytes = 0;
bool RedrawRequested = false;

void PerformFlip() {
    if (BackBufferBase == 0 || FrameBufferBase == 0 || FrameBufferSize == 0) {
        return;
    }
    Status |= DeviceSpecs::Gop::StatusFlipPending;
    GuestMem::CopyRegion(FrameBufferBase, BackBufferBase, static_cast<size_t>(FrameBufferSize));
    Status = static_cast<uint8_t>((Status & ~DeviceSpecs::Gop::StatusFlipPending) |
                                  DeviceSpecs::Gop::StatusReady |
                                  DeviceSpecs::Gop::StatusVerticalSync);
    RedrawRequested = true;
}

void RecomputeFrameBufferSize() {
    FrameBufferSize =
        static_cast<uint64_t>(PixelsPerScanLine) * VerticalResolution * 4U;
}

void ApplyDefaults() {
    HorizontalResolution = GetFramebufferWidth();
    VerticalResolution = GetFramebufferHeight();
    PixelsPerScanLine = HorizontalResolution;
    PixelFormat = DeviceSpecs::Gop::PixelRedGreenBlueReserved8BitPerColor;
    FrameBufferBase = GetFramebufferBase();
    BackBufferBase = FrameBufferBase;
    StrideBytes = PixelsPerScanLine * DeviceSpecs::Gop::BytesPerPixelXrgb;
    RecomputeFrameBufferSize();
    Status = DeviceSpecs::Gop::StatusReady;
}

} // namespace

namespace Display {

void Reset() {
    Control = static_cast<uint8_t>(DeviceSpecs::Gop::ControlEnable |
                                   DeviceSpecs::Gop::ControlVsyncIrq);
    RedrawRequested = false;
    ApplyDefaults();
}

void SetEnabled(bool IsEnabled) { Enabled = IsEnabled; }

bool IsEnabled() { return Enabled; }

uint64_t ReadMmioQuad(uint64_t Offset) {
    if (!Enabled) {
        return 0;
    }

    using Quad = MmioWindow::RegisterQuadWord;
    switch (static_cast<Quad>(Offset)) {
        case Quad::GopVersion:
            return Version;
        case Quad::GopHorizontalResolution:
            return HorizontalResolution;
        case Quad::GopVerticalResolution:
            return VerticalResolution;
        case Quad::GopPixelFormat:
            return PixelFormat;
        case Quad::GopPixelsPerScanLine:
            return PixelsPerScanLine;
        case Quad::GopFrameBufferBase:
            return FrameBufferBase;
        case Quad::GopFrameBufferSize:
            return FrameBufferSize;
        case Quad::GopControl:
            return Control;
        case Quad::GopStatus:
            return Status;
        case Quad::GopPixelMaskR:
            return PixelMaskR;
        case Quad::GopPixelMaskG:
            return PixelMaskG;
        case Quad::GopPixelMaskB:
            return PixelMaskB;
        case Quad::GopPixelMaskReserved:
            return PixelMaskReserved;
        case Quad::GopBackBufferBase:
            return BackBufferBase;
        case Quad::GopStrideBytes:
            return StrideBytes;
        case Quad::GopFlipControl:
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
        case Quad::GopHorizontalResolution:
            HorizontalResolution = static_cast<uint32_t>(Value);
            if (PixelsPerScanLine < HorizontalResolution) {
                PixelsPerScanLine = HorizontalResolution;
            }
            RecomputeFrameBufferSize();
            StrideBytes = PixelsPerScanLine * DeviceSpecs::Gop::BytesPerPixelXrgb;
            HostIo::ResizeWindow(HorizontalResolution, VerticalResolution);
            break;
        case Quad::GopVerticalResolution:
            VerticalResolution = static_cast<uint32_t>(Value);
            RecomputeFrameBufferSize();
            HostIo::ResizeWindow(HorizontalResolution, VerticalResolution);
            break;
        case Quad::GopPixelFormat:
            PixelFormat = static_cast<uint32_t>(Value);
            break;
        case Quad::GopPixelsPerScanLine:
            PixelsPerScanLine = static_cast<uint32_t>(Value);
            RecomputeFrameBufferSize();
            break;
        case Quad::GopFrameBufferBase:
            FrameBufferBase = Value;
            break;
        case Quad::GopBackBufferBase:
            BackBufferBase = Value;
            break;
        case Quad::GopStrideBytes:
            StrideBytes = static_cast<uint32_t>(Value);
            break;
        case Quad::GopFlipControl:
            if ((Value & DeviceSpecs::Gop::ControlFlip) != 0) {
                PerformFlip();
            }
            break;
        case Quad::GopControl:
            Control = static_cast<uint8_t>(Value & 0xFF);
            if ((Control & DeviceSpecs::Gop::ControlRedraw) != 0) {
                RedrawRequested = true;
                Control = static_cast<uint8_t>(Control & ~DeviceSpecs::Gop::ControlRedraw);
            }
            break;
        case Quad::GopStatus:
            Status = static_cast<uint8_t>(Value & 0xFF);
            break;
        default:
            break;
    }
}

bool FrameDue() {
    if (!Enabled || (Control & DeviceSpecs::Gop::ControlEnable) == 0) {
        return false;
    }
    if (RedrawRequested) {
        return true;
    }
    return HostIo::IsActive() && HostIo::VsyncDue();
}

void Present(const uint8_t* RamImage) {
    if (!Enabled || RamImage == nullptr) {
        RedrawRequested = false;
        return;
    }
    if ((Control & DeviceSpecs::Gop::ControlEnable) == 0) {
        RedrawRequested = false;
        return;
    }

    const uint32_t Pitch = StrideBytes != 0 ? StrideBytes : PixelsPerScanLine * 4U;
    HostIo::PresentSurface(RamImage, FrameBufferBase, HorizontalResolution, VerticalResolution,
                           Pitch, PixelFormat);
    RedrawRequested = false;
    Status = static_cast<uint8_t>(DeviceSpecs::Gop::StatusReady |
                                  DeviceSpecs::Gop::StatusVerticalSync);
    if ((Control & DeviceSpecs::Gop::ControlVsyncIrq) != 0) {
        PlatformIrq::SignalLine(DeviceSpecs::IrqLine::Display);
    }
}

} // namespace Display
