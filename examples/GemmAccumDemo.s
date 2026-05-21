// GEMM_OFFLOAD: C <- A*B + C (alpha=beta=1, identity * B + ones).

.data
    MatA: .quad 0x3F80000000000000
    .quad 0x000000003F800000
    MatB: .quad 0x4000000040400000
    .quad 0x4080000040A00000
    MatC: .quad 0x3F8000003F800000
    MatC1: .quad 0x3F8000003F800000
    GemmDesc: .quad MatA
    .quad MatB
    .quad MatC
    .quad 0x4000200020002
    .quad 0x3F8000003F800000

.text
BootEntry:
    gemm x4, GemmDesc
    load r10, [MatC]
    cmp r10, #0x4040000040800000
    jne BadOutcome
    load r10, [MatC1]
    cmp r10, #0x40A0000040C00000
    jne BadOutcome
    hlt

BadOutcome:
    load r10, #0
    hlt
