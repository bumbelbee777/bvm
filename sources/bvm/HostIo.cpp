#include "HostIo.h"
#include "DeviceSpecs.h"
#include "Machine.h"

#include <cstdio>
#include <cstring>
#include <deque>

#ifdef HONEYCOMB_HOST_IO
#include <SDL3/SDL.h>
#endif

bool HostIo::Active = false;
bool HostIo::SdlReady = false;
bool HostIo::QuitRequested = false;
bool HostIo::VsyncReady = false;

namespace {

#ifdef HONEYCOMB_HOST_IO
struct SdlState {
    SDL_Window* Window = nullptr;
    SDL_Renderer* Renderer = nullptr;
    SDL_Texture* Texture = nullptr;
    uint32_t TextureWidth = 0;
    uint32_t TextureHeight = 0;
    uint32_t TextureFormat = DeviceSpecs::Gop::PixelRedGreenBlueReserved8BitPerColor;
    uint8_t ModifierMask = 0;
    uint8_t MouseButtonState = 0;
    uint16_t MousePosX = 0;
    uint16_t MousePosY = 0;
    bool MouseMoved = false;
    bool MouseButtonsChanged = false;
    uint64_t LastVsyncTicks = 0;
    struct PendingKey {
        uint8_t Code = 0;
        bool Up = false;
    };
    std::deque<PendingKey> KeyQueue;
} Sdl;

constexpr uint64_t VsyncIntervalMs = 16;

uint8_t ModifierMaskFromSdl(SDL_Keymod Mod) {
    uint8_t Mask = 0;
    if ((Mod & SDL_KMOD_SHIFT) != 0) {
        Mask |= DeviceSpecs::Xhci::ModifierShift;
    }
    if ((Mod & SDL_KMOD_CTRL) != 0) {
        Mask |= DeviceSpecs::Xhci::ModifierCtrl;
    }
    if ((Mod & SDL_KMOD_ALT) != 0) {
        Mask |= DeviceSpecs::Xhci::ModifierAlt;
    }
    if ((Mod & SDL_KMOD_GUI) != 0) {
        Mask |= DeviceSpecs::Xhci::ModifierSuper;
    }
    return Mask;
}

void EnsureTexture(uint32_t Width, uint32_t Height, uint32_t PixelFormat) {
    if (Sdl.Texture != nullptr && Sdl.TextureWidth == Width && Sdl.TextureHeight == Height &&
        Sdl.TextureFormat == PixelFormat) {
        return;
    }
    if (Sdl.Texture != nullptr) {
        SDL_DestroyTexture(Sdl.Texture);
        Sdl.Texture = nullptr;
    }
    const SDL_PixelFormat SdlFormat =
        PixelFormat == DeviceSpecs::Gop::PixelBlueGreenRedReserved8BitPerColor
            ? SDL_PIXELFORMAT_BGRA8888
            : SDL_PIXELFORMAT_ARGB8888;
    Sdl.Texture = SDL_CreateTexture(
        Sdl.Renderer,
        SdlFormat,
        SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(Width),
        static_cast<int>(Height));
    Sdl.TextureWidth = Width;
    Sdl.TextureHeight = Height;
    Sdl.TextureFormat = PixelFormat;
}
#endif

} // namespace

void HostIo::Init() {
    Active = true;
    QuitRequested = false;
    VsyncReady = true;

#ifdef HONEYCOMB_HOST_IO
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "HostIo: SDL_Init failed: %s\n", SDL_GetError());
        SdlReady = false;
        return;
    }

    Sdl.Window = SDL_CreateWindow(
        "Honeycomb BVM",
        GetFramebufferWidth(),
        GetFramebufferHeight(),
        SDL_WINDOW_RESIZABLE);
    if (!Sdl.Window || !(Sdl.Renderer = SDL_CreateRenderer(Sdl.Window, nullptr))) {
        std::fprintf(stderr, "HostIo: display init failed: %s\n", SDL_GetError());
        if (Sdl.Renderer) {
            SDL_DestroyRenderer(Sdl.Renderer);
        }
        if (Sdl.Window) {
            SDL_DestroyWindow(Sdl.Window);
        }
        SDL_Quit();
        SdlReady = false;
        return;
    }

    EnsureTexture(GetFramebufferWidth(), GetFramebufferHeight(),
                  DeviceSpecs::Gop::PixelRedGreenBlueReserved8BitPerColor);
    Sdl.LastVsyncTicks = SDL_GetTicks();
    SdlReady = true;
#else
    SdlReady = false;
#endif
}

void HostIo::Shutdown() {
    if (!Active) {
        return;
    }

#ifdef HONEYCOMB_HOST_IO
    if (SdlReady) {
        if (Sdl.Texture) {
            SDL_DestroyTexture(Sdl.Texture);
        }
        if (Sdl.Renderer) {
            SDL_DestroyRenderer(Sdl.Renderer);
        }
        if (Sdl.Window) {
            SDL_DestroyWindow(Sdl.Window);
        }
        SDL_Quit();
    }
    Sdl = SdlState{};
#endif

    Active = false;
    SdlReady = false;
    QuitRequested = false;
    VsyncReady = false;
}

