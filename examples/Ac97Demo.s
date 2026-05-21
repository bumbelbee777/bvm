// AC97 MMIO smoke test (headless).
// Run: bvm -e 1 Ac97Demo.bin

.section .text, "x", full
.data
    PcmBuffer:
        .quad 0x3020100010203040

.text
_start:
    load r6, PcmBuffer
    store r6, [0x1000050]
    load r7, #8
    store r7, [0x1000058]

    load r8, #0x02
    store r8, [0x1000040]

    load r9, [0x1000048]
    cmp r9, #0
    je BadOutcome

    load r10, 0xC0FFEE03
    hlt

BadOutcome:
    load r10, 0
    hlt
