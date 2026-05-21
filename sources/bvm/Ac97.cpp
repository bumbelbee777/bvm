#include "Ac97.h"

#include "DeviceSpecs.h"

#include "GuestMem.h"

#include "Machine.h"

#include "MmioMap.h"
#include "PlatformIrq.h"



#include <algorithm>

#include <cmath>

#include <cstring>



#ifdef HONEYCOMB_HOST_IO

#include <SDL3/SDL.h>

#endif



namespace {



bool Enabled = true;

uint8_t Control = 0;

uint64_t BufferPtr = 0;

uint64_t BufferLength = 0;

uint64_t BufferCursor = 0;

uint32_t Format = DeviceSpecs::Ac97::FormatPcm8Mono;

uint32_t SampleRateHz = DeviceSpecs::Ac97::DefaultSampleRateHz;

uint64_t RingRead = 0;

uint64_t RingWrite = 0;

uint8_t Status = DeviceSpecs::Ac97::StatusReady;

uint16_t MasterVolume = 0;

float MasterGain = 1.0f;

int ActiveSampleRate = DeviceSpecs::Ac97::DefaultSampleRateHz;

#ifdef HONEYCOMB_HOST_IO

SDL_AudioStream* PlaybackStream = nullptr;

#endif



#ifdef HONEYCOMB_HOST_IO

void EnsurePlaybackStream() {

    if (PlaybackStream != nullptr) {

        return;

    }

    if (!SDL_WasInit(SDL_INIT_AUDIO)) {

        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {

            return;

        }

    }

    SDL_AudioSpec Spec{};

    Spec.format = SDL_AUDIO_S16;

    Spec.channels = 2;

    Spec.freq = ActiveSampleRate;

    PlaybackStream =

        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &Spec, nullptr, nullptr);

}



void ShutdownPlaybackStream() {

    if (PlaybackStream != nullptr) {

        SDL_DestroyAudioStream(PlaybackStream);

        PlaybackStream = nullptr;

    }

}



void QueueFrame(int16_t Left, int16_t Right) {

    EnsurePlaybackStream();

    if (PlaybackStream == nullptr) {

        return;

    }

    const int16_t ScaledLeft =

        static_cast<int16_t>(static_cast<float>(Left) * MasterGain);

    const int16_t ScaledRight =

        static_cast<int16_t>(static_cast<float>(Right) * MasterGain);

    const int16_t Frame[2] = {ScaledLeft, ScaledRight};

    SDL_PutAudioStreamData(PlaybackStream, Frame, sizeof(Frame));

    SDL_ResumeAudioStreamDevice(PlaybackStream);

}

#else

void QueueFrame(int16_t, int16_t) {}

#endif



int16_t ExpandPcmByte(uint8_t Value) {

    const int16_t Signed = static_cast<int16_t>(static_cast<int8_t>(Value));

    return static_cast<int16_t>(Signed << 8);

}



size_t BytesPerFrame() {

    switch (Format) {

        case DeviceSpecs::Ac97::FormatPcm16Stereo:

            return 4;

        case DeviceSpecs::Ac97::FormatPcm16Mono:

            return 2;

        case DeviceSpecs::Ac97::FormatPcm8Mono:

        default:

            return 1;

    }

}



bool FeedOneFrame() {

    if (BufferLength == 0 || BufferPtr == 0) {

        Status |= DeviceSpecs::Ac97::StatusUnderrun;

        return false;

    }



    int16_t Left = 0;

    int16_t Right = 0;



    if (Format == DeviceSpecs::Ac97::FormatPcm16Stereo) {

        if (BufferCursor + 4 > BufferLength ||

            !GuestMem::RegionValid(BufferPtr + BufferCursor, 4)) {

            Status |= DeviceSpecs::Ac97::StatusUnderrun;

            return false;

        }

        int16_t Samples[2] = {};

        std::memcpy(Samples, Ram.data() + static_cast<size_t>(BufferPtr + BufferCursor),

                    sizeof(Samples));

        Left = Samples[0];

        Right = Samples[1];

        BufferCursor += 4;

    } else if (Format == DeviceSpecs::Ac97::FormatPcm16Mono) {

        if (BufferCursor + 2 > BufferLength ||

            !GuestMem::RegionValid(BufferPtr + BufferCursor, 2)) {

            Status |= DeviceSpecs::Ac97::StatusUnderrun;

            return false;

        }

        int16_t Sample = 0;

        std::memcpy(&Sample, Ram.data() + static_cast<size_t>(BufferPtr + BufferCursor),

                    sizeof(Sample));

        Left = Sample;

        Right = Sample;

        BufferCursor += 2;

    } else {

        uint8_t Sample = 0;

        if (!GuestMem::ReadU8(BufferPtr + BufferCursor, Sample)) {

            Status |= DeviceSpecs::Ac97::StatusUnderrun;

            return false;

        }

        Left = ExpandPcmByte(Sample);

        Right = Left;

        ++BufferCursor;

    }



    if (BufferCursor >= BufferLength) {
        BufferCursor = 0;
        RingWrite = BufferCursor;
        if ((Control & DeviceSpecs::Ac97::ControlPeriodIrq) != 0) {
            PlatformIrq::SignalLine(DeviceSpecs::IrqLine::Ac97);
        }
    }

    RingRead = BufferCursor;



    QueueFrame(Left, Right);

    Status = DeviceSpecs::Ac97::StatusReady;

    return true;

}



} // namespace



