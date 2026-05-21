#pragma once

#include <cstdint>

namespace VirtioMmio {

void ResetAll();
void SetEnabled(bool Enabled);
bool IsEnabled();

uint64_t ReadMmioQuad(uint64_t Offset);
void WriteMmioQuad(uint64_t Offset, uint64_t Value);

} // namespace VirtioMmio
