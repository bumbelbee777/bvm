#include "Security.h"
#include "SecurityProfile.h"
#include "MemAddrProfile.h"
#include "RegFile.h"
#include "SsxCrypto.h"

#include <cstring>
#include <stdexcept>

using namespace Honeycomb;

namespace {

enum class SecurityFaultKind : uint32_t {
    SandboxMemory = 1,
    SandboxSyscall = 2,
    PacAuth = 3,
    CfiViolation = 4,
    SspMismatch = 5,
    RopTrap = 6,
    Capability = 7,
};

void SecurityTrap(Cpu& Vm, uint64_t Addr, SecurityFaultKind Kind, const char* Message) {
    Vm.SecurityFaultAddr = Addr;
    Vm.SecurityFaultKind = static_cast<uint32_t>(Kind);
    if (Vm.InUserMode()) {
        Vm.DeliverInterruptVector(SecurityProfile::SecurityFaultVector);
        return;
    }
    throw std::runtime_error(Message);
}

uint64_t ResolveScalarMemAddr(const Cpu& CpuState, const DecodedInsn& Insn) {
    if (Insn.Imm1Flag) {
        return Insn.Imm1;
    }
    uint64_t Addr = static_cast<uint64_t>(static_cast<int64_t>(Insn.Disp));
    if (Insn.Rs != 0) {
        Addr += CpuState.ReadGpr(Insn.Rs);
    }
    if (Insn.Rt != 0) {
        Addr += CpuState.ReadGpr(Insn.Rt);
    }
    return Addr;
}

uint64_t RequireImm(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag) {
        throw std::runtime_error("Security op requires imm1");
    }
    return Insn.Imm1;
}

uint8_t PacSignature(const Cpu& Vm, uint64_t Address, uint8_t KeySelector) {
    const uint64_t Seed = (Address & SecurityProfile::PacAddressMask) ^
                          (static_cast<uint64_t>(KeySelector) << 32) ^
                          static_cast<uint64_t>(Vm.PacKey & 0xFFFFFFFFFFFFFFFFULL);
    const Uint128 Mixed = SsxCrypto::XgenFromSeed(Seed);
    return static_cast<uint8_t>(Mixed & 0xFFu);
}

uint64_t ApplyPac(const Cpu& Vm, uint64_t Pointer, uint8_t KeySelector) {
    const uint64_t Address = Pointer & SecurityProfile::PacAddressMask;
    const uint8_t Sig = PacSignature(Vm, Address, KeySelector);
    return Address | (static_cast<uint64_t>(KeySelector & 3u) << SecurityProfile::PacKeySelectorShift) |
           (static_cast<uint64_t>(Sig) << SecurityProfile::PacSignatureShift);
}

uint64_t AuthenticatePac(const Cpu& Vm, uint64_t Pointer, uint8_t KeySelector) {
    const uint8_t Expected = PacSignature(Vm, Pointer, KeySelector);
    const uint8_t Actual =
        static_cast<uint8_t>((Pointer >> SecurityProfile::PacSignatureShift) & 0xFFu);
    if (Expected != Actual) {
        SecurityTrap(const_cast<Cpu&>(Vm), Pointer, SecurityFaultKind::PacAuth,
                     "PAC authentication failed");
    }
    return Pointer & SecurityProfile::PacAddressMask;
}

uint64_t VmcsField(const Cpu& Vm, uint32_t Offset) {
    if (Vm.VmcsBase == 0) {
        return 0;
    }
    return Vm.Read64(Vm.VmcsBase + Offset);
}

void WriteVmcsField(Cpu& Vm, uint32_t Offset, uint64_t Value) {
    if (Vm.VmcsBase == 0) {
        throw std::runtime_error("VMWRITE with zero VMCS_BASE");
    }
    Vm.Write64(Vm.VmcsBase + Offset, Value);
}

void SimpleSha256Extend(uint8_t Pcr[32], const uint8_t* Data, size_t Len) {
    for (size_t Index = 0; Index < Len; ++Index) {
        const uint8_t Mix = static_cast<uint8_t>(Pcr[Index % 32] ^ Data[Index]);
        Pcr[Index % 32] = static_cast<uint8_t>((Mix * 131u) + Pcr[(Index + 7) % 32]);
    }
}

} // namespace

bool Honeycomb::IsSecurityCsr(uint8_t Code) { return IsSecurityCsrCode(Code); }

