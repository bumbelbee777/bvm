#include "VirtioMmio.h"

#include "Block.h"
#include "DeviceSpecs.h"
#include "Display.h"
#include "GuestMem.h"
#include "Machine.h"
#include "MmioMap.h"
#include "PlatformIrq.h"
#include "Uart.h"

#include <algorithm>
#include <cstring>

namespace {

bool Enabled = true;

namespace VirtioRegs = DeviceSpecs::VirtioMmioSpec;

constexpr uint32_t VirtioVendorId = 0x1AF4U;
constexpr uint32_t VirtioBlkFeatureSizeMax = 1U << 0;

struct VirtioDeviceState {
    uint32_t DeviceId = 0;
    uint32_t DeviceFeatures = 0;
    uint32_t DriverFeatures = 0;
    uint32_t QueueSel = 0;
    uint32_t QueueNumMax = 64;
    uint32_t QueueNum = 0;
    uint32_t QueueReady = 0;
    uint32_t InterruptStatus = 0;
    uint32_t Status = 0;
    uint64_t QueueDesc = 0;
    uint64_t QueueDriver = 0;
    uint64_t QueueDevice = 0;
};

VirtioDeviceState BlockDev;
VirtioDeviceState ConsoleDev;
VirtioDeviceState GpuDev;

uint64_t WindowBase() { return GetMmioWindowBase(); }

VirtioDeviceState* DeviceForOffset(uint64_t Offset, uint64_t& RegionBase) {
    const uint64_t BlockBase =
        static_cast<uint64_t>(MmioWindow::RegisterQuadWord::VirtioBlockBase);
    const uint64_t ConsoleBase =
        static_cast<uint64_t>(MmioWindow::RegisterQuadWord::VirtioConsoleBase);
    const uint64_t GpuBase = static_cast<uint64_t>(MmioWindow::RegisterQuadWord::VirtioGpuBase);
    const uint64_t Region = MmioWindow::VirtioDeviceRegionBytes;

    if (Offset >= BlockBase && Offset < BlockBase + Region) {
        RegionBase = BlockBase;
        return &BlockDev;
    }
    if (Offset >= ConsoleBase && Offset < ConsoleBase + Region) {
        RegionBase = ConsoleBase;
        return &ConsoleDev;
    }
    if (Offset >= GpuBase && Offset < GpuBase + Region) {
        RegionBase = GpuBase;
        return &GpuDev;
    }
    RegionBase = 0;
    return nullptr;
}

uint32_t ReadReg(const VirtioDeviceState& Dev, uint32_t RegOffset) {
    switch (RegOffset) {
        case VirtioRegs::RegMagic:
            return VirtioRegs::MagicValue;
        case VirtioRegs::RegVersion:
            return VirtioRegs::VersionValue;
        case VirtioRegs::RegDeviceId:
            return Dev.DeviceId;
        case VirtioRegs::RegVendorId:
            return VirtioVendorId;
        case VirtioRegs::RegDeviceFeatures:
            return Dev.DeviceFeatures;
        case VirtioRegs::RegDriverFeatures:
            return Dev.DriverFeatures;
        case VirtioRegs::RegQueueSel:
            return Dev.QueueSel;
        case VirtioRegs::RegQueueNumMax:
            return Dev.QueueNumMax;
        case VirtioRegs::RegQueueNum:
            return Dev.QueueNum;
        case VirtioRegs::RegQueueReady:
            return Dev.QueueReady;
        case VirtioRegs::RegInterruptStatus:
            return Dev.InterruptStatus;
        case VirtioRegs::RegStatus:
            return Dev.Status;
        case VirtioRegs::RegQueueDesc:
            return static_cast<uint32_t>(Dev.QueueDesc);
        case VirtioRegs::RegQueueDriver:
            return static_cast<uint32_t>(Dev.QueueDriver);
        case VirtioRegs::RegQueueDevice:
            return static_cast<uint32_t>(Dev.QueueDevice);
        default:
            return 0;
    }
}

void WriteReg(VirtioDeviceState& Dev, uint32_t RegOffset, uint32_t Value) {
    switch (RegOffset) {
        case VirtioRegs::RegDriverFeatures:
            Dev.DriverFeatures = Value;
            break;
        case VirtioRegs::RegQueueSel:
            Dev.QueueSel = Value;
            break;
        case VirtioRegs::RegQueueNum:
            Dev.QueueNum = std::min(Value, Dev.QueueNumMax);
            break;
        case VirtioRegs::RegQueueReady:
            Dev.QueueReady = Value & 1U;
            break;
        case VirtioRegs::RegInterruptAck:
            Dev.InterruptStatus &= ~Value;
            break;
        case VirtioRegs::RegStatus:
            Dev.Status = Value;
            if (Value == 0) {
                Dev.QueueReady = 0;
                Dev.InterruptStatus = 0;
            }
            break;
        case VirtioRegs::RegQueueDesc:
            Dev.QueueDesc = (Dev.QueueDesc & 0xFFFFFFFF00000000ULL) | Value;
            break;
        case VirtioRegs::RegQueueDriver:
            Dev.QueueDriver = (Dev.QueueDriver & 0xFFFFFFFF00000000ULL) | Value;
            break;
        case VirtioRegs::RegQueueDevice:
            Dev.QueueDevice = (Dev.QueueDevice & 0xFFFFFFFF00000000ULL) | Value;
            break;
        default:
            break;
    }
}

struct VirtqDesc {
    uint64_t Addr = 0;
    uint32_t Len = 0;
    uint16_t Flags = 0;
    uint16_t Next = 0;
};

bool ReadDescriptor(uint64_t Table, uint16_t Index, VirtqDesc& Out) {
    const uint64_t Addr = Table + static_cast<uint64_t>(Index) * 16ULL;
    if (!GuestMem::ReadU64(Addr, Out.Addr) ||
        !GuestMem::ReadBytes(Addr + 8, &Out.Len, sizeof(Out.Len)) ||
        !GuestMem::ReadBytes(Addr + 12, &Out.Flags, sizeof(Out.Flags)) ||
        !GuestMem::ReadBytes(Addr + 14, &Out.Next, sizeof(Out.Next))) {
        return false;
    }
    return true;
}

void ProcessBlockQueue() {
    if (BlockDev.QueueReady == 0 || BlockDev.QueueNum == 0) {
        return;
    }

    uint16_t AvailIdx = 0;
    uint16_t UsedIdx = 0;
    if (!GuestMem::ReadBytes(BlockDev.QueueDriver + 4, &AvailIdx, 2) ||
        !GuestMem::ReadBytes(BlockDev.QueueDevice + 2, &UsedIdx, 2) || AvailIdx == UsedIdx) {
        return;
    }

    uint16_t RingIndex = 0;
    const uint64_t RingBase = BlockDev.QueueDriver + 6;
    if (!GuestMem::ReadBytes(RingBase + static_cast<uint64_t>(UsedIdx % BlockDev.QueueNum) * 2,
                             &RingIndex, 2)) {
        return;
    }

    VirtqDesc HeaderDesc{};
    VirtqDesc DataDesc{};
    VirtqDesc StatusDesc{};
    if (!ReadDescriptor(BlockDev.QueueDesc, RingIndex, HeaderDesc) ||
        !ReadDescriptor(BlockDev.QueueDesc, HeaderDesc.Next, DataDesc) ||
        !ReadDescriptor(BlockDev.QueueDesc, DataDesc.Next, StatusDesc)) {
        return;
    }

    uint32_t RequestType = 0;
    uint64_t Sector = 0;
    if (!GuestMem::ReadBytes(HeaderDesc.Addr, &RequestType, 4) ||
        !GuestMem::ReadU64(HeaderDesc.Addr + 8, Sector)) {
        return;
    }

    uint8_t BlkStatus = 1;
    Block::WriteMmioQuad(static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockLba), Sector);
    Block::WriteMmioQuad(static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockBufferPtr),
                         DataDesc.Addr);
    Block::WriteMmioQuad(static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockSectorCount), 1);
    if (RequestType == 0) {
        Block::WriteMmioQuad(static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockCommand),
                             DeviceSpecs::VirtioBlock::CommandRead);
    } else if (RequestType == 1) {
        Block::WriteMmioQuad(static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockCommand),
                             DeviceSpecs::VirtioBlock::CommandWrite);
    } else {
        BlkStatus = 2;
    }

    if (BlkStatus == 1) {
        const uint64_t BlockStatus = Block::ReadMmioQuad(
            static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BlockStatus));
        BlkStatus = BlockStatus == DeviceSpecs::VirtioBlock::StatusComplete ? 0 : 1;
    }

    GuestMem::WriteU8(StatusDesc.Addr, BlkStatus);

    ++UsedIdx;
    GuestMem::WriteBytes(BlockDev.QueueDevice + 2, &UsedIdx, 2);
    const uint64_t UsedElem = BlockDev.QueueDevice + 4 +
                              static_cast<uint64_t>((UsedIdx - 1) % BlockDev.QueueNum) * 8ULL;
    GuestMem::WriteBytes(UsedElem, &RingIndex, 4);

    BlockDev.InterruptStatus = 1;
    PlatformIrq::SignalLine(DeviceSpecs::IrqLine::Block);
}

