#pragma once

#include "Lexer.h"

#include <string_view>

namespace HyperC {

inline bool IsTypeKeyword(TokenKind Kind) {
    switch (Kind) {
        case TokenKind::KwInt:
        case TokenKind::KwVoid:
        case TokenKind::KwBool:
        case TokenKind::KwChar:
        case TokenKind::KwShort:
        case TokenKind::KwFloat:
        case TokenKind::KwDouble:
        case TokenKind::KwLong:
        case TokenKind::KwSigned:
        case TokenKind::KwUnsigned:
            return true;
        default:
            return false;
    }
}

inline bool IsDeclStart(TokenKind Kind) {
    return Kind == TokenKind::KwConst || IsTypeKeyword(Kind);
}

inline TokenKind LookupKeyword(std::string_view Name) {
    if (Name == "int") return TokenKind::KwInt;
    if (Name == "void") return TokenKind::KwVoid;
    if (Name == "bool") return TokenKind::KwBool;
    if (Name == "char") return TokenKind::KwChar;
    if (Name == "short") return TokenKind::KwShort;
    if (Name == "float") return TokenKind::KwFloat;
    if (Name == "double") return TokenKind::KwDouble;
    if (Name == "long") return TokenKind::KwLong;
    if (Name == "signed") return TokenKind::KwSigned;
    if (Name == "unsigned") return TokenKind::KwUnsigned;
    if (Name == "const") return TokenKind::KwConst;
    if (Name == "if") return TokenKind::KwIf;
    if (Name == "else") return TokenKind::KwElse;
    if (Name == "return") return TokenKind::KwReturn;
    if (Name == "while") return TokenKind::KwWhile;
    if (Name == "for") return TokenKind::KwFor;
    if (Name == "asm") return TokenKind::KwAsm;
    if (Name == "true") return TokenKind::KwTrue;
    if (Name == "false") return TokenKind::KwFalse;
    if (Name == "nullptr") return TokenKind::KwNullptr;
    if (Name == "module") return TokenKind::KwModule;
    if (Name == "import") return TokenKind::KwImport;
    if (Name == "enum") return TokenKind::KwEnum;
    if (Name == "switch") return TokenKind::KwSwitch;
    if (Name == "case") return TokenKind::KwCase;
    if (Name == "default") return TokenKind::KwDefault;
    if (Name == "break") return TokenKind::KwBreak;
    if (Name == "interface" || Name == "template" ||
        Name == "using" || Name == "namespace") {
        return TokenKind::KwReserved;
    }
    return TokenKind::Ident;
}

} // namespace HyperC
