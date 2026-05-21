// SSX fused negated FMA: VFNMADD (FP32 lanes, VL=2).

.data
    LaneA: .quad 0x4040000040400000
    LaneB: .quad 0x4000000040000000
    LaneAcc: .quad 0x3F8000003F800000
    LaneOut: .quad 0

.text
BootEntry:
    vload x0, [LaneA]
    vload x1, [LaneB]
    vload x2, [LaneAcc]
    vfnmadd x2, x1, x0
    vstore x2, [LaneOut]

    load r10, [LaneOut]
    cmp r10, #0xC0A00000C0A00000
    jne BadOutcome
    hlt

BadOutcome:
    load r10, #0
    hlt
