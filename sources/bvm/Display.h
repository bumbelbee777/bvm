#pragma once

#include <cstdint>

namespace Display {

void Reset();
void SetEnabled(bool Enabled);
bool IsEnabled();

uint64_t ReadMmioQuad(uint64_t Offset);
void WriteMmioQuad(uint64_t Offset, uint64_t Value);

bool FrameDue();
void Present(const uint8_t* RamImage);

} // namespace Display
