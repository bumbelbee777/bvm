#pragma once

#include "Shared.h"

#include <cstdint>

/** FSX / SSX index normalization and access (architected codes or 0..15). */
uint8_t NormalizeFsxIndex(uint8_t Code);
uint8_t NormalizeSsxIndex(uint8_t Code);

double ReadFsReg(const Cpu& Vm, uint8_t Index);
void WriteFsReg(Cpu& Vm, uint8_t Index, double Value);

Uint128 ReadXReg(const Cpu& Vm, uint8_t Index);
void WriteXReg(Cpu& Vm, uint8_t Index, Uint128 Value);

Uint128& AccRegRef(Cpu& Vm, uint8_t Index);
