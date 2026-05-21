// SSX lane reciprocals: VRCP + VRSQRT (FP32, VL=2).

.data
    LaneIn: .quad 0x4080000040800000
    LaneRcp: .quad 0
    LaneRsqrt: .quad 0

.text
BootEntry:
    vload x0, [LaneIn]
    vrcp x1, x0
    vrsqrt x2, x0
    vstore x1, [LaneRcp]
    vstore x2, [LaneRsqrt]

    load r10, [LaneRcp]
    cmp r10, #0x3E8000003E800000
    jne BadOutcome
    load r10, [LaneRsqrt]
    cmp r10, #0x3F0000003F000000
    jne BadOutcome
    hlt

BadOutcome:
    load r10, #0
    hlt
