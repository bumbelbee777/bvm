// SSX lane program: VBROADCAST + VMUL + VFMA + VSQRT (FP32, VL=2).

.data
    LaneA: .quad 0x3F80000040000000
    LaneB: .quad 0
    LaneC: .quad 0
    LaneD: .quad 0

.text
BootEntry:
    vbroadcast x0, #0x3F000000
    xload x1, [LaneA]
    vmul x2, x1, x0
    vfma x2, x2, x0
    vsqrt x4, x2
    xstore x4, [LaneD]

    load r10, [LaneD]
    cmp r10, #0x3F5DB3D73F9CC471
    jne BadOutcome
    hlt

BadOutcome:
    load r10, #0
    hlt
