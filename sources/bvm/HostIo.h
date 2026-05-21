#pragma once

#include <cstdint>

class HostIo {
public:
    static void Init();
    static void Shutdown();
    static void Poll();

    static void PresentSurface(const uint8_t* Ram, uint64_t Base, uint32_t Width, uint32_t Height,
                               uint32_t Stride, uint32_t Format);
    static void ResizeWindow(uint32_t Width, uint32_t Height);

    static bool ConsumeKeyboard(uint8_t& Key, bool& KeyUp);
    static uint8_t GetModifiers();
    static bool ConsumeMouseUpdate(uint8_t& Buttons, uint16_t& X, uint16_t& Y);

    static bool IsActive() { return Active; }
    static bool ShouldQuit() { return QuitRequested; }
    static bool VsyncDue();

private:
    static bool Active;
    static bool SdlReady;
    static bool QuitRequested;
    static bool VsyncReady;
};
