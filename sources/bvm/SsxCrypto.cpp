#include "SsxCrypto.h"
#include "RegFile.h"

#include <cstring>
#include <cstdint>
#include <stdexcept>

using namespace Honeycomb;

namespace {

constexpr int Aes128RoundCount = 10;
constexpr int Aes128RoundKeyBytes = 176;

void Uint128ToBytes(Uint128 Value, uint8_t Out[16]) {
    for (int Index = 0; Index < 16; ++Index) {
        Out[Index] = static_cast<uint8_t>(Value >> (120 - 8 * Index));
    }
}

Uint128 BytesToUint128(const uint8_t In[16]) {
    Uint128 Value = 0;
    for (int Index = 0; Index < 16; ++Index) {
        Value |= static_cast<Uint128>(In[Index]) << (120 - 8 * Index);
    }
    return Value;
}

// AES S-box / inverse (FIPS-197).
static const uint8_t Sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7,
    0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf,
    0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5,
    0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
    0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e,
    0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
    0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef,
    0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff,
    0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d,
    0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee,
    0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5,
    0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e,
    0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e,
    0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55,
    0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16};

static const uint8_t InvSbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3,
    0xd7, 0xfb, 0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44,
    0xc4, 0xde, 0xe9, 0xcb, 0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c,
    0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e, 0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
    0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25, 0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68,
    0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92, 0x6c, 0x70, 0x48, 0x50,
    0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84, 0x90, 0xd8,
    0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13,
    0x8a, 0x6b, 0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce,
    0xf0, 0xb4, 0xe6, 0x73, 0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9,
    0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e, 0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
    0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b, 0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2,
    0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4, 0x1f, 0xdd, 0xa8, 0x33,
    0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f, 0x60, 0x51,
    0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53,
    0x99, 0x61, 0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63,
    0x55, 0x21, 0x0c, 0x7d};

