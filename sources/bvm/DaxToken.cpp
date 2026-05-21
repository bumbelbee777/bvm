#include "DaxToken.h"
#include "Shared.h"

#include <stdexcept>

uint64_t DaxToken::ResolveSlotIndex(const Cpu& Vm, uint8_t Slot6) {
    return (Vm.Wind + static_cast<uint64_t>(Slot6)) & Vm.KMask;
}

DaxTokenMeta& DaxToken::Meta(Cpu& Vm, uint64_t Index) {
    if (Index >= Vm.DaxMeta.size()) {
        throw std::out_of_range("DAX K-slot out of range");
    }
    return Vm.DaxMeta[Index];
}

const DaxTokenMeta& DaxToken::Meta(const Cpu& Vm, uint64_t Index) {
    if (Index >= Vm.DaxMeta.size()) {
        throw std::out_of_range("DAX K-slot out of range");
    }
    return Vm.DaxMeta[Index];
}

uint64_t DaxToken::Data(const Cpu& Vm, uint64_t Index) {
    return Vm.ReadKReg(Index);
}

void DaxToken::SetData(Cpu& Vm, uint64_t Index, uint64_t Value) {
    Vm.WriteKReg(Index, Value);
}

void DaxToken::Produce(Cpu& Vm, uint64_t Index, uint64_t Data, uint8_t Tag, uint8_t RefCnt) {
    SetData(Vm, Index, Data);
    DaxTokenMeta& Slot = Meta(Vm, Index);
    Slot.Valid = true;
    Slot.Tag = Tag;
    Slot.RefCnt = RefCnt;
}

void DaxToken::Invalidate(Cpu& Vm, uint64_t Index) {
    DaxTokenMeta& Slot = Meta(Vm, Index);
    Slot.Valid = false;
    Slot.RefCnt = 0;
    Slot.Tag = 0;
}

bool DaxToken::IsValid(const Cpu& Vm, uint64_t Index) {
    return Meta(Vm, Index).Valid;
}

void DaxToken::ConsumeSingle(Cpu& Vm, uint64_t Index) {
    DaxTokenMeta& Slot = Meta(Vm, Index);
    if (!Slot.Valid) {
        return;
    }
    if (Slot.RefCnt <= 1) {
        Invalidate(Vm, Index);
        return;
    }
    Slot.RefCnt -= 1;
}

void DaxToken::BumpRef(Cpu& Vm, uint64_t Index, uint8_t Delta) {
    DaxTokenMeta& Slot = Meta(Vm, Index);
    if (!Slot.Valid) {
        throw std::runtime_error("DAX ref bump on invalid token");
    }
    const unsigned Next = static_cast<unsigned>(Slot.RefCnt) + static_cast<unsigned>(Delta);
    if (Next > 255) {
        throw std::runtime_error("DAX refcnt overflow");
    }
    Slot.RefCnt = static_cast<uint8_t>(Next);
}
