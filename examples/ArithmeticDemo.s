// Demonstrates GPR arithmetic, LOAD/STORE, compare, conditional branch, and loop.
// Mirrors the legacy hello.s sanity check binary.

.data
    ValueOne: .quad 0x123456789ABCDEF0
    ValueTwo: .quad 0x0F0F0F0F0F0F0F0F
    ResultQuad: .quad 0

.text
_start:
    load r1, [ValueOne]
    load r2, [ValueTwo]

    add r10, r1, r2<<4
    add r3, r1, r2
    sub r4, r1, r2
    and r5, r1, r2
    or r6, r1, r2
    xor r7, r1, r2

    store r3, [ResultQuad]

    cmp r1, r2
    jgt Greater
    hlt

Greater:
    load r8, 5
    load r9, 0

LoopAccum:
    add r9, r9, r8
    dec r8
    jnz LoopAccum

    store r9, [ResultQuad]
    hlt
