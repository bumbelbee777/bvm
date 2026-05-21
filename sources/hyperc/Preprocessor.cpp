#include "Preprocessor.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>

namespace HyperC {

namespace {

constexpr const char* HyperCVersion = "4";

std::string ReadFileOrThrow(const std::string& Path) {
    std::ifstream InFile(Path, std::ios::binary);
    if (!InFile) {
        throw std::runtime_error("could not open file: " + Path);
    }
    return std::string((std::istreambuf_iterator<char>(InFile)),
                       std::istreambuf_iterator<char>());
}

std::string ParentDir(const std::string& Path) {
    const std::filesystem::path P(Path);
    if (P.has_parent_path()) {
        return P.parent_path().string();
    }
    return ".";
}

std::string Basename(const std::string& Path) {
    return std::filesystem::path(Path).filename().string();
}

bool IsIdentStart(char Ch) {
    return std::isalpha(static_cast<unsigned char>(Ch)) || Ch == '_';
}

bool IsIdentChar(char Ch) {
    return std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_';
}

} // namespace

std::string Preprocessor::Trim(const std::string& Text) {
    size_t Start = 0;
    while (Start < Text.size() && std::isspace(static_cast<unsigned char>(Text[Start]))) {
        ++Start;
    }
    size_t End = Text.size();
    while (End > Start && std::isspace(static_cast<unsigned char>(Text[End - 1]))) {
        --End;
    }
    return Text.substr(Start, End - Start);
}

bool Preprocessor::StartsWith(const std::string& Text, const std::string& Prefix) {
    return Text.size() >= Prefix.size() && Text.compare(0, Prefix.size(), Prefix) == 0;
}

std::vector<std::string> Preprocessor::SplitArgs(const std::string& Text) {
    std::vector<std::string> Args;
    std::string Current;
    int Depth = 0;
    for (char Ch : Text) {
        if (Ch == '(') {
            ++Depth;
            Current.push_back(Ch);
            continue;
        }
        if (Ch == ')') {
            --Depth;
            Current.push_back(Ch);
            continue;
        }
        if (Ch == ',' && Depth == 0) {
            Args.push_back(Trim(Current));
            Current.clear();
            continue;
        }
        Current.push_back(Ch);
    }
    if (!Trim(Current).empty()) {
        Args.push_back(Trim(Current));
    }
    return Args;
}

std::string Preprocessor::ExpandText(const std::string& Text, PreprocessorContext& Ctx,
                                     int Line) const {
    std::ostringstream Out;
    for (size_t I = 0; I < Text.size();) {
        if (IsIdentStart(Text[I])) {
            size_t J = I + 1;
            while (J < Text.size() && IsIdentChar(Text[J])) {
                ++J;
            }
            const std::string Ident = Text.substr(I, J - I);
            if (Ident == "__LINE__") {
                Out << Line;
                I = J;
                continue;
            }
            if (Ident == "__FILE__") {
                Out << '"' << Basename(Ctx.SourcePath) << '"';
                I = J;
                continue;
            }
            if (Ident == "__HYPERC__") {
                Out << HyperCVersion;
                I = J;
                continue;
            }
            auto It = Ctx.Defines.find(Ident);
            if (It != Ctx.Defines.end()) {
                if (It->second.FunctionLike) {
                    if (J < Text.size() && Text[J] == '(') {
                        size_t K = J + 1;
                        int Depth = 1;
                        while (K < Text.size() && Depth > 0) {
                            if (Text[K] == '(') {
                                ++Depth;
                            } else if (Text[K] == ')') {
                                --Depth;
                            }
                            ++K;
                        }
                        const std::string ArgText = Text.substr(J + 1, K - J - 2);
                        const std::vector<std::string> Actual = SplitArgs(ArgText);
                        std::string Expanded = It->second.Body;
                        for (size_t P = 0; P < It->second.Params.size(); ++P) {
                            const std::string& Param = It->second.Params[P];
                            const std::string Replacement =
                                P < Actual.size() ? Actual[P] : "";
                            size_t Pos = 0;
                            while ((Pos = Expanded.find(Param, Pos)) != std::string::npos) {
                                const bool LeftOk =
                                    Pos == 0 || !IsIdentChar(Expanded[Pos - 1]);
                                const bool RightOk = Pos + Param.size() >= Expanded.size() ||
                                                     !IsIdentChar(Expanded[Pos + Param.size()]);
                                if (LeftOk && RightOk) {
                                    Expanded.replace(Pos, Param.size(), Replacement);
                                    Pos += Replacement.size();
                                } else {
                                    Pos += Param.size();
                                }
                            }
                        }
                        Out << ExpandText(Expanded, Ctx, Line);
                        I = K;
                        continue;
                    }
                } else {
                    Out << ExpandText(It->second.Body, Ctx, Line);
                    I = J;
                    continue;
                }
            }
            Out << Ident;
            I = J;
            continue;
        }
        Out << Text[I];
        ++I;
    }
    return Out.str();
}

std::string Preprocessor::ExpandLine(const std::string& Line, PreprocessorContext& Ctx,
                                     int LineNo) const {
    return ExpandText(Line, Ctx, LineNo);
}

int64_t Preprocessor::EvalIfExpr(const std::string& RawExpr, const PreprocessorContext& Ctx,
                                 int Line) const {
    std::string Expr = Trim(RawExpr);
    auto EvalDefined = [&](size_t& Pos) -> int64_t {
        if (!StartsWith(Expr.substr(Pos), "defined")) {
            return 0;
        }
        Pos += 7;
        while (Pos < Expr.size() && std::isspace(static_cast<unsigned char>(Expr[Pos]))) {
            ++Pos;
        }
        bool Negate = false;
        if (Pos < Expr.size() && Expr[Pos] == '!') {
            Negate = true;
            ++Pos;
        }
        if (Pos >= Expr.size() || Expr[Pos] != '(') {
            throw std::runtime_error("invalid defined() expression");
        }
        ++Pos;
        size_t Start = Pos;
        while (Pos < Expr.size() && IsIdentChar(Expr[Pos])) {
            ++Pos;
        }
        const std::string Name = Expr.substr(Start, Pos - Start);
        if (Pos >= Expr.size() || Expr[Pos] != ')') {
            throw std::runtime_error("invalid defined() expression");
        }
        ++Pos;
        const int64_t Value = Ctx.Defines.count(Name) ? 1 : 0;
        return Negate ? !Value : Value;
    };

    struct Tok {
        enum Kind { NumLit, Ident, Op, LParen, RParen } Kind = NumLit;
        int64_t Number = 0;
        std::string Text;
    };
    std::vector<Tok> Tokens;
    for (size_t I = 0; I < Expr.size();) {
        while (I < Expr.size() && std::isspace(static_cast<unsigned char>(Expr[I]))) {
            ++I;
        }
        if (I >= Expr.size()) {
            break;
        }
        if (StartsWith(Expr.substr(I), "defined")) {
            Tok T;
            T.Kind = Tok::NumLit;
            T.Number = EvalDefined(I);
            Tokens.push_back(T);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(Expr[I]))) {
            size_t J = I;
            while (J < Expr.size() && std::isdigit(static_cast<unsigned char>(Expr[J]))) {
                ++J;
            }
            Tok T;
            T.Kind = Tok::NumLit;
            T.Number = std::stoll(Expr.substr(I, J - I));
            Tokens.push_back(T);
            I = J;
            continue;
        }
        if (IsIdentStart(Expr[I])) {
            size_t J = I + 1;
            while (J < Expr.size() && IsIdentChar(Expr[J])) {
                ++J;
            }
            const std::string Ident = Expr.substr(I, J - I);
            auto It = Ctx.Defines.find(Ident);
            Tok T;
            T.Kind = Tok::NumLit;
            if (It == Ctx.Defines.end() || It->second.FunctionLike) {
                T.Number = 0;
            } else {
                const std::string Expanded = ExpandText(It->second.Body, const_cast<PreprocessorContext&>(Ctx), Line);
                T.Number = std::stoll(Trim(Expanded));
            }
            Tokens.push_back(T);
            I = J;
            continue;
        }
        if (Expr.compare(I, 2, "==") == 0 || Expr.compare(I, 2, "!=") == 0 ||
            Expr.compare(I, 2, "&&") == 0 || Expr.compare(I, 2, "||") == 0 ||
            Expr.compare(I, 2, "<=") == 0 || Expr.compare(I, 2, ">=") == 0) {
            Tok T;
            T.Kind = Tok::Op;
            T.Text = Expr.substr(I, 2);
            Tokens.push_back(T);
            I += 2;
            continue;
        }
        if (Expr[I] == '+' || Expr[I] == '-' || Expr[I] == '!' || Expr[I] == '*' ||
            Expr[I] == '/' || Expr[I] == '%' || Expr[I] == '<' || Expr[I] == '>') {
            Tok T;
            T.Kind = Tok::Op;
            T.Text = std::string(1, Expr[I]);
            Tokens.push_back(T);
            ++I;
            continue;
        }
        if (Expr[I] == '(') {
            Tok T;
            T.Kind = Tok::LParen;
            Tokens.push_back(T);
            ++I;
            continue;
        }
        if (Expr[I] == ')') {
            Tok T;
            T.Kind = Tok::RParen;
            Tokens.push_back(T);
            ++I;
            continue;
        }
        throw std::runtime_error("invalid #if expression: " + Expr);
    }

