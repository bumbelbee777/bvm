// MATMUL 2x2 FP32 tiles via memory descriptor (Honeycomb 0x180).

.data
    MatA: .quad 0x3F80000040000000
    .quad 0x4040000040800000
    MatB: .quad 0x40A0000040C00000
    .quad 0x40E0000041000000
    MatC: .quad 0
    MatC1: .quad 0
    MatDesc: .quad MatA
    .quad MatB
    .quad MatC
    .quad 0x4000200020002

.text
BootEntry:
    matmul x3, MatDesc
    load r10, [MatC]
    cmp r10, #0x4198000041B00000
    jne BadOutcome
    load r10, [MatC1]
    cmp r10, #0x422C000042480000
    jne BadOutcome
    hlt

BadOutcome:
    load r10, #0
    hlt
