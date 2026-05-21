#pragma once

#include "Ast.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace HyperC {
struct FutureContext;
}

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace HyperC {

struct ModuleInfo {
    std::string Name;
    std::string SourcePath;
};

class ModuleRegistry {
public:
    void RegisterModule(const std::string& Name, const std::string& SourcePath);
    bool IsPathLoaded(const std::string& SourcePath) const;
    bool HasModule(const std::string& Name) const;
    const ModuleInfo& GetModule(const std::string& Name) const;

    void ImportPath(const std::string& Path, FutureContext& Ctx, Program& Into);
    void MergeProgram(Program& Target, Program&& Imported);

private:
    std::unordered_map<std::string, ModuleInfo> ByName_;
    std::unordered_set<std::string> LoadedPaths_;
};

} // namespace HyperC
