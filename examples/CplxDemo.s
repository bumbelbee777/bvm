// CPLX baseline: (3+4i) * (1+2i) = -5 + 10i (C_FP64, adjacent GPR pairs).
// Uses `add rd, r12, #imm` pattern; R12 stays zero from reset.

.data
    Three: .quad 0x4008000000000000
    Four: .quad 0x4010000000000000
    One: .quad 0x3FF0000000000000
    Two: .quad 0x4000000000000000
    ExpectReal: .quad 0xC014000000000000
    ExpectImag: .quad 0x4024000000000000
    MemPair: .quad 0, 0

.text
BootEntry:
    load r0, [Three]
    load r1, [Four]
    cpack cr0, r0, r1
    load r2, [One]
    load r3, [Two]
    cpack cr1, r2, r3
    cmul cr2, cr0, cr1
    cextract r4, cr2, #0
    cextract r5, cr2, #1
    load r10, [ExpectReal]
    cmp r4, r10
    jne fail
    load r10, [ExpectImag]
    cmp r5, r10
    jne fail

    cstore cr2, [MemPair]
    cload cr3, [MemPair]
    cextract r6, cr3, #0
    load r10, [ExpectReal]
    cmp r6, r10
    jne fail

    hlt
fail:
    add r0, r12, #1
    hlt
