#pragma once

#include <cstdint>

namespace Usb3 {

void Reset();
void SetEnabled(bool Enabled);
bool IsEnabled();

uint64_t ReadMmioQuad(uint64_t Offset);
void WriteMmioQuad(uint64_t Offset, uint64_t Value);

void Poll();

} // namespace Usb3
