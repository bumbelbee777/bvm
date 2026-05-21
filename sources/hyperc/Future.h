#pragma once

#include "Preprocessor.h"

#include <string>

namespace HyperC {

/** Placeholders for post-M1 HyperC features (M2+). */
struct TemplateRegistry {
    void RegisterTemplate(const std::string& /*Name*/) {}
};
struct InterfaceTable {
    void DeclareInterface(const std::string& /*Name*/) {}
};
struct OverloadSet {
    void AddOverload(const std::string& /*Name*/) {}
};
struct AutoPtrScope {
    void EnterScope() {}
    void ExitScope() {}
};

struct FutureContext {
    TemplateRegistry Templates;
    InterfaceTable Interfaces;
    OverloadSet Overloads;
    AutoPtrScope AutoPtr;
    Preprocessor Pp;
    PreprocessorContext PpCtx;
};

} // namespace HyperC
