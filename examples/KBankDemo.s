// Flat K-Bank smoke test: K[100] = 42, read back via KLOADI.
BootEntry:
    kwind #100
    load r0, #42
    kstorei r0, #0
    kloadi r1, #0
    cmp r1, #42
    jne fail
    hlt
fail:
    load r0, #1
    hlt
