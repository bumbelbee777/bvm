#pragma once

#include "KBankProfile.h"
#include "DeviceSpecs.h"
#include "MmioMap.h"
#include "NumaProfile.h"
#include "SkbProfile.h"

#include <cstddef>
#include <cstdint>
#include <string>

/** Runtime Honeycomb machine parameters (CLI / firmware knobs). */
struct VmConfig {
    size_t RamBytes = RamSizeBytesConstant;
    uint32_t CoreCount = 1;
    size_t KBankBytes = KBankProfile::ByteSize;
    size_t SkbBytes = SkbProfile::SizeBytes;
    uint32_t NumaNodes = 1;
    size_t RemoteKBytes = NumaProfile::RemoteKBytes;
    size_t RemoteSkbBytes = NumaProfile::RemoteSkbBytes;
    size_t RemoteDramBytes = NumaProfile::RemoteDramBytes;
    uint32_t FramebufferWidth = 640;
    uint32_t FramebufferHeight = 480;
    float HostAudioGain = 1.0f;
    size_t DiskBytes = 4U * 1024U * 1024U;
    uint64_t TimerCyclesPerTick = 100000ULL;
    uint64_t TimerFrequencyHz = DeviceSpecs::Timer::DefaultFrequencyHz;
    bool EnableAc97 = true;
    bool EnableUsb3 = true;
    bool EnableBlock = true;
    bool EnableUart = true;
    bool EnablePlatformIrq = true;
    bool EnablePlatformTimer = true;
    bool EnablePlatformRtc = true;
    bool EnableDma = true;
    bool EnableVirtio = true;
    std::string DiskImagePath;
    std::string FirmwarePath;
    uint64_t FirmwareLoadAddress = DeviceSpecs::Platform::DefaultFirmwareLoadAddress;
    uint64_t FirmwareEntryOffset = 0;
};

constexpr size_t RamSizeBytesMax = static_cast<size_t>(1) << 28;

bool ParseByteSize(const std::string& Token, size_t& OutBytes);
VmConfig VmConfigDefaults();
bool ValidateVmConfig(VmConfig& Config, std::string& Error);
void PrintVmConfigUsage();