    size_t Pos = 0;
    std::function<int64_t()> ParseExpr;
    std::function<int64_t()> ParseOr;
    std::function<int64_t()> ParseAnd;
    std::function<int64_t()> ParseEquality;
    std::function<int64_t()> ParseRelational;
    std::function<int64_t()> ParseAdd;
    std::function<int64_t()> ParseMul;
    std::function<int64_t()> ParseUnary;
    std::function<int64_t()> ParsePrimary;

    ParsePrimary = [&]() -> int64_t {
        if (Pos >= Tokens.size()) {
            throw std::runtime_error("unexpected end of #if expression");
        }
        if (Tokens[Pos].Kind == Tok::NumLit) {
            return Tokens[Pos++].Number;
        }
        if (Tokens[Pos].Kind == Tok::LParen) {
            ++Pos;
            const int64_t Value = ParseExpr();
            if (Pos >= Tokens.size() || Tokens[Pos].Kind != Tok::RParen) {
                throw std::runtime_error("expected ')' in #if expression");
            }
            ++Pos;
            return Value;
        }
        throw std::runtime_error("invalid primary in #if expression");
    };
    ParseUnary = [&]() -> int64_t {
        if (Pos < Tokens.size() && Tokens[Pos].Kind == Tok::Op &&
            Tokens[Pos].Text == "!") {
            ++Pos;
            return ParseUnary() ? 0 : 1;
        }
        if (Pos < Tokens.size() && Tokens[Pos].Kind == Tok::Op &&
            Tokens[Pos].Text == "-") {
            ++Pos;
            return -ParseUnary();
        }
        return ParsePrimary();
    };
    ParseMul = [&]() -> int64_t {
        int64_t Value = ParseUnary();
        while (Pos < Tokens.size() && Tokens[Pos].Kind == Tok::Op &&
               (Tokens[Pos].Text == "*" || Tokens[Pos].Text == "/" ||
                Tokens[Pos].Text == "%")) {
            const std::string Op = Tokens[Pos++].Text;
            const int64_t Right = ParseUnary();
            if (Op == "*") {
                Value *= Right;
            } else if (Op == "/") {
                Value = Right == 0 ? 0 : Value / Right;
            } else {
                Value = Right == 0 ? 0 : Value % Right;
            }
        }
        return Value;
    };
    ParseAdd = [&]() -> int64_t {
        int64_t Value = ParseMul();
        while (Pos < Tokens.size() && Tokens[Pos].Kind == Tok::Op &&
               (Tokens[Pos].Text == "+" || Tokens[Pos].Text == "-")) {
            const std::string Op = Tokens[Pos++].Text;
            const int64_t Right = ParseMul();
            Value = Op == "+" ? Value + Right : Value - Right;
        }
        return Value;
    };
    ParseRelational = [&]() -> int64_t {
        int64_t Value = ParseAdd();
        while (Pos < Tokens.size() && Tokens[Pos].Kind == Tok::Op &&
               (Tokens[Pos].Text == "<" || Tokens[Pos].Text == ">" ||
                Tokens[Pos].Text == "<=" || Tokens[Pos].Text == ">=")) {
            const std::string Op = Tokens[Pos++].Text;
            const int64_t Right = ParseAdd();
            bool Result = false;
            if (Op == "<") Result = Value < Right;
            else if (Op == ">") Result = Value > Right;
            else if (Op == "<=") Result = Value <= Right;
            else Result = Value >= Right;
            Value = Result ? 1 : 0;
        }
        return Value;
    };
    ParseEquality = [&]() -> int64_t {
        int64_t Value = ParseRelational();
        while (Pos < Tokens.size() && Tokens[Pos].Kind == Tok::Op &&
               (Tokens[Pos].Text == "==" || Tokens[Pos].Text == "!=")) {
            const std::string Op = Tokens[Pos++].Text;
            const int64_t Right = ParseRelational();
            const bool Result = Op == "==" ? Value == Right : Value != Right;
            Value = Result ? 1 : 0;
        }
        return Value;
    };
    ParseAnd = [&]() -> int64_t {
        int64_t Value = ParseEquality();
        while (Pos < Tokens.size() && Tokens[Pos].Kind == Tok::Op &&
               Tokens[Pos].Text == "&&") {
            ++Pos;
            const int64_t Right = ParseEquality();
            Value = (Value && Right) ? 1 : 0;
        }
        return Value;
    };
    ParseOr = [&]() -> int64_t {
        int64_t Value = ParseAnd();
        while (Pos < Tokens.size() && Tokens[Pos].Kind == Tok::Op &&
               Tokens[Pos].Text == "||") {
            ++Pos;
            const int64_t Right = ParseAnd();
            Value = (Value || Right) ? 1 : 0;
        }
        return Value;
    };
    ParseExpr = ParseOr;
    const int64_t Result = ParseExpr();
    return Result != 0 ? 1 : 0;
}

