// SKB smoke test: line 1 holds 42; atomic add 10; K-Bank bind alias read.
// Use R12 as a zero source (reset clears GPRs); `load rd, #imm` is ADD rd, R0, #imm
// and R0 is not architecturally hard-zero once written.
BootEntry:
    add r0, r12, #42
    skbzero #1
    skbstore r0, #1
    skbload r1, #1
    cmp r1, #42
    jne fail

    add r10, r12, #10
    skbatadd r10, #1
    skbload r1, #1
    cmp r1, #52
    jne fail

    skbbind #1, #0
    kloadi r2, #0
    cmp r2, #52
    jne fail

    hlt
fail:
    add r0, r12, #1
    hlt
