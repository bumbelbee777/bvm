// Software TLB refill (vector 15) + wired entry for the high 2 MiB mapping.

.data
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
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad 0
        .quad TlbMissHandler

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

    lea ivb, IvtImage
    load ivl, 16
    load ptb, 0x8000
    esr
    ep
    load r10, 0x00200000
    cmp r10, r1
    jne BadOutcome
    load r10, 0xFEEDFACE
    hlt

BadOutcome:
    load r10, 0
    hlt

TlbMissHandler:
    tlbinsert
    wiretlbentry #0
    iret
