// Syscall sandbox + protected memory region (user EL0).
// Success: R10 = 0xC0FEC0DE after denied syscall #99 traps; FaultKind = 2.

.data
    Handshake: .quad 0
    FaultKind: .quad 0
    SyscallPcDesc: .quad SyscallHandler
    IvtImage:
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad SecurityFaultHandler

.text
BootEntry:
    load r1, [SyscallPcDesc]
    store r1, [0x1000018]
    lea ivb, IvtImage
    load ivl, 63
    ei
    load r2, #0
    copy sandbox_base, r2
    load r3, #0x100000
    copy sandbox_limit, r3
    sandbox_allow #42
    eu
    syscall #42
    load r4, [Handshake]
    cmp r4, #42
    jne BadOutcome
    syscall #99
    jmp BadOutcome

SyscallHandler:
    load r5, [0x1000020]
    cmp r5, #42
    jne SyscallBad
    load r6, #42
    store r6, [Handshake]
    sysret
SyscallBad:
    load r10, #0
    sysret

SecurityFaultHandler:
    load r7, #2
    store r7, [FaultKind]
    load r10, #0xC0FEC0DE
    hlt

BadOutcome:
    load r10, #0
    hlt
