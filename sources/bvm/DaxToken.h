#pragma once

#include "DaxProfile.h"

#include <cstdint>

class Cpu;

/** Per-slot DAX token state (data lives in `Cpu::KBank[slot]`). */
struct DaxTokenMeta {
    bool Valid = false;
    uint8_t Tag = 0;
    uint8_t RefCnt = 0;
};

namespace DaxToken {

uint64_t ResolveSlotIndex(const Cpu& Vm, uint8_t Slot6);
DaxTokenMeta& Meta(Cpu& Vm, uint64_t Index);
const DaxTokenMeta& Meta(const Cpu& Vm, uint64_t Index);
uint64_t Data(const Cpu& Vm, uint64_t Index);
void SetData(Cpu& Vm, uint64_t Index, uint64_t Value);

void Produce(Cpu& Vm, uint64_t Index, uint64_t Data, uint8_t Tag = 0, uint8_t RefCnt = 1);
void Invalidate(Cpu& Vm, uint64_t Index);
bool IsValid(const Cpu& Vm, uint64_t Index);
void ConsumeSingle(Cpu& Vm, uint64_t Index);
void BumpRef(Cpu& Vm, uint64_t Index, uint8_t Delta);

} // namespace DaxToken
