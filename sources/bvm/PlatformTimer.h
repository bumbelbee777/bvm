#pragma once

#include <cstdint>

class Cpu;

namespace PlatformTimer {

void Reset();
void SetEnabled(bool Enabled);
bool IsEnabled();

uint64_t ReadMmioQuad(uint64_t Offset);
void WriteMmioQuad(uint64_t Offset, uint64_t Value);

void Service(Cpu& Vm);
void DeliverInterrupts(Cpu& Vm);

} // namespace PlatformTimer
