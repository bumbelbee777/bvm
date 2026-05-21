#include "Uart.h"

#include "DeviceSpecs.h"
#include "HostIo.h"
#include "MmioMap.h"
#include "PlatformIrq.h"



#include <cstdio>

#include <deque>



#ifdef _WIN32

#include <conio.h>

#else

#include <sys/select.h>

#include <unistd.h>

#endif



namespace {



bool Enabled = true;

bool EchoEnabled = true;
bool RxIrqEnabled = true;

std::deque<uint8_t> RxQueue;

uint8_t LineStatus = DeviceSpecs::Uart16550::LineStatusTransmitEmpty;



uint8_t NormalizeInputByte(uint8_t Byte) {

    if (Byte == '\r') {

        return '\n';

    }

    return Byte;

}



void PushRx(uint8_t Byte) {

    Byte = NormalizeInputByte(Byte);

    if (RxQueue.size() >= 256) {

        RxQueue.pop_front();

    }

    RxQueue.push_back(Byte);
    LineStatus |= DeviceSpecs::Uart16550::LineStatusDataReady;
    if (RxIrqEnabled) {
        PlatformIrq::SignalLine(DeviceSpecs::IrqLine::Uart);
    }
}



void DrainHostInput() {

    if (HostIo::IsActive()) {

        uint8_t Key = 0;

        bool KeyUp = false;

        while (HostIo::ConsumeKeyboard(Key, KeyUp)) {

            if (!KeyUp && Key >= 32 && Key <= 126) {

                PushRx(Key);

            }

            if (!KeyUp && Key == 13) {

                PushRx('\n');

            }

        }

        return;

    }



#ifdef _WIN32

    while (_kbhit() != 0) {

        const int Raw = _getch();

        if (Raw == 0 || Raw == 0xE0) {

            (void)_getch();

            continue;

        }

        PushRx(static_cast<uint8_t>(Raw));

    }

#else

    fd_set Set;

    FD_ZERO(&Set);

    FD_SET(STDIN_FILENO, &Set);

    timeval Timeout{};

    Timeout.tv_sec = 0;

    Timeout.tv_usec = 0;

    while (select(STDIN_FILENO + 1, &Set, nullptr, nullptr, &Timeout) > 0) {

        uint8_t Byte = 0;

        if (read(STDIN_FILENO, &Byte, 1) != 1) {

            break;

        }

        PushRx(Byte);

        FD_ZERO(&Set);

        FD_SET(STDIN_FILENO, &Set);

    }

#endif

}



} // namespace



namespace Uart {



void Reset() {

    RxQueue.clear();

    EchoEnabled = true;

    LineStatus = DeviceSpecs::Uart16550::LineStatusTransmitEmpty;

}



void SetEnabled(bool IsEnabled) { Enabled = IsEnabled; }



bool IsEnabled() { return Enabled; }



uint64_t ReadMmioQuad(uint64_t Offset) {

    if (!Enabled) {

        return 0;

    }



    using Quad = MmioWindow::RegisterQuadWord;

    switch (static_cast<Quad>(Offset)) {

        case Quad::UartData:

            if (!RxQueue.empty()) {

                const uint8_t Byte = RxQueue.front();

                RxQueue.pop_front();

                if (RxQueue.empty()) {

                    LineStatus = static_cast<uint8_t>(

                        LineStatus & ~DeviceSpecs::Uart16550::LineStatusDataReady);

                }

                return Byte;

            }

            return 0;

        case Quad::UartLineStatus:

            return LineStatus;

        case Quad::UartControl:

            return EchoEnabled ? 1U : 0U;

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

        case Quad::UartData: {

            const char Byte = static_cast<char>(Value & 0xFF);

            std::fputc(Byte, stdout);

            if (EchoEnabled) {

                std::fflush(stdout);

            }

            break;

        }

        case Quad::UartControl:
            EchoEnabled = (Value & 0x1U) != 0;
            RxIrqEnabled = (Value & DeviceSpecs::Uart16550::ControlRxIrq) != 0;
            break;

        default:

            break;

    }

}



void Poll() {

    if (!Enabled) {

        return;

    }

    DrainHostInput();

}



} // namespace Uart

