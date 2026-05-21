// UEFI GOP display smoke test (requires --host-io SDL build).
// Run: bvm -e 1 --host-io HostIoDemo.bin

.section .text, "x", full
.text
_start:
    load r1, 0x00FF00FF
    store r1, [0x00F00000]

    load r2, #0x03
    store r2, [0x1000138]

    load r10, 0xC0FFEE01
    hlt
