#pragma once



#include <memory>

#include <optional>

#include <string>

#include <variant>

#include <vector>



namespace HyperC {



enum class TypeKind {

    Void, Bool, Char, Short, Int, Signed, Unsigned, Long, Float, Double, Enum

};



enum class BinOp {

    Add, Sub, Mul, Div, Mod,

    Eq, Ne, Lt, Le, Gt, Ge,

    And, Or,

    BitAnd, BitOr, BitXor, Shl, Shr

};



struct TypeSpec {

    TypeKind Kind = TypeKind::Int;

    bool IsConst = false;

    std::optional<std::string> EnumName;

    int PointerDepth = 0;

};



struct ExprNode;



using Expr = std::unique_ptr<ExprNode>;



struct IntLiteralExpr { int64_t Value = 0; };

struct NullPtrExpr {};

struct FloatLiteralExpr { double Value = 0.0; bool IsFloat = false; };

struct IdentExpr { std::string Name; };

struct EnumFieldExpr { std::string EnumName; std::string FieldName; };

struct IndexExpr { Expr Base; Expr Index; };

struct CallExpr { std::string Callee; std::vector<Expr> Args; };

struct ArrayInitExpr { std::vector<Expr> Elements; };

struct CastExpr { TypeSpec Target; Expr Inner; };

struct UnaryExpr {

    enum class Op { Neg, Not, BitNot, Deref, Addr } Kind = Op::Neg;

    Expr Inner;

};

struct BinaryExpr {

    BinOp Op = BinOp::Add;

    Expr Left;

    Expr Right;

};

struct TernaryExpr {

    Expr Cond;

    Expr TrueExpr;

    Expr FalseExpr;

};

struct AssignExpr {

    Expr Target;

    std::optional<BinOp> CompoundOp;

    Expr Value;

};



struct ExprNode {

    std::variant<IntLiteralExpr, NullPtrExpr, FloatLiteralExpr, IdentExpr, EnumFieldExpr, IndexExpr, CallExpr,

                 ArrayInitExpr, CastExpr, UnaryExpr, BinaryExpr, TernaryExpr, AssignExpr>

        Data;

};



struct VarBinding {

    std::string Name;

    int PointerDepth = 0;

    std::optional<int64_t> ArrayLength;

    std::optional<Expr> Init;

};



struct StmtNode;



using Stmt = std::unique_ptr<StmtNode>;



struct BlockStmt { std::vector<Stmt> Body; };



struct VarDeclStmt {

    TypeSpec Type;

    std::vector<VarBinding> Bindings;

};

struct ReturnStmt { std::optional<Expr> Value; };

struct ExprStmt { Expr Value; };

struct IfStmt {

    Expr Cond;

    Stmt Then;

    std::optional<Stmt> Else;

};

struct WhileStmt {

    Expr Cond;

    Stmt Body;

};

struct ForStmt {

    std::optional<Stmt> Init;

    std::optional<Expr> Cond;

    std::optional<Expr> Step;

    Stmt Body;

};

struct AsmStmt { std::vector<std::string> Lines; };

struct BreakStmt {};

struct SwitchCaseGroup {

    std::vector<std::optional<int64_t>> Values;

    std::vector<Stmt> Body;

};

struct SwitchStmt {

    Expr Discriminant;

    std::vector<SwitchCaseGroup> Cases;

};



struct StmtNode {

    std::variant<VarDeclStmt, ReturnStmt, ExprStmt, IfStmt, BlockStmt, WhileStmt, ForStmt,

                 AsmStmt, SwitchStmt, BreakStmt>

        Data;

};



struct Param {

    TypeSpec Type;

    std::string Name;

    std::optional<int64_t> ArrayLength;

};



struct GlobalDecl {

    TypeSpec Type;

    std::vector<VarBinding> Bindings;

};

struct FuncDecl {

    TypeSpec ReturnType;

    std::string Name;

    std::vector<Param> Params;

    BlockStmt Body;

};

struct EnumField {

    std::string Name;

    std::optional<int64_t> Value;

};

struct EnumDecl {

    std::string Name;

    std::vector<EnumField> Fields;

};



struct Program {

    std::optional<std::string> ModuleName;

    std::vector<std::string> Imports;

    std::vector<EnumDecl> Enums;

    std::vector<GlobalDecl> Globals;

    std::vector<FuncDecl> Functions;

};



} // namespace HyperC