uint64_t Honeycomb::ReadSecurityCsr(const Cpu& Vm, uint8_t Code) {
    switch (static_cast<SecurityCsr>(Code)) {
        case SecurityCsr::PacKeyH:
            return static_cast<uint64_t>(Vm.PacKey >> 64);
        case SecurityCsr::PacKeyL:
            return static_cast<uint64_t>(Vm.PacKey);
        case SecurityCsr::CfiKeyH:
            return static_cast<uint64_t>(Vm.CfiKey >> 64);
        case SecurityCsr::CfiKeyL:
            return static_cast<uint64_t>(Vm.CfiKey);
        case SecurityCsr::StackGuard:
            return Vm.StackGuard;
        case SecurityCsr::ShadowStackBase:
            return Vm.ShadowStackBase;
        case SecurityCsr::ShadowStackTop:
            return Vm.ShadowStackPtr;
        case SecurityCsr::RopTrap:
            return Vm.RopTrapBase;
        case SecurityCsr::SandboxMask:
            return Vm.SandboxAllowMask;
        case SecurityCsr::SandboxBase:
            return Vm.SandboxBase;
        case SecurityCsr::SandboxLimit:
            return Vm.SandboxLimit;
        case SecurityCsr::CapKeyH:
            return static_cast<uint64_t>(Vm.CapKey >> 64);
        case SecurityCsr::CapKeyL:
            return static_cast<uint64_t>(Vm.CapKey);
        case SecurityCsr::PcrBase:
            return Vm.PcrBase;
        case SecurityCsr::VmcsBase:
            return Vm.VmcsBase;
        case SecurityCsr::MemEncryptKeyH:
            return static_cast<uint64_t>(Vm.MemEncryptKey >> 64);
        case SecurityCsr::IommuBase:
            return Vm.IommuBase;
        case SecurityCsr::IommuDevice:
            return Vm.IommuDevice;
        case SecurityCsr::IommuFaultAddr:
            return Vm.IommuFaultAddr;
        default:
            throw std::out_of_range("Unknown security CSR");
    }
}

void Honeycomb::WriteSecurityCsr(Cpu& Vm, uint8_t Code, uint64_t Value) {
    Vm.RequireSupervisor("Security CSR write requires supervisor mode");
    switch (static_cast<SecurityCsr>(Code)) {
        case SecurityCsr::PacKeyH:
            Vm.PacKey = (Vm.PacKey & 0xFFFFFFFFFFFFFFFFULL) |
                        (static_cast<Uint128>(Value) << 64);
            return;
        case SecurityCsr::PacKeyL:
            Vm.PacKey = (Vm.PacKey & ~static_cast<Uint128>(0xFFFFFFFFFFFFFFFFULL)) | Value;
            return;
        case SecurityCsr::CfiKeyH:
            Vm.CfiKey = (Vm.CfiKey & 0xFFFFFFFFFFFFFFFFULL) |
                        (static_cast<Uint128>(Value) << 64);
            return;
        case SecurityCsr::CfiKeyL:
            Vm.CfiKey = (Vm.CfiKey & ~static_cast<Uint128>(0xFFFFFFFFFFFFFFFFULL)) | Value;
            return;
        case SecurityCsr::StackGuard:
            Vm.StackGuard = Value;
            return;
        case SecurityCsr::ShadowStackBase:
            Vm.ShadowStackBase = Value;
            Vm.ShadowStackPtr = Value;
            return;
        case SecurityCsr::ShadowStackTop:
            Vm.ShadowStackTop = Value;
            return;
        case SecurityCsr::RopTrap:
            Vm.RopTrapBase = Value;
            return;
        case SecurityCsr::SandboxMask:
            Vm.SandboxAllowMask = Value;
            return;
        case SecurityCsr::SandboxBase:
            Vm.SandboxBase = Value;
            return;
        case SecurityCsr::SandboxLimit:
            Vm.SandboxLimit = Value;
            return;
        case SecurityCsr::CapKeyH:
            Vm.CapKey = (Vm.CapKey & 0xFFFFFFFFFFFFFFFFULL) |
                        (static_cast<Uint128>(Value) << 64);
            return;
        case SecurityCsr::CapKeyL:
            Vm.CapKey = (Vm.CapKey & ~static_cast<Uint128>(0xFFFFFFFFFFFFFFFFULL)) | Value;
            return;
        case SecurityCsr::PcrBase:
            Vm.PcrBase = Value;
            return;
        case SecurityCsr::VmcsBase:
            Vm.VmcsBase = Value;
            return;
        case SecurityCsr::MemEncryptKeyH:
            Vm.MemEncryptKey = (Vm.MemEncryptKey & 0xFFFFFFFFFFFFFFFFULL) |
                               (static_cast<Uint128>(Value) << 64);
            return;
        case SecurityCsr::IommuBase:
            Vm.IommuBase = Value;
            return;
        case SecurityCsr::IommuDevice:
            Vm.IommuDevice = Value;
            return;
        case SecurityCsr::IommuFaultAddr:
            Vm.IommuFaultAddr = Value;
            return;
        default:
            throw std::out_of_range("Unknown security CSR");
    }
}

