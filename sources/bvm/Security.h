#pragma once

#include "../Isa.h"
#include "Shared.h"

namespace Honeycomb {

void ExecuteSecurityOpcode(Cpu& Vm, const DecodedInsn& Insn);
bool IsSecurityCsr(uint8_t Code);
uint64_t ReadSecurityCsr(const Cpu& Vm, uint8_t Code);
void WriteSecurityCsr(Cpu& Vm, uint8_t Code, uint64_t Value);
void SecurityCheckMemAccess(Cpu& Vm, uint64_t Addr, uint64_t Size);
void SecurityCheckSyscall(Cpu& Vm, uint64_t Number);

} // namespace Honeycomb
