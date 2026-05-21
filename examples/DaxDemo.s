// DAX smoke test: (a+b)×(c+d) with a=5, b=3, c=2, d=4 → 48
BootEntry:
    kwind #0
    dax_token k0, #5
    dax_token k1, #3
    dax_token k2, #2
    dax_token k3, #4
    dax_add k4, k0, k1
    dax_add k5, k2, k3
    dax_mul k6, k4, k5
    dax_wait k6
    dax_peek r0, k6
    cmp r0, #48
    jne fail
    hlt
fail:
    load r0, #1
    hlt
