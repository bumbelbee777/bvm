#pragma once

#include <cstdint>

class Cpu;

namespace PlatformIrq {

void Reset();
void SetEnabled(bool Enabled);
bool IsEnabled();

uint64_t ReadMmioQuad(uint64_t Offset);
void WriteMmioQuad(uint64_t Offset, uint64_t Value);

void SignalLine(uint8_t Line);
void Deliver(Cpu& Vm);

} // namespace PlatformIrq