std::string Preprocessor::ProcessFile(const std::string& Path, PreprocessorContext& Ctx) const {
    for (const std::string& Seen : Ctx.IncludeStack) {
        if (Seen == Path) {
            throw std::runtime_error("include cycle detected: " + Path);
        }
    }
    Ctx.IncludeStack.push_back(Path);
    const std::string Source = ReadFileOrThrow(Path);
    PreprocessorContext FileCtx = Ctx;
    FileCtx.SourcePath = Path;
    const std::string Processed = Process(Source, FileCtx);
    Ctx.Defines = FileCtx.Defines;
    Ctx.IncludeStack.pop_back();
    return Processed;
}

std::string Preprocessor::Process(const std::string& Source, PreprocessorContext& Ctx) const {
    std::istringstream Input(Source);
    std::ostringstream Output;
    std::string Line;
    int LineNo = 0;
    std::vector<bool> CondStack;
    CondStack.push_back(true);

    auto IsActive = [&]() {
        for (bool Active : CondStack) {
            if (!Active) {
                return false;
            }
        }
        return true;
    };

    auto HandleConditional = [&](const std::string& Directive) {
        std::string Body = Trim(Directive.substr(Directive.find(' ') + 1));
        if (StartsWith(Directive, "ifdef ")) {
            Body = Trim(Directive.substr(5));
            const bool Defined = Ctx.Defines.count(Body) != 0;
            CondStack.push_back(IsActive() && Defined);
            return;
        }
        if (StartsWith(Directive, "ifndef ")) {
            Body = Trim(Directive.substr(6));
            const bool Defined = Ctx.Defines.count(Body) != 0;
            CondStack.push_back(IsActive() && !Defined);
            return;
        }
        if (StartsWith(Directive, "if ")) {
            Body = Trim(Directive.substr(3));
            const bool Result = EvalIfExpr(Body, Ctx, LineNo) != 0;
            CondStack.push_back(IsActive() && Result);
            return;
        }
        if (StartsWith(Directive, "elifdef ")) {
            if (CondStack.size() <= 1) {
                throw std::runtime_error("unmatched #elifdef");
            }
            Body = Trim(Directive.substr(7));
            const bool ParentActive =
                std::all_of(CondStack.begin(), CondStack.end() - 1,
                            [](bool Active) { return Active; });
            const bool BranchTaken = CondStack.back();
            CondStack.back() = false;
            if (ParentActive && !BranchTaken && Ctx.Defines.count(Body) != 0) {
                CondStack.back() = true;
            }
            return;
        }
        if (StartsWith(Directive, "elifndef ")) {
            if (CondStack.size() <= 1) {
                throw std::runtime_error("unmatched #elifndef");
            }
            Body = Trim(Directive.substr(8));
            const bool ParentActive =
                std::all_of(CondStack.begin(), CondStack.end() - 1,
                            [](bool Active) { return Active; });
            const bool BranchTaken = CondStack.back();
            CondStack.back() = false;
            if (ParentActive && !BranchTaken && Ctx.Defines.count(Body) == 0) {
                CondStack.back() = true;
            }
            return;
        }
        if (StartsWith(Directive, "elif ")) {
            if (CondStack.size() <= 1) {
                throw std::runtime_error("unmatched #elif");
            }
            Body = Trim(Directive.substr(4));
            const bool ParentActive =
                std::all_of(CondStack.begin(), CondStack.end() - 1,
                            [](bool Active) { return Active; });
            const bool BranchTaken = CondStack.back();
            CondStack.back() = false;
            if (ParentActive && !BranchTaken && EvalIfExpr(Body, Ctx, LineNo) != 0) {
                CondStack.back() = true;
            }
            return;
        }
        if (Directive == "else") {
            if (CondStack.size() <= 1) {
                throw std::runtime_error("unmatched #else");
            }
            const bool ParentActive =
                std::all_of(CondStack.begin(), CondStack.end() - 1,
                            [](bool Active) { return Active; });
            CondStack.back() = ParentActive && !CondStack.back();
            return;
        }
        if (Directive == "endif") {
            if (CondStack.size() <= 1) {
                throw std::runtime_error("unmatched #endif");
            }
            CondStack.pop_back();
            return;
        }
        throw std::runtime_error("unknown conditional directive: " + Directive);
    };

    while (std::getline(Input, Line)) {
        ++LineNo;
        const std::string Trimmed = Trim(Line);
        if (!StartsWith(Trimmed, "#")) {
            if (IsActive()) {
                Output << ExpandLine(Line, Ctx, LineNo) << '\n';
            }
            continue;
        }

        std::string Directive = Trim(Trimmed.substr(1));
        if (StartsWith(Directive, "define")) {
            if (!IsActive()) {
                continue;
            }
            Directive = Trim(Directive.substr(6));
            const size_t Space = Directive.find_first_of(" \t(");
            if (Space == std::string::npos) {
                throw std::runtime_error("invalid #define: " + Trimmed);
            }
            const std::string Name = Directive.substr(0, Space);
            MacroDef Def;
            if (Directive[Space] == '(') {
                const size_t Close = Directive.find(')', Space);
                if (Close == std::string::npos) {
                    throw std::runtime_error("unterminated macro parameter list");
                }
                const std::string ParamText = Directive.substr(Space + 1, Close - Space - 1);
                if (!Trim(ParamText).empty()) {
                    Def.Params = SplitArgs(ParamText);
                }
                Def.FunctionLike = true;
                Def.Body = Trim(Directive.substr(Close + 1));
            } else {
                Def.Body = Trim(Directive.substr(Space));
            }
            Ctx.Defines[Name] = std::move(Def);
            continue;
        }
        if (StartsWith(Directive, "undef")) {
            if (!IsActive()) {
                continue;
            }
            Directive = Trim(Directive.substr(5));
            Ctx.Defines.erase(Directive);
            continue;
        }
        if (StartsWith(Directive, "error")) {
            if (!IsActive()) {
                continue;
            }
            throw std::runtime_error("#error: " + Trim(Directive.substr(5)));
        }
        if (StartsWith(Directive, "warning")) {
            if (!IsActive()) {
                continue;
            }
            continue;
        }
        if (StartsWith(Directive, "ifdef") || StartsWith(Directive, "ifndef") ||
            StartsWith(Directive, "if ") || Directive == "if" ||
            StartsWith(Directive, "elif") || Directive == "else" || Directive == "endif") {
            HandleConditional(Directive);
            continue;
        }
        if (StartsWith(Directive, "include")) {
            if (!IsActive()) {
                continue;
            }
            Directive = Trim(Directive.substr(7));
            bool System = false;
            std::string Path = Directive;
            if (!Path.empty() && Path.front() == '<' && Path.back() == '>') {
                System = true;
                Path = Path.substr(1, Path.size() - 2);
            } else if (!Path.empty() && Path.front() == '"') {
                Path = Path.substr(1, Path.size() - 2);
            }
            Path = Trim(Path);
            const std::string Resolved = ResolveInclude(Path, System, Ctx);
            Output << ProcessFile(Resolved, Ctx);
            continue;
        }
        if (StartsWith(Directive, "pragma")) {
            continue;
        }
        throw std::runtime_error("unsupported preprocessor directive: " + Trimmed);
    }

    if (CondStack.size() != 1) {
        throw std::runtime_error("unterminated conditional directive");
    }
    return Output.str();
}

std::string Preprocessor::ResolveInclude(const std::string& Path, bool System,
                                         const PreprocessorContext& Ctx) const {
    if (!System && !Ctx.SourcePath.empty()) {
        const std::filesystem::path Candidate =
            std::filesystem::path(ParentDir(Ctx.SourcePath)) / Path;
        if (std::filesystem::exists(Candidate)) {
            return Candidate.string();
        }
    }
    for (const std::string& IncludePath : Ctx.IncludePaths) {
        const std::filesystem::path Candidate = std::filesystem::path(IncludePath) / Path;
        if (std::filesystem::exists(Candidate)) {
            return Candidate.string();
        }
    }
    if (std::filesystem::exists(Path)) {
        return Path;
    }
    throw std::runtime_error("include file not found: " + Path);
}

} // namespace HyperC
