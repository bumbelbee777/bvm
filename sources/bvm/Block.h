#pragma once

#include <cstdint>
#include <string>

namespace Block {

void Reset();
void SetEnabled(bool Enabled);
bool IsEnabled();
void ConfigureStorage(size_t Bytes);
bool LoadImage(const std::string& Path);
bool SaveImage(const std::string& Path);

uint64_t ReadMmioQuad(uint64_t Offset);
void WriteMmioQuad(uint64_t Offset, uint64_t Value);

} // namespace Block
