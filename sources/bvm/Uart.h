#pragma once

#include <cstdint>

namespace Uart {

void Reset();
void SetEnabled(bool Enabled);
bool IsEnabled();

uint64_t ReadMmioQuad(uint64_t Offset);
void WriteMmioQuad(uint64_t Offset, uint64_t Value);

void Poll();

} // namespace Uart
