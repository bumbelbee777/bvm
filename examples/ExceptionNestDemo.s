// Three nested hardware exception frames (vectors 10 -> 11 -> 12).

.data
    NestCountQuad: .quad 0
    IvtImage:
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad NestHandler0
        .quad NestHandler1
        .quad NestHandler2

.text
_start:
    add r10, zero, zero
    pause
    lea ivb, IvtImage
    load ivl, 13
    ei
    int #10

    load r11, [NestCountQuad]
    cmp r11, #3
    jne BadOutcome
    load r10, 0xECEXEC00
    hlt

BadOutcome:
    load r10, 0
    hlt

NestHandler0:
    load r12, [NestCountQuad]
    add r12, r12, #1
    store r12, [NestCountQuad]
    int #11
    iret

NestHandler1:
    load r12, [NestCountQuad]
    add r12, r12, #1
    store r12, [NestCountQuad]
    int #12
    iret

NestHandler2:
    load r12, [NestCountQuad]
    add r12, r12, #1
    store r12, [NestCountQuad]
    iret
