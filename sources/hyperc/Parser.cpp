#include "Parser.h"
#include "TypeUtils.h"

namespace HyperC {

Parser::Parser(Lexer& LexerRef, FutureContext* Future)
    : Lex_(LexerRef), Future_(Future) {
    Current_ = Lex_.Next();
}

SourceLoc Parser::CurLoc() const {
    return SourceLoc{Current_.Line, Current_.Column};
}

Token Parser::Cur() const { return Current_; }

Token Parser::Peek() const { return Lex_.Peek(); }

Token Parser::Advance() {
    Token Prev = Current_;
    Current_ = Lex_.Next();
    return Prev;
}

bool Parser::Match(TokenKind Kind) {
    if (Current_.Kind == Kind) {
        Advance();
        return true;
    }
    return false;
}

void Parser::Expect(TokenKind Kind, const char* What) {
    if (Current_.Kind != Kind) {
        throw ParseError(CurLoc(), std::string("expected ") + What);
    }
    Advance();
}

TypeKind Parser::ParseTypeCore() {
    bool SawUnsigned = false;
    bool SawSigned = false;
    bool SawLong = false;
    TypeKind Base = TypeKind::Int;

    while (IsTypeKeyword(Current_.Kind) || Current_.Kind == TokenKind::KwReserved) {
        if (Match(TokenKind::KwUnsigned)) {
            SawUnsigned = true;
            continue;
        }
        if (Match(TokenKind::KwSigned)) {
            SawSigned = true;
            continue;
        }
        if (Match(TokenKind::KwLong)) {
            SawLong = true;
            continue;
        }
        if (Match(TokenKind::KwBool)) {
            Base = TypeKind::Bool;
            break;
        }
        if (Match(TokenKind::KwChar)) {
            Base = TypeKind::Char;
            break;
        }
        if (Match(TokenKind::KwShort)) {
            Base = TypeKind::Short;
            break;
        }
        if (Match(TokenKind::KwFloat)) {
            Base = TypeKind::Float;
            break;
        }
        if (Match(TokenKind::KwDouble)) {
            Base = TypeKind::Double;
            break;
        }
        if (Match(TokenKind::KwInt)) {
            Base = TypeKind::Int;
            break;
        }
        if (Match(TokenKind::KwVoid)) {
            return TypeKind::Void;
        }
        if (Current_.Kind == TokenKind::KwReserved) {
            throw ParseError(CurLoc(), "HyperC feature not implemented yet: " + Current_.Text);
        }
        break;
    }

    if (SawLong && Base == TypeKind::Int) {
        Base = TypeKind::Long;
    }
    if (SawUnsigned) {
        return TypeKind::Unsigned;
    }
    if (SawSigned) {
        return TypeKind::Signed;
    }
    return Base;
}

TypeSpec Parser::ParseTypeSpec() {
    TypeSpec Spec;
    if (Match(TokenKind::KwConst)) {
        Spec.IsConst = true;
    }
    if (Cur().Kind == TokenKind::Ident && IsEnumName(Cur().Text)) {
        Spec.Kind = TypeKind::Enum;
        Spec.EnumName = ParseIdent();
    } else {
        Spec.Kind = ParseTypeCore();
    }
    while (Match(TokenKind::Star)) {
        ++Spec.PointerDepth;
    }
    if (Match(TokenKind::KwConst)) {
        Spec.IsConst = true;
    }
    return Spec;
}

bool Parser::IsLvalueExpr(const Expr& Node) const {
    if (std::get_if<IdentExpr>(&Node->Data)) {
        return true;
    }
    if (std::get_if<IndexExpr>(&Node->Data)) {
        return true;
    }
    if (const auto* Un = std::get_if<UnaryExpr>(&Node->Data)) {
        return Un->Kind == UnaryExpr::Op::Deref;
    }
    return false;
}

bool Parser::IsEnumName(const std::string& Name) const {
    return EnumNames_.find(Name) != EnumNames_.end();
}

bool Parser::IsLocalDeclStart() const {
    if (IsDeclStart(Cur().Kind)) {
        return true;
    }
    return Cur().Kind == TokenKind::Ident && IsEnumName(Cur().Text);
}

bool Parser::IsCastTypeStart() const {
    return IsTypeKeyword(Cur().Kind) || Cur().Kind == TokenKind::KwConst ||
           (Cur().Kind == TokenKind::Ident && IsEnumName(Cur().Text));
}

int64_t Parser::ParseArrayLength() {
    Expect(TokenKind::LBracket, "'['");
    if (Match(TokenKind::RBracket)) {
        return -1;
    }
    if (Cur().Kind != TokenKind::IntLit) {
        throw ParseError(CurLoc(), "expected array length");
    }
    const int64_t Length = std::stoll(Cur().Text);
    Advance();
    Expect(TokenKind::RBracket, "']'");
    return Length;
}

std::string Parser::ParseIdent() {
    if (Current_.Kind != TokenKind::Ident) {
        throw ParseError(CurLoc(), "expected identifier");
    }
    std::string Name = Current_.Text;
    Advance();
    return Name;
}

std::string Parser::ParseStringLiteral() {
    if (Current_.Kind != TokenKind::StringLit) {
        throw ParseError(CurLoc(), "expected string literal");
    }
    std::string Value = Current_.Text;
    Advance();
    return Value;
}

Expr Parser::ParsePrimary() {
    if (Cur().Kind == TokenKind::IntLit) {
        const int64_t Value = std::stoll(Cur().Text);
        Advance();
        return std::make_unique<ExprNode>(ExprNode{IntLiteralExpr{Value}});
    }
    if (Cur().Kind == TokenKind::FloatLit) {
        const bool IsFloat = Cur().Text.front() == 'f';
        const double Value = std::stod(Cur().Text.substr(2));
        Advance();
        return std::make_unique<ExprNode>(ExprNode{FloatLiteralExpr{Value, IsFloat}});
    }
    if (Cur().Kind == TokenKind::CharLit) {
        const int64_t Value = std::stoll(Cur().Text);
        Advance();
        return std::make_unique<ExprNode>(ExprNode{IntLiteralExpr{Value}});
    }
    if (Match(TokenKind::KwTrue)) {
        return std::make_unique<ExprNode>(ExprNode{IntLiteralExpr{1}});
    }
    if (Match(TokenKind::KwFalse)) {
        return std::make_unique<ExprNode>(ExprNode{IntLiteralExpr{0}});
    }
    if (Match(TokenKind::KwNullptr)) {
        return std::make_unique<ExprNode>(ExprNode{NullPtrExpr{}});
    }
    if (Cur().Kind == TokenKind::Ident) {
        std::string Name = ParseIdent();
        if (Match(TokenKind::ColonColon)) {
            const std::string Field = ParseIdent();
            return std::make_unique<ExprNode>(ExprNode{EnumFieldExpr{std::move(Name), Field}});
        }
        if (Match(TokenKind::LParen)) {
            auto Call = std::make_unique<CallExpr>();
            Call->Callee = Name;
            if (!Match(TokenKind::RParen)) {
                do {
                    Call->Args.push_back(ParseExpr());
                } while (Match(TokenKind::Comma));
                Expect(TokenKind::RParen, "')'");
            }
            return std::make_unique<ExprNode>(ExprNode{std::move(*Call)});
        }
        return std::make_unique<ExprNode>(ExprNode{IdentExpr{std::move(Name)}});
    }
    if (Match(TokenKind::LParen)) {
        Expr Inner = ParseExpr();
        Expect(TokenKind::RParen, "')'");
        return Inner;
    }
    throw ParseError(CurLoc(), "expected expression");
}

Expr Parser::ParsePostfix() {
    Expr Node = ParsePrimary();
    while (true) {
        if (Match(TokenKind::LBracket)) {
            if (const auto* Id = std::get_if<IdentExpr>(&Node->Data)) {
                auto Index = std::make_unique<IndexExpr>();
                Index->Base =
                    std::make_unique<ExprNode>(ExprNode{IdentExpr{Id->Name}});
                Index->Index = ParseExpr();
                Expect(TokenKind::RBracket, "']'");
                Node = std::make_unique<ExprNode>(ExprNode{std::move(*Index)});
                continue;
            }
            throw ParseError(CurLoc(), "invalid array index target");
        }
        break;
    }
    return Node;
}

Expr Parser::ParseCast() {
    if (Match(TokenKind::LParen)) {
        if (IsCastTypeStart()) {
            const TypeSpec Target = ParseTypeSpec();
            Expect(TokenKind::RParen, "')'");
            auto Node = std::make_unique<CastExpr>();
            Node->Target = Target;
            Node->Inner = ParseCast();
            return std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
        }
        Expr Inner = ParseExpr();
        Expect(TokenKind::RParen, "')'");
        return Inner;
    }
    return ParsePostfix();
}

Expr Parser::ParseUnary() {
    if (Match(TokenKind::Minus)) {
        auto Node = std::make_unique<UnaryExpr>();
        Node->Kind = UnaryExpr::Op::Neg;
        Node->Inner = ParseUnary();
        return std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    if (Match(TokenKind::Bang)) {
        auto Node = std::make_unique<UnaryExpr>();
        Node->Kind = UnaryExpr::Op::Not;
        Node->Inner = ParseUnary();
        return std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    if (Match(TokenKind::Tilde)) {
        auto Node = std::make_unique<UnaryExpr>();
        Node->Kind = UnaryExpr::Op::BitNot;
        Node->Inner = ParseUnary();
        return std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    if (Match(TokenKind::Star)) {
        auto Node = std::make_unique<UnaryExpr>();
        Node->Kind = UnaryExpr::Op::Deref;
        Node->Inner = ParseUnary();
        return std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    if (Match(TokenKind::Amp)) {
        auto Node = std::make_unique<UnaryExpr>();
        Node->Kind = UnaryExpr::Op::Addr;
        Node->Inner = ParseUnary();
        return std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return ParseCast();
}

Expr Parser::ParseMultiplicative() {
    Expr Left = ParseUnary();
    while (Cur().Kind == TokenKind::Star || Cur().Kind == TokenKind::Slash ||
           Cur().Kind == TokenKind::Percent) {
        BinOp Op = BinOp::Mul;
        if (Match(TokenKind::Slash)) Op = BinOp::Div;
        else if (Match(TokenKind::Percent)) Op = BinOp::Mod;
        else Advance();
        auto Node = std::make_unique<BinaryExpr>();
        Node->Op = Op;
        Node->Left = std::move(Left);
        Node->Right = ParseUnary();
        Left = std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return Left;
}

Expr Parser::ParseAdditive() {
    Expr Left = ParseMultiplicative();
    while (Cur().Kind == TokenKind::Plus || Cur().Kind == TokenKind::Minus) {
        const BinOp Op = Match(TokenKind::Plus) ? BinOp::Add : BinOp::Sub;
        auto Node = std::make_unique<BinaryExpr>();
        Node->Op = Op;
        Node->Left = std::move(Left);
        Node->Right = ParseMultiplicative();
        Left = std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return Left;
}

Expr Parser::ParseShift() {
    Expr Left = ParseAdditive();
    while (Cur().Kind == TokenKind::Shl || Cur().Kind == TokenKind::Shr) {
        BinOp Op = Match(TokenKind::Shl) ? BinOp::Shl : BinOp::Shr;
        auto Node = std::make_unique<BinaryExpr>();
        Node->Op = Op;
        Node->Left = std::move(Left);
        Node->Right = ParseAdditive();
        Left = std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return Left;
}

Expr Parser::ParseRelational() {
    Expr Left = ParseShift();
    while (Cur().Kind == TokenKind::Lt || Cur().Kind == TokenKind::Le ||
           Cur().Kind == TokenKind::Gt || Cur().Kind == TokenKind::Ge) {
        BinOp Op = BinOp::Lt;
        if (Match(TokenKind::Le)) Op = BinOp::Le;
        else if (Match(TokenKind::Gt)) Op = BinOp::Gt;
        else if (Match(TokenKind::Ge)) Op = BinOp::Ge;
        else Advance();
        auto Node = std::make_unique<BinaryExpr>();
        Node->Op = Op;
        Node->Left = std::move(Left);
        Node->Right = ParseShift();
        Left = std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return Left;
}

Expr Parser::ParseEquality() {
    Expr Left = ParseRelational();
    while (Cur().Kind == TokenKind::EqEq || Cur().Kind == TokenKind::Ne) {
        BinOp Op = BinOp::Eq;
        if (Match(TokenKind::EqEq)) {
            Op = BinOp::Eq;
        } else {
            Match(TokenKind::Ne);
            Op = BinOp::Ne;
        }
        auto Node = std::make_unique<BinaryExpr>();
        Node->Op = Op;
        Node->Left = std::move(Left);
        Node->Right = ParseRelational();
        Left = std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return Left;
}

Expr Parser::ParseBitAnd() {
    Expr Left = ParseEquality();
    while (Match(TokenKind::Amp)) {
        auto Node = std::make_unique<BinaryExpr>();
        Node->Op = BinOp::BitAnd;
        Node->Left = std::move(Left);
        Node->Right = ParseEquality();
        Left = std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return Left;
}

Expr Parser::ParseBitXor() {
    Expr Left = ParseBitAnd();
    while (Match(TokenKind::Caret)) {
        auto Node = std::make_unique<BinaryExpr>();
        Node->Op = BinOp::BitXor;
        Node->Left = std::move(Left);
        Node->Right = ParseBitAnd();
        Left = std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return Left;
}

Expr Parser::ParseBitOr() {
    Expr Left = ParseBitXor();
    while (Match(TokenKind::Pipe)) {
        auto Node = std::make_unique<BinaryExpr>();
        Node->Op = BinOp::BitOr;
        Node->Left = std::move(Left);
        Node->Right = ParseBitXor();
        Left = std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return Left;
}

Expr Parser::ParseAnd() {
    Expr Left = ParseBitOr();
    while (Match(TokenKind::AndAnd)) {
        auto Node = std::make_unique<BinaryExpr>();
        Node->Op = BinOp::And;
        Node->Left = std::move(Left);
        Node->Right = ParseBitOr();
        Left = std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return Left;
}

Expr Parser::ParseOr() {
    Expr Left = ParseAnd();
    while (Match(TokenKind::OrOr)) {
        auto Node = std::make_unique<BinaryExpr>();
        Node->Op = BinOp::Or;
        Node->Left = std::move(Left);
        Node->Right = ParseAnd();
        Left = std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return Left;
}

Expr Parser::ParseConditional() {
    Expr Left = ParseOr();
    if (Match(TokenKind::Question)) {
        auto Node = std::make_unique<TernaryExpr>();
        Node->Cond = std::move(Left);
        Node->TrueExpr = ParseExpr();
        Expect(TokenKind::Colon, "':'");
        Node->FalseExpr = ParseConditional();
        return std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return Left;
}

bool Parser::IsCompoundAssignToken(TokenKind Kind) const {
    switch (Kind) {
        case TokenKind::PlusEq:
        case TokenKind::MinusEq:
        case TokenKind::StarEq:
        case TokenKind::SlashEq:
        case TokenKind::PercentEq:
        case TokenKind::AmpEq:
        case TokenKind::PipeEq:
        case TokenKind::CaretEq:
        case TokenKind::ShlEq:
        case TokenKind::ShrEq:
            return true;
        default:
            return false;
    }
}

std::optional<BinOp> Parser::CompoundOpFromToken(TokenKind Kind) const {
    switch (Kind) {
        case TokenKind::PlusEq: return BinOp::Add;
        case TokenKind::MinusEq: return BinOp::Sub;
        case TokenKind::StarEq: return BinOp::Mul;
        case TokenKind::SlashEq: return BinOp::Div;
        case TokenKind::PercentEq: return BinOp::Mod;
        case TokenKind::AmpEq: return BinOp::BitAnd;
        case TokenKind::PipeEq: return BinOp::BitOr;
        case TokenKind::CaretEq: return BinOp::BitXor;
        case TokenKind::ShlEq: return BinOp::Shl;
        case TokenKind::ShrEq: return BinOp::Shr;
        default: return std::nullopt;
    }
}

bool Parser::IsAssignTargetToken() const {
    if (Cur().Kind != TokenKind::Ident) {
        return false;
    }
    const TokenKind Next = Peek().Kind;
    return Next == TokenKind::Assign || IsCompoundAssignToken(Next) ||
           Next == TokenKind::LBracket;
}

Expr Parser::ParseAssign() {
    Expr Left = ParseConditional();
    if (IsLvalueExpr(Left) &&
        (Cur().Kind == TokenKind::Assign || IsCompoundAssignToken(Cur().Kind))) {
        std::optional<BinOp> Compound;
        if (Cur().Kind == TokenKind::Assign) {
            Advance();
        } else {
            Compound = CompoundOpFromToken(Cur().Kind);
            Advance();
        }
        auto Node = std::make_unique<AssignExpr>();
        Node->Target = std::move(Left);
        Node->CompoundOp = Compound;
        Node->Value = ParseAssign();
        return std::make_unique<ExprNode>(ExprNode{std::move(*Node)});
    }
    return Left;
}

Expr Parser::ParseExpr() { return ParseAssign(); }

VarBinding Parser::ParseVarBinding() {
    VarBinding Binding;
    while (Match(TokenKind::Star)) {
        ++Binding.PointerDepth;
    }
    Binding.Name = ParseIdent();
    if (Cur().Kind == TokenKind::LBracket) {
        Binding.ArrayLength = ParseArrayLength();
    }
    if (Match(TokenKind::Assign)) {
        if (Match(TokenKind::LBrace)) {
            auto Init = std::make_unique<ArrayInitExpr>();
            if (Cur().Kind != TokenKind::RBrace) {
                do {
                    Init->Elements.push_back(ParseExpr());
                } while (Match(TokenKind::Comma));
            }
            Expect(TokenKind::RBrace, "'}'");
            Binding.Init = std::make_unique<ExprNode>(ExprNode{std::move(*Init)});
        } else {
            Binding.Init = ParseExpr();
        }
    }
    return Binding;
}

Param Parser::ParseParam() {
    Param P;
    P.Type = ParseTypeSpec();
    int DeclaratorDepth = 0;
    while (Match(TokenKind::Star)) {
        ++DeclaratorDepth;
    }
    P.Type = MergePointerDepth(P.Type, DeclaratorDepth);
    P.Name = ParseIdent();
    if (Cur().Kind == TokenKind::LBracket) {
        P.ArrayLength = ParseArrayLength();
    }
    return P;
}

BlockStmt Parser::ParseBlock() {
    Expect(TokenKind::LBrace, "'{'");
    BlockStmt Block;
    while (Cur().Kind != TokenKind::RBrace && Cur().Kind != TokenKind::End) {
        Block.Body.push_back(ParseStmt());
    }
    Expect(TokenKind::RBrace, "'}'");
    return Block;
}

AsmStmt Parser::ParseAsmStmt() {
    AsmStmt Node;
    if (Match(TokenKind::LParen)) {
        Node.Lines.push_back(ParseStringLiteral());
        Expect(TokenKind::RParen, "')'");
        Expect(TokenKind::Semi, "';'");
        return Node;
    }
    Expect(TokenKind::LBrace, "'{'");
    while (Cur().Kind != TokenKind::RBrace && Cur().Kind != TokenKind::End) {
        Node.Lines.push_back(ParseStringLiteral());
        Match(TokenKind::Semi);
    }
    Expect(TokenKind::RBrace, "'}'");
    Expect(TokenKind::Semi, "';'");
    return Node;
}

Stmt Parser::ParseForInit() {
    if (IsLocalDeclStart()) {
        VarDeclStmt Decl;
        Decl.Type = ParseTypeSpec();
        do {
            Decl.Bindings.push_back(ParseVarBinding());
        } while (Match(TokenKind::Comma));
        Expect(TokenKind::Semi, "';'");
        return std::make_unique<StmtNode>(StmtNode{std::move(Decl)});
    }
    if (Match(TokenKind::Semi)) {
        return nullptr;
    }
    ExprStmt Node;
    Node.Value = ParseExpr();
    Expect(TokenKind::Semi, "';'");
    return std::make_unique<StmtNode>(StmtNode{std::move(Node)});
}

EnumDecl Parser::ParseEnumDecl() {
    EnumDecl Decl;
    Decl.Name = ParseIdent();
    if (EnumNames_.find(Decl.Name) != EnumNames_.end()) {
        throw ParseError(CurLoc(), "duplicate enum name: " + Decl.Name);
    }
    EnumNames_.insert(Decl.Name);
    Expect(TokenKind::LBrace, "'{'");
    int64_t NextValue = 0;
    if (Cur().Kind != TokenKind::RBrace) {
        do {
            EnumField Field;
            Field.Name = ParseIdent();
            if (Match(TokenKind::Assign)) {
                if (Cur().Kind != TokenKind::IntLit) {
                    throw ParseError(CurLoc(), "expected enum constant value");
                }
                NextValue = std::stoll(Cur().Text);
                Advance();
                Field.Value = NextValue;
                ++NextValue;
            } else {
                Field.Value = NextValue;
                ++NextValue;
            }
            Decl.Fields.push_back(std::move(Field));
            EnumConstants_[Decl.Name + "::" + Decl.Fields.back().Name] = *Decl.Fields.back().Value;
        } while (Match(TokenKind::Comma));
    }
    Expect(TokenKind::RBrace, "'}'");
    Expect(TokenKind::Semi, "';'");
    return Decl;
}

SwitchCaseGroup Parser::ParseSwitchCaseGroup(std::vector<std::optional<int64_t>> PendingValues) {
    SwitchCaseGroup Group;
    Group.Values = std::move(PendingValues);
    while (Cur().Kind != TokenKind::RBrace && Cur().Kind != TokenKind::End &&
           Cur().Kind != TokenKind::KwCase && Cur().Kind != TokenKind::KwDefault) {
        Group.Body.push_back(ParseStmt());
    }
    return Group;
}

int64_t Parser::ParseCaseConstant() {
    if (Cur().Kind == TokenKind::IntLit) {
        const int64_t Value = std::stoll(Cur().Text);
        Advance();
        return Value;
    }
    if (Cur().Kind == TokenKind::Ident) {
        const std::string EnumName = ParseIdent();
        if (Match(TokenKind::ColonColon)) {
            const std::string FieldName = ParseIdent();
            const std::string Key = EnumName + "::" + FieldName;
            const auto It = EnumConstants_.find(Key);
            if (It == EnumConstants_.end()) {
                throw ParseError(CurLoc(), "unknown enum constant: " + Key);
            }
            return It->second;
        }
        throw ParseError(CurLoc(), "expected enum constant after case");
    }
    throw ParseError(CurLoc(), "expected case constant");
}

SwitchStmt Parser::ParseSwitchStmt() {
    SwitchStmt Node;
    Expect(TokenKind::LParen, "'('");
    Node.Discriminant = ParseExpr();
    Expect(TokenKind::RParen, "')'");
    Expect(TokenKind::LBrace, "'{'");
    std::vector<std::optional<int64_t>> PendingValues;
    while (Cur().Kind != TokenKind::RBrace && Cur().Kind != TokenKind::End) {
        if (Match(TokenKind::KwCase)) {
            PendingValues.push_back(ParseCaseConstant());
            Expect(TokenKind::Colon, "':'");
            continue;
        }
        if (Match(TokenKind::KwDefault)) {
            PendingValues.push_back(std::nullopt);
            Expect(TokenKind::Colon, "':'");
            continue;
        }
        if (PendingValues.empty()) {
            throw ParseError(CurLoc(), "switch statement outside of case/default");
        }
        Node.Cases.push_back(ParseSwitchCaseGroup(std::move(PendingValues)));
        PendingValues.clear();
    }
    if (!PendingValues.empty()) {
        Node.Cases.push_back(ParseSwitchCaseGroup(std::move(PendingValues)));
    }
    Expect(TokenKind::RBrace, "'}'");
    return Node;
}

Stmt Parser::ParseStmt() {
    if (IsLocalDeclStart()) {
        VarDeclStmt Decl;
        Decl.Type = ParseTypeSpec();
        do {
            Decl.Bindings.push_back(ParseVarBinding());
        } while (Match(TokenKind::Comma));
        Expect(TokenKind::Semi, "';'");
        return std::make_unique<StmtNode>(StmtNode{std::move(Decl)});
    }
    if (Match(TokenKind::KwReturn)) {
        ReturnStmt Ret;
        if (Cur().Kind != TokenKind::Semi) {
            Ret.Value = ParseExpr();
        }
        Expect(TokenKind::Semi, "';'");
        return std::make_unique<StmtNode>(StmtNode{std::move(Ret)});
    }
    if (Match(TokenKind::KwIf)) {
        IfStmt Node;
        Expect(TokenKind::LParen, "'('");
        Node.Cond = ParseExpr();
        Expect(TokenKind::RParen, "')'");
        Node.Then = ParseStmt();
        if (Match(TokenKind::KwElse)) {
            Node.Else = ParseStmt();
        }
        return std::make_unique<StmtNode>(StmtNode{std::move(Node)});
    }
    if (Match(TokenKind::KwWhile)) {
        WhileStmt Node;
        Expect(TokenKind::LParen, "'('");
        Node.Cond = ParseExpr();
        Expect(TokenKind::RParen, "')'");
        Node.Body = ParseStmt();
        return std::make_unique<StmtNode>(StmtNode{std::move(Node)});
    }
    if (Match(TokenKind::KwFor)) {
        ForStmt Node;
        Expect(TokenKind::LParen, "'('");
        Node.Init = ParseForInit();
        if (Cur().Kind != TokenKind::Semi) {
            Node.Cond = ParseExpr();
        }
        Expect(TokenKind::Semi, "';'");
        if (Cur().Kind != TokenKind::RParen) {
            Node.Step = ParseExpr();
        }
        Expect(TokenKind::RParen, "')'");
        Node.Body = ParseStmt();
        return std::make_unique<StmtNode>(StmtNode{std::move(Node)});
    }
    if (Match(TokenKind::KwSwitch)) {
        return std::make_unique<StmtNode>(StmtNode{ParseSwitchStmt()});
    }
    if (Match(TokenKind::KwBreak)) {
        Expect(TokenKind::Semi, "';'");
        return std::make_unique<StmtNode>(StmtNode{BreakStmt{}});
    }
    if (Match(TokenKind::KwAsm)) {
        return std::make_unique<StmtNode>(StmtNode{ParseAsmStmt()});
    }
    if (Cur().Kind == TokenKind::LBrace) {
        return std::make_unique<StmtNode>(StmtNode{ParseBlock()});
    }
    ExprStmt Node;
    Node.Value = ParseExpr();
    Expect(TokenKind::Semi, "';'");
    return std::make_unique<StmtNode>(StmtNode{std::move(Node)});
}

GlobalDecl Parser::ParseGlobalDecl() {
    GlobalDecl Decl;
    Decl.Type = ParseTypeSpec();
    do {
        Decl.Bindings.push_back(ParseVarBinding());
    } while (Match(TokenKind::Comma));
    Expect(TokenKind::Semi, "';'");
    return Decl;
}

FuncDecl Parser::ParseFuncDecl() {
    FuncDecl Decl;
    Decl.ReturnType = ParseTypeSpec();
    Decl.Name = ParseIdent();
    Expect(TokenKind::LParen, "'('");
    if (!Match(TokenKind::RParen)) {
        if (Cur().Kind == TokenKind::KwVoid) {
            Advance();
            Expect(TokenKind::RParen, "')'");
        } else {
            do {
                Decl.Params.push_back(ParseParam());
            } while (Match(TokenKind::Comma));
            Expect(TokenKind::RParen, "')'");
        }
    }
    Decl.Body = ParseBlock();
    return Decl;
}

void Parser::ParseModuleDecl(Program& Prog) {
    Prog.ModuleName = ParseIdent();
    Expect(TokenKind::Semi, "';'");
}

void Parser::ParseImportDecl(Program& Prog) {
    std::string Path;
    if (Cur().Kind == TokenKind::StringLit) {
        Path = ParseStringLiteral();
    } else {
        Path = ParseIdent();
    }
    Expect(TokenKind::Semi, "';'");
    Prog.Imports.push_back(std::move(Path));
}

Program Parser::ParseProgram() {
    Program Prog;
    if (Future_) {
        (void)Future_->Templates;
    }
    while (Cur().Kind != TokenKind::End) {
        if (Match(TokenKind::KwModule)) {
            ParseModuleDecl(Prog);
            continue;
        }
        if (Match(TokenKind::KwImport)) {
            ParseImportDecl(Prog);
            continue;
        }
        if (Match(TokenKind::KwEnum)) {
            Prog.Enums.push_back(ParseEnumDecl());
            continue;
        }
        if (!IsLocalDeclStart()) {
            throw ParseError(CurLoc(), "expected declaration");
        }
        const TypeSpec DeclType = ParseTypeSpec();
        const std::string Name = ParseIdent();
        if (Match(TokenKind::LParen)) {
            FuncDecl Fn;
            Fn.ReturnType = DeclType;
            Fn.Name = Name;
            if (!Match(TokenKind::RParen)) {
                if (Cur().Kind == TokenKind::KwVoid) {
                    Advance();
                    Expect(TokenKind::RParen, "')'");
                } else {
                    do {
                        Fn.Params.push_back(ParseParam());
                    } while (Match(TokenKind::Comma));
                    Expect(TokenKind::RParen, "')'");
                }
            }
            Fn.Body = ParseBlock();
            Prog.Functions.push_back(std::move(Fn));
            continue;
        }
        GlobalDecl Glob;
        Glob.Type = DeclType;
        VarBinding First;
        First.Name = Name;
        if (Cur().Kind == TokenKind::LBracket) {
            First.ArrayLength = ParseArrayLength();
        }
        if (Match(TokenKind::Assign)) {
            if (Match(TokenKind::LBrace)) {
                auto Init = std::make_unique<ArrayInitExpr>();
                if (Cur().Kind != TokenKind::RBrace) {
                    do {
                        Init->Elements.push_back(ParseExpr());
                    } while (Match(TokenKind::Comma));
                }
                Expect(TokenKind::RBrace, "'}'");
                First.Init = std::make_unique<ExprNode>(ExprNode{std::move(*Init)});
            } else {
                First.Init = ParseExpr();
            }
        }
        Glob.Bindings.push_back(std::move(First));
        while (Match(TokenKind::Comma)) {
            Glob.Bindings.push_back(ParseVarBinding());
        }
        Expect(TokenKind::Semi, "';'");
        Prog.Globals.push_back(std::move(Glob));
    }
    return Prog;
}

} // namespace HyperC
