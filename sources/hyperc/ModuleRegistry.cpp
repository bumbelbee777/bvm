#include "ModuleRegistry.h"

#include "HyperC.h"
#include "Lexer.h"
#include "Parser.h"
#include "Preprocessor.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace HyperC {

void ModuleRegistry::RegisterModule(const std::string& Name, const std::string& SourcePath) {
    if (ByName_.count(Name)) {
        throw std::runtime_error("module already registered: " + Name);
    }
    ModuleInfo Info;
    Info.Name = Name;
    Info.SourcePath = SourcePath;
    ByName_[Name] = std::move(Info);
}

bool ModuleRegistry::IsPathLoaded(const std::string& SourcePath) const {
    return LoadedPaths_.count(SourcePath) != 0;
}

bool ModuleRegistry::HasModule(const std::string& Name) const {
    return ByName_.count(Name) != 0;
}

const ModuleInfo& ModuleRegistry::GetModule(const std::string& Name) const {
    auto It = ByName_.find(Name);
    if (It == ByName_.end()) {
        throw std::runtime_error("unknown module: " + Name);
    }
    return It->second;
}

void ModuleRegistry::MergeProgram(Program& Target, Program&& Imported) {
    Target.Globals.insert(Target.Globals.end(), std::make_move_iterator(Imported.Globals.begin()),
                          std::make_move_iterator(Imported.Globals.end()));
    Target.Functions.insert(Target.Functions.end(),
                            std::make_move_iterator(Imported.Functions.begin()),
                            std::make_move_iterator(Imported.Functions.end()));
    Target.Imports.insert(Target.Imports.end(), Imported.Imports.begin(), Imported.Imports.end());
}

void ModuleRegistry::ImportPath(const std::string& Path, FutureContext& Ctx, Program& Into) {
    std::string Resolved = Path;
    if (!std::filesystem::exists(Resolved) && !Ctx.PpCtx.SourcePath.empty()) {
        const std::filesystem::path Candidate =
            std::filesystem::path(std::filesystem::path(Ctx.PpCtx.SourcePath).parent_path()) /
            Path;
        if (std::filesystem::exists(Candidate)) {
            Resolved = Candidate.string();
        }
    }
    if (LoadedPaths_.count(Resolved)) {
        return;
    }
    LoadedPaths_.insert(Resolved);

    std::ifstream InFile(Resolved);
    if (!InFile) {
        throw std::runtime_error("could not import module file: " + Path);
    }
    const std::string Source((std::istreambuf_iterator<char>(InFile)),
                             std::istreambuf_iterator<char>());

    PreprocessorContext FileCtx = Ctx.PpCtx;
    FileCtx.SourcePath = Resolved;
    const std::string Processed = Ctx.Pp.Process(Source, FileCtx);
    Ctx.PpCtx.Defines = FileCtx.Defines;

    Lexer Lex(Processed);
    Parser Parser(Lex, &Ctx);
    Program Imported = Parser.ParseProgram();
    if (Imported.ModuleName) {
        RegisterModule(*Imported.ModuleName, Resolved);
    }
    for (const std::string& NestedImport : Imported.Imports) {
        ImportPath(NestedImport, Ctx, Into);
    }
    MergeProgram(Into, std::move(Imported));
}

} // namespace HyperC
