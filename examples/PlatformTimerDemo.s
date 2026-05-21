// Platform timer counter smoke test (headless).
// Run: bvm -e 1 PlatformTimerDemo.bin

.section .text, "x", full
.data
    StartCounterQuad: .quad 0

.text
_start:
    load r1, [0x1000200]
    store r1, [StartCounterQuad]

    load r2, #5000
WaitLoop:
    wait
    sub r2, r2, #1
    cmp r2, #0
    jne WaitLoop

    load r3, [0x1000200]
    load r4, [StartCounterQuad]
    cmp r3, r4
    jgt TimerAdvanced
    jmp BadOutcome
TimerAdvanced:

    load r10, 0xC0FFEE07
    hlt

BadOutcome:
    load r10, 0
    hlt
