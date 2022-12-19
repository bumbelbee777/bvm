# Honeycomb revision 0.1.
### Introduction.
Honeycomb is a 64 bit turing-complete computer architecture designed to be simple and compact yet robust and reliable.
### Registers.
The Honeycomb architecture contains a few registers to play around with and they are:
- R1-3: R1 is used to store the result of addition and multiplication, R2 is used to store division and subtraction and R3 is used to store bitwise and logical operations (and, not, or, right shift and so on...).
- SP: The stack pointer, it points to the top of the stack just like in other CPUs.
- IC: The instruction counter, it points to the memory address of the current instruction.
- ZF: The zero flag register, it's set to one when the result of the previous instruction is zero, otherwise it's zero by default.
### Instruction set.
The honeycomb instruction set contains the following instructions:
- NOP: No operation, its opcode is 0x00.
- HLT: Halts the processor, its opcode is 0x01.
- MOV: Moves the first parameter to the memory address of the next parameter (mov $0x69, $0x42), its opcode is 0x02.
- AND: Performs a bitwise AND operation on the R1 and R2 registers and stores the result in the R3 register, its opcode is 0x03.
- NOT: Performs a bitwise NOT operation on the R1 and R2 registers and stores the result in the R3 register, its opcode is 0x04.
- OR:  Performs a bitwise OR operation on the R1 and R2 registers and stores the result in the R3 register, its opcode is 0x05.
- XOR: Performs a bitwise XOR (Exclusive OR) operation on the R1 and R2 registers and stores the result in the R3 register, its opcode is 0x06.
- ADD: Adds the R1 and R2 registers and stores the result in the R1 register, its opcode is 0x07.
- SUB: Subtracts the R1 and R2 registers and stores the result in the R2 register, its opcode is 0x08.
- MUL: Multiplies the R1 and R2 registers and stores the result in the R1 register, its opcode is 0x09.
- DIV: Divides the R1 and R2 registers and stores the result in the R2 register, its opcode is 0x0a.
- SQRT: Square roots the R1 and R2 registers and stores the result in the R3 register, its opcode is 0x0b.
- JMP: Jump to the specified label, its opcode is 0x0c.
- JE: Jump if R1 and R2 are equal, its opcode is 0x0d.
- JNE: Jump if R1 and R2 are not equal, its opcode is 0x0e.
- JZ: Jump if the zero flag is set, its opcode is 0x0f.
- JNZ: Jump if the zero flag is set, its opcode is 0x10.
- IN: Read a byte from a port and store it in the R3 register, its opcode is 0x11.
- OUT: Write a byte to a port and store it in the R3 register, its opcode is 0x12.
- RSH: Perform a right shift on the R1 and R2 registers, its opcode is 0x13.
- LSH: Perform a left shift on the R1 and R2 registers, its opcode is 0x14.
- PUSH: Push a memory address onto the stack, its opcode is 0x15.
- POP: Pop a memory address from the stack, its opcode is 0x16.
- LOAD: Load a memory address onto the R3 register, its opcode is 0x17.
- STORE: Store a memory address onto the R3 register, its opcode is 0x18.
- CALL: Transfer control to a subroutine, its opcode is 0x19.
- RET: Return from the current subroutine, its opcode is 0x20.
Honeycomb instructions are executed in a fetch-decode-execute cycle
### Memory Layout.
The honeycomb architecture memory is 64-bits wide and its total size is 0x70000, it's split into various parts like this:
- 0x00000-0x16666: Free memory.
- 0x16667-0x20694: Stack memory.
- 0x20695-0x70000: ROM memory.