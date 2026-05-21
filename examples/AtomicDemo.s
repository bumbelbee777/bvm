// Core ISA: atomic memory RMW, saturating ALU, IN/OUT, prefetch (no-op in BVM).

.data
    Cell: .quad 100

.text
_start:
    load r1, #7
    atadd r1, [Cell]

    cmp r1, #100
    jne BadExit

    load r2, [Cell]
    cmp r2, #107
    jne BadExit

    atinc [Cell]
    load r2, [Cell]
    cmp r2, #108
    jne BadExit

    load r3, #200
    atcas r3, #108, [Cell]
    load r2, [Cell]
    cmp r2, #200
    jne BadExit

    load r4, 0x7FFFFFFFFFFFFFFF
    satadd r5, r4, #10
    cmp r5, #0x7FFFFFFFFFFFFFFF
    jne BadExit

    satmul r6, r4, #2
    cmp r6, #0x7FFFFFFFFFFFFFFF
    jne BadExit

    load r11, #0xAB
    out r11, #7
    load r12, #0
    in r12, #7
    cmp r12, #0xAB
    jne BadExit

    prefetch [Cell], #0

    load r9, 1
    hlt

BadExit:
    load r9, 0
    hlt
