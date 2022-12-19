#include <iostream>
#include <cstdio>
#include <Bvm.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#ifdef __linux__
#include <sys/io.h>
#elif _WIN32
#include <conio.h>
#endif

using namespace std;
using namespace Bvm::RAM;

namespace Bvm {
    namespace RAM {
        uint64_t TotalRAM[0x70000];
    }

    namespace CPU {
        using namespace Bvm::RAM;
        bool IsRunning = false;
    
        uint64_t r1  = 0;        //register 1
        uint64_t r2  = 0;        //register 2
        uint64_t r3  = 0;        //register 3
        uint64_t sp  = 0x20695;   //stack pointer
        uint64_t ic  = 0x00000;   //instruction counter
        uint64_t zf  = 0;        //zero flag

        void StartEmulator(vector<uint64_t> ROM) {
            if(IsRunning) {
                cerr <<"Tried to start CPU while it's running, exiting..." << endl;
                throw 1;
            }
            memset(TotalRAM, 0, 0x70000);
            for(int i = 0x20695; i <= 0x70000; i++) {
                if(i - 0x20695 > ROM.size()) break;
                TotalRAM[i] = ROM[i - 0x20695];
            }
            IsRunning = true;
            cout << "Emulator is up and running!" << endl;
            ClockCycle();
        }

        void HaltEmulator(void) {
            if(!IsRunning) {
                cerr << "Tried to halt emulator while it's not running." << endl;
                throw 1;
            }
            r1 = 0;
            r2 = 0;
            IsRunning = false;
            printf("Emulator halted!\n");
        }

        void ClockCycle(void) {
            if(!IsRunning) {
                cerr << "tried to cycle while CPU isn't running, exiting..." << endl;
                throw 1;
            }

            uint64_t CurrentInstruction = TotalRAM[ic];
            uint64_t ReturnAddress;

            switch(CurrentInstruction) {
                case 0x00: { //hlt
                    HaltEmulator();
                    break;
                }

                case 0x01: { //nop
                    ic++;
                    break;
                }

                case 0x02: { //mov
                    uint64_t Source = RAM::TotalRAM[ic + 1], Destination = RAM::TotalRAM[ic + 2];
                    if(Destination > 0x70000) {
                        cerr << "MOV tried to write to " << Destination << "which is beyond availble RAM, exiting..." << endl;
                        throw 1;
                    } else {
                        RAM::TotalRAM[Source] = RAM::TotalRAM[Destination];
                    }
                    break;
                }

                case 0x03: { //and
                    r3 = r1 & r2;
                    ic++;
                    break;
                }

                case 0x04: { //or 
                    r3 = r1 | r2;
                    ic++;
                    break;
                }

                case 0x05: { //not 
                    r3 = !(r1 | r2);
                    ic++;
                    break;
                }

                case 0x06: { //xor
                    r3 = r1 ^ r2;
                    ic++;
                    break;
                }

                case 0x07: { //add
                    r1 += r2;
                    ic++;
                    break;
                }

                case 0x08: { //sub
                    r2 -= r1;
                    ic++;
                    break;
                }

                case 0x09: { //mul
                    r1 *= r2;
                    ic++;
                    break;
                }

                case 0x0a: { //div
                    r2 /= r1;
                    ic++;
                    break;
                }

                case 0x0b: { //sqrt
                    sqrt(r1);
                    ic++;
                    break;
                }

                case 0x0c: { //jmp
                    ic += RAM::TotalRAM[ic + 1];
                    break;
                }

                case 0x0d: { //je
                    uint8_t Address = TotalRAM[ic + 1];
                    if(r1 == r2) ic = Address;
                    else ic++;                    
                    break;
                }

                case 0x0e: { //jne
                    if(r1 != r2) ic = RAM::TotalRAM[ic + 1];
                    break;
                }
                
                case 0x0f: { //jz
                    if(zf == 0) {
                        uint64_t Address = TotalRAM[ic + 1];
                        ic = Address;
                    }
                    break;
                }

                case 0x10: { //jnz
                    if(zf != 0) {
                        uint64_t Address = TotalRAM[ic + 1];
                        ic = Address;
                    }
                    else break;                    
                }

                case 0x11: { //in
                    #ifdef __linux__
                    inb(RAM::TotalRAM[ic+2]);
                    #elif _WIN32
                    _inb(RAM::TotalRAM[ic+2]);
                    #endif
                    ic++;
                    break;
                }

                case 0x12: { //out
                    #ifdef __linux__
                    outb(r3, RAM::TotalRAM[ic+2]);
                    #elif _WIN32
                    _outb(r3, RAM::TotalRAM[ic+2]);
                    #endif
                    ic++;
                    break;
                }

                case 0x13: { //push
                    if(sp == 0x70000) {
                        cerr << "Stack overflow." << endl;
                        throw 1;
                    }
                    RAM::TotalRAM[sp++] = RAM::TotalRAM[ic + 1];
                    ic++;
                    break;
                }

                case 0x14: { //pop
                    if(sp == 0x00000) {
                        cerr << "Stack underflow." << endl;
                        throw 1;
                    }
                    RAM::TotalRAM[sp--] = RAM::TotalRAM[ic + 1];
                    ic++; 
                    break;
                }

                case 0x15: { //rsh
                    r3 = r1 << r2; 
                    ic++;
                    break;
                }

                case 0x16: { //lsh 
                    r3 = r1 >> r2;
                    ic++;
                    break;
                }

                case 0x17: { //load 
                    r3 = RAM::TotalRAM[ic + 1];
                    ic++;
                    break;
                }

                case 0x18: { //store 
                    r3 = RAM::TotalRAM[ic + 1];
                    ic++;
                    break;
                }

                case 0x19: { //call
                    ReturnAddress = RAM::TotalRAM[ic + 2];
                    ic = RAM::TotalRAM[ic + 1];
                    break;
                }

                case 0x20: {//ret
                    ic = ReturnAddress;
                    break;
                }

                default: {
                    cerr << "Got unkown instruction " << CurrentInstruction << ", exiting..." << endl;
                    throw 1;
                }
                if(RAM::TotalRAM[ic] > 0x70000) { 
                    cout << "Program done executing!" << endl; 
                    exit(0);
                }
            }
        }
    }
}