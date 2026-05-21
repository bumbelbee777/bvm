// CPLX vector + fused + FFT (C_FP32 in SSX). hello_test sets SsxVectorLength=4 before run.
// VCFFT4: vcfft4 CXd, CXs_lo, CXs_hi (four complexes across two cx registers).

.data
    VecLo: .quad 0x3F80000000000000
    VecLoPad: .quad 0
    VecHi: .quad 0x4000000000000000
    VecHiPad: .quad 0x000000003F800000
    VecOut: .quad 0
    VecOutPad: .quad 0

.text
BootEntry:
    xload cx0, [VecLo]
    xload cx1, [VecHi]

    vcadd cx2, cx0, cx1
    xstore cx2, [VecOut]
    load r10, [VecOut]
    cmp r10, #0x404000003F800000
    jne fail

    add r0, r12, #0x3F800000
    add r1, r12, #0
    add r2, r12, #0x40000000
    add r3, r12, #0
    cpack cr0, r0, r1
    cpack cr1, r2, r3
    cfma cr0, cr0, cr1

    vcfft2 cx0, cx0
    vcfft4 cx0, cx0, cx1

    hlt
fail:
    add r0, r12, #1
    hlt
