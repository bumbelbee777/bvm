// PUSH with an immediate word, then POP into a GPR.

.text
_start:
    push #0x4242424242424242
    pop r14
    cmp r14, #0x4242424242424242
    jne BadOutcome
    hlt

BadOutcome:
    load r14, 0
    hlt
