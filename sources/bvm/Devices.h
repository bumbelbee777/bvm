#pragma once

#include <cstdint>
#include <string>

class Cpu;

namespace Devices {

void ResetAll();
void SetAc97Enabled(bool Enabled);
void SetUsb3Enabled(bool Enabled);
void SetBlockEnabled(bool Enabled);
void SetUartEnabled(bool Enabled);

bool LoadDiskImage(const std::string& Path);
bool SaveDiskImage(const std::string& Path);

uint64_t GetPresentMask();

bool IsDeviceMmioQuad(uint64_t Offset);
uint64_t ReadMmioQuad(uint64_t Offset);
void WriteMmioQuad(uint64_t Offset, uint64_t Value);

void Poll(Cpu& Vm);
void Service(Cpu& Vm);

} // namespace Devices
