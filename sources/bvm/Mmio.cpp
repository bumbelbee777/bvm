#include "Shared.h"
#include "Devices.h"
#include "Machine.h"
#include <stdexcept>

namespace {

uint64_t QuadWordOffsetAligned(uint64_t PhysicalAddress) {
    return (PhysicalAddress & ~7ULL) - GetMmioWindowBase();
}

} // namespace
uint64_t Cpu::MmioDispatchReadQuadWord(uint64_t PhysicalAddress) const {
    RequireSupervisor("Honeycomb MMIO read is supervisor-only");
    const uint64_t Off = QuadWordOffsetAligned(PhysicalAddress);
    switch (Off) {
        case static_cast<uint64_t>(MmioWindow::RegisterQuadWord::DeviceIdentity):
            return MmioWindow::DeviceIdentityMagicValue;
        case static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BootControl):
            return MmioBootControlRegister;
        case static_cast<uint64_t>(MmioWindow::RegisterQuadWord::InterruptPost):
            return 0;
        case static_cast<uint64_t>(MmioWindow::RegisterQuadWord::SyscallHandlerIc):
            return MmioSyscallProgramCounter;
        case static_cast<uint64_t>(MmioWindow::RegisterQuadWord::LastSyscallNumber):
            return MmioLastSyscallNumberRegister;
        case static_cast<uint64_t>(MmioWindow::RegisterQuadWord::PlatformDevicePresent):
            return Devices::GetPresentMask();
        default:
            if (Devices::IsDeviceMmioQuad(Off)) {
                return Devices::ReadMmioQuad(Off);
            }
            throw std::runtime_error(
                "Unmapped Honeycomb MMIO quad read (baseline BVM trap)");
    }
}

void Cpu::MmioDispatchWriteQuadWord(uint64_t PhysicalAddress, uint64_t Value) {
    RequireSupervisor("Honeycomb MMIO write is supervisor-only");
    const uint64_t Off = QuadWordOffsetAligned(PhysicalAddress);
    switch (Off) {
        case static_cast<uint64_t>(MmioWindow::RegisterQuadWord::DeviceIdentity):
            throw std::runtime_error(
                "Honeycomb MMIO DeviceIdentity quad is read-only baseline state");
        case static_cast<uint64_t>(MmioWindow::RegisterQuadWord::BootControl):
            MmioBootControlRegister = Value;
            return;
        case static_cast<uint64_t>(MmioWindow::RegisterQuadWord::InterruptPost): {
            const uint8_t Vector = static_cast<uint8_t>(Value & 0xFFULL);
            RaiseInterrupt(Vector);
            return;
        }
        case static_cast<uint64_t>(MmioWindow::RegisterQuadWord::SyscallHandlerIc):
            MmioSyscallProgramCounter = Value;
            return;
        case static_cast<uint64_t>(MmioWindow::RegisterQuadWord::LastSyscallNumber):
            throw std::runtime_error(
                "Honeycomb MMIO LastSyscallNumber quad updates only inside SYSCALL");
        case static_cast<uint64_t>(MmioWindow::RegisterQuadWord::PlatformDevicePresent):
            throw std::runtime_error(
                "Honeycomb MMIO PlatformDevicePresent quad is read-only baseline state");
        default:
            if (Devices::IsDeviceMmioQuad(Off)) {
                Devices::WriteMmioQuad(Off, Value);
                return;
            }
            throw std::runtime_error(
                "Unmapped Honeycomb MMIO quad write (baseline BVM trap)");
    }
}
