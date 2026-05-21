// Boot narrative: DRAM init -> MMIO identity -> BootControl -> IVT setup ->
// InterruptPost raises vector 1 with EI -> supervisor SYSCALL #42 ->
// EU (enter user mode) -> user SYSCALL #7 -> SYSRET restores FR.UserMode.
//
// Physical MMIO quads (PascalCase; window base == RAM size == 0x1000000).
// Assembler supports only GPRs R0-R15 (+ control regs such as ivb/ivl/fr).

.data
    MagicScratchQuad: .quad 0
    InterruptHandshakeQuad: .quad 0
    UserSyscallHandshakeQuad: .quad 0
    IvtImage:
        .quad 0
        .quad ExternalIrqHandle
    SupervisorIcDescriptor:
        .quad SupervisorEntry

.text
BootEntry:
    load r1, [0x1000000]
    store r1, [MagicScratchQuad]

    load r2, #0xA5
    store r2, [0x1000008]

    lea ivb, IvtImage
    load ivl, 2
    ei

    load r3, #1
    store r3, [0x1000010]

    load r4, [SupervisorIcDescriptor]
    store r4, [0x1000018]

    syscall #42

    load r5, [InterruptHandshakeQuad]
    cmp r5, #1
    jne BadSequence

    load r6, [0x1000020]
    cmp r6, #42
    jne BadSequence

    load r7, [MagicScratchQuad]
    cmp r7, r1
    jne BadSequence

    cmp r14, #1
    jne BadSequence

    sandbox_allow #7
    eu

    syscall #7

    load r8, [UserSyscallHandshakeQuad]
    cmp r8, #7
    jne BadSequence

    cmp r14, #2
    jne BadSequence

    copy r9, fr
    load r11, #512
    and r9, r9, r11
    cmp r9, r11
    jne BadSequence

    load r10, 0xC0FEC0DE
    hlt

BadSequence:
    load r10, 0
    hlt

ExternalIrqHandle:
    load r13, #1
    store r13, [InterruptHandshakeQuad]
    iret

SupervisorEntry:
    load r12, [0x1000020]
    cmp r12, #42
    je SupervisorKernelOk

    cmp r12, #7
    jne SupervisorBad

    store r12, [UserSyscallHandshakeQuad]
    load r14, #2
    sysret

SupervisorKernelOk:
    load r14, #1
    sysret

SupervisorBad:
    load r14, #0
    sysret