void Honeycomb::SecurityCheckMemAccess(Cpu& Vm, uint64_t Addr, uint64_t Size) {
    if (!Vm.InUserMode()) {
        return;
    }
    if (Vm.SandboxLimit <= Vm.SandboxBase) {
        return;
    }
    const uint64_t End = Addr + Size;
    if (Addr < Vm.SandboxBase || End > Vm.SandboxLimit) {
        SecurityTrap(Vm, Addr, SecurityFaultKind::SandboxMemory,
                     "Sandbox memory access violation");
    }
}

void Honeycomb::SecurityCheckSyscall(Cpu& Vm, uint64_t Number) {
    if (!Vm.InUserMode()) {
        return;
    }
    if (Number > SecurityProfile::MaxSyscallNumber) {
        SecurityTrap(Vm, Number, SecurityFaultKind::SandboxSyscall,
                     "Syscall number out of sandbox range");
    }
    if ((Vm.SandboxDenyMask >> Number) & 1ULL) {
        SecurityTrap(Vm, Number, SecurityFaultKind::SandboxSyscall, "Syscall denied");
    }
    if (Vm.SandboxAllowMask == 0) {
        SecurityTrap(Vm, Number, SecurityFaultKind::SandboxSyscall,
                     "Syscall not in allowlist (empty SANDBOX_MASK)");
    }
    if (((Vm.SandboxAllowMask >> Number) & 1ULL) == 0) {
        SecurityTrap(Vm, Number, SecurityFaultKind::SandboxSyscall, "Syscall not allowed");
    }
}

