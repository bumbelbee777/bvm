// DAX_STREAM: first 8 bytes of SKB line 0 → token k0.
BootEntry:
    kwind #0
    add r0, r12, #0x1111111111111111
    skbzero #0
    skbstore r0, #0
    dax_stream k0, #0, #8
    dax_wait k0
    dax_peek r0, k0
    cmp r0, #0x1111111111111111
    jne fail
    hlt
fail:
    load r0, #1
    hlt
