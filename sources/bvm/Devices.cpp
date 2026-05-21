#include "Devices.h"
#include "Ac97.h"
#include "Block.h"
#include "DeviceSpecs.h"
#include "Display.h"
#include "DmaEngine.h"
#include "HostIo.h"
#include "Machine.h"
#include "MmioMap.h"
#include "PlatformIrq.h"
#include "PlatformRtc.h"
#include "PlatformTimer.h"
#include "Uart.h"
#include "Usb3.h"
#include "VirtioMmio.h"

#include <stdexcept>

namespace Devices {

namespace {

uint64_t BuildPresentMask() {
    uint64_t Mask = 0;
    if (Ac97::IsEnabled()) {
        Mask |= DeviceSpecs::Platform::PresentAc97;
    }
    if (Usb3::IsEnabled()) {
        Mask |= DeviceSpecs::Platform::PresentUsb3;
    }
    if (Display::IsEnabled()) {
        Mask |= DeviceSpecs::Platform::PresentDisplay;
    }
    if (Block::IsEnabled()) {
        Mask |= DeviceSpecs::Platform::PresentBlock;
    }
    if (Uart::IsEnabled()) {
        Mask |= DeviceSpecs::Platform::PresentUart;
    }
    if (PlatformIrq::IsEnabled()) {
        Mask |= DeviceSpecs::Platform::PresentIrq;
    }
    if (PlatformTimer::IsEnabled()) {
        Mask |= DeviceSpecs::Platform::PresentTimer;
    }
    if (PlatformRtc::IsEnabled()) {
        Mask |= DeviceSpecs::Platform::PresentRtc;
    }
    if (DmaEngine::IsEnabled()) {
        Mask |= DeviceSpecs::Platform::PresentDma;
    }
    if (VirtioMmio::IsEnabled()) {
        Mask |= DeviceSpecs::Platform::PresentVirtio;
    }
    return Mask;
}

bool DispatchRead(uint64_t Offset) {
    return IsAc97MmioQuad(Offset) || IsUsb3MmioQuad(Offset) || IsGopMmioQuad(Offset) ||
           IsBlockMmioQuad(Offset) || IsUartMmioQuad(Offset) || IsIrqMmioQuad(Offset) ||
           IsTimerMmioQuad(Offset) || IsRtcMmioQuad(Offset) || IsDmaMmioQuad(Offset) ||
           IsVirtioPciMmioQuad(Offset) || IsVirtioDeviceMmioQuad(Offset);
}

uint64_t ReadFromDevice(uint64_t Offset) {
    if (IsAc97MmioQuad(Offset)) {
        return Ac97::ReadMmioQuad(Offset);
    }
    if (IsUsb3MmioQuad(Offset)) {
        return Usb3::ReadMmioQuad(Offset);
    }
    if (IsGopMmioQuad(Offset)) {
        return Display::ReadMmioQuad(Offset);
    }
    if (IsBlockMmioQuad(Offset)) {
        return Block::ReadMmioQuad(Offset);
    }
    if (IsUartMmioQuad(Offset)) {
        return Uart::ReadMmioQuad(Offset);
    }
    if (IsIrqMmioQuad(Offset)) {
        return PlatformIrq::ReadMmioQuad(Offset);
    }
    if (IsTimerMmioQuad(Offset)) {
        return PlatformTimer::ReadMmioQuad(Offset);
    }
    if (IsRtcMmioQuad(Offset)) {
        return PlatformRtc::ReadMmioQuad(Offset);
    }
    if (IsDmaMmioQuad(Offset)) {
        return DmaEngine::ReadMmioQuad(Offset);
    }
    if (IsVirtioPciMmioQuad(Offset) || IsVirtioDeviceMmioQuad(Offset)) {
        return VirtioMmio::ReadMmioQuad(Offset);
    }
    return 0;
}

void WriteToDevice(uint64_t Offset, uint64_t Value) {
    if (IsAc97MmioQuad(Offset)) {
        Ac97::WriteMmioQuad(Offset, Value);
        return;
    }
    if (IsUsb3MmioQuad(Offset)) {
        Usb3::WriteMmioQuad(Offset, Value);
        return;
    }
    if (IsGopMmioQuad(Offset)) {
        Display::WriteMmioQuad(Offset, Value);
        return;
    }
    if (IsBlockMmioQuad(Offset)) {
        Block::WriteMmioQuad(Offset, Value);
        return;
    }
    if (IsUartMmioQuad(Offset)) {
        Uart::WriteMmioQuad(Offset, Value);
        return;
    }
    if (IsIrqMmioQuad(Offset)) {
        PlatformIrq::WriteMmioQuad(Offset, Value);
        return;
    }
    if (IsTimerMmioQuad(Offset)) {
        PlatformTimer::WriteMmioQuad(Offset, Value);
        return;
    }
    if (IsRtcMmioQuad(Offset)) {
        PlatformRtc::WriteMmioQuad(Offset, Value);
        return;
    }
    if (IsDmaMmioQuad(Offset)) {
        DmaEngine::WriteMmioQuad(Offset, Value);
        return;
    }
    if (IsVirtioPciMmioQuad(Offset) || IsVirtioDeviceMmioQuad(Offset)) {
        VirtioMmio::WriteMmioQuad(Offset, Value);
    }
}

} // namespace

void ResetAll() {
    const VmConfig& Config = GetMachineConfig();
    PlatformIrq::Reset();
    PlatformIrq::SetEnabled(Config.EnablePlatformIrq);
    PlatformTimer::Reset();
    PlatformTimer::SetEnabled(Config.EnablePlatformTimer);
    PlatformRtc::Reset();
    PlatformRtc::SetEnabled(Config.EnablePlatformRtc);
    DmaEngine::Reset();
    DmaEngine::SetEnabled(Config.EnableDma);
    VirtioMmio::ResetAll();
    VirtioMmio::SetEnabled(Config.EnableVirtio);
    Display::Reset();
    Block::ConfigureStorage(Config.DiskBytes);
    if (!Config.DiskImagePath.empty()) {
        Block::LoadImage(Config.DiskImagePath);
    }
    Block::Reset();
    Block::SetEnabled(Config.EnableBlock);
    Ac97::Reset();
    Ac97::SetEnabled(Config.EnableAc97);
    Usb3::Reset();
    Usb3::SetEnabled(Config.EnableUsb3);
    Uart::Reset();
    Uart::SetEnabled(Config.EnableUart);
}

void SetAc97Enabled(bool Enabled) { Ac97::SetEnabled(Enabled); }
void SetUsb3Enabled(bool Enabled) { Usb3::SetEnabled(Enabled); }
void SetBlockEnabled(bool Enabled) { Block::SetEnabled(Enabled); }
void SetUartEnabled(bool Enabled) { Uart::SetEnabled(Enabled); }

bool LoadDiskImage(const std::string& Path) { return Block::LoadImage(Path); }
bool SaveDiskImage(const std::string& Path) { return Block::SaveImage(Path); }

uint64_t GetPresentMask() { return BuildPresentMask(); }

bool IsDeviceMmioQuad(uint64_t Offset) { return ::IsDeviceMmioQuad(Offset); }

uint64_t ReadMmioQuad(uint64_t Offset) {
    if (!DispatchRead(Offset)) {
        throw std::runtime_error("Unmapped device MMIO quad read");
    }
    return ReadFromDevice(Offset);
}

void WriteMmioQuad(uint64_t Offset, uint64_t Value) {
    if (!DispatchRead(Offset)) {
        throw std::runtime_error("Unmapped device MMIO quad write");
    }
    WriteToDevice(Offset, Value);
}

void Poll(Cpu& Vm) {
    if (HostIo::IsActive()) {
        HostIo::Poll();
    }
    PlatformTimer::Service(Vm);
    PlatformRtc::Service(Vm);
    Usb3::Poll();
    Uart::Poll();
    Ac97::Poll();
}

void Service(Cpu& Vm) {
    Poll(Vm);
    if (Display::FrameDue()) {
        Display::Present(Ram.data());
    }
    PlatformTimer::DeliverInterrupts(Vm);
    PlatformIrq::Deliver(Vm);
}

} // namespace Devices
