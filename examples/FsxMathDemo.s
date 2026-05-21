// FSX unary math: FSQRT / FABS / FNEG / RECIP / RSQRT (FP64).

.data
    ScalarA: .quad 0x4010000000000000
    ScalarB: .quad 0xC024000000000000
    OutSqrt: .quad 0
    OutAbs: .quad 0
    OutRsqrt: .quad 0

.text
BootEntry:
    fload f0, [ScalarA]
    fsqrt f1, f0
    fstore f1, [OutSqrt]
    fload f0, [ScalarB]
    fneg f1, f0
    fabs f2, f1
    fstore f2, [OutAbs]
    fload f0, [ScalarA]
    rsqrt f1, f0
    fstore f1, [OutRsqrt]

    load r10, [OutSqrt]
    cmp r10, #0x4000000000000000
    jne BadOutcome
    load r10, [OutAbs]
    cmp r10, #0x4024000000000000
    jne BadOutcome
    load r10, [OutRsqrt]
    cmp r10, #0x3FE0000000000000
    jne BadOutcome
    hlt

BadOutcome:
    load r10, #0
    hlt
