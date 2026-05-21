#pragma once

#include "VmConfig.h"
#include "MmioMap.h"

#include <cstddef>
#include <cstdint>
#include <vector>

void InitMachine(const VmConfig& Config);
const VmConfig& GetMachineConfig();

size_t GetRamSize();
uint64_t GetMmioWindowBase();
inline bool CoversMmioPhysicalAddress(uint64_t Addr) noexcept {
    return Addr >= GetMmioWindowBase() &&
           Addr < GetMmioWindowBase() + MmioWindow::WindowByteExtent;
}
uint64_t GetFramebufferBase();
uint32_t GetFramebufferWidth();
uint32_t GetFramebufferHeight();
size_t GetFramebufferByteSize();
uint64_t GetDefaultKMask();

uint32_t GetActiveNumaNodeCount();
size_t GetSkbByteSize();
size_t GetRemoteKBytes();
size_t GetRemoteSkbBytes();
size_t GetRemoteDramBytes();
bool IsNumaNodeActive(uint8_t Node);

void ConfigureSkbStorage(size_t Bytes);
void ConfigureNumaStorage(uint32_t Nodes, size_t RemoteK, size_t RemoteSkb, size_t RemoteDram);

extern std::vector<uint8_t> Ram;