namespace Ac97 {



void Reset() {

    Control = DeviceSpecs::Ac97::ControlPeriodIrq;

    BufferPtr = 0;

    BufferLength = 0;

    BufferCursor = 0;

    Format = DeviceSpecs::Ac97::FormatPcm16Stereo;

    SampleRateHz = DeviceSpecs::Ac97::DefaultSampleRateHz;

    RingRead = 0;

    RingWrite = 0;

    Status = DeviceSpecs::Ac97::StatusReady;

    ActiveSampleRate = static_cast<int>(SampleRateHz);

    MasterGain = GetMachineConfig().HostAudioGain;

    MasterVolume = static_cast<uint16_t>((1.0f - MasterGain) * 0x3F);

#ifdef HONEYCOMB_HOST_IO

    ShutdownPlaybackStream();

#endif

}



void SetEnabled(bool IsEnabled) { Enabled = IsEnabled; }



bool IsEnabled() { return Enabled; }



uint64_t ReadMmioQuad(uint64_t Offset) {

    if (!Enabled) {

        return 0;

    }



    using Quad = MmioWindow::RegisterQuadWord;

    switch (static_cast<Quad>(Offset)) {

        case Quad::Ac97Control:

            return Control;

        case Quad::Ac97Status:

            return Status;

        case Quad::Ac97BufferPtr:

            return BufferPtr;

        case Quad::Ac97BufferLength:

            return BufferLength;

        case Quad::Ac97MasterVolume:

            return MasterVolume;

        case Quad::Ac97BufferCursor:

            return BufferCursor;

        case Quad::Ac97Format:

            return Format;

        case Quad::Ac97SampleRate:

            return SampleRateHz;

        case Quad::Ac97RingRead:

            return RingRead;

        case Quad::Ac97RingWrite:

            return RingWrite;

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

        case Quad::Ac97Control:

            if ((Value & DeviceSpecs::Ac97::ControlReset) != 0) {

                Reset();

                return;

            }

            Control = static_cast<uint8_t>(Value & (DeviceSpecs::Ac97::ControlReset |

                                                    DeviceSpecs::Ac97::ControlRun));

            if ((Control & DeviceSpecs::Ac97::ControlRun) == 0) {

                BufferCursor = 0;

            }

            break;

        case Quad::Ac97Status:

            Status = static_cast<uint8_t>(Value & 0xFF);

            break;

        case Quad::Ac97BufferPtr:

            BufferPtr = Value;

            BufferCursor = 0;

            break;

        case Quad::Ac97BufferLength:

            BufferLength = Value;

            BufferCursor = 0;

            break;

        case Quad::Ac97MasterVolume:

            MasterVolume = static_cast<uint16_t>(Value & 0xFFFF);

            MasterGain = 1.0f - static_cast<float>(MasterVolume & 0x3F) / 63.0f;

            MasterGain = std::clamp(MasterGain, 0.0f, 1.0f);

            break;

        case Quad::Ac97Format:

            if (Value <= DeviceSpecs::Ac97::FormatPcm16Stereo) {

                Format = static_cast<uint32_t>(Value);

                BufferCursor = 0;

                RingRead = 0;

                RingWrite = 0;

            }

            break;

        case Quad::Ac97SampleRate:

            if (Value >= 8000 && Value <= 192000) {

                SampleRateHz = static_cast<uint32_t>(Value);

                ActiveSampleRate = static_cast<int>(SampleRateHz);

#ifdef HONEYCOMB_HOST_IO

                ShutdownPlaybackStream();

#endif

            }

            break;

        case Quad::Ac97RingRead:

            RingRead = Value;

            if (RingRead <= BufferLength) {

                BufferCursor = RingRead;

            }

            break;

        case Quad::Ac97RingWrite:

            RingWrite = Value;

            break;

        default:

            break;

    }

}



void Poll() {

    if (!Enabled || (Control & DeviceSpecs::Ac97::ControlRun) == 0) {

        return;

    }



    const size_t FrameBytes = BytesPerFrame();

    if (FrameBytes == 0 || BufferLength < FrameBytes) {

        Status |= DeviceSpecs::Ac97::StatusUnderrun;

        return;

    }



    uint32_t FramesRemaining = DeviceSpecs::Ac97::SamplesPerService;

    while (FramesRemaining > 0) {

        if (!FeedOneFrame()) {

            break;

        }

        --FramesRemaining;

    }

}



} // namespace Ac97

