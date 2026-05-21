// Device platform MMIO smoke test (headless).
// Verifies PlatformDevicePresent reports enabled devices.
// Run: bvm -e 1 DevicePresentDemo.bin

.section .text, "x", full
.text
_start:
    load r1, [0x1000028]
    cmp r1, #0
    je BadOutcome

    load r2, #1023
    and r2, r1, r2
    cmp r2, #1023
    jne BadOutcome

    load r10, 0xC0FFEE06
    hlt

BadOutcome:
    load r10, 0
    hlt
