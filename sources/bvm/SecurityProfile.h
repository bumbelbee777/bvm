#pragma once

#include <cstdint>

/** Honeycomb security architecture (v1.0) baseline BVM profile. */
struct SecurityProfile {
    static constexpr uint8_t SecurityFaultVector = 0x3E;
    static constexpr uint64_t MaxSyscallNumber = 63;
    static constexpr unsigned PacSignatureShift = 56;
    static constexpr uint64_t PacSignatureMask = 0xFFULL << PacSignatureShift;
    static constexpr uint64_t PacKeySelectorShift = 54;
    static constexpr uint64_t PacKeySelectorMask = 0x3ULL << PacKeySelectorShift;
    static constexpr uint64_t PacAddressMask = (1ULL << 54) - 1;
    static constexpr uint32_t ShadowStackSlots = 64;
    static constexpr uint32_t VmcsBytes = 128;
    static constexpr uint32_t PcrCount = 16;
    static constexpr uint32_t PcrBytes = 32;
};

/** VMCS guest/host field offsets (bytes from `VMCS_BASE`). */
enum class VmcsOffset : uint32_t {
    GuestIc = 0,
    GuestFr = 8,
    GuestPtb = 16,
    GuestSp = 24,
    ExitReason = 32,
    ExitQualification = 40,
    HostIc = 56,
    HostFr = 64,
};

/** Security CSR codes (`0x60..0x6F`, `0x80..0x82`). */
enum class SecurityCsr : uint8_t {
    PacKeyH = 0x60,
    PacKeyL = 0x61,
    CfiKeyH = 0x62,
    CfiKeyL = 0x63,
    StackGuard = 0x64,
    ShadowStackBase = 0x65,
    ShadowStackTop = 0x66,
    RopTrap = 0x67,
    SandboxMask = 0x68,
    SandboxBase = 0x69,
    SandboxLimit = 0x6A,
    CapKeyH = 0x6B,
    CapKeyL = 0x6C,
    PcrBase = 0x6D,
    VmcsBase = 0x6E,
    MemEncryptKeyH = 0x6F,
    IommuBase = 0x80,
    IommuDevice = 0x81,
    IommuFaultAddr = 0x82,
};

inline bool IsSecurityCsrCode(uint8_t Code) {
    return (Code >= 0x60 && Code <= 0x6F) || (Code >= 0x80 && Code <= 0x82);
}
