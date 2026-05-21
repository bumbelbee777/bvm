// Hybrid compact boot + full HPC stub via CALL/RET mode switching.
// Success: R10 = 0xC0FE0001

.section .boot, "x", compact
BootEntry:
    movs r1
    cmpi r1, #0
    bnz r1, DoInit
    ldi r10, #0
    hlt
DoInit:
    call full_init
    hlt

.section .hpc, "x", full
full_init:
    load r10, #0xC0FE0001
    ret
