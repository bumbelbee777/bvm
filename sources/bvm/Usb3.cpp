#include "Usb3.h"

#include "DeviceSpecs.h"

#include "GuestMem.h"

#include "HostIo.h"

#include "Machine.h"

#include "MmioMap.h"
#include "PlatformIrq.h"



namespace {



bool Enabled = true;



uint32_t Command = 0;

uint32_t Status = DeviceSpecs::Xhci::StatusHalted;

uint64_t EventRingPtr = 0;

uint32_t EventWriteIndex = 0;

uint32_t EventReadIndex = 0;

uint32_t Doorbell = 0;

uint32_t PendingCount = 0;



bool ControllerRunning() {

    return (Status & DeviceSpecs::Xhci::StatusHalted) == 0;

}



void UpdateEventPendingStatus() {

    if (PendingCount > 0) {

        Status |= DeviceSpecs::Xhci::StatusEventPending;

    } else {

        Status = static_cast<uint32_t>(Status & ~DeviceSpecs::Xhci::StatusEventPending);

    }

}



bool AppendGuestEvent(uint64_t Type, uint64_t Data0, uint64_t Data1) {

    ++PendingCount;

    UpdateEventPendingStatus();

    if (EventRingPtr == 0) {

        PlatformIrq::SignalLine(DeviceSpecs::IrqLine::Usb3);

        return true;

    }

    const uint64_t Slot = EventWriteIndex % DeviceSpecs::Xhci::EventRingCapacity;

    const uint64_t Base = EventRingPtr + Slot * DeviceSpecs::Xhci::EventEntryBytes;

    if (!GuestMem::WriteU64(Base + 0, Type)) {

        return false;

    }

    if (!GuestMem::WriteU64(Base + 8, Data0)) {

        return false;

    }

    if (!GuestMem::WriteU64(Base + 16, Data1)) {

        return false;

    }

    if (!GuestMem::WriteU64(Base + 24, 0)) {

        return false;

    }

    EventWriteIndex = (EventWriteIndex + 1) % DeviceSpecs::Xhci::EventRingCapacity;

    PlatformIrq::SignalLine(DeviceSpecs::IrqLine::Usb3);

    return true;

}



void IngestHostInput() {

    if (!HostIo::IsActive() || !ControllerRunning()) {

        return;

    }



    uint8_t Key = 0;

    bool KeyUp = false;

    while (HostIo::ConsumeKeyboard(Key, KeyUp)) {

        const uint64_t Type =

            KeyUp ? DeviceSpecs::Xhci::EventKeyUp : DeviceSpecs::Xhci::EventKeyDown;

        AppendGuestEvent(Type, Key, HostIo::GetModifiers());

    }



    uint8_t Buttons = 0;

    uint16_t X = 0;

    uint16_t Y = 0;

    while (HostIo::ConsumeMouseUpdate(Buttons, X, Y)) {

        AppendGuestEvent(DeviceSpecs::Xhci::EventMouseButton, Buttons, 0);

        AppendGuestEvent(DeviceSpecs::Xhci::EventMouseMotion, X, Y);

    }

}



} // namespace



namespace Usb3 {



void Reset() {

    Command = 0;

    Status = DeviceSpecs::Xhci::StatusHalted;

    EventRingPtr = 0;

    EventWriteIndex = 0;

    EventReadIndex = 0;

    Doorbell = 0;

    PendingCount = 0;

}



void SetEnabled(bool IsEnabled) { Enabled = IsEnabled; }



bool IsEnabled() { return Enabled; }



uint64_t ReadMmioQuad(uint64_t Offset) {

    if (!Enabled) {

        return 0;

    }



    using Quad = MmioWindow::RegisterQuadWord;

    switch (static_cast<Quad>(Offset)) {

        case Quad::Usb3Capabilities:

            return DeviceSpecs::Xhci::CapabilityMask;

        case Quad::Usb3Command:

            return Command;

        case Quad::Usb3Status:

            return Status;

        case Quad::Usb3EventRingPtr:

            return EventRingPtr;

        case Quad::Usb3EventReadIndex:

            return EventReadIndex;

        case Quad::Usb3EventWriteIndex:

            return EventWriteIndex;

        case Quad::Usb3EventPending:

            return PendingCount;

        case Quad::Usb3Doorbell:

            return Doorbell;

        default:

            return 0;

    }

}



void WriteMmioQuad(uint64_t Offset, uint64_t Value) {

    if (!Enabled) {

        return;

    }



    using Quad = MmioWindow::RegisterQuadWord;

    switch (static_cast<Quad>(Offset)) {

        case Quad::Usb3Command:

            Command = static_cast<uint32_t>(Value & 0xFFFF);

            if ((Command & DeviceSpecs::Xhci::CommandReset) != 0) {

                Reset();

                Command = 0;

                return;

            }

            if ((Command & DeviceSpecs::Xhci::CommandRun) != 0) {

                Status = static_cast<uint32_t>(Status & ~DeviceSpecs::Xhci::StatusHalted);

            } else {

                Status = static_cast<uint32_t>(Status | DeviceSpecs::Xhci::StatusHalted);

            }

            break;

        case Quad::Usb3Status:

            Status = static_cast<uint32_t>(Value & 0xFFFF);

            UpdateEventPendingStatus();

            break;

        case Quad::Usb3EventRingPtr:

            EventRingPtr = Value;

            EventWriteIndex = 0;

            EventReadIndex = 0;

            PendingCount = 0;

            UpdateEventPendingStatus();

            if (EventRingPtr != 0) {

                Status = static_cast<uint32_t>(Status & ~DeviceSpecs::Xhci::StatusHalted);

                Command |= DeviceSpecs::Xhci::CommandRun;

            }

            break;

        case Quad::Usb3EventReadIndex:

            EventReadIndex = static_cast<uint32_t>(Value % DeviceSpecs::Xhci::EventRingCapacity);

            if (PendingCount > 0) {

                --PendingCount;

            }

            UpdateEventPendingStatus();

            break;

        case Quad::Usb3Doorbell:

            Doorbell = static_cast<uint32_t>(Value & 0xFFFF);

            IngestHostInput();

            break;

        default:

            break;

    }

}



void Poll() {

    if (!Enabled) {

        return;

    }

    IngestHostInput();

}



} // namespace Usb3

