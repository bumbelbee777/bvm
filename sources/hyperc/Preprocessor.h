#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace HyperC {

struct MacroDef {
    std::string Body;
    std::vector<std::string> Params;
    bool FunctionLike = false;
};

struct PreprocessorContext {
    std::string SourcePath;
    std::vector<std::string> IncludePaths;
    std::unordered_map<std::string, MacroDef> Defines;
    std::vector<std::string> IncludeStack;
};

class Preprocessor {
public:
    std::string Process(const std::string& Source, PreprocessorContext& Ctx) const;

private:
    std::string ProcessFile(const std::string& Path, PreprocessorContext& Ctx) const;
    std::string ResolveInclude(const std::string& Path, bool System,
                               const PreprocessorContext& Ctx) const;
    std::string ExpandText(const std::string& Text, PreprocessorContext& Ctx, int Line) const;
    std::string ExpandLine(const std::string& Line, PreprocessorContext& Ctx, int LineNo) const;
    int64_t EvalIfExpr(const std::string& Expr, const PreprocessorContext& Ctx, int Line) const;
    static std::string Trim(const std::string& Text);
    static bool StartsWith(const std::string& Text, const std::string& Prefix);
    static std::vector<std::string> SplitArgs(const std::string& Text);
};

} // namespace HyperC
