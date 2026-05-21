// FSX baseline: FLOAD / FADD / FSTORE (FP64, opmode 001).
// 3.0 + 4.0 -> 7.0 (IEEE bit patterns in .data).

.data
    OperandA: .quad 0x4008000000000000
    OperandB: .quad 0x4010000000000000
    ResultQuad: .quad 0

.text
BootEntry:
    fload f0, [OperandA]
    fload f1, [OperandB]
    fadd f2, f0, f1
    fstore f2, [ResultQuad]

    load r10, [ResultQuad]
    cmp r10, #0x401C000000000000
    jne BadOutcome
    hlt

BadOutcome:
    load r10, #0
    hlt