void ProcessConsoleQueue() {
    if (ConsoleDev.QueueReady == 0 || ConsoleDev.QueueNum == 0) {
        return;
    }

    uint16_t AvailIdx = 0;
    uint16_t UsedIdx = 0;
    if (!GuestMem::ReadBytes(ConsoleDev.QueueDriver + 4, &AvailIdx, 2) ||
        !GuestMem::ReadBytes(ConsoleDev.QueueDevice + 2, &UsedIdx, 2) ||
        AvailIdx == UsedIdx) {
        return;
    }

    uint16_t RingIndex = 0;
    const uint64_t RingBase = ConsoleDev.QueueDriver + 6;
    if (!GuestMem::ReadBytes(RingBase + static_cast<uint64_t>(UsedIdx % ConsoleDev.QueueNum) * 2,
                             &RingIndex, 2)) {
        return;
    }

    VirtqDesc Desc{};
    if (!ReadDescriptor(ConsoleDev.QueueDesc, RingIndex, Desc)) {
        return;
    }

    uint8_t Byte = 0;
    if (GuestMem::ReadU8(Desc.Addr, Byte)) {
        Uart::WriteMmioQuad(static_cast<uint64_t>(MmioWindow::RegisterQuadWord::UartData), Byte);
    }

    ++UsedIdx;
    GuestMem::WriteBytes(ConsoleDev.QueueDevice + 2, &UsedIdx, 2);
    ConsoleDev.InterruptStatus = 1;
    PlatformIrq::SignalLine(DeviceSpecs::IrqLine::Uart);
}