void HostIo::Poll() {
    if (!Active || !SdlReady) {
        return;
    }

#ifdef HONEYCOMB_HOST_IO
    SDL_Event Event{};
    while (SDL_PollEvent(&Event)) {
        switch (Event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                QuitRequested = true;
                break;
            case SDL_EVENT_KEY_DOWN:
                Sdl.ModifierMask = ModifierMaskFromSdl(Event.key.mod);
                if (Event.key.key >= 0 && Event.key.key < 256 && !Event.key.repeat) {
                    Sdl.KeyQueue.push_back(
                        {static_cast<uint8_t>(Event.key.key), false});
                }
                break;
            case SDL_EVENT_KEY_UP:
                Sdl.ModifierMask = ModifierMaskFromSdl(Event.key.mod);
                if (Event.key.key >= 0 && Event.key.key < 256) {
                    Sdl.KeyQueue.push_back({static_cast<uint8_t>(Event.key.key), true});
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                Sdl.MousePosX = static_cast<uint16_t>(Event.motion.x);
                Sdl.MousePosY = static_cast<uint16_t>(Event.motion.y);
                Sdl.MouseMoved = true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (Event.button.button >= 1 && Event.button.button <= 8) {
                    Sdl.MouseButtonState = static_cast<uint8_t>(
                        Sdl.MouseButtonState | (1U << (Event.button.button - 1)));
                    Sdl.MouseButtonsChanged = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (Event.button.button >= 1 && Event.button.button <= 8) {
                    Sdl.MouseButtonState = static_cast<uint8_t>(
                        Sdl.MouseButtonState & ~(1U << (Event.button.button - 1)));
                    Sdl.MouseButtonsChanged = true;
                }
                break;
            default:
                break;
        }
    }

    if (SDL_GetTicks() - Sdl.LastVsyncTicks >= VsyncIntervalMs) {
        VsyncReady = true;
    }
#endif
}

bool HostIo::VsyncDue() {
    if (!Active) {
        return false;
    }
#ifdef HONEYCOMB_HOST_IO
    return SdlReady && VsyncReady;
#else
    return false;
#endif
}

void HostIo::PresentSurface(const uint8_t* Ram, uint64_t Base, uint32_t Width, uint32_t Height,
                            uint32_t Stride, uint32_t Format) {
    if (!Active || Ram == nullptr) {
        return;
    }

#ifdef HONEYCOMB_HOST_IO
    if (!SdlReady || !Sdl.Renderer || Width == 0 || Height == 0) {
        VsyncReady = false;
        return;
    }
    if (Format != DeviceSpecs::Gop::PixelRedGreenBlueReserved8BitPerColor &&
        Format != DeviceSpecs::Gop::PixelBlueGreenRedReserved8BitPerColor) {
        VsyncReady = false;
        return;
    }
    if (Base == 0 || Base + static_cast<uint64_t>(Stride) * Height > GetRamSize()) {
        VsyncReady = false;
        return;
    }

    EnsureTexture(Width, Height, Format);
    if (!Sdl.Texture) {
        VsyncReady = false;
        return;
    }

    void* Pixels = nullptr;
    int Pitch = 0;
    if (!SDL_LockTexture(Sdl.Texture, nullptr, &Pixels, &Pitch)) {
        VsyncReady = false;
        return;
    }

    const uint8_t* Source = Ram + Base;
    const int RowBytes = static_cast<int>(Stride);
    if (Pitch == RowBytes) {
        std::memcpy(Pixels, Source, static_cast<size_t>(RowBytes) * Height);
    } else {
        auto* Dest = static_cast<uint8_t*>(Pixels);
        for (uint32_t Row = 0; Row < Height; ++Row) {
            std::memcpy(Dest + static_cast<size_t>(Row) * static_cast<size_t>(Pitch),
                        Source + static_cast<size_t>(Row) * static_cast<size_t>(RowBytes),
                        static_cast<size_t>(RowBytes));
        }
    }

    SDL_UnlockTexture(Sdl.Texture);
    SDL_RenderClear(Sdl.Renderer);
    SDL_RenderTexture(Sdl.Renderer, Sdl.Texture, nullptr, nullptr);
    SDL_RenderPresent(Sdl.Renderer);
    Sdl.LastVsyncTicks = SDL_GetTicks();
#else
    (void)Base;
    (void)Width;
    (void)Height;
    (void)Stride;
    (void)Format;
#endif

    VsyncReady = false;
}

void HostIo::ResizeWindow(uint32_t Width, uint32_t Height) {
    if (!Active || Width == 0 || Height == 0) {
        return;
    }
#ifdef HONEYCOMB_HOST_IO
    if (SdlReady && Sdl.Window) {
        SDL_SetWindowSize(Sdl.Window, static_cast<int>(Width), static_cast<int>(Height));
    }
#else
    (void)Width;
    (void)Height;
#endif
}

bool HostIo::ConsumeKeyboard(uint8_t& Key, bool& KeyUp) {
    if (!Active) {
        return false;
    }
#ifdef HONEYCOMB_HOST_IO
    if (SdlReady && !Sdl.KeyQueue.empty()) {
        const auto Pending = Sdl.KeyQueue.front();
        Sdl.KeyQueue.pop_front();
        Key = Pending.Code;
        KeyUp = Pending.Up;
        return true;
    }
#else
    (void)Key;
    (void)KeyUp;
#endif
    return false;
}

uint8_t HostIo::GetModifiers() {
    if (!Active) {
        return 0;
    }
#ifdef HONEYCOMB_HOST_IO
    return SdlReady ? Sdl.ModifierMask : 0;
#else
    return 0;
#endif
}

bool HostIo::ConsumeMouseUpdate(uint8_t& Buttons, uint16_t& X, uint16_t& Y) {
    if (!Active) {
        return false;
    }
#ifdef HONEYCOMB_HOST_IO
    if (!SdlReady) {
        return false;
    }
    const bool Changed = Sdl.MouseMoved || Sdl.MouseButtonsChanged;
    if (!Changed) {
        return false;
    }
    Buttons = Sdl.MouseButtonState;
    X = Sdl.MousePosX;
    Y = Sdl.MousePosY;
    Sdl.MouseMoved = false;
    Sdl.MouseButtonsChanged = false;
    return true;
#else
    (void)Buttons;
    (void)X;
    (void)Y;
    return false;
#endif
}
