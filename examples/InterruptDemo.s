// Software interrupt using IVB + IVL (IVT byte size = IVL * 8).

.data
IntVectorTable:
    .quad IntServiceRoutine

.text
_start:
    lea ivb, IntVectorTable
    load ivl, 1
    load r10, 0
    ei
    int 0
    cmp r10, #1
    jne BadOutcome
    perfmon r11, #2
    cmp r11, #0
    je BadOutcome
    hlt

BadOutcome:
    load r10, #0
    hlt

IntServiceRoutine:
    load r10, 1
    iret