void ProcessGpuQueue() {
    if (GpuDev.QueueReady == 0) {
        return;
    }
    Display::WriteMmioQuad(static_cast<uint64_t>(MmioWindow::RegisterQuadWord::GopControl),
                           DeviceSpecs::Gop::ControlRedraw);
    GpuDev.InterruptStatus = 1;
    PlatformIrq::SignalLine(DeviceSpecs::IrqLine::Display);
}

void ProcessQueueNotify(VirtioDeviceState& Dev) {
    if ((Dev.Status & VirtioRegs::StatusDriverOk) == 0) {
        return;
    }
    if (&Dev == &BlockDev) {
        ProcessBlockQueue();
    } else if (&Dev == &ConsoleDev) {
        ProcessConsoleQueue();
    } else if (&Dev == &GpuDev) {
        ProcessGpuQueue();
    }
}

void InitDevice(VirtioDeviceState& Dev, uint32_t DeviceId, uint32_t Features) {
    Dev = VirtioDeviceState{};
    Dev.DeviceId = DeviceId;
    Dev.DeviceFeatures = Features;
    Dev.QueueNumMax = 64;
}

} // namespace

namespace VirtioMmio {

void ResetAll() {
    InitDevice(BlockDev, VirtioRegs::DeviceIdBlock, VirtioBlkFeatureSizeMax);
    InitDevice(ConsoleDev, VirtioRegs::DeviceIdConsole, 0);
    InitDevice(GpuDev, VirtioRegs::DeviceIdGpu, 0);
}

void SetEnabled(bool IsEnabled) { Enabled = IsEnabled; }

bool IsEnabled() { return Enabled; }

uint64_t ReadMmioQuad(uint64_t Offset) {
    if (!Enabled) {
        return 0;
    }

    using Quad = MmioWindow::RegisterQuadWord;
    switch (static_cast<Quad>(Offset)) {
        case Quad::VirtioPciVendorDevice:
            return VirtioVendorId |
                   (static_cast<uint64_t>(VirtioRegs::DeviceIdBlock) << 32);
        case Quad::VirtioPciBlockBase:
            return WindowBase() +
                   static_cast<uint64_t>(MmioWindow::RegisterQuadWord::VirtioBlockBase);
        case Quad::VirtioPciConsoleBase:
            return WindowBase() +
                   static_cast<uint64_t>(MmioWindow::RegisterQuadWord::VirtioConsoleBase);
        case Quad::VirtioPciGpuBase:
            return WindowBase() +
                   static_cast<uint64_t>(MmioWindow::RegisterQuadWord::VirtioGpuBase);
        case Quad::VirtioPciIrqLine:
            return DeviceSpecs::IrqLine::Block;
        default:
            break;
    }

    uint64_t RegionBase = 0;
    VirtioDeviceState* Dev = DeviceForOffset(Offset, RegionBase);
    if (Dev == nullptr) {
        return 0;
    }

    const uint32_t RegOffset = static_cast<uint32_t>(Offset - RegionBase);
    if (RegOffset == VirtioRegs::RegQueueDesc + 4) {
        return static_cast<uint32_t>(Dev->QueueDesc >> 32);
    }
    if (RegOffset == VirtioRegs::RegQueueDriver + 4) {
        return static_cast<uint32_t>(Dev->QueueDriver >> 32);
    }
    if (RegOffset == VirtioRegs::RegQueueDevice + 4) {
        return static_cast<uint32_t>(Dev->QueueDevice >> 32);
    }
    return ReadReg(*Dev, RegOffset);
}

void WriteMmioQuad(uint64_t Offset, uint64_t Value) {
    if (!Enabled) {
        return;
    }

    using Quad = MmioWindow::RegisterQuadWord;
    switch (static_cast<Quad>(Offset)) {
        case Quad::VirtioPciVendorDevice:
        case Quad::VirtioPciBlockBase:
        case Quad::VirtioPciConsoleBase:
        case Quad::VirtioPciGpuBase:
        case Quad::VirtioPciIrqLine:
            return;
        default:
            break;
    }

    uint64_t RegionBase = 0;
    VirtioDeviceState* Dev = DeviceForOffset(Offset, RegionBase);
    if (Dev == nullptr) {
        return;
    }

    const uint32_t RegOffset = static_cast<uint32_t>(Offset - RegionBase);
    if (RegOffset == VirtioRegs::RegQueueDesc + 4) {
        Dev->QueueDesc = (Dev->QueueDesc & 0xFFFFFFFFULL) | (Value << 32);
        return;
    }
    if (RegOffset == VirtioRegs::RegQueueDriver + 4) {
        Dev->QueueDriver = (Dev->QueueDriver & 0xFFFFFFFFULL) | (Value << 32);
        return;
    }
    if (RegOffset == VirtioRegs::RegQueueDevice + 4) {
        Dev->QueueDevice = (Dev->QueueDevice & 0xFFFFFFFFULL) | (Value << 32);
        return;
    }
    if (RegOffset == VirtioRegs::RegQueueNotify) {
        ProcessQueueNotify(*Dev);
        return;
    }

    WriteReg(*Dev, RegOffset, static_cast<uint32_t>(Value));
}

} // namespace VirtioMmio
