// VirtIO-style block device MMIO smoke test (headless).
// Run: bvm -e 1 BlockDemo.bin

.section .text, "x", full
.text
_start:
    load r1, [0x1000180]
    cmp r1, #0
    je BadOutcome

    load r2, 0xDEADBEEFCAFEBABE
    store r2, [0x2000]

    load r3, #0x2000
    store r3, [0x10001A0]
    load r4, #0
    store r4, [0x1000198]
    load r5, #2
    store r5, [0x1000190]

    load r6, [0x10001A8]
    cmp r6, #1
    jne BadOutcome

    load r7, #0x3000
    store r7, [0x10001A0]
    load r8, #1
    store r8, [0x1000190]

    load r9, [0x10001A8]
    cmp r9, #1
    jne BadOutcome

    load r10, [0x3000]
    cmp r10, r2
    jne BadOutcome

    load r10, 0xC0FFEE04
    hlt

BadOutcome:
    load r10, 0
    hlt
