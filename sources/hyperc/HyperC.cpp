#include "HyperC.h"

#include "Codegen.h"
#include "Future.h"
#include "Lexer.h"
#include "ModuleRegistry.h"
#include "Parser.h"
#include "Preprocessor.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {

std::string ReadSourceFile(const std::string& InputPath) {
    std::ifstream InFile(InputPath);
    if (!InFile) {
        throw std::runtime_error("Could not open input file: " + InputPath);
    }
    return std::string((std::istreambuf_iterator<char>(InFile)),
                       std::istreambuf_iterator<char>());
}

} // namespace

HyperCImage CompileSource(const std::string& Source, const std::string& SourceName) {
    HyperC::FutureContext Future;
    Future.PpCtx.SourcePath = SourceName;
    const std::string Processed = Future.Pp.Process(Source, Future.PpCtx);

    HyperC::ModuleRegistry Registry;
    HyperC::Lexer Lex(Processed);
    HyperC::Parser Parser(Lex, &Future);
    HyperC::Program Prog = Parser.ParseProgram();

    if (Prog.ModuleName) {
        Registry.RegisterModule(*Prog.ModuleName, SourceName);
    }
    for (const std::string& ImportPath : Prog.Imports) {
        if (Registry.HasModule(ImportPath)) {
            const HyperC::ModuleInfo& Info = Registry.GetModule(ImportPath);
            Registry.ImportPath(Info.SourcePath, Future, Prog);
            continue;
        }
        Registry.ImportPath(ImportPath, Future, Prog);
    }

    bool HasMain = false;
    for (const HyperC::FuncDecl& Fn : Prog.Functions) {
        if (Fn.Name == "main") {
            HasMain = true;
            break;
        }
    }
    if (!HasMain) {
        throw std::runtime_error("HyperC program requires an int main() entry point");
    }
    return HyperC::CompileProgram(Prog, &Future);
}

HyperCImage CompileFile(const std::string& InputPath) {
    const std::filesystem::path Path(InputPath);
    const std::string Source = ReadSourceFile(InputPath);
    HyperC::FutureContext Future;
    Future.PpCtx.SourcePath = Path.string();
    Future.PpCtx.IncludePaths.push_back(Path.has_parent_path() ? Path.parent_path().string()
                                                               : ".");
    const std::string Processed = Future.Pp.Process(Source, Future.PpCtx);

    HyperC::ModuleRegistry Registry;
    HyperC::Lexer Lex(Processed);
    HyperC::Parser Parser(Lex, &Future);
    HyperC::Program Prog = Parser.ParseProgram();

    if (Prog.ModuleName) {
        Registry.RegisterModule(*Prog.ModuleName, Path.string());
    }
    for (const std::string& ImportPath : Prog.Imports) {
        if (Registry.HasModule(ImportPath)) {
            const HyperC::ModuleInfo& Info = Registry.GetModule(ImportPath);
            Registry.ImportPath(Info.SourcePath, Future, Prog);
            continue;
        }
        Registry.ImportPath(ImportPath, Future, Prog);
    }

    bool HasMain = false;
    for (const HyperC::FuncDecl& Fn : Prog.Functions) {
        if (Fn.Name == "main") {
            HasMain = true;
            break;
        }
    }
    if (!HasMain) {
        throw std::runtime_error("HyperC program requires an int main() entry point");
    }
    return HyperC::CompileProgram(Prog, &Future);
}

void WriteImage(const HyperCImage& Image, const std::string& OutputPath) {
    std::ofstream OutFile(OutputPath, std::ios::binary);
    if (!OutFile) {
        throw std::runtime_error("Could not open output file: " + OutputPath);
    }
    OutFile.write(reinterpret_cast<const char*>(Image.Bytes.data()),
                  static_cast<std::streamsize>(Image.Bytes.size()));
}
