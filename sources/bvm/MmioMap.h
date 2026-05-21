#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t RamSizeBytesConstant = static_cast<size_t>(1) << 24;

/** MMIO aperture: 8 KiB immediately above RAM. */
struct MmioWindow {
    static constexpr uint64_t WindowBasePhysicalAddress =
        static_cast<uint64_t>(RamSizeBytesConstant);

    static constexpr uint64_t WindowByteExtent = 8192ULL;

    static constexpr bool CoversPhysicalAddress(uint64_t Addr) noexcept {
        return Addr >= WindowBasePhysicalAddress &&
               Addr < WindowBasePhysicalAddress + WindowByteExtent;
    }

    static constexpr uint64_t OffsetWithinWindow(uint64_t Addr) noexcept {
        return Addr - WindowBasePhysicalAddress;
    }

    enum class RegisterQuadWord : uint64_t {
        DeviceIdentity = 0,
        BootControl = 8,
        InterruptPost = 16,
        SyscallHandlerIc = 24,
        LastSyscallNumber = 32,
        PlatformDevicePresent = 40,

        Ac97Control = 64,
        Ac97Status = 72,
        Ac97BufferPtr = 80,
        Ac97BufferLength = 88,
        Ac97MasterVolume = 96,
        Ac97BufferCursor = 104,
        Ac97Format = 112,
        Ac97SampleRate = 120,
        Ac97RingRead = 128,
        Ac97RingWrite = 136,

        Usb3Capabilities = 160,
        Usb3Command = 168,
        Usb3Status = 176,
        Usb3EventRingPtr = 184,
        Usb3EventReadIndex = 192,
        Usb3EventWriteIndex = 200,
        Usb3EventPending = 208,
        Usb3Doorbell = 216,

        GopVersion = 256,
        GopHorizontalResolution = 264,
        GopVerticalResolution = 272,
        GopPixelFormat = 280,
        GopPixelsPerScanLine = 288,
        GopFrameBufferBase = 296,
        GopFrameBufferSize = 304,
        GopControl = 312,
        GopStatus = 320,
        GopPixelMaskR = 328,
        GopPixelMaskG = 336,
        GopPixelMaskB = 344,
        GopPixelMaskReserved = 352,
        GopBackBufferBase = 360,
        GopStrideBytes = 368,
        GopFlipControl = 376,

        BlockCapacitySectors = 384,
        BlockSizeBytes = 392,
        BlockCommand = 400,
        BlockLba = 408,
        BlockBufferPtr = 416,
        BlockStatus = 424,
        BlockSectorCount = 432,

        UartData = 448,
        UartLineStatus = 456,
        UartControl = 464,

        IrqPending = 480,
        IrqEnable = 488,
        IrqVectors = 496,
        IrqControl = 504,

        Mtime = 512,
        MtimeCmp = 520,
        MtimeFreq = 528,
        MtimeControl = 536,

        RtcSeconds = 560,
        RtcNanoseconds = 568,
        RtcSetSeconds = 576,
        RtcAlarmSeconds = 584,
        RtcControl = 592,
        RtcDayOfWeek = 600,
        RtcTimezoneOffsetMinutes = 608,
        RtcAlarmNanoseconds = 616,
        RtcStatus = 624,

        DmaStatus = 640,
        DmaControl = 648,
        DmaSrc = 656,
        DmaDst = 664,
        DmaLength = 672,

        VirtioPciVendorDevice = 704,
        VirtioPciBlockBase = 712,
        VirtioPciConsoleBase = 720,
        VirtioPciGpuBase = 728,
        VirtioPciIrqLine = 736,

        VirtioBlockBase = 768,
        VirtioConsoleBase = 896,
        VirtioGpuBase = 1024
    };

    static constexpr uint64_t DeviceIdentityMagicValue = 0x00BEEF05650C041DULL;
    static constexpr uint64_t VirtioDeviceRegionBytes = 128ULL;
};

inline bool IsAc97MmioQuad(uint64_t Offset) {
    return Offset >= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::Ac97Control) &&
           Offset <= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::Ac97RingWrite);
}

inline bool IsUsb3MmioQuad(uint64_t Offset) {
    return Offset >= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::Usb3Capabilities) &&
           Offset <= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::Usb3Doorbell);
}

inline bool IsGopMmioQuad(uint64_t Offset) {
    return Offset >= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::GopVersion) &&
           Offset <= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::GopFlipControl);
}

inline bool IsBlockMmioQuad(uint64_t Offset) {
    return Offset >= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockCapacitySectors) &&
           Offset <= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockSectorCount);
}

inline bool IsUartMmioQuad(uint64_t Offset) {
    return Offset >= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::UartData) &&
           Offset <= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::UartControl);
}

inline bool IsIrqMmioQuad(uint64_t Offset) {
    return Offset >= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::IrqPending) &&
           Offset <= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::IrqControl);
}

inline bool IsTimerMmioQuad(uint64_t Offset) {
    return Offset >= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::Mtime) &&
           Offset <= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::MtimeControl);
}

inline bool IsRtcMmioQuad(uint64_t Offset) {
    return Offset >= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::RtcSeconds) &&
           Offset <= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::RtcStatus);
}

inline bool IsDmaMmioQuad(uint64_t Offset) {
    return Offset >= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::DmaStatus) &&
           Offset <= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::DmaLength);
}

inline bool IsVirtioPciMmioQuad(uint64_t Offset) {
    return Offset >= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::VirtioPciVendorDevice) &&
           Offset <= static_cast<uint64_t>(MmioWindow::RegisterQuadWord::VirtioPciIrqLine);
}

inline bool IsVirtioDeviceMmioQuad(uint64_t Offset) {
    const uint64_t BlockBase =
        static_cast<uint64_t>(MmioWindow::RegisterQuadWord::VirtioBlockBase);
    const uint64_t ConsoleBase =
        static_cast<uint64_t>(MmioWindow::RegisterQuadWord::VirtioConsoleBase);
    const uint64_t GpuBase = static_cast<uint64_t>(MmioWindow::RegisterQuadWord::VirtioGpuBase);
    const uint64_t Region = MmioWindow::VirtioDeviceRegionBytes;
    return (Offset >= BlockBase && Offset < BlockBase + Region) ||
           (Offset >= ConsoleBase && Offset < ConsoleBase + Region) ||
           (Offset >= GpuBase && Offset < GpuBase + Region);
}

inline bool IsDeviceMmioQuad(uint64_t Offset) {
    return IsAc97MmioQuad(Offset) || IsUsb3MmioQuad(Offset) || IsGopMmioQuad(Offset) ||
           IsBlockMmioQuad(Offset) || IsUartMmioQuad(Offset) || IsIrqMmioQuad(Offset) ||
           IsTimerMmioQuad(Offset) || IsRtcMmioQuad(Offset) || IsDmaMmioQuad(Offset) ||
           IsVirtioPciMmioQuad(Offset) || IsVirtioDeviceMmioQuad(Offset);
}
