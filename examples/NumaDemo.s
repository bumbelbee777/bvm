// NUMA remote domains: STORE/LOAD remote DRAM + SKB, NUMASTREAM, NUMABIND, HINT.

.text
BootEntry:
    add r0, r12, #0x4242

    store r0, 0x5000000000000000
    load r1, 0x5000000000000000
    cmp r1, r0
    jne fail

    store r0, 0x3000000000000000
    numabind #0, #0
    kloadi r3, #0
    cmp r3, r0
    jne fail

    numastream #0, #8, 0x5000000000000000
    kloadi r4, #0
    cmp r4, r0
    jne fail

    hint #0x61, #0
    hintbarrier
    numafence
    hlt

fail:
    add r0, r12, #1
    hlt
