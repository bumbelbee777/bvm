// Platform RTC wall-clock smoke test (headless).
// Run: bvm -e 1 PlatformRtcDemo.bin

.section .text, "x", full
.text
_start:
    load r1, [0x1000230]
    cmp r1, #1000000000
    jlt BadOutcome

    load r10, 0xC0FFEE08
    hlt

BadOutcome:
    load r10, 0
    hlt