static const uint8_t Rcon[11] = {0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

uint8_t Xtime(uint8_t X) { return static_cast<uint8_t>((X << 1) ^ (((X >> 7) & 1) * 0x1b)); }

void SubBytes(uint8_t State[16]) {
    for (int Index = 0; Index < 16; ++Index) {
        State[Index] = Sbox[State[Index]];
    }
}

void InvSubBytes(uint8_t State[16]) {
    for (int Index = 0; Index < 16; ++Index) {
        State[Index] = InvSbox[State[Index]];
    }
}

void ShiftRows(uint8_t State[16]) {
    uint8_t Temp = State[1];
    State[1] = State[5];
    State[5] = State[9];
    State[9] = State[13];
    State[13] = Temp;
    Temp = State[2];
    State[2] = State[10];
    State[10] = Temp;
    Temp = State[6];
    State[6] = State[14];
    State[14] = Temp;
    Temp = State[3];
    State[3] = State[15];
    State[15] = State[11];
    State[11] = State[7];
    State[7] = Temp;
}

void InvShiftRows(uint8_t State[16]) {
    uint8_t Temp = State[13];
    State[13] = State[9];
    State[9] = State[5];
    State[5] = State[1];
    State[1] = Temp;
    Temp = State[2];
    State[2] = State[10];
    State[10] = Temp;
    Temp = State[6];
    State[6] = State[14];
    State[14] = Temp;
    Temp = State[3];
    State[3] = State[7];
    State[7] = State[11];
    State[11] = State[15];
    State[15] = Temp;
}

void MixColumns(uint8_t State[16]) {
    for (int Column = 0; Column < 4; ++Column) {
        const int Base = Column * 4;
        const uint8_t A = State[Base];
        const uint8_t B = State[Base + 1];
        const uint8_t C = State[Base + 2];
        const uint8_t D = State[Base + 3];
        const uint8_t E = A ^ B ^ C ^ D;
        const uint8_t Xa = Xtime(A ^ B);
        const uint8_t Xb = Xtime(B ^ C);
        const uint8_t Xc = Xtime(C ^ D);
        const uint8_t Xd = Xtime(D ^ A);
        State[Base] ^= E ^ Xa;
        State[Base + 1] ^= E ^ Xb;
        State[Base + 2] ^= E ^ Xc;
        State[Base + 3] ^= E ^ Xd;
    }
}

uint8_t Multiply(uint8_t X, uint8_t Y) {
    uint8_t Product = 0;
    for (int Bit = 0; Bit < 8; ++Bit) {
        if ((Y & 1) != 0) {
            Product ^= X;
        }
        const bool High = (X & 0x80) != 0;
        X = static_cast<uint8_t>(X << 1);
        if (High) {
            X ^= 0x1b;
        }
        Y >>= 1;
    }
    return Product;
}

void InvMixColumns(uint8_t State[16]) {
    for (int Column = 0; Column < 4; ++Column) {
        const int Base = Column * 4;
        const uint8_t A = State[Base];
        const uint8_t B = State[Base + 1];
        const uint8_t C = State[Base + 2];
        const uint8_t D = State[Base + 3];
        State[Base] = Multiply(A, 0x0e) ^ Multiply(B, 0x0b) ^ Multiply(C, 0x0d) ^
                      Multiply(D, 0x09);
        State[Base + 1] = Multiply(A, 0x09) ^ Multiply(B, 0x0e) ^ Multiply(C, 0x0b) ^
                          Multiply(D, 0x0d);
        State[Base + 2] = Multiply(A, 0x0d) ^ Multiply(B, 0x09) ^ Multiply(C, 0x0e) ^
                          Multiply(D, 0x0b);
        State[Base + 3] = Multiply(A, 0x0b) ^ Multiply(B, 0x0d) ^ Multiply(C, 0x09) ^
                          Multiply(D, 0x0e);
    }
}

void AddRoundKey(uint8_t Round, uint8_t State[16], const uint8_t* RoundKey) {
    for (int Index = 0; Index < 16; ++Index) {
        State[Index] ^= RoundKey[Round * 16 + Index];
    }
}

void KeyExpansion(const uint8_t Key[16], uint8_t* RoundKey) {
    std::memcpy(RoundKey, Key, 16);
    uint32_t Words = 4;
    while (Words < 44) {
        uint32_t Temp = (static_cast<uint32_t>(RoundKey[(Words - 1) * 4]) << 24) |
                        (static_cast<uint32_t>(RoundKey[(Words - 1) * 4 + 1]) << 16) |
                        (static_cast<uint32_t>(RoundKey[(Words - 1) * 4 + 2]) << 8) |
                        static_cast<uint32_t>(RoundKey[(Words - 1) * 4 + 3]);
        if (Words % 4 == 0) {
            const uint8_t Rotated[4] = {
                static_cast<uint8_t>(Temp >> 16),
                static_cast<uint8_t>(Temp >> 8),
                static_cast<uint8_t>(Temp),
                static_cast<uint8_t>(Temp >> 24)};
            Temp = (static_cast<uint32_t>(Sbox[Rotated[0]]) << 24) |
                   (static_cast<uint32_t>(Sbox[Rotated[1]]) << 16) |
                   (static_cast<uint32_t>(Sbox[Rotated[2]]) << 8) |
                   static_cast<uint32_t>(Sbox[Rotated[3]]);
            Temp ^= static_cast<uint32_t>(Rcon[Words / 4]) << 24;
        }
        const uint32_t Prev =
            (static_cast<uint32_t>(RoundKey[(Words - 4) * 4]) << 24) |
            (static_cast<uint32_t>(RoundKey[(Words - 4) * 4 + 1]) << 16) |
            (static_cast<uint32_t>(RoundKey[(Words - 4) * 4 + 2]) << 8) |
            static_cast<uint32_t>(RoundKey[(Words - 4) * 4 + 3]);
        Temp ^= Prev;
        RoundKey[Words * 4] = static_cast<uint8_t>(Temp >> 24);
        RoundKey[Words * 4 + 1] = static_cast<uint8_t>(Temp >> 16);
        RoundKey[Words * 4 + 2] = static_cast<uint8_t>(Temp >> 8);
        RoundKey[Words * 4 + 3] = static_cast<uint8_t>(Temp);
        ++Words;
    }
}

void Cipher(uint8_t State[16], const uint8_t* RoundKey, int RoundCount) {
    AddRoundKey(0, State, RoundKey);
    for (int Round = 1; Round < RoundCount; ++Round) {
        SubBytes(State);
        ShiftRows(State);
        MixColumns(State);
        AddRoundKey(Round, State, RoundKey);
    }
    SubBytes(State);
    ShiftRows(State);
    AddRoundKey(RoundCount, State, RoundKey);
}

void InvCipher(uint8_t State[16], const uint8_t* RoundKey, int RoundCount) {
    AddRoundKey(RoundCount, State, RoundKey);
    for (int Round = RoundCount - 1; Round > 0; --Round) {
        InvShiftRows(State);
        InvSubBytes(State);
        AddRoundKey(Round, State, RoundKey);
        InvMixColumns(State);
    }
    InvShiftRows(State);
    InvSubBytes(State);
    AddRoundKey(0, State, RoundKey);
}

uint64_t SplitMix64(uint64_t& State) {
    State += 0x9E3779B97F4A7C15ULL;
    uint64_t Z = State;
    Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBULL;
    return Z ^ (Z >> 31);
}

int ResolveAesRoundCount(const DecodedInsn& Insn) {
    if (!Insn.Imm1Flag) {
        return Aes128RoundCount;
    }
    const int Rounds = static_cast<int>(Insn.Imm1 & 0xFF);
    if (Rounds == 0 || Rounds == Aes128RoundCount) {
        return Aes128RoundCount;
    }
    throw std::runtime_error("Baseline BVM XCRYPT/XDECRYPT support AES-128 (10 rounds) only");
}

} // namespace

