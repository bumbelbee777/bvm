// USB3 xHCI MMIO smoke test (headless).
// Run: bvm -e 1 Usb3Demo.bin

.section .text, "x", full
.data
    EventRing:
        .quad 0, 0, 0, 0

.text
_start:
    load r1, [0x10000A0]
    cmp r1, #0
    je BadOutcome

    load r3, #0x02
    store r3, [0x10000A8]

    load r4, EventRing
    store r4, [0x10000B8]

    load r5, #1
    store r5, [0x10000D8]

    load r6, [0x10000B0]
    and r6, r6, #1
    cmp r6, #0
    jne BadOutcome

    load r10, 0xC0FFEE02
    hlt

BadOutcome:
    load r10, 0
    hlt
