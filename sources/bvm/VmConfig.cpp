#include "VmConfig.h"
#include "DeviceSpecs.h"
#include "PhysMap.h"

#include <algorithm>
#include <cctype>
#include <iostream>

namespace {

bool ParseSizeMagnitude(uint64_t Value, char Suffix, size_t& OutBytes, std::string& Error) {
    uint64_t Scale = 1;
    switch (std::tolower(static_cast<unsigned char>(Suffix))) {
        case '\0':
        case 'b':
            Scale = 1;
            break;
        case 'k':
            Scale = 1024ULL;
            break;
        case 'm':
            Scale = 1024ULL * 1024ULL;
            break;
        case 'g':
            Scale = 1024ULL * 1024ULL * 1024ULL;
            break;
        default:
            Error = "size suffix must be K, M, G, or B";
            return false;
    }
    const uint64_t Product = Value * Scale;
    if (Product > static_cast<uint64_t>(RamSizeBytesMax)) {
        Error = "size exceeds maximum RAM cap";
        return false;
    }
    OutBytes = static_cast<size_t>(Product);
    return true;
}

} // namespace

bool ParseByteSize(const std::string& Token, size_t& OutBytes) {
    std::string Error;
    if (Token.empty()) {
        return false;
    }
    size_t End = 0;
    uint64_t Value = 0;
    try {
        Value = std::stoull(Token, &End, 0);
    } catch (...) {
        return false;
    }
    char Suffix = '\0';
    if (End < Token.size()) {
        Suffix = Token[End];
    }
    return ParseSizeMagnitude(Value, Suffix, OutBytes, Error);
}

VmConfig VmConfigDefaults() { return VmConfig{}; }

bool ValidateVmConfig(VmConfig& Config, std::string& Error) {
    if (Config.RamBytes == 0 || Config.RamBytes > RamSizeBytesMax) {
        Error = "--ram must be between 1 byte and " + std::to_string(RamSizeBytesMax);
        return false;
    }
    if (Config.CoreCount == 0 || Config.CoreCount > 64) {
        Error = "--cores must be 1..64";
        return false;
    }
    if (Config.KBankBytes == 0 ||
        Config.KBankBytes > KBankProfile::ByteSize ||
        (Config.KBankBytes % PhysMap::LocalKBytesPerSlot) != 0) {
        Error = "--kbank must be a positive multiple of 16 up to 256K";
        return false;
    }
    if (Config.SkbBytes == 0 ||
        Config.SkbBytes > SkbProfile::SizeBytes ||
        (Config.SkbBytes % SkbProfile::LineBytes) != 0) {
        Error = "--skb must be a positive 64-byte multiple up to 32M";
        return false;
    }
    if (Config.NumaNodes == 0 || Config.NumaNodes > NumaProfile::MaxNodes) {
        Error = "--numa-nodes must be 1.." + std::to_string(NumaProfile::MaxNodes);
        return false;
    }
    if (Config.RemoteKBytes == 0 || Config.RemoteKBytes > NumaProfile::RemoteKBytes) {
        Error = "--remote-k invalid";
        return false;
    }
    if (Config.RemoteSkbBytes == 0 ||
        Config.RemoteSkbBytes > NumaProfile::RemoteSkbBytes ||
        (Config.RemoteSkbBytes % SkbProfile::LineBytes) != 0) {
        Error = "--remote-skb invalid";
        return false;
    }
    if (Config.RemoteDramBytes == 0 || Config.RemoteDramBytes > NumaProfile::RemoteDramBytes) {
        Error = "--remote-dram invalid";
        return false;
    }
    if (Config.FramebufferWidth == 0 || Config.FramebufferWidth > 1920 ||
        Config.FramebufferHeight == 0 || Config.FramebufferHeight > 1080) {
        Error = "--fb-width/--fb-height out of range (1..1920 x 1..1080)";
        return false;
    }
    const size_t FbBytes =
        static_cast<size_t>(Config.FramebufferWidth) * Config.FramebufferHeight * 4U;
    if (FbBytes > Config.RamBytes) {
        Error = "framebuffer does not fit in configured RAM";
        return false;
    }
    if (Config.HostAudioGain < 0.0f || Config.HostAudioGain > 1.0f) {
        Config.HostAudioGain = std::clamp(Config.HostAudioGain, 0.0f, 1.0f);
    }
    if (Config.DiskBytes < 512 || Config.DiskBytes > 64ULL * 1024ULL * 1024ULL) {
        Error = "--disk must be between 512 bytes and 64M";
        return false;
    }
    if (Config.TimerCyclesPerTick == 0) {
        Error = "timer cycles per tick must be non-zero";
        return false;
    }
    if (Config.TimerFrequencyHz == 0) {
        Error = "timer frequency must be non-zero";
        return false;
    }
    if (!Config.FirmwarePath.empty() && Config.FirmwareLoadAddress >= Config.RamBytes) {
        Error = "firmware load address must be within RAM";
        return false;
    }
    return true;
}

void PrintVmConfigUsage() {
    std::cout << "Machine options:\n"
              << "      --ram SIZE         Legacy DRAM bytes (default 16M, max 256M)\n"
              << "      --cores N          Logical core count (default 1; execution is single-core)\n"
              << "      --kbank SIZE       Local K-Bank size (default 256K, max 256K, multiple of 16)\n"
              << "      --skb SIZE         Local SKB size (default 32M, max 32M, multiple of 64)\n"
              << "      --numa-nodes N     Active NUMA nodes 1..4 (default 1 = local only)\n"
              << "      --remote-k SIZE    Per-node remote K pool (default 256K)\n"
              << "      --remote-skb SIZE  Per-node remote SKB (default 4M)\n"
              << "      --remote-dram SIZE Per-node remote DRAM (default 4M)\n"
              << "      --fb-width W       Host framebuffer width when using --host-io (default 640)\n"
              << "      --fb-height H      Host framebuffer height (default 480)\n"
              << "      --audio-gain F     AC97 master gain 0..1 (default 1)\n"
              << "      --timer-freq HZ    MTIME frequency in Hz (default 10 MHz)\n"
              << "      --disk SIZE        VirtIO-style block backing store (default 4M)\n"
              << "      --disk-image PATH  Load (and save on exit) persistent block image\n"
              << "      --firmware PATH    Load firmware ROM/BIN at 0x100000 before kernel\n"
              << "      --firmware-address Load address for --firmware (default 0x100000)\n"
              << "      --firmware-entry   Entry offset from firmware load address (default 0)\n"
              << "      --no-ac97          Disable AC97 audio device\n"
              << "      --no-usb3          Disable USB3 xHCI controller\n"
              << "      --no-block         Disable block storage device\n"
              << "      --no-uart          Disable NS16550 UART device\n"
              << "      --no-dma           Disable platform DMA engine\n"
              << "      --no-virtio        Disable VirtIO-MMIO PCI shims\n";
}
