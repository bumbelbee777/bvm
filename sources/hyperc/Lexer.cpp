#include "Lexer.h"
#include "Keywords.h"

#include <cctype>
#include <stdexcept>

namespace HyperC {

ParseError::ParseError(SourceLoc Loc, const std::string& Message)
    : std::runtime_error("line " + std::to_string(Loc.Line) + ", column " +
                         std::to_string(Loc.Column) + ": " + Message),
      Loc(Loc) {}

Lexer::Lexer(std::string Source) : Source_(std::move(Source)) {}

char Lexer::Cur() const {
    return Pos_ < Source_.size() ? Source_[Pos_] : '\0';
}

char Lexer::PeekChar() const {
    return Pos_ + 1 < Source_.size() ? Source_[Pos_ + 1] : '\0';
}

void Lexer::Advance() {
    if (Pos_ < Source_.size()) {
        if (Source_[Pos_] == '\n') {
            ++Line_;
            Col_ = 1;
        } else {
            ++Col_;
        }
        ++Pos_;
    }
}

void Lexer::SkipWhitespace() {
    while (std::isspace(static_cast<unsigned char>(Cur()))) {
        Advance();
    }
}

void Lexer::SkipLineComment() {
    while (Cur() != '\0' && Cur() != '\n') {
        Advance();
    }
}

void Lexer::SkipBlockComment() {
    Advance();
    Advance();
    while (Cur() != '\0') {
        if (Cur() == '*' && PeekChar() == '/') {
            Advance();
            Advance();
            return;
        }
        Advance();
    }
    throw std::runtime_error("unterminated block comment");
}

Token Lexer::MakeToken(TokenKind Kind, std::string Text, int Line, int Col) {
    Token Tok;
    Tok.Kind = Kind;
    Tok.Text = std::move(Text);
    Tok.Line = Line;
    Tok.Column = Col;
    return Tok;
}

Token Lexer::ScanFloatSuffix(const std::string& IntPart, int Line, int Col) {
    std::string Text = IntPart;
    bool IsFloat = false;
    if (Cur() == '.') {
        Text.push_back(Cur());
        Advance();
        while (std::isdigit(static_cast<unsigned char>(Cur()))) {
            Text.push_back(Cur());
            Advance();
        }
    }
    if (Cur() == 'f' || Cur() == 'F') {
        IsFloat = true;
        Advance();
    }
    const double Value = std::stod(Text);
    return MakeToken(TokenKind::FloatLit,
                     (IsFloat ? "f:" : "d:") + std::to_string(Value), Line, Col);
}

Token Lexer::ScanNumber() {
    const int Line = Line_;
    const int Col = Col_;
    std::string Text;
    int Base = 10;
    if (Cur() == '0' && (PeekChar() == 'x' || PeekChar() == 'X')) {
        Text.push_back(Cur());
        Advance();
        Text.push_back(Cur());
        Advance();
        Base = 16;
    }
    while (std::isxdigit(static_cast<unsigned char>(Cur()))) {
        Text.push_back(Cur());
        Advance();
    }
    if (Text.empty()) {
        throw std::runtime_error("invalid number at line " + std::to_string(Line));
    }
    if (Cur() == '.' || Cur() == 'f' || Cur() == 'F') {
        return ScanFloatSuffix(Text, Line, Col);
    }
    const int64_t Value = std::stoll(Text, nullptr, Base);
    return MakeToken(TokenKind::IntLit, std::to_string(Value), Line, Col);
}

Token Lexer::ScanCharLiteral() {
    const int Line = Line_;
    const int Col = Col_;
    Advance();
    if (Cur() == '\0') {
        throw std::runtime_error("unterminated character literal at line " + std::to_string(Line));
    }
    const char Value = Cur();
    Advance();
    if (Cur() != '\'') {
        throw std::runtime_error("multi-character literal not supported at line " +
                                 std::to_string(Line));
    }
    Advance();
    return MakeToken(TokenKind::CharLit, std::to_string(static_cast<unsigned char>(Value)), Line,
                     Col);
}

Token Lexer::ScanStringLiteral() {
    const int Line = Line_;
    const int Col = Col_;
    Advance();
    std::string Value;
    while (Cur() != '\0' && Cur() != '"') {
        if (Cur() == '\\') {
            Advance();
            if (Cur() == '\0') {
                break;
            }
            switch (Cur()) {
                case 'n': Value.push_back('\n'); break;
                case 't': Value.push_back('\t'); break;
                case '"': Value.push_back('"'); break;
                case '\\': Value.push_back('\\'); break;
                default: Value.push_back(Cur()); break;
            }
            Advance();
            continue;
        }
        Value.push_back(Cur());
        Advance();
    }
    if (Cur() != '"') {
        throw std::runtime_error("unterminated string literal at line " + std::to_string(Line));
    }
    Advance();
    return MakeToken(TokenKind::StringLit, Value, Line, Col);
}

Token Lexer::ScanIdent() {
    const int Line = Line_;
    const int Col = Col_;
    std::string Text;
    while (std::isalnum(static_cast<unsigned char>(Cur())) || Cur() == '_') {
        Text.push_back(Cur());
        Advance();
    }
    const TokenKind Kind = LookupKeyword(Text);
    return MakeToken(Kind, Text, Line, Col);
}

Token Lexer::Next() {
    if (HasPeek_) {
        HasPeek_ = false;
        return PeekToken_;
    }

    SkipWhitespace();
    const int Line = Line_;
    const int Col = Col_;
    const char Ch = Cur();
    if (Ch == '\0') {
        return MakeToken(TokenKind::End, "", Line, Col);
    }

    if (Ch == '/' && PeekChar() == '/') {
        Advance();
        Advance();
        SkipLineComment();
        return Next();
    }
    if (Ch == '/' && PeekChar() == '*') {
        Advance();
        Advance();
        SkipBlockComment();
        return Next();
    }

    if (std::isalpha(static_cast<unsigned char>(Ch)) || Ch == '_') {
        return ScanIdent();
    }
    if (std::isdigit(static_cast<unsigned char>(Ch)) ||
        (Ch == '.' && std::isdigit(static_cast<unsigned char>(PeekChar())))) {
        if (Ch == '.') {
            return ScanFloatSuffix("", Line, Col);
        }
        return ScanNumber();
    }
    if (Ch == '\'') {
        return ScanCharLiteral();
    }
    if (Ch == '"') {
        return ScanStringLiteral();
    }

    Advance();
    switch (Ch) {
        case '(': return MakeToken(TokenKind::LParen, "(", Line, Col);
        case ')': return MakeToken(TokenKind::RParen, ")", Line, Col);
        case '{': return MakeToken(TokenKind::LBrace, "{", Line, Col);
        case '}': return MakeToken(TokenKind::RBrace, "}", Line, Col);
        case '[': return MakeToken(TokenKind::LBracket, "[", Line, Col);
        case ']': return MakeToken(TokenKind::RBracket, "]", Line, Col);
        case ',': return MakeToken(TokenKind::Comma, ",", Line, Col);
        case ';': return MakeToken(TokenKind::Semi, ";", Line, Col);
        case '+':
            if (Cur() == '=') {
                Advance();
                return MakeToken(TokenKind::PlusEq, "+=", Line, Col);
            }
            return MakeToken(TokenKind::Plus, "+", Line, Col);
        case '-':
            if (Cur() == '=') {
                Advance();
                return MakeToken(TokenKind::MinusEq, "-=", Line, Col);
            }
            return MakeToken(TokenKind::Minus, "-", Line, Col);
        case '*':
            if (Cur() == '=') {
                Advance();
                return MakeToken(TokenKind::StarEq, "*=", Line, Col);
            }
            return MakeToken(TokenKind::Star, "*", Line, Col);
        case '/':
            if (Cur() == '=') {
                Advance();
                return MakeToken(TokenKind::SlashEq, "/=", Line, Col);
            }
            return MakeToken(TokenKind::Slash, "/", Line, Col);
        case '%':
            if (Cur() == '=') {
                Advance();
                return MakeToken(TokenKind::PercentEq, "%=", Line, Col);
            }
            return MakeToken(TokenKind::Percent, "%", Line, Col);
        case '~':
            return MakeToken(TokenKind::Tilde, "~", Line, Col);
        case '^':
            if (Cur() == '=') {
                Advance();
                return MakeToken(TokenKind::CaretEq, "^=", Line, Col);
            }
            return MakeToken(TokenKind::Caret, "^", Line, Col);
        case '!':
            if (Cur() == '=') {
                Advance();
                return MakeToken(TokenKind::Ne, "!=", Line, Col);
            }
            return MakeToken(TokenKind::Bang, "!", Line, Col);
        case '=':
            if (Cur() == '=') {
                Advance();
                return MakeToken(TokenKind::EqEq, "==", Line, Col);
            }
            return MakeToken(TokenKind::Assign, "=", Line, Col);
        case '<':
            if (Cur() == '<') {
                Advance();
                if (Cur() == '=') {
                    Advance();
                    return MakeToken(TokenKind::ShlEq, "<<=", Line, Col);
                }
                return MakeToken(TokenKind::Shl, "<<", Line, Col);
            }
            if (Cur() == '=') {
                Advance();
                return MakeToken(TokenKind::Le, "<=", Line, Col);
            }
            return MakeToken(TokenKind::Lt, "<", Line, Col);
        case '>':
            if (Cur() == '>') {
                Advance();
                if (Cur() == '=') {
                    Advance();
                    return MakeToken(TokenKind::ShrEq, ">>=", Line, Col);
                }
                return MakeToken(TokenKind::Shr, ">>", Line, Col);
            }
            if (Cur() == '=') {
                Advance();
                return MakeToken(TokenKind::Ge, ">=", Line, Col);
            }
            return MakeToken(TokenKind::Gt, ">", Line, Col);
        case '&':
            if (Cur() == '&') {
                Advance();
                return MakeToken(TokenKind::AndAnd, "&&", Line, Col);
            }
            if (Cur() == '=') {
                Advance();
                return MakeToken(TokenKind::AmpEq, "&=", Line, Col);
            }
            return MakeToken(TokenKind::Amp, "&", Line, Col);
        case '|':
            if (Cur() == '|') {
                Advance();
                return MakeToken(TokenKind::OrOr, "||", Line, Col);
            }
            if (Cur() == '=') {
                Advance();
                return MakeToken(TokenKind::PipeEq, "|=", Line, Col);
            }
            return MakeToken(TokenKind::Pipe, "|", Line, Col);
        case '?': return MakeToken(TokenKind::Question, "?", Line, Col);
        case ':':
            if (Cur() == ':') {
                Advance();
                return MakeToken(TokenKind::ColonColon, "::", Line, Col);
            }
            return MakeToken(TokenKind::Colon, ":", Line, Col);
        default: break;
    }

    throw std::runtime_error("unexpected character '" + std::string(1, Ch) + "' at line " +
                             std::to_string(Line));
}

Token Lexer::Peek() {
    if (!HasPeek_) {
        PeekToken_ = Next();
        HasPeek_ = true;
    }
    return PeekToken_;
}

void Lexer::ResetPeek() {
    HasPeek_ = false;
}

} // namespace HyperC
