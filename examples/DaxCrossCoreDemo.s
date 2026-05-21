// Simulated cross-core: SEND to core 1 slot 2, RECV on same mailbox (baseline single-core VM).
BootEntry:
    kwind #0
    dax_token k0, #99
    dax_send k0, #1, #2
    dax_recv k1, #1, #2
    dax_peek r0, k1
    cmp r0, #99
    jne fail
    hlt
fail:
    load r0, #1
    hlt
