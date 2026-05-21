// Iterative Fibonacci: after ten iterations GPR r12 holds fib(10) == 55.

.text
_start:
    load r12, 0               // F(k)
    load r13, 1               // F(k+1)
    load r8, 10

FibIterate:
    cmp r8, #0
    je FibFinish
    add r14, r12, r13         // Next value
    copy r12, r13
    copy r13, r14
    dec r8
    jmp FibIterate

FibFinish:
    cmp r12, #55
    jne BadExit
    hlt

BadExit:
    load r14, 0xFFFFFFFFFFFFFFFF
    hlt
