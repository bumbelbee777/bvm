# Honeycomb Security Architecture (v1.0)

Companion to [Honeycomb.md](Honeycomb.md). Normative opcode values live in `sources/Isa.h` (`0x500..0x521`).

## Overview

Honeycomb's security model provides defense in depth:

| Layer | Protection |
|-------|------------|
| **Hypervisor (EL2)** | VM isolation, nested paging stub, IOMMU CSRs |
| **OS Kernel (EL1)** | Memory protection, syscall filtering, CSR policy |
| **User (EL0)** | PAC, CFI, SSP, syscall sandbox, protected regions |

**Cryptography reuses SSX** `XCRYPT` / `XDECRYPT` / `XGEN` — no separate crypto block.

## Privilege levels

| Level | Name | Purpose |
|-------|------|---------|
| EL3 | Secure Monitor | Trusted firmware (reserved) |
| EL2 | Hypervisor | `VMLAUNCH` / `VMRESUME` / `VMEXIT` / VMCS (`FR.Hypervisor` bit **12**) |
| EL1 | OS Kernel | `EP`/`DP`, sandbox CSRs |
| EL0 | User | `EU` sets `FR.UserMode` |

## Security CSRs (`0x60..0x6F`, `0x80..0x82`)

| Code | Register | BVM |
|------|----------|-----|
| 0x60/0x61 | `PAC_KEY_H/L` | ✓ read/write (supervisor) |
| 0x64 | `STACK_GUARD` | ✓ |
| 0x65/0x66 | `SHADOW_STACK_*` | ✓ |
| 0x67 | `ROP_TRAP` | ✓ |
| 0x68 | `SANDBOX_MASK` | ✓ allowlist bits |
| 0x69/0x6A | `SANDBOX_BASE/LIMIT` | ✓ user `LOAD`/`STORE` bounds |
| 0x6D | `PCR_BASE` | ✓ `MEASURE` |
| 0x6E | `VMCS_BASE` | ✓ hypervisor stub |
| 0x80..0x82 | IOMMU | ✓ storage only (baseline) |

Assembler aliases: `sandbox_base`, `sandbox_limit`, `sandbox_mask`, etc. (see `TryParseRegister` in `Isa.h`).

## Opcode map (baseline BVM)

| Range | Feature | Status |
|-------|---------|--------|
| 0x500..0x505 | PAC sign/auth/strip | ✓ (`PAC_KEY_INIT` uses `XGEN`) |
| 0x506..0x508 | CFI label/call/jump | ✓ |
| 0x509..0x50B | Shadow stack / canary | ✓ |
| 0x50C..0x50E | Syscall sandbox | ✓ integrated with `SYSCALL` |
| 0x514..0x517 | Encrypted load/store | ✓ (`XCRYPT` path) |
| 0x518 | `MEASURE` (PCR extend) | ✓ simplified hash |
| 0x51C..0x520 | Hypervisor VMCS | ✓ stub |
| 0x50F..0x513, 0x519..0x51B | Capabilities / quote / seal | trap (defined) |

**User-mode violations** deliver IVT vector **`0x3E`** (see `SecurityProfile::SecurityFaultVector`). `SecurityFaultAddr` and `SecurityFaultKind` record the fault.

## Syscall sandbox

Supervisor configures allowlist before `EU`:

```asm
sandbox_reset
sandbox_allow #42
copy sandbox_base, r1    ; region lo
copy sandbox_limit, r2   ; region hi
eu
syscall #42              ; allowed
syscall #99              ; trap vector 0x3E
```

Empty `SANDBOX_MASK` after `sandbox_reset` denies all user syscalls until `sandbox_allow`.

## Protected memory (hardware seccomp)

When `SANDBOX_LIMIT > SANDBOX_BASE`, user-mode `LOAD`/`STORE` must fall inside `[base, limit)` or trap.

## Verification

- `examples/SecuritySandboxDemo.s` — allow syscall **42**, deny **99**, success **`R10 = 0xC0FEC0DE`**
- `tests/HelloTest.cpp` includes the demo

## Implementation files

| Component | Path |
|-----------|------|
| Opcodes | `sources/Isa.h` |
| Profile | `sources/bvm/SecurityProfile.h` |
| Execution | `sources/bvm/Security.cpp` |
| Integration | `sources/bvm/Core.cpp` (`LOAD`/`STORE`/`SYSCALL`) |
| Assembler | `EncodeSecurityMnemonic` in `sources/assembler/Assembler.cpp` |
