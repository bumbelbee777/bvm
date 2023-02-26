#include <cstdint>
#include <Shared.h>

void CPU::And(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2) {
    this->WriteRegister(Destination, Immediate1 & Immediate2);
}

void CPU::Or(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2) {
    this->WriteRegister(Destination, Immediate1 | Immediate2);
}

void CPU::Not(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2) {
    this->WriteRegister(Destination, ~(Immediate1 ^ Immediate2));
}

void CPU::Xor(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2) {
    this->WriteRegister(Destination, Immediate1 ^ Immediate2);
}

void CPU::Add(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2) {
    this->WriteRegister(Destination, Immediate1 + Immediate2);
}

void CPU::Sub(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2) {
    this->WriteRegister(Destination, Immediate1 - Immediate2);
}

void CPU::Mul(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2) {
    this->WriteRegister(Destination, Immediate1 * Immediate2);
}

void CPU::Div(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2) {
    this->WriteRegister(Destination, Immediate1 / Immediate2);
}

void CPU::Cmp(uint64_t Immediate1, uint64_t Immediate2) {
    if(Immediate1 == Immediate2) this->fr |= (1ull << 1);
}

void CPU::Rsh(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2) {
    this->WriteRegister(Destination, Immediate1 << Immediate2);
}

void CPU::Lsh(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2) {
    this->WriteRegister(Destination, Immediate1 >> Immediate2);
}

void CPU::Ror(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2) {
    WriteRegister(Destination, (Immediate1 >>  Immediate2) | (Immediate1 << (64 - Immediate2)));
}

void CPU::Rol(uint64_t Destination, uint64_t Immediate1, uint64_t Immediate2) {
    WriteRegister(Destination, (Immediate1 <<  Immediate2) | (Immediate1 >> (64 - Immediate2)));
}

void CPU::Jmp(uint64_t Address) {
    ic = Address;
}

void CPU::Je(uint64_t Address) {
    if(this->fr & 0x2) ic = Address;
}

void CPU::Jne(uint64_t Address) {
    if(!(this->fr & 0x2)) ic = Address;
}

void CPU::Jz(uint64_t Address) {
    if(this->fr & 0x1) ic = Address;
}

void CPU::Jnz(uint64_t Address) {
    if(!(this->fr & 0x1)) ic = Address;
}

void CPU::Push(uint64_t Immediate) {
    RAM[sp + 1] = Immediate;
}

void CPU::Pop(uint64_t Immediate) {
    RAM[sp - 1] = Immediate;
}

void CPU::Load(uint64_t Immediate) {
    WriteRegister(RAM[ic + sizeof(uint64_t)], RAM[ic + 2 * sizeof(uint64_t)]);
}

void CPU::Store(uint64_t Immediate) {
    RAM[ic + sizeof(uint64_t)] = ReadRegister(RAM[ic + 2 * sizeof(uint64_t)]);
}

void CPU::In(uint64_t Port, uint64_t Destination) {
    if(IsRegister(Destination)) WriteRegister(Destination, (uint64_t)Ports[Port]); else Ports[Port] = Destination;
}

void CPU::Out(uint64_t Port, uint64_t Immediate) {
    Ports[Port] = Immediate;
}

void CPU::Call(uint64_t Subroutine) {
    RAM[sp + 1] = ic;
    ic = Subroutine;
}

void CPU::Ret() {
    ic = RAM[sp];
    sp--;
}