void Honeycomb::ExecuteSecurityOpcode(Cpu& Vm, const DecodedInsn& Insn) {
    switch (Insn.Op) {
        case Opcode::PAC_KEY_INIT: {
            Vm.RequireSupervisor("PAC_KEY_INIT requires supervisor");
            const uint64_t Seed = Vm.Ic ^ 0xA5A5A5A5A5A5A5A5ULL;
            Vm.PacKey = SsxCrypto::XgenFromSeed(Seed);
            Vm.CfiKey = SsxCrypto::XgenFromSeed(Seed ^ 0xC71C71C71C71C71CULL);
            Vm.CapKey = SsxCrypto::XgenFromSeed(Seed ^ 0xCAFEBABECAFEBABEULL);
            Vm.StackGuard = static_cast<uint64_t>(Vm.PacKey & 0xFFFFFFFFFFFFFFFFULL);
            return;
        }
        case Opcode::PACIA:
        case Opcode::PACDA:
        case Opcode::PACIB: {
            const uint8_t Selector = (Insn.Op == Opcode::PACIB) ? 1u : 0u;
            Vm.WriteGpr(Insn.Rd, ApplyPac(Vm, Vm.ReadGpr(Insn.Rs), Selector));
            return;
        }
        case Opcode::AUTIA:
        case Opcode::AUTDA: {
            Vm.WriteGpr(Insn.Rd, AuthenticatePac(Vm, Vm.ReadGpr(Insn.Rs), 0));
            return;
        }
        case Opcode::XPACI:
            Vm.WriteGpr(Insn.Rd, Vm.ReadGpr(Insn.Rs) & SecurityProfile::PacAddressMask);
            return;
        case Opcode::CFI_LABEL: {
            const uint32_t Label = static_cast<uint32_t>(RequireImm(Insn) & 0xFFFFu);
            const uint32_t Slot =
                static_cast<uint32_t>((Vm.Ic >> 2) % Vm.CfiLabelTable.size());
            Vm.CfiLabelTable[Slot] = Label;
            Vm.ExpectedCfiLabel = Label;
            return;
        }
        case Opcode::CFI_CALL: {
            const uint64_t Target = Vm.ReadGpr(Insn.Rs) & SecurityProfile::PacAddressMask;
            const uint32_t Slot =
                static_cast<uint32_t>((Target >> 2) % Vm.CfiLabelTable.size());
            if (Vm.CfiLabelTable[Slot] != Vm.ExpectedCfiLabel) {
                SecurityTrap(Vm, Target, SecurityFaultKind::CfiViolation,
                             "CFI indirect call target mismatch");
            }
            Vm.Ic = Target;
            return;
        }
        case Opcode::CFI_JUMP:
            Vm.Ic = Vm.ReadGpr(Insn.Rs) & SecurityProfile::PacAddressMask;
            return;
        case Opcode::SSP_PUSH: {
            if (Vm.ShadowStackPtr >= Vm.ShadowStackTop) {
                throw std::runtime_error("Shadow stack overflow");
            }
            const uint32_t Index = static_cast<uint32_t>(
                (Vm.ShadowStackPtr - Vm.ShadowStackBase) / sizeof(uint64_t));
            if (Index >= SecurityProfile::ShadowStackSlots) {
                throw std::runtime_error("Shadow stack index out of range");
            }
            Vm.ShadowStack[Index] = Vm.ReadGpr(Insn.Rs);
            Vm.ShadowStackPtr += sizeof(uint64_t);
            return;
        }
        case Opcode::SSP_POP: {
            if (Vm.ShadowStackPtr <= Vm.ShadowStackBase) {
                throw std::runtime_error("Shadow stack underflow");
            }
            Vm.ShadowStackPtr -= sizeof(uint64_t);
            const uint32_t Index = static_cast<uint32_t>(
                (Vm.ShadowStackPtr - Vm.ShadowStackBase) / sizeof(uint64_t));
            Vm.WriteGpr(Insn.Rd, Vm.ShadowStack[Index]);
            return;
        }
        case Opcode::SSP_CHECK: {
            const uint64_t Canary = Vm.StackGuard;
            const uint64_t StackTop = Vm.Sp;
            if (StackTop < 8) {
                throw std::runtime_error("SSP_CHECK with invalid SP");
            }
            const uint64_t Stored = Vm.Read64(StackTop - 8);
            if (Stored != Canary) {
                SecurityTrap(Vm, StackTop, SecurityFaultKind::SspMismatch,
                             "Stack canary mismatch");
            }
            return;
        }
        case Opcode::SANDBOX_ALLOW: {
            Vm.RequireSupervisor("SANDBOX_ALLOW requires supervisor");
            const uint64_t Num = RequireImm(Insn) & SecurityProfile::MaxSyscallNumber;
            Vm.SandboxAllowMask |= (1ULL << Num);
            return;
        }
        case Opcode::SANDBOX_DENY: {
            Vm.RequireSupervisor("SANDBOX_DENY requires supervisor");
            const uint64_t Num = RequireImm(Insn) & SecurityProfile::MaxSyscallNumber;
            Vm.SandboxDenyMask |= (1ULL << Num);
            return;
        }
        case Opcode::SANDBOX_RESET: {
            Vm.RequireSupervisor("SANDBOX_RESET requires supervisor");
            Vm.SandboxAllowMask = 0;
            Vm.SandboxDenyMask = 0;
            return;
        }
        case Opcode::ENC_GENKEY: {
            Vm.RequireSupervisor("ENC_GENKEY requires supervisor");
            Vm.MemEncryptKey = SsxCrypto::XgenFromSeed(Vm.Ic ^ 0xE0C0DEULL);
            return;
        }
        case Opcode::ENC_SWITCH:
            Vm.RequireSupervisor("ENC_SWITCH requires supervisor");
            Vm.MemEncryptDomain = RequireImm(Insn) & 0xFFFFu;
            return;
        case Opcode::ENC_LOAD: {
            const uint64_t Addr = ResolveScalarMemAddr(Vm, Insn);
            SecurityCheckMemAccess(Vm, Addr, 8);
            Uint128 Cipher = Vm.Read128(Addr);
            Uint128 Plain = SsxCrypto::Aes128Decrypt(Cipher, Vm.MemEncryptKey);
            Vm.WriteGpr(Insn.Rd, static_cast<uint64_t>(Plain));
            return;
        }
        case Opcode::ENC_STORE: {
            const uint64_t Addr = ResolveScalarMemAddr(Vm, Insn);
            SecurityCheckMemAccess(Vm, Addr, 8);
            const Uint128 Plain = static_cast<Uint128>(Vm.ReadGpr(Insn.Rd));
            const Uint128 Cipher = SsxCrypto::Aes128Encrypt(Plain, Vm.MemEncryptKey);
            Vm.Write128(Addr, Cipher);
            return;
        }
        case Opcode::MEASURE: {
            Vm.RequireSupervisor("MEASURE requires supervisor");
            if (Vm.PcrBase == 0) {
                throw std::runtime_error("MEASURE requires PCR_BASE");
            }
            const uint32_t PcrIndex = static_cast<uint32_t>(RequireImm(Insn) & 0xFu);
            const uint64_t Addr = Insn.Imm2Flag ? Insn.Imm2 : Vm.ReadGpr(Insn.Rs);
            const uint32_t Len = static_cast<uint32_t>(Insn.Disp & 0xFFFFu);
            uint8_t Pcr[SecurityProfile::PcrBytes]{};
            for (uint32_t Index = 0; Index < SecurityProfile::PcrBytes; ++Index) {
                Pcr[Index] = Vm.Read8(Vm.PcrBase + PcrIndex * SecurityProfile::PcrBytes + Index);
            }
            for (uint32_t Offset = 0; Offset < Len; ++Offset) {
                uint8_t Byte = Vm.Read8(Addr + Offset);
                SimpleSha256Extend(Pcr, &Byte, 1);
            }
            for (uint32_t Index = 0; Index < SecurityProfile::PcrBytes; ++Index) {
                Vm.Write8(Vm.PcrBase + PcrIndex * SecurityProfile::PcrBytes + Index, Pcr[Index]);
            }
            return;
        }
        case Opcode::VMLAUNCH: {
            Vm.RequireSupervisor("VMLAUNCH requires supervisor");
            if (Vm.VmGuestActive) {
                throw std::runtime_error("VMLAUNCH with guest already active");
            }
            WriteVmcsField(Vm, static_cast<uint32_t>(VmcsOffset::HostIc), Vm.Ic);
            WriteVmcsField(Vm, static_cast<uint32_t>(VmcsOffset::HostFr), Vm.Fr);
            WriteVmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestSp), Vm.Sp);
            Vm.VmHostIc = Vm.Ic;
            Vm.VmHostFr = Vm.Fr;
            Vm.VmGuestActive = true;
            Vm.Ic = VmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestIc));
            Vm.Fr = VmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestFr)) |
                    StatusFlags::Hypervisor;
            Vm.Ptb = VmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestPtb));
            Vm.Sp = VmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestSp));
            return;
        }
        case Opcode::VMRESUME: {
            Vm.RequireSupervisor("VMRESUME requires supervisor");
            if (Vm.VmGuestActive) {
                throw std::runtime_error("VMRESUME with guest already active");
            }
            const uint64_t GuestIc =
                VmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestIc));
            if (GuestIc == 0) {
                throw std::runtime_error("VMRESUME with zero guest IC in VMCS");
            }
            Vm.VmGuestActive = true;
            Vm.Ic = GuestIc;
            Vm.Fr = VmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestFr)) |
                    StatusFlags::Hypervisor;
            Vm.Ptb = VmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestPtb));
            Vm.Sp = VmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestSp));
            return;
        }
        case Opcode::VMEXIT: {
            if (!Vm.VmGuestActive) {
                throw std::runtime_error("VMEXIT without active guest");
            }
            WriteVmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestIc), Vm.Ic);
            WriteVmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestFr), Vm.Fr);
            WriteVmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestPtb), Vm.Ptb);
            WriteVmcsField(Vm, static_cast<uint32_t>(VmcsOffset::GuestSp), Vm.Sp);
            WriteVmcsField(Vm, static_cast<uint32_t>(VmcsOffset::ExitReason), 1);
            WriteVmcsField(Vm, static_cast<uint32_t>(VmcsOffset::ExitQualification), 0);
            Vm.Ic = VmcsField(Vm, static_cast<uint32_t>(VmcsOffset::HostIc));
            if (Vm.Ic == 0) {
                Vm.Ic = Vm.VmHostIc;
            }
            Vm.Fr = VmcsField(Vm, static_cast<uint32_t>(VmcsOffset::HostFr));
            if (Vm.Fr == 0) {
                Vm.Fr = Vm.VmHostFr;
            }
            Vm.Fr &= ~StatusFlags::Hypervisor;
            Vm.VmGuestActive = false;
            return;
        }
        case Opcode::VMREAD: {
            const uint32_t Field = static_cast<uint32_t>(RequireImm(Insn) & 0xFFFFu);
            Vm.WriteGpr(Insn.Rd, VmcsField(Vm, Field));
            return;
        }
        case Opcode::VMWRITE: {
            const uint32_t Field = static_cast<uint32_t>(RequireImm(Insn) & 0xFFFFu);
            WriteVmcsField(Vm, Field, Vm.ReadGpr(Insn.Rd));
            return;
        }
        case Opcode::CAP_LOAD:
        case Opcode::CAP_STORE:
        case Opcode::CAP_CALL:
        case Opcode::CAP_SEAL:
        case Opcode::CAP_UNSEAL:
        case Opcode::QUOTE:
        case Opcode::SEAL:
        case Opcode::UNSEAL:
            throw std::runtime_error(
                "Security opcode defined but not yet implemented in baseline BVM");
        default:
            throw std::runtime_error("Not a security opcode");
    }
}
