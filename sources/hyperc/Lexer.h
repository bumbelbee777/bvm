#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace HyperC {

enum class TokenKind {
    End,
    Ident,
    IntLit,
    FloatLit,
    CharLit,
    StringLit,
    KwInt,
    KwVoid,
    KwBool,
    KwChar,
    KwShort,
    KwFloat,
    KwDouble,
    KwLong,
    KwSigned,
    KwUnsigned,
    KwConst,
    KwIf,
    KwElse,
    KwReturn,
    KwWhile,
    KwFor,
    KwAsm,
    KwTrue,
    KwFalse,
    KwNullptr,
    KwModule,
    KwImport,
    KwEnum,
    KwSwitch,
    KwCase,
    KwDefault,
    KwBreak,
    KwReserved,
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Semi,
    Assign,
    PlusEq,
    MinusEq,
    StarEq,
    SlashEq,
    PercentEq,
    AmpEq,
    PipeEq,
    CaretEq,
    ShlEq,
    ShrEq,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    EqEq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    AndAnd,
    OrOr,
    Bang,
    Amp,
    Pipe,
    Caret,
    Tilde,
    Shl,
    Shr,
    Question,
    Colon,
    ColonColon,
};

struct Token {
    TokenKind Kind = TokenKind::End;
    std::string Text;
    int Line = 1;
    int Column = 1;
};

class Lexer {
public:
    explicit Lexer(std::string Source);

    Token Next();
    Token Peek();
    void ResetPeek();

private:
    char Cur() const;
    char PeekChar() const;
    void Advance();
    void SkipWhitespace();
    void SkipLineComment();
    void SkipBlockComment();
    Token MakeToken(TokenKind Kind, std::string Text, int Line, int Col);
    Token ScanNumber();
    Token ScanFloatSuffix(const std::string& IntPart, int Line, int Col);
    Token ScanIdent();
    Token ScanCharLiteral();
    Token ScanStringLiteral();

    std::string Source_;
    size_t Pos_ = 0;
    int Line_ = 1;
    int Col_ = 1;
    bool HasPeek_ = false;
    Token PeekToken_{};
};

struct SourceLoc {
    int Line = 1;
    int Column = 1;
};

class ParseError : public std::runtime_error {
public:
    ParseError(SourceLoc Loc, const std::string& Message);

    SourceLoc Loc;
};

} // namespace HyperC
