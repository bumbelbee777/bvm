// NS16550 UART MMIO smoke test (headless).
// Run: bvm -e 1 UartDemo.bin

.section .text, "x", full
.text
_start:
    load r1, [0x10001C8]
    and r1, r1, #32
    cmp r1, #32
    jne BadOutcome

    load r2, 0x55415254
    store r2, [0x10001C0]

    load r10, 0xC0FFEE05
    hlt

BadOutcome:
    load r10, 0
    hlt
