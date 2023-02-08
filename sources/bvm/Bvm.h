#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <cstring>
#include <sys/types.h>
#include <vector>

extern uint64_t RAM[];
struct CPU {
    uint64_t r0, r1, r2, r3, r4, r5, r6, r7;
    __uint128_t x0, x1, x2, x3;
    uint64_t ic, sp, fr;
    bool IsRegister(uint64_t Value);
    uint64_t ReadRegister(uint64_t Register);
    void WriteRegister(uint64_t Register, uint64_t Value);
    void Fetch();
    uint64_t DecodeAndExecute(uint64_t Instruction);
    CPU(const char *ROM) {
        FILE *ROMFile = fopen(ROM, "rb");
        if(!ROMFile) return;
        fseek(ROMFile, 0, SEEK_END);
        long Size = ftell(ROMFile);
        uint64_t *Buffer = new uint64_t[Size];
        fread(Buffer, 1, Size, ROMFile);
        fclose(ROMFile);
        memcpy(&RAM[0], Buffer, sizeof(uint64_t));
        ic = RAM[0];
    }
};