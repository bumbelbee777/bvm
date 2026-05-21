#pragma once

#include <cstdint>

namespace DeviceSpecs {

namespace Gop {

constexpr uint32_t PixelRedGreenBlueReserved8BitPerColor = 0;
constexpr uint32_t PixelBlueGreenRedReserved8BitPerColor = 1;
constexpr uint32_t PixelBitMask = 2;
constexpr uint32_t PixelBltOnly = 3;

constexpr uint32_t FourCcXrgb8888 = 0x38424752;
constexpr uint32_t FourCcBgra8888 = 0x41524742;

constexpr uint32_t Version100 = 0x00010000;
constexpr uint32_t BytesPerPixelXrgb = 4;

constexpr uint8_t ControlEnable = 0x01;
constexpr uint8_t ControlRedraw = 0x02;
constexpr uint8_t ControlVsyncIrq = 0x04;
constexpr uint8_t ControlFlip = 0x08;

constexpr uint8_t StatusReady = 0x01;
constexpr uint8_t StatusVerticalSync = 0x02;
constexpr uint8_t StatusFlipPending = 0x04;

} // namespace Gop

namespace Xhci {

constexpr uint64_t CapabilityMask = 0x0001000100000001ULL;
constexpr uint8_t StatusHalted = 0x01;
constexpr uint8_t StatusEventPending = 0x02;
constexpr uint8_t CommandReset = 0x01;
constexpr uint8_t CommandRun = 0x02;
constexpr uint32_t EventEntryBytes = 32;
constexpr uint32_t EventRingCapacity = 64;
constexpr uint64_t EventKeyDown = 1;
constexpr uint64_t EventKeyUp = 2;
constexpr uint64_t EventMouseButton = 3;
constexpr uint64_t EventMouseMotion = 4;
constexpr uint8_t ModifierShift = 0x01;
constexpr uint8_t ModifierCtrl = 0x02;
constexpr uint8_t ModifierAlt = 0x04;
constexpr uint8_t ModifierSuper = 0x08;

} // namespace Xhci

namespace Ac97 {

constexpr uint8_t StatusReady = 0x01;
constexpr uint8_t StatusUnderrun = 0x02;
constexpr uint8_t ControlReset = 0x01;
constexpr uint8_t ControlRun = 0x02;
constexpr uint8_t ControlPeriodIrq = 0x04;
constexpr uint16_t CodecReady = 0x8000;
constexpr uint32_t FormatPcm8Mono = 0;
constexpr uint32_t FormatPcm16Mono = 1;
constexpr uint32_t FormatPcm16Stereo = 2;
constexpr uint32_t SamplesPerService = 2048;
constexpr uint32_t DefaultSampleRateHz = 48000;

} // namespace Ac97

namespace VirtioBlock {

constexpr uint32_t BlockSizeBytes = 512;
constexpr uint32_t CommandIdle = 0;
constexpr uint32_t CommandRead = 1;
constexpr uint32_t CommandWrite = 2;
constexpr uint32_t CommandFlush = 3;
constexpr uint32_t StatusIdle = 0;
constexpr uint32_t StatusComplete = 1;
constexpr uint32_t StatusError = 2;
constexpr uint32_t StatusBusy = 3;
constexpr uint32_t MaxSectorsPerCommand = 128;

} // namespace VirtioBlock

namespace VirtioMmioSpec {

constexpr uint32_t MagicValue = 0x74726976U; // "virt"
constexpr uint32_t VersionValue = 2;
constexpr uint32_t VendorIdHoneycomb = 0x484F4E59U; // "HONY"

constexpr uint32_t DeviceIdBlock = 2;
constexpr uint32_t DeviceIdConsole = 3;
constexpr uint32_t DeviceIdGpu = 16;

constexpr uint32_t StatusDriverOk = 0x4;

constexpr uint32_t RegMagic = 0;
constexpr uint32_t RegVersion = 4;
constexpr uint32_t RegDeviceId = 8;
constexpr uint32_t RegVendorId = 12;
constexpr uint32_t RegDeviceFeatures = 16;
constexpr uint32_t RegDriverFeatures = 32;
constexpr uint32_t RegQueueSel = 48;
constexpr uint32_t RegQueueNumMax = 52;
constexpr uint32_t RegQueueNum = 56;
constexpr uint32_t RegQueueReady = 68;
constexpr uint32_t RegQueueNotify = 80;
constexpr uint32_t RegInterruptStatus = 96;
constexpr uint32_t RegInterruptAck = 100;
constexpr uint32_t RegStatus = 112;
constexpr uint32_t RegQueueDesc = 128;
constexpr uint32_t RegQueueDriver = 136;
constexpr uint32_t RegQueueDevice = 144;

} // namespace VirtioMmioSpec

namespace Platform {

constexpr uint64_t PresentAc97 = 1ULL << 0;
constexpr uint64_t PresentUsb3 = 1ULL << 1;
constexpr uint64_t PresentDisplay = 1ULL << 2;
constexpr uint64_t PresentBlock = 1ULL << 3;
constexpr uint64_t PresentUart = 1ULL << 4;
constexpr uint64_t PresentIrq = 1ULL << 5;
constexpr uint64_t PresentTimer = 1ULL << 6;
constexpr uint64_t PresentRtc = 1ULL << 7;
constexpr uint64_t PresentDma = 1ULL << 8;
constexpr uint64_t PresentVirtio = 1ULL << 9;

constexpr uint64_t DefaultFirmwareLoadAddress = 0x100000ULL;

} // namespace Platform

namespace IrqLine {

constexpr uint8_t Timer = 0;
constexpr uint8_t Rtc = 1;
constexpr uint8_t Uart = 2;
constexpr uint8_t Block = 3;
constexpr uint8_t Ac97 = 4;
constexpr uint8_t Usb3 = 5;
constexpr uint8_t Display = 6;
constexpr uint8_t Count = 7;
constexpr uint8_t DefaultVectorBase = 0x30;

} // namespace IrqLine

namespace Timer {

/** Dedicated machine-timer vector (RISC-V MTI-style). */
constexpr uint8_t IrqVector = 0x07;

constexpr uint8_t ControlIrqEnable = 0x01;

constexpr uint64_t DefaultFrequencyHz = 10000000ULL;

} // namespace Timer

namespace Rtc {

constexpr uint8_t ControlAlarmEnable = 0x01;
constexpr uint8_t ControlSetTime = 0x02;
constexpr uint8_t ControlSyncHost = 0x04;
constexpr uint8_t StatusAlarmPending = 0x01;
constexpr uint8_t StatusValid = 0x02;

} // namespace Rtc

namespace Dma {

constexpr uint8_t ControlStart = 0x01;
constexpr uint8_t ControlMemToMem = 0x02;
constexpr uint8_t StatusComplete = 0x01;
constexpr uint8_t StatusError = 0x02;
constexpr uint8_t StatusBusy = 0x04;
constexpr size_t MaxTransferBytes = 16U * 1024U * 1024U;

} // namespace Dma

namespace Uart16550 {

constexpr uint8_t LineStatusDataReady = 0x01;
constexpr uint8_t LineStatusTransmitEmpty = 0x20;
constexpr uint8_t ControlRxIrq = 0x02;

} // namespace Uart16550

} // namespace DeviceSpecs
