#include "DaxMailbox.h"

#include <array>
#include <stdexcept>

namespace DaxMailbox {

namespace {

struct Slot {
    uint64_t Data = 0;
    uint8_t Tag = 0;
    bool Valid = false;
};

std::array<std::array<Slot, MaxSlotsPerCore>, MaxCores> Boxes = {};

} // namespace

void Reset() {
    for (auto& Core : Boxes) {
        for (auto& Slot : Core) {
            Slot = {};
        }
    }
}

void Send(unsigned DstCore, unsigned DstSlot, uint64_t Data, uint8_t Tag) {
    if (DstCore >= MaxCores || DstSlot >= MaxSlotsPerCore) {
        throw std::out_of_range("DAX mailbox core/slot out of range");
    }
    Slot& Box = Boxes[DstCore][DstSlot];
    Box.Data = Data;
    Box.Tag = Tag;
    Box.Valid = true;
}

bool TryReceive(unsigned DstCore, unsigned DstSlot, uint64_t& Data, uint8_t& Tag) {
    if (DstCore >= MaxCores || DstSlot >= MaxSlotsPerCore) {
        throw std::out_of_range("DAX mailbox core/slot out of range");
    }
    const Slot& Box = Boxes[DstCore][DstSlot];
    if (!Box.Valid) {
        return false;
    }
    Data = Box.Data;
    Tag = Box.Tag;
    return true;
}

void Clear(unsigned DstCore, unsigned DstSlot) {
    if (DstCore >= MaxCores || DstSlot >= MaxSlotsPerCore) {
        throw std::out_of_range("DAX mailbox core/slot out of range");
    }
    Boxes[DstCore][DstSlot] = {};
}

} // namespace DaxMailbox
