#pragma once

#include "Ast.h"
#include "Future.h"
#include "Keywords.h"
#include "Lexer.h"

#include <unordered_map>
#include <unordered_set>

namespace HyperC {

class Parser {
public:
    explicit Parser(Lexer& LexerRef, FutureContext* Future = nullptr);

    Program ParseProgram();

private:
    SourceLoc CurLoc() const;
    Token Cur() const;
    Token Peek() const;
    Token Advance();
    bool Match(TokenKind Kind);
    void Expect(TokenKind Kind, const char* What);

    TypeSpec ParseTypeSpec();
    TypeKind ParseTypeCore();
    bool IsEnumName(const std::string& Name) const;
    bool IsLocalDeclStart() const;
    bool IsCastTypeStart() const;
    bool IsLvalueExpr(const Expr& Node) const;
    int64_t ParseArrayLength();
    std::string ParseIdent();
    std::string ParseStringLiteral();
    Expr ParseExpr();
    Expr ParseAssign();
    Expr ParseConditional();
    Expr ParseOr();
    Expr ParseAnd();
    Expr ParseBitOr();
    Expr ParseBitXor();
    Expr ParseBitAnd();
    Expr ParseEquality();
    Expr ParseRelational();
    Expr ParseShift();
    Expr ParseAdditive();
    Expr ParseMultiplicative();
    Expr ParseUnary();
    Expr ParseCast();
    Expr ParsePostfix();
    Expr ParsePrimary();

    std::optional<BinOp> CompoundOpFromToken(TokenKind Kind) const;
    bool IsCompoundAssignToken(TokenKind Kind) const;
    bool IsAssignTargetToken() const;

    void ParseModuleDecl(Program& Prog);
    void ParseImportDecl(Program& Prog);
    EnumDecl ParseEnumDecl();
    SwitchCaseGroup ParseSwitchCaseGroup(std::vector<std::optional<int64_t>> PendingValues);
    SwitchStmt ParseSwitchStmt();
    int64_t ParseCaseConstant();

    VarBinding ParseVarBinding();
    Param ParseParam();
    Stmt ParseForInit();
    Stmt ParseStmt();
    BlockStmt ParseBlock();
    GlobalDecl ParseGlobalDecl();
    FuncDecl ParseFuncDecl();
    AsmStmt ParseAsmStmt();

    Lexer& Lex_;
    FutureContext* Future_;
    Token Current_{};
    std::unordered_set<std::string> EnumNames_;
    std::unordered_map<std::string, int64_t> EnumConstants_;
};

} // namespace HyperC
