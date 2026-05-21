// Virtual memory: identity 2 MiB at VA 0 + VA 0x0020_0000 -> PA 0x4000 (2 MiB huge pages).

.text
_start:
    load r1, 0xCAFEBABEDEADBEEF
    store r1, 0x4000

    load r2, 0x9000
    load r3, 0x0F
    or r2, r2, r3
    store r2, 0x8000

    load r2, 0xA000
    or r2, r2, r3
    store r2, 0x9000

    load r2, 0x4F
    store r2, 0xA000
    load r2, 0x404F
    store r2, 0xA008

    load ptb, 0x8000
    ep
    load r10, 0x00200000
    cmp r10, r1
    jne BadOutcome
    hlt

BadOutcome:
    load r10, 0
    hlt
