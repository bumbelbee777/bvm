#pragma once

#include <cstdint>

namespace DaxMailbox {

constexpr unsigned MaxCores = 8;
constexpr unsigned MaxSlotsPerCore = 64;

void Reset();
void Send(unsigned DstCore, unsigned DstSlot, uint64_t Data, uint8_t Tag);
bool TryReceive(unsigned DstCore, unsigned DstSlot, uint64_t& Data, uint8_t& Tag);
void Clear(unsigned DstCore, unsigned DstSlot);

} // namespace DaxMailbox
