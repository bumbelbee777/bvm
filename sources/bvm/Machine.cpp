#include "Machine.h"

#include "PhysMap.h"
#include "SkbProfile.h"

namespace {

VmConfig ActiveConfig = VmConfigDefaults();
size_t ActiveRamSize = RamSizeBytesConstant;
uint64_t ActiveMmioBase = RamSizeBytesConstant;
uint64_t ActiveFramebufferBase = 0x00F00000ULL;

} // namespace

std::vector<uint8_t> Ram(RamSizeBytesConstant, 0);

void InitMachine(const VmConfig& Config) {
    ActiveConfig = Config;
    ActiveRamSize = Config.RamBytes;
    ActiveMmioBase = static_cast<uint64_t>(Config.RamBytes);

    const size_t FbBytes =
        static_cast<size_t>(Config.FramebufferWidth) * Config.FramebufferHeight * 4U;
    if (Config.RamBytes >= FbBytes + 0x1000) {
        ActiveFramebufferBase =
            static_cast<uint64_t>(Config.RamBytes - FbBytes) & ~0xFULL;
    } else {
        ActiveFramebufferBase = 0;
    }

    Ram.assign(Config.RamBytes, 0);
    ConfigureSkbStorage(Config.SkbBytes);
    ConfigureNumaStorage(Config.NumaNodes, Config.RemoteKBytes, Config.RemoteSkbBytes,
                         Config.RemoteDramBytes);
}

const VmConfig& GetMachineConfig() { return ActiveConfig; }

size_t GetRamSize() { return ActiveRamSize; }

uint64_t GetMmioWindowBase() { return ActiveMmioBase; }

uint64_t GetFramebufferBase() { return ActiveFramebufferBase; }

uint32_t GetFramebufferWidth() { return ActiveConfig.FramebufferWidth; }

uint32_t GetFramebufferHeight() { return ActiveConfig.FramebufferHeight; }

size_t GetFramebufferByteSize() {
    return static_cast<size_t>(ActiveConfig.FramebufferWidth) *
           ActiveConfig.FramebufferHeight * 4U;
}

uint64_t GetDefaultKMask() {
    const size_t Slots = ActiveConfig.KBankBytes / PhysMap::LocalKBytesPerSlot;
    if (Slots == 0) {
        return 0;
    }
    return static_cast<uint64_t>(Slots - 1U);
}

uint32_t GetActiveNumaNodeCount() { return ActiveConfig.NumaNodes; }

size_t GetSkbByteSize() { return ActiveConfig.SkbBytes; }

size_t GetRemoteKBytes() { return ActiveConfig.RemoteKBytes; }

size_t GetRemoteSkbBytes() { return ActiveConfig.RemoteSkbBytes; }

size_t GetRemoteDramBytes() { return ActiveConfig.RemoteDramBytes; }

bool IsNumaNodeActive(uint8_t Node) {
    return Node < ActiveConfig.NumaNodes;
}
