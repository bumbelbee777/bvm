// SSX baseline: XLOAD / VADD (FP32 lanes) / XSTORE on 128-bit pack.
// Lane0: 1.0f + 10.0f = 11.0f; Lane1: 2.0f + 20.0f = 22.0f (big-endian lanes).

.data
    VectorLeft: .quad 0x3F80000040000000
    VectorLeftPad: .quad 0
    VectorRight: .quad 0x4120000041A00000
    VectorRightPad: .quad 0
    VectorResult: .quad 0
    VectorResultPad: .quad 0

.text
BootEntry:
    xload x0, [VectorLeft]
    xload x1, [VectorRight]
    vadd x2, x0, x1
    xstore x2, [VectorResult]

    load r10, [VectorResult]
    cmp r10, #0x4130000041B00000
    jne BadOutcome
    hlt

BadOutcome:
    load r10, #0
    hlt
