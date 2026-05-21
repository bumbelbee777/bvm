// FSX FMA: F2 <- F0 * F1 + F2  with operands 2.0, 3.0, 1.0 -> 7.0

.data
    OperandA: .quad 0x4000000000000000
    OperandB: .quad 0x4008000000000000
    SeedAcc: .quad 0x3FF0000000000000
    ResultQuad: .quad 0

.text
BootEntry:
    fload f0, [OperandA]
    fload f1, [OperandB]
    fload f2, [SeedAcc]
    fma f2, f0, f1
    fstore f2, [ResultQuad]

    load r10, [ResultQuad]
    cmp r10, #0x401C000000000000
    jne BadOutcome
    hlt

BadOutcome:
    load r10, #0
    hlt
