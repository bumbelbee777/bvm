// SSX lane ops: VDIV / VMIN / VMAX / VNEG / VABS (FP32, VL=2).

.data
    LaneA: .quad 0x4100000041400000
    LaneB: .quad 0x4000000040400000
    LaneC: .quad 0
    LaneD: .quad 0
    LaneE: .quad 0

.text
BootEntry:
    xload x0, [LaneA]
    xload x1, [LaneB]
    vdiv x2, x0, x1
    xstore x2, [LaneC]
    vmin x3, x0, x1
    vmax x4, x0, x1
    xstore x4, [LaneD]
    vneg x5, x0
    vabs x6, x5
    xstore x6, [LaneE]

    load r10, [LaneC]
    cmp r10, #0x4080000040800000
    jne BadOutcome
    load r10, [LaneD]
    cmp r10, #0x4100000041400000
    jne BadOutcome
    load r10, [LaneE]
    cmp r10, #0x4100000041400000
    jne BadOutcome
    hlt

BadOutcome:
    load r10, #0
    hlt
