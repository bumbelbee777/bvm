import struct
import sys

data = open(sys.argv[1], "rb").read()
pc = 0
while pc + 8 <= len(data):
    w = struct.unpack(">Q", data[pc : pc + 8])[0]
    op = (w >> 53) & 0x7FF
    i1f = (w >> 52) & 1
    i2f = (w >> 51) & 1
    rd = (w >> 45) & 0x3F
    rs = (w >> 39) & 0x3F
    disp = ((w & 0x3FFFFFFF) << 2) >> 2
    print(f"{pc:04x}: op=0x{op:03x} rd={rd} rs={rs} i1f={i1f} i2f={i2f} disp={disp}")
    pc += 8
    if i1f:
        if pc + 8 > len(data):
            break
        imm1 = struct.unpack(">Q", data[pc : pc + 8])[0]
        print(f"      imm1=0x{imm1:016x}")
        pc += 8
    if i2f:
        if pc + 8 > len(data):
            break
        imm2 = struct.unpack(">Q", data[pc : pc + 8])[0]
        print(f"      imm2=0x{imm2:016x}")
        pc += 8
