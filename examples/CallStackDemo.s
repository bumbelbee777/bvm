// CALL / RET, indexed stack [sp], [sp+8], PUSH/POP, COPY.

.text
_start:
    load r3, 7
    load r4, 33
    call SumRegs
    cmp r15, #40
    jne BadOutcome
    load r13, 0x100
    push r13
    push r15
    load r2, [sp]
    cmp r2, r15
    jne BadOutcome
    load r14, [sp+8]
    cmp r14, r13
    jne BadOutcome
    copy r14, sp
    jmp NormalExit

BadOutcome:
    load r15, #0
    hlt

NormalExit:
    hlt

SumRegs:
    add r15, r3, r4
    ret
