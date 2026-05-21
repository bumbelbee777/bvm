// Shift-integrated ALU: third operand uses <<, >>, <<<, >>> in disp[8:0].

.data
    Accum: .quad 0

.text
_start:
    load r1, 0x8000000000000000
    add r2, zero, r1<<1
    jz ShlKnownZero
    hlt

ShlKnownZero:
    load r3, 0xFFFFFFFFFFFFFFFF
    add r4, zero, r3>>4
    load r10, 0x0FFFFFFFFFFFFFFF
    cmp r4, r10
    jne BadOutcome
    load r5, 0xA5A5A5A5A5A5A5A5
    add r6, zero, r5>>>4
    load r11, 0x5A5A5A5A5A5A5A5A
    cmp r6, r11
    jne BadOutcome
    load r1, 1
    store r1, [Accum]
    hlt

BadOutcome:
    load r12, 0xDEADDEADDEADDEAD
    store r12, [Accum]
    hlt
