#include "PlatformRtc.h"

#include "DeviceSpecs.h"
#include "Machine.h"
#include "MmioMap.h"
#include "PlatformIrq.h"
#include "Shared.h"

#include <chrono>

namespace {

bool Enabled = true;
uint64_t EpochSeconds = 1'700'000'000ULL;
uint32_t Nanoseconds = 0;
uint64_t AlarmSeconds = 0;
uint32_t AlarmNanoseconds = 0;
int32_t TimezoneOffsetMinutes = 0;
uint8_t Control = 0;
uint8_t Status = DeviceSpecs::Rtc::StatusValid;
uint64_t LastWholeSecond = 0;
uint64_t LastServiceCycles = 0;

uint8_t DayOfWeekFromEpoch(uint64_t Seconds) {
    // 1970-01-01 was Thursday (4); keep in 0=Sunday .. 6=Saturday.
    return static_cast<uint8_t>((Seconds / 86400ULL + 4ULL) % 7ULL);
}

#ifdef HONEYCOMB_HOST_IO
void SyncFromHostClock() {
    const auto Now = std::chrono::system_clock::now();
    const auto Sec = std::chrono::duration_cast<std::chrono::seconds>(Now.time_since_epoch());
    const auto Ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Now.time_since_epoch()) - Sec;
    EpochSeconds = static_cast<uint64_t>(Sec.count());
    Nanoseconds = static_cast<uint32_t>(Ns.count());
    Status |= DeviceSpecs::Rtc::StatusValid;
}
#endif

#ifndef HONEYCOMB_HOST_IO
void AdvanceSimulatedClock(uint64_t DeltaTicks, uint64_t FrequencyHz) {
    if (DeltaTicks == 0 || FrequencyHz == 0) {
        return;
    }
    const uint64_t NsPerTick = 1'000'000'000ULL / FrequencyHz;
    const uint64_t AddedNs = DeltaTicks * NsPerTick;
    const uint64_t TotalNs = static_cast<uint64_t>(Nanoseconds) + AddedNs;
    EpochSeconds += TotalNs / 1'000'000'000ULL;
    Nanoseconds = static_cast<uint32_t>(TotalNs % 1'000'000'000ULL);
    Status |= DeviceSpecs::Rtc::StatusValid;
}
#endif

void CheckAlarm() {
    if ((Control & DeviceSpecs::Rtc::ControlAlarmEnable) == 0 || AlarmSeconds == 0) {
        return;
    }
    const uint64_t NowNs =
        EpochSeconds * 1'000'000'000ULL + static_cast<uint64_t>(Nanoseconds);
    const uint64_t AlarmNs =
        AlarmSeconds * 1'000'000'000ULL + static_cast<uint64_t>(AlarmNanoseconds);
    if (NowNs >= AlarmNs) {
        Status |= DeviceSpecs::Rtc::StatusAlarmPending;
        PlatformIrq::SignalLine(DeviceSpecs::IrqLine::Rtc);
        Control = static_cast<uint8_t>(Control & ~DeviceSpecs::Rtc::ControlAlarmEnable);
    }
}

} // namespace

namespace PlatformRtc {

void Reset() {
#ifdef HONEYCOMB_HOST_IO
    SyncFromHostClock();
#else
    EpochSeconds = 1'700'000'000ULL;
    Nanoseconds = 0;
#endif
    AlarmSeconds = 0;
    AlarmNanoseconds = 0;
    TimezoneOffsetMinutes = 0;
    Control = 0;
    Status = DeviceSpecs::Rtc::StatusValid;
    LastWholeSecond = 0;
    LastServiceCycles = 0;
}

void SetEnabled(bool IsEnabled) { Enabled = IsEnabled; }

bool IsEnabled() { return Enabled; }

uint64_t ReadMmioQuad(uint64_t Offset) {
    if (!Enabled) {
        return 0;
    }

    using Quad = MmioWindow::RegisterQuadWord;
    switch (static_cast<Quad>(Offset)) {
        case Quad::RtcSeconds:
            return EpochSeconds;
        case Quad::RtcNanoseconds:
            return Nanoseconds;
        case Quad::RtcSetSeconds:
            return EpochSeconds;
        case Quad::RtcAlarmSeconds:
            return AlarmSeconds;
        case Quad::RtcControl:
            return Control;
        case Quad::RtcDayOfWeek:
            return DayOfWeekFromEpoch(EpochSeconds);
        case Quad::RtcTimezoneOffsetMinutes:
            return static_cast<uint64_t>(static_cast<int64_t>(TimezoneOffsetMinutes));
        case Quad::RtcAlarmNanoseconds:
            return AlarmNanoseconds;
        case Quad::RtcStatus:
            return Status;
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
        case Quad::RtcSetSeconds:
            EpochSeconds = Value;
            Nanoseconds = 0;
            LastWholeSecond = 0;
            Status |= DeviceSpecs::Rtc::StatusValid;
            break;
        case Quad::RtcAlarmSeconds:
            AlarmSeconds = Value;
            Status = static_cast<uint8_t>(Status & ~DeviceSpecs::Rtc::StatusAlarmPending);
            break;
        case Quad::RtcAlarmNanoseconds:
            AlarmNanoseconds = static_cast<uint32_t>(Value);
            Status = static_cast<uint8_t>(Status & ~DeviceSpecs::Rtc::StatusAlarmPending);
            break;
        case Quad::RtcTimezoneOffsetMinutes:
            TimezoneOffsetMinutes = static_cast<int32_t>(static_cast<int64_t>(Value));
            break;
        case Quad::RtcControl:
            Control = static_cast<uint8_t>(Value & 0xFF);
            if ((Control & DeviceSpecs::Rtc::ControlSetTime) != 0) {
#ifdef HONEYCOMB_HOST_IO
                SyncFromHostClock();
#endif
                Control = static_cast<uint8_t>(Control & ~DeviceSpecs::Rtc::ControlSetTime);
            }
            if ((Control & DeviceSpecs::Rtc::ControlSyncHost) != 0) {
#ifdef HONEYCOMB_HOST_IO
                SyncFromHostClock();
#endif
                Control = static_cast<uint8_t>(Control & ~DeviceSpecs::Rtc::ControlSyncHost);
            }
            break;
        case Quad::RtcStatus:
            Status = static_cast<uint8_t>(Value & 0xFF);
            break;
        default:
            break;
    }
}

void Service(Cpu& Vm) {
    if (!Enabled) {
        return;
    }

#ifdef HONEYCOMB_HOST_IO
    (void)Vm;
    SyncFromHostClock();
#else
    const uint64_t CyclesPerTick = GetMachineConfig().TimerCyclesPerTick;
    const uint64_t FrequencyHz = GetMachineConfig().TimerFrequencyHz;
    if (CyclesPerTick == 0) {
        return;
    }
    const uint64_t NowCycles = Vm.GetPerfCycles();
    if (LastServiceCycles == 0) {
        LastServiceCycles = NowCycles;
        return;
    }
    const uint64_t Delta = NowCycles - LastServiceCycles;
    LastServiceCycles = NowCycles;
    AdvanceSimulatedClock(Delta / CyclesPerTick, FrequencyHz);
#endif

    CheckAlarm();
}

} // namespace PlatformRtc
