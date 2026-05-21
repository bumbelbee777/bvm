#pragma once

#include "Shared.h"

namespace Honeycomb::SsxCrypto {

/** AES-128 ECB single block (FIPS-197 byte order in `Uint128`). */
Uint128 Aes128Encrypt(Uint128 Block, Uint128 Key);
Uint128 Aes128Decrypt(Uint128 Block, Uint128 Key);

/** Deterministic 128-bit output from a 64-bit seed (`XGEN` immediate form). */
Uint128 XgenFromSeed(uint64_t Seed);

/** KDF-lite: expand 128-bit input into output (`XGEN` register form). */
Uint128 XgenFromRegister(Uint128 Input);

void Execute(Cpu& Vm, const DecodedInsn& Insn);

} // namespace Honeycomb::SsxCrypto
