#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <cstring>
#include <sys/types.h>
#include <vector>

extern uint64_t RAM[];

extern uint8_t Ports[]; //FIXME: MMIO.

using uint128_t = unsigned __int128;

enum Instructions : uint64_t { //XXX: Convert to binary.
    NOP 	= 100,
    HLT 	= 110,
    AND 	= 120,
    OR  	= 130,
    NOT 	= 140,
    XOR 	= 150,
    ADD 	= 160,
    SUB 	= 170,
    MUL 	= 180,
    DIV 	= 190,
    MOD 	= 200,
    CMP 	= 210,
    RSH 	= 220,
    LSH 	= 230,
    ROR 	= 240,
    ROL 	= 250,
    JMP 	= 260,
    JE  	= 270,
    JNE 	= 280,
    JZ  	= 290,
    JNZ 	= 300,
    JC  	= 310,
    JNC 	= 320,
    JLT 	= 330,
    JGT 	= 340,
    JO  	= 350,
    JNO 	= 360,
    PUSH	= 370,
    POP 	= 370,
    LOAD	= 380,
    STORE	= 390,
    SWAP	= 400,
    COPY	= 410,
    INC 	= 420,
    DEC 	= 430,
    IN  	= 440,
    OUT 	= 450,
    CALL	= 460,
    RET 	= 470,
    INT 	= 480,
    IRET	= 490,
    EI  	= 500,
    DI  	= 510,
    SYSCALL	= 520,
    SYSRET	= 530,
    ATAND	= 540,
    ATOR	= 550,
    ATNOT	= 560,
    ATXOR	= 570,
    ATADD	= 580,
    ATSUB	= 590,
    ATMUL	= 600,
    ATDIV	= 610,
    ATMOD	= 620,
    ATFADD	= 630,
    ATFSUB	= 640,
    ATCAS	= 650,
    ATCMP	= 660,
    ATINC	= 670,
    ATDEC	= 680,
    ATLOAD	= 690,
    ATSTORE	= 700,
    ATSWAP	= 710,
    WAIT	= 720,
    XAND	= 730,
    OR128	= 740,
    XNOT 	= 750,
    XXOR 	= 760,
    XADD 	= 770,
    XSUB 	= 780,
    XMUL 	= 790,
    XDIV 	= 800,
    XMOD 	= 810,
    XCMP 	= 820,
    XRSH 	= 830,
    XLSH 	= 840,
    XROR 	= 850,
    XROL 	= 860,
    XLOAD 	= 870,
    XSTORE	= 880,
    XCOPY 	= 890,
    XSWAP 	= 900,
    XCRYPT 	= 910,
    XDECRYPT= 920,
    XGEN 	= 930,
    FAND 	= 940,
    FOR 	= 950,
    FNOT 	= 960,
    FXOR 	= 970,
    FADD 	= 980,
    FSUB 	= 990,
    FMUL 	= 1000,
    FDIV 	= 1010,
    FMOD 	= 1020,
    FCMP 	= 1030,
    FRSH 	= 1040,
    FLSH 	= 1050,
    FROR 	= 1060,
    FROL 	= 1070,
    FLOAD	= 1080,
    FSTORE 	= 1090,
    FCOPY 	= 1100,
    FSWAP 	= 1110,
};

class CPU {
public:
    CPU(const char *ROM) {
        FILE *ROMFile = fopen(ROM, "rb");
        if (!ROMFile) return;
        fseek(ROMFile, 0, SEEK_END);
        long Size = ftell(ROMFile);
        uint64_t Buffer[Size];
        fread(Buffer, 1, Size, ROMFile);
        fclose(ROMFile);
        memcpy(&RAM[0], Buffer, sizeof(uint64_t) * Size);
        ic = RAM[0];
        while (ic != 0b00011101) Decode(RAM[ic / sizeof(uint64_t)]);
    }
private:
    uint64_t r0, r1, r2, r3, r4, r5, r6, r7;
    uint128_t x0, x1, x2, x3, x4, x5, x6, x7;
	float f0, f1, f2, f3, f4, f5, f6, f7;
    uint64_t ic, sp, fr, ptb;
    bool IsRegister(uint64_t Value);
    bool IsRegister(uint128_t Value);
    uint64_t ReadRegister(uint64_t Register);
	uint128_t ReadRegister(uint128_t Register);
    void WriteRegister(uint64_t Register, uint64_t Value);
	void WriteRegister(uint64_t Register, uint128_t Value);
    void Fetch();
    void Fetch(int Cycles);
    uint64_t Get(int Cycles);
    void Decode(uint64_t ROM);
    void Execute(uint64_t Instruction);
    void And(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2);
    void Or(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2);
    void Not(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2);
    void Xor(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2);
    void Add(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2);
    void Sub(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2);
    void Mul(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2);
    void Div(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2);
    void Cmp(uint64_t Immediate1, uint64_t Immediate2);
    void Lsh(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2);
    void Rsh(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2);
    void Ror(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2);
    void Rol(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2);
    void Jmp(uint64_t Address);
    void Je(uint64_t Address);
	void Jne(uint64_t Address);
	void Jz(uint64_t Address);
	void Jnz(uint64_t Address);
	void Push(uint64_t Immediate);
	void Pop(uint64_t Immediate);
	void Load(uint64_t Immediate);
	void Store(uint64_t Immediate);
    void In(uint64_t Port, uint64_t Immediate);
    void Out(uint64_t Port, uint64_t Destination);
    void Call(uint64_t Subroutine);
    void Ret();
};