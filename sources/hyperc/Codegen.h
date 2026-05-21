#pragma once

#include "Ast.h"
#include "Future.h"

struct HyperCImage;

namespace HyperC {

HyperCImage CompileProgram(const Program& Prog, FutureContext* Future = nullptr);

} // namespace HyperC
