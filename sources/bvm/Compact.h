#pragma once

#include "../Isa.h"

namespace Honeycomb {

DecodedInsn DecodeCompactInsn(uint16_t Word, uint64_t Pc);

} // namespace Honeycomb
