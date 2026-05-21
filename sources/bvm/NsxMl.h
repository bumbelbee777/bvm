#pragma once

#include "../Isa.h"
#include "Shared.h"

namespace Honeycomb {

/** NSX ML training: losses, transformer, tape, SGD (see NsxMl.cpp). */
void ExecuteNsxMlOpcode(Cpu& Vm, const DecodedInsn& Insn);

} // namespace Honeycomb
