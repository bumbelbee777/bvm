#include "RegFile.h"
#include "FsxProfile.h"
#include "SsxProfile.h"

#include <stdexcept>

uint8_t NormalizeFsxIndex(uint8_t Code) {
    if (Code >= 0x2C && Code <= 0x3B) {
        return static_cast<uint8_t>(Code - 0x2C);
    }
    if (Code < FsxBaselineProfile::RegisterCount) {
        return Code;
    }
    throw std::out_of_range("FSX register index out of range");
}

uint8_t NormalizeSsxIndex(uint8_t Code) {
    if (Code >= 0x40 && Code <= 0x4F) {
        return static_cast<uint8_t>(Code - 0x40);
    }
    if (Code < SsxBaselineProfile::RegisterCount) {
        return Code;
    }
    throw std::out_of_range("SSX register index out of range");
}

double ReadFsReg(const Cpu& Vm, uint8_t Index) {
    const uint8_t N = NormalizeFsxIndex(Index);
    return Vm.FRegs[N];
}

void WriteFsReg(Cpu& Vm, uint8_t Index, double Value) {
    const uint8_t N = NormalizeFsxIndex(Index);
    Vm.FRegs[N] = Value;
}

Uint128 ReadXReg(const Cpu& Vm, uint8_t Index) {
    const uint8_t N = NormalizeSsxIndex(Index);
    return Vm.XRegs[N];
}

void WriteXReg(Cpu& Vm, uint8_t Index, Uint128 Value) {
    const uint8_t N = NormalizeSsxIndex(Index);
    Vm.XRegs[N] = Value;
}

Uint128& AccRegRef(Cpu& Vm, uint8_t Index) {
    if (Index > 3) {
        throw std::out_of_range("Accumulator index out of range (A0..A3)");
    }
    switch (Index) {
        case 0: return Vm.A0;
        case 1: return Vm.A1;
        case 2: return Vm.A2;
        default: return Vm.A3;
    }
}