namespace Honeycomb::SsxCrypto {

Uint128 Aes128Encrypt(Uint128 Block, Uint128 Key) {
    uint8_t State[16];
    uint8_t KeyBytes[16];
    uint8_t RoundKey[Aes128RoundKeyBytes];
    Uint128ToBytes(Block, State);
    Uint128ToBytes(Key, KeyBytes);
    KeyExpansion(KeyBytes, RoundKey);
    Cipher(State, RoundKey, Aes128RoundCount);
    return BytesToUint128(State);
}

Uint128 Aes128Decrypt(Uint128 Block, Uint128 Key) {
    uint8_t State[16];
    uint8_t KeyBytes[16];
    uint8_t RoundKey[Aes128RoundKeyBytes];
    Uint128ToBytes(Block, State);
    Uint128ToBytes(Key, KeyBytes);
    KeyExpansion(KeyBytes, RoundKey);
    InvCipher(State, RoundKey, Aes128RoundCount);
    return BytesToUint128(State);
}

Uint128 XgenFromSeed(uint64_t Seed) {
    uint64_t State = Seed;
    const uint64_t Lo = SplitMix64(State);
    const uint64_t Hi = SplitMix64(State);
    return (static_cast<Uint128>(Hi) << 64) | Lo;
}

Uint128 XgenFromRegister(Uint128 Input) {
    const uint64_t Lo = static_cast<uint64_t>(Input);
    const uint64_t Hi = static_cast<uint64_t>(Input >> 64);
    uint64_t State = Lo ^ (Hi * 0xD6E8FEB86659FD93ULL);
    const uint64_t OutLo = SplitMix64(State);
    const uint64_t OutHi = SplitMix64(State);
    return (static_cast<Uint128>(OutHi) << 64) | OutLo;
}

void Execute(Cpu& Vm, const DecodedInsn& Insn) {
    switch (Insn.Op) {
        case Opcode::XCRYPT: {
            const int Rounds = ResolveAesRoundCount(Insn);
            if (Rounds != Aes128RoundCount) {
                throw std::runtime_error("Unsupported AES round count");
            }
            const Uint128 Block = ReadXReg(Vm, Insn.Rs);
            const Uint128 Key = ReadXReg(Vm, Insn.Rt);
            WriteXReg(Vm, Insn.Rd, Aes128Encrypt(Block, Key));
            return;
        }
        case Opcode::XDECRYPT: {
            const int Rounds = ResolveAesRoundCount(Insn);
            if (Rounds != Aes128RoundCount) {
                throw std::runtime_error("Unsupported AES round count");
            }
            const Uint128 Block = ReadXReg(Vm, Insn.Rs);
            const Uint128 Key = ReadXReg(Vm, Insn.Rt);
            WriteXReg(Vm, Insn.Rd, Aes128Decrypt(Block, Key));
            return;
        }
        case Opcode::XGEN:
            if (Insn.Imm1Flag) {
                WriteXReg(Vm, Insn.Rd, XgenFromSeed(Insn.Imm1));
            } else {
                WriteXReg(Vm, Insn.Rd, XgenFromRegister(ReadXReg(Vm, Insn.Rs)));
            }
            return;
        default:
            throw std::runtime_error("Not an SSX crypto opcode");
    }
}

} // namespace Honeycomb::SsxCrypto